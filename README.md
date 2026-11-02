# LedWorm

ESP32-S3-Firmware für eine 16×16-WS2812-Matrix und bis zu acht
WebSocket-Spieler. Jeder verbundene Controller erhält einen eigenen Wurm,
eine feste Farbe und einen verteilten Startpunkt. Der Joystick bewegt den
Spieler auf beiden Achsen; an den Matrixrändern stoppt die Bewegung.

## Matrix und Rendering

In `src/globals.h` sind Abmessungen, LED-Pin und Verdrahtung festgelegt:

- `MATRIX_SERPENTINE = false`: zeilenweise, LED-Index `y * 16 + x`.
- `MATRIX_SERPENTINE = true`: jede zweite Zeile läuft rückwärts.
- Der Ursprung `(0, 0)` ist links unten; positives Y bewegt nach oben.
  Die physische Orientierung muss entsprechend zur Verdrahtung passen.

Der Render-Task aktualisiert alle Spieler mit demselben Zeitstempel und
übernimmt ihre Positionen unter einem Mutex. Die LED-Ausgabe erfolgt danach
ohne diesen Lock. Pro Frame werden alle Köpfe zusammengeführt und einmal an
die Hardware gesendet. Überlappende Köpfe mischen ihre Farben; alte Positionen
blenden zur gewählten Hintergrundfarbe aus. Beim Verbindungsende wird der
Spielerplatz freigegeben. Die Schweife sind ein visueller Fade, keine bereits
implementierten Spielkörper oder Kollisionen.

Spieler werden aktuell beim WebSocket-Handshake angelegt. Die vorhandene
Lobby-Oberfläche mit Wunsch-ID/Join-Anfrage ist noch ein separater Prototyp.

## Build und Tests

Die in `platformio.ini` gepinnte pioarduino-Plattform benötigt PlatformIO
Core mindestens 6.1.19. Firmware bauen:

```sh
pio run -e LedWorm -j 4
```

Für acht Spieler sind `CONFIG_LWIP_MAX_SOCKETS=16` in `sdkconfig.LedWorm`
und `sdkconfig.defaults` hinterlegt. Das WLAN erlaubt acht Stationen, der
HTTP-Server zehn Client-Sockets einschließlich WebSockets.

Hardwareunabhängige Regressionstests mit G++, AddressSanitizer und
UndefinedBehaviorSanitizer:

```sh
bash tests/run_host_tests.sh
```

Die Tests prüfen unabhängige Bewegung, Matrixgrenzen, acht Spieler,
Slot-Wiederverwendung, ungültige Eingaben, parallele Joystick-/Render-Zugriffe,
beide Verdrahtungsvarianten, Farbmischung und ausblendende Schweife. Der
FreeRTOS-Mutex wird dabei durch einen echten Host-Mutex ersetzt. Die Tests
ersetzen keinen Firmware-Build oder Funktionstest am ESP32.
