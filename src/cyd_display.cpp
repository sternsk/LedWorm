#include "cyd_display.h"

#include "globals.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_commands.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_ili9341.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace {
constexpr int LCD_SCLK = 14, LCD_MOSI = 13, LCD_MISO = 12;
constexpr int LCD_CS = 15, LCD_DC = 2, LCD_BL = 21;
constexpr int TOUCH_SCLK = 25, TOUCH_MOSI = 32, TOUCH_MISO = 39;
constexpr int TOUCH_CS = 33, TOUCH_IRQ = 36;
constexpr int WIDTH = 240, HEIGHT = 320;
constexpr uint16_t BLACK = 0x0000, WHITE = 0xffff, CYAN = 0x07ff;
constexpr uint16_t RED = 0xf800, GREEN = 0x07e0, BLUE = 0x001f;
constexpr int RAW_X_MIN = 200, RAW_X_MAX = 3850;
constexpr int RAW_Y_MIN = 380, RAW_Y_MAX = 3950;
static const char *TAG = "cyd";
esp_lcd_panel_handle_t panel;
spi_device_handle_t touch;
SemaphoreHandle_t lcdDone;
uint16_t pixels[WIDTH*16];

bool lcdTransferDone(esp_lcd_panel_io_handle_t,esp_lcd_panel_io_event_data_t*,void*) {
    BaseType_t wake=pdFALSE; xSemaphoreGiveFromISR(lcdDone,&wake); return wake==pdTRUE;
}

// Compact 5x7 glyphs used by this menu (ASCII columns, least significant bit at top).
const uint8_t *glyph(char c) {
    static const uint8_t digits[][5]={{62,81,73,69,62},{0,66,127,64,0},{66,97,81,73,70},{33,65,69,75,49},{24,20,18,127,16},{39,69,69,69,57},{60,74,73,73,48},{1,113,9,5,3},{54,73,73,73,54},{6,73,73,41,30}};
    static const char letters[]=" ABCDEFGHIJKLMNOPQRSTUVWXYZ:-";
    static const uint8_t data[][5]={{0,0,0,0,0},{126,17,17,17,126},{127,73,73,73,54},{62,65,65,65,34},{127,65,65,34,28},{127,73,73,73,65},{127,9,9,9,1},{62,65,73,73,122},{127,8,8,8,127},{0,65,127,65,0},{32,64,65,63,1},{127,8,20,34,65},{127,64,64,64,64},{127,2,12,2,127},{127,4,8,16,127},{62,65,65,65,62},{127,9,9,9,6},{62,65,81,33,94},{127,9,25,41,70},{70,73,73,73,49},{1,1,127,1,1},{63,64,64,64,63},{31,32,64,32,31},{63,64,56,64,63},{99,20,8,20,99},{3,4,120,4,3},{97,81,73,69,67},{0,54,54,0,0},{8,8,8,8,8}};
    if (c>='0'&&c<='9') return digits[c-'0'];
    if (c>='a'&&c<='z') c-=32;
    const char *p=strchr(letters,c); return p?data[p-letters]:data[0];
}
void rect(int x,int y,int w,int h,uint16_t color) {
    for(int row=0;row<h;) {
        const int rows=std::min(16,h-row);
        std::fill(pixels,pixels+w*rows,color);
        if(esp_lcd_panel_draw_bitmap(panel,x,y+row,x+w,y+row+rows,pixels)==ESP_OK)
            xSemaphoreTake(lcdDone,portMAX_DELAY);
        row+=rows;
    }
}
void text(int x,int y,const char *s,uint16_t color,int scale=2) {
    for(;*s;s++,x+=6*scale) { const uint8_t *g=glyph(*s); for(int col=0;col<5;col++) for(int row=0;row<7;row++) if(g[col]&(1<<row)) rect(x+col*scale,y+row*scale,scale,scale,color); }
}
uint16_t rgb565(uint8_t r,uint8_t g,uint8_t b) {
    return uint16_t((r & 0xf8) << 8) | uint16_t((g & 0xfc) << 3) | (b >> 3);
}
void draw() {
    rect(0,0,WIDTH,HEIGHT,BLACK); text(12,8,"LEDWORM CONFIG",CYAN,2);
    text(12,38,"32 X 8   GPIO 27",WHITE,1);
    struct Row { const char *name; std::atomic<uint8_t> *value; uint16_t color; } rows[]={{"RED",&ambient_r,RED},{"GREEN",&ambient_g,GREEN},{"BLUE",&ambient_b,BLUE}};
    const uint8_t r=ambient_r.load(),g=ambient_g.load(),b=ambient_b.load();
    rect(12,56,216,34,rgb565(r,g,b));
    char hex[16]; snprintf(hex,sizeof(hex),"#%02X%02X%02X",r,g,b); text(78,66,hex,WHITE,1);
    for(int i=0;i<3;i++) {
        const int y=103+i*67; const int value=rows[i].value->load(); char number[5];
        snprintf(number,sizeof(number),"%3d",value); text(12,y,rows[i].name,WHITE); text(192,y,number,WHITE,1);
        rect(12,y+22,216,28,WHITE); rect(14,y+24,212,24,BLACK);
        const int fill=value*212/255; if(fill) rect(14,y+24,fill,24,rows[i].color);
    }
}
uint16_t touchCommand(uint8_t command) {
    uint8_t tx[3]={command,0,0},rx[3]={}; spi_transaction_t t={}; t.length=24;t.tx_buffer=tx;t.rx_buffer=rx;
    if(spi_device_transmit(touch,&t)!=ESP_OK) return 0; return uint16_t((rx[1]<<8)|rx[2])>>3;
}
bool readTouch(int &x,int &y) {
    // Pressure sampling is more reliable than TP_IRQ on GPIO36, which has no
    // ESP32 internal pull-up. An untouched panel returns a near-zero Z1 value.
    const uint16_t z1=touchCommand(0xB0);
    if(z1<120) return false;
    uint16_t rawX=touchCommand(0xD0),rawY=touchCommand(0x90);
    const uint16_t z2=touchCommand(0xC0);
    // XPT2046 coordinates follow the native 240x320 touch surface. The LCD's
    // MADCTL axis swap does not change the touch controller coordinate system.
    x=std::clamp((int(rawX)-RAW_X_MIN)*(WIDTH-1)/(RAW_X_MAX-RAW_X_MIN),0,WIDTH-1);
    y=std::clamp((RAW_Y_MAX-int(rawY))*(HEIGHT-1)/(RAW_Y_MAX-RAW_Y_MIN),0,HEIGHT-1);
    return z2>z1&&rawX>100&&rawY>100&&rawX<4090&&rawY<4090;
}
void task(void*) {
    draw();
    while(true) { int x=0,y=0; bool down=readTouch(x,y);
        if(down&&x>=8&&x<=232) {
            int row=-1;
            for(int i=0;i<3;i++) { const int top=103+i*67; if(y>=top+16&&y<=top+56) row=i; }
            if(row>=0) {
                auto *value=row==0?&ambient_r:row==1?&ambient_g:&ambient_b;
                const uint8_t next=uint8_t(std::clamp((x-14)*255/212,0,255));
                if(value->load()!=next) { *value=next; draw(); }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(30));
    }
}
}

esp_err_t cyd_display_start() {
    gpio_config_t out={}; out.pin_bit_mask=1ULL<<LCD_BL;out.mode=GPIO_MODE_OUTPUT;ESP_ERROR_CHECK(gpio_config(&out));gpio_set_level((gpio_num_t)LCD_BL,0);
    spi_bus_config_t lcdBus={};lcdBus.mosi_io_num=LCD_MOSI;lcdBus.miso_io_num=LCD_MISO;lcdBus.sclk_io_num=LCD_SCLK;lcdBus.quadwp_io_num=-1;lcdBus.quadhd_io_num=-1;lcdBus.max_transfer_sz=WIDTH*20*2;
    ESP_RETURN_ON_ERROR(spi_bus_initialize(SPI2_HOST,&lcdBus,SPI_DMA_CH_AUTO),TAG,"LCD SPI");
    // Every color transaction is awaited before the DMA buffer is reused.
    esp_lcd_panel_io_spi_config_t ioCfg={};ioCfg.cs_gpio_num=LCD_CS;ioCfg.dc_gpio_num=LCD_DC;ioCfg.spi_mode=0;ioCfg.pclk_hz=40000000;ioCfg.trans_queue_depth=1;ioCfg.lcd_cmd_bits=8;ioCfg.lcd_param_bits=8;
    esp_lcd_panel_io_handle_t io;ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi(SPI2_HOST,&ioCfg,&io),TAG,"LCD IO");
    lcdDone=xSemaphoreCreateBinary(); if(!lcdDone) return ESP_ERR_NO_MEM;
    esp_lcd_panel_io_callbacks_t callbacks={}; callbacks.on_color_trans_done=lcdTransferDone;
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_register_event_callbacks(io,&callbacks,nullptr),TAG,"LCD callback");
    esp_lcd_panel_dev_config_t dev={};dev.reset_gpio_num=-1;dev.rgb_ele_order=LCD_RGB_ELEMENT_ORDER_BGR;dev.bits_per_pixel=16;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_ili9341(io,&dev,&panel),TAG,"ILI9341");
    esp_lcd_panel_reset(panel);
    esp_lcd_panel_init(panel);
    // This CYD mounting needs exchanged controller axes, while the UI itself
    // remains a full 240x320 portrait canvas.
    esp_lcd_panel_swap_xy(panel,true);
    // Clear the complete visible canvas, including the 80-pixel region missed
    // by the former 320x240/portrait geometry mismatch.
    rect(0,0,WIDTH,HEIGHT,BLACK);
    esp_lcd_panel_mirror(panel,false,false);
    esp_lcd_panel_disp_on_off(panel,true);
    gpio_set_level((gpio_num_t)LCD_BL,1);
    spi_bus_config_t touchBus={};touchBus.mosi_io_num=TOUCH_MOSI;touchBus.miso_io_num=TOUCH_MISO;touchBus.sclk_io_num=TOUCH_SCLK;touchBus.quadwp_io_num=-1;touchBus.quadhd_io_num=-1;touchBus.max_transfer_sz=3;
    ESP_RETURN_ON_ERROR(spi_bus_initialize(SPI3_HOST,&touchBus,SPI_DMA_DISABLED),TAG,"touch SPI");
    spi_device_interface_config_t touchCfg={};touchCfg.mode=0;touchCfg.clock_speed_hz=2000000;touchCfg.spics_io_num=TOUCH_CS;touchCfg.queue_size=1;ESP_RETURN_ON_ERROR(spi_bus_add_device(SPI3_HOST,&touchCfg,&touch),TAG,"touch device");
    gpio_config_t in={};in.pin_bit_mask=1ULL<<TOUCH_IRQ;in.mode=GPIO_MODE_INPUT;in.pull_up_en=GPIO_PULLUP_DISABLE;ESP_RETURN_ON_ERROR(gpio_config(&in),TAG,"touch IRQ");
    return xTaskCreate(task,"cyd_display",4096,nullptr,3,nullptr)==pdPASS?ESP_OK:ESP_ERR_NO_MEM;
}
