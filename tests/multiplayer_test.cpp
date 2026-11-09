#include "matrix_renderer.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>
#include <thread>
#include <initializer_list>
#include <utility>

static void movement() {
    LedWorm tuned;
    assert(tuned.init(1));
    assert(!tuned.move(1, 0, 0));
    assert(tuned.move(1, 0, 20000));
    assert(std::fabs(tuned.x_position - tuned.nominal_speed * 0.02f) < 0.00001f);
    // Use a fixed speed for the boundary/time-clamping scenarios below.
    LedWorm a, b;
    a.nominal_speed = b.nominal_speed = 50;
    assert(!a.move(1, 1, 0));
    assert(!a.init(0));
    assert(a.init(1) && b.init(2));
    a.x_position = b.x_position = 8;
    a.y_position = b.y_position = 8;
    assert(!a.move(1, 0, 0));
    assert(!b.move(0, -1, 0));
    assert(a.move(1, 0, 20000));
    assert(b.move(0, -1, 20000));
    assert(a.x_position == 9 && a.y_position == 8);
    assert(b.x_position == 8 && b.y_position == 7);
    // Ein zweiter Spieler darf weder Zeitmessung noch Position des ersten ändern.
    a.move(1, 0, 40000);
    assert(a.x_position == 10);
    a.move(1, 1, 10000000);
    assert(a.x_position == 15 && a.y_position == 13);
    a.move(1, 1, 10020000);
    assert(a.x_position == 0 && a.y_position == 14);
    for (unsigned i = 1; i < 20; ++i) a.move(-1, -1, 10020000 + i * 100000);
    assert(a.x_position == 1 && a.y_position == 15);
    // Fractional overshoot is retained on both axes, in both directions.
    a.x_position = MATRIX_WIDTH - 0.25f;
    a.y_position = MATRIX_HEIGHT - 0.25f;
    a.move(1, 1, 11940000);
    assert(a.x_position == 0.75f && a.y_position == 0.75f);
    a.move(-1, -1, 11960000);
    assert(a.x_position == MATRIX_WIDTH - 0.25f);
    assert(a.y_position == MATRIX_HEIGHT - 0.25f);
    assert(!a.move(std::numeric_limits<float>::quiet_NaN(), 0, 13000000));
    a.deactivate();
    assert(!a.move(1, 1, 14000000));
    assert(a.init(3));
    assert(a.joystick_x == 0 && a.joystick_y == 0);
    assert(!a.move(1, 1, 15000000));
}

static void players() {
    assert(player_manager_init());
    assert(!addPlayer(-1));
    // FD 0 ist gültig, inaktive Slots dürfen ihn nicht versehentlich reservieren.
    assert(addPlayer(0));
    assert(!addPlayer(0));
    removePlayer(0);
    for (int i = 0; i < MAX_PLAYERS; ++i) assert(addPlayer(10 + i));
    assert(!addPlayer(99));
    assert(player_manager_init()); // Darf laufende Spieler nicht zurücksetzen.
    player_frame_t frames[MAX_PLAYERS];
    assert(updatePlayerFrames(0, frames, MAX_PLAYERS) == MAX_PLAYERS);
    for (size_t i = 0; i < MAX_PLAYERS; ++i) {
        assert(frames[i].player_id != 0);
        for (size_t j = 0; j < i; ++j) {
            assert(frames[i].player_id != frames[j].player_id);
            assert(frames[i].hue != frames[j].hue);
            assert(frames[i].x != frames[j].x || frames[i].y != frames[j].y);
        }
    }
    const auto a = frames[0], b = frames[1];
    player_state_t state;
    assert(getPlayerState(10, state));
    assert(state.player_id == a.player_id && state.x == a.x && state.y == a.y);
    assert(state.length == 8 && state.brightness == 63 && state.active_players == MAX_PLAYERS);
    assert(!getPlayerState(99, state));
    assert(updateBrightness(10, 0));
    assert(updateBrightness(11, 255));
    assert(!updateBrightness(99, 12));
    for (double invalid : {-1.0, 256.0, 1.5, std::numeric_limits<double>::infinity(),
                           std::numeric_limits<double>::quiet_NaN()}) {
        assert(!updateBrightness(10, invalid));
    }
    assert(getPlayerState(10, state) && state.brightness == 0);
    assert(getPlayerState(11, state) && state.brightness == 255);
    assert(getPlayerState(12, state) && state.brightness == 63);
    assert(updateJoystick(10, 1, 0));
    assert(updateJoystick(11, 0, -1));
    assert(!updateJoystick(12, std::numeric_limits<float>::infinity(), 0));
    assert(!updateJoystick(12, 0, std::numeric_limits<float>::quiet_NaN()));
    assert(!updateJoystick(12, 2, 0));
    assert(!updateJoystick(99, 1, 0));
    assert(updatePlayerFrames(20000, frames, MAX_PLAYERS) == MAX_PLAYERS);
    assert(frames[0].brightness == 0 && frames[1].brightness == 255);
    const float step = LedWorm{}.nominal_speed * 0.02f;
    assert(std::fabs(frames[0].x - (a.x + step)) < 0.00001f && frames[0].y == a.y);
    assert(frames[1].x == b.x && std::fabs(frames[1].y - (b.y - step)) < 0.00001f);
    assert(updatePlayerFrames(40000, frames, 1) == 1);
    // Die Snapshot-Daten dürfen von späteren Joystick-Updates unberührt bleiben.
    const auto copy = frames[0];
    assert(updateJoystick(10, -1, -1));
    assert(frames[0].x == copy.x && frames[0].y == copy.y);
    removePlayer(10);
    assert(!updateJoystick(10, 1, 1));
    assert(addPlayer(99));
    assert(updatePlayerFrames(60000, frames, MAX_PLAYERS) == MAX_PLAYERS);
    assert(frames[0].player_id != a.player_id);
    assert(frames[0].brightness == 63);
    assert(!updateBrightness(10, 50));
    assert(frames[0].x == a.x && frames[0].y == a.y);
    // Netzwerktask und Render-Task arbeiten parallel auf denselben Instanzen.
    std::thread input([] {
        for (int i = 0; i < 1000; ++i) {
            assert(updateJoystick(11, i % 2 ? 1 : -1, 0));
            assert(updateBrightness(11, i % 256));
            player_state_t snapshot;
            assert(getPlayerState(11, snapshot));
        }
    });
    for (uint64_t i = 1; i <= 1000; ++i) {
        assert(updatePlayerFrames(60000 + i * 20000, frames, MAX_PLAYERS) == MAX_PLAYERS);
        assert(frames[1].x >= 0 && frames[1].x < MATRIX_WIDTH);
    }
    input.join();
    removePlayer(99);
    for (int i = 1; i < MAX_PLAYERS; ++i) removePlayer(10 + i);
    assert(updatePlayerFrames(21000000, frames, MAX_PLAYERS) == 0);
}

static void rendering() {
    // Nearest-LED rounding must wrap rather than address beyond the matrix.
    for (const auto &position : {std::pair<float, float>{15.75f, 3.0f},
                                 {3.0f, 15.75f}, {15.75f, 15.75f}}) {
        MatrixRenderer edge;
        player_frame_t head = {1, position.first, position.second, 0, 255};
        edge.render(&head, 1, {0, 0, 0});
        const auto x = uint8_t(int(position.first + 0.5f) % MATRIX_WIDTH);
        const auto y = uint8_t(int(position.second + 0.5f) % MATRIX_HEIGHT);
        assert(edge.pixels()[matrixPixelIndex(x, y)].r == 255);
        unsigned total = 0;
        for (size_t i = 0; i < PIXEL_COUNT; ++i) total += edge.pixels()[i].r;
        assert(total == 255);
    }
    assert(matrixPixelIndex(0, 0, false) == 0);
    assert(matrixPixelIndex(15, 15, false) == 255);
    assert(matrixPixelIndex(0, 1, false) == 16);
    assert(matrixPixelIndex(0, 1, true) == 31);
    assert(matrixPixelIndex(15, 1, true) == 16);
    assert(matrixPixelIndex(0, 2, true) == 32);
    MatrixRenderer first, reversed;
    player_frame_t pair[] = {{1, 2, 4, 0}, {2, 6, 4, 86}};
    first.render(pair, 2, {0, 0, 0});
    assert(first.pixels()[matrixPixelIndex(2, 4)].r == 63);
    assert(first.pixels()[matrixPixelIndex(6, 4)].g == 63);
    assert(first.pixels()[matrixPixelIndex(0, 0)].r == 0);
    // Köpfe auf derselben LED werden unabhängig von der Reihenfolge gemischt.
    pair[1].x = pair[0].x;
    first.render(pair, 2, {0, 0, 0});
    const player_frame_t backwards[] = {pair[1], pair[0]};
    reversed.render(backwards, 2, {0, 0, 0});
    const size_t overlap = matrixPixelIndex(2, 4);
    assert(first.pixels()[overlap].r == 63 && first.pixels()[overlap].g == 63);
    assert(reversed.pixels()[overlap].r == first.pixels()[overlap].r);
    assert(reversed.pixels()[overlap].g == first.pixels()[overlap].g);
    // Stationäre Köpfe dürfen durch den Schweif nicht immer heller werden.
    for (int i = 0; i < 100; ++i) first.render(pair, 2, {0, 0, 0});
    assert(first.pixels()[overlap].r == 63);
    // Brightness affects only the matching head, including full off and full on.
    player_frame_t dimmed[] = {{1, 2, 4, 0, 0}, {2, 6, 4, 86, 255}};
    first.render(dimmed, 2, {0, 0, 0});
    assert(first.pixels()[matrixPixelIndex(2, 4)].r == 0);
    assert(first.pixels()[matrixPixelIndex(2, 4)].g == 0);
    assert(first.pixels()[matrixPixelIndex(6, 4)].g == 255);
    dimmed[0].brightness = 17;
    first.render(dimmed, 2, {0, 0, 0});
    assert(first.pixels()[matrixPixelIndex(2, 4)].r == 17);
    assert(first.pixels()[matrixPixelIndex(6, 4)].g == 255);
    // Getrennte Spieler verschwinden vollständig, auch bei hellem Hintergrund.
    for (int i = 0; i < 100; ++i) first.render(nullptr, 0, {12, 8, 9});
    for (size_t i = 0; i < PIXEL_COUNT; ++i) {
        assert(first.pixels()[i].r == 12);
        assert(first.pixels()[i].g == 8);
        assert(first.pixels()[i].b == 9);
    }
    player_frame_t invalid[] = {{1, -1, 0, 0}, {2, 16, 15, 0},
        {3, 0, std::numeric_limits<float>::quiet_NaN(), 0}};
    first.render(invalid, 3, {12, 8, 9});
    assert(first.pixels()[0].r == 12);
    // Alle acht Spieler in einem einzigen Frame sichtbar.
    player_frame_t all[MAX_PLAYERS];
    assert(addPlayer(10));
    for (int i = 1; i < MAX_PLAYERS; ++i) assert(addPlayer(10 + i));
    assert(updatePlayerFrames(0, all, MAX_PLAYERS) == MAX_PLAYERS);
    first.render(all, MAX_PLAYERS, {0, 0, 0});
    for (const auto &p : all) {
        const auto color = first.pixels()[matrixPixelIndex(uint8_t(p.x), uint8_t(p.y))];
        assert(color.r == 63 || color.g == 63 || color.b == 63);
    }
    for (int i = 0; i < MAX_PLAYERS; ++i) removePlayer(10 + i);
}

static void vitamins() {
    player_frame_t frames[MAX_PLAYERS];
    vitamin_frame_t vitamin;
    uint64_t now = 0;
    updatePlayerFrames(now, frames, MAX_PLAYERS, &vitamin);
    assert(!vitamin.active); // No vitamins without players.
    assert(addPlayer(201) && addPlayer(202));
    updatePlayerFrames(now, frames, MAX_PLAYERS, &vitamin);
    assert(!vitamin.active);
    now = 4999999;
    updatePlayerFrames(now, frames, MAX_PLAYERS, &vitamin);
    assert(!vitamin.active);
    now = 5000000;
    updatePlayerFrames(now, frames, MAX_PLAYERS, &vitamin);
    assert(vitamin.active && vitamin.x < MATRIX_WIDTH && vitamin.y < MATRIX_HEIGHT);
    for (const auto &p : {frames[0], frames[1]}) {
        assert(int(p.x + 0.5f) != vitamin.x || int(p.y + 0.5f) != vitamin.y);
    }
    now += 8000000;
    updatePlayerFrames(now, frames, MAX_PLAYERS, &vitamin);
    assert(!vitamin.active); // Expiration awards no points.
    player_state_t own, other;
    assert(getPlayerState(201, own) && own.score == 0);
    now += 2999999;
    updatePlayerFrames(now, frames, MAX_PLAYERS, &vitamin);
    assert(!vitamin.active);
    ++now;
    updatePlayerFrames(now, frames, MAX_PLAYERS, &vitamin);
    assert(vitamin.active);
    const auto target = vitamin;
    for (int step = 0; step < 100 && vitamin.active; ++step) {
        assert(getPlayerState(201, own));
        auto direction = [](float target, float current, float extent) {
            float delta = target - current;
            if (delta > extent / 2) delta -= extent;
            if (delta < -extent / 2) delta += extent;
            return std::fmax(-1.0f, std::fmin(1.0f, delta / 0.4f));
        };
        assert(updateJoystick(201, direction(target.x, own.x, MATRIX_WIDTH),
                                  direction(target.y, own.y, MATRIX_HEIGHT)));
        now += 20000;
        updatePlayerFrames(now, frames, MAX_PLAYERS, &vitamin);
    }
    assert(!vitamin.active);
    assert(getPlayerState(201, own) && own.score == 1);
    assert(getPlayerState(202, other) && other.score == 0);
    updatePlayerFrames(now, frames, MAX_PLAYERS, &vitamin);
    assert(getPlayerState(201, own) && own.score == 1);
    assert(!vitamin.active);
    removePlayer(201);
    assert(addPlayer(203));
    assert(getPlayerState(203, own) && own.score == 0);
    removePlayer(203);
    removePlayer(202);
    now += 10000000;
    updatePlayerFrames(now, frames, MAX_PLAYERS, &vitamin);
    assert(!vitamin.active);

    MatrixRenderer renderer;
    vitamin_frame_t visible = {true, 3, 7};
    const auto index = matrixPixelIndex(3, 7);
    renderer.render(nullptr, 0, {0, 0, 0}, &visible);
    assert(renderer.pixels()[index].r == 180 && renderer.pixels()[index].g == 180);
    visible.active = false;
    renderer.render(nullptr, 0, {0, 0, 0}, &visible);
    assert(renderer.pixels()[index].r == 0); // No vitamin trail.
    visible.active = true;
    player_frame_t head = {1, 3, 7, 0, 63};
    renderer.render(&head, 1, {0, 0, 0}, &visible);
    assert(renderer.pixels()[index].r == 63 && renderer.pixels()[index].g == 0);
}

int main() {
    movement(); puts("PASS: unabhängige Bewegung und Matrixgrenzen");
    players(); puts("PASS: 8 Spieler, Eingaben, Reconnect und parallele Zugriffe");
    rendering(); puts("PASS: Matrixmapping, mehrere Köpfe, Mischung und Fade");
    vitamins(); puts("PASS: vitamin timing, free spawn, pickup, per-player score, reconnect, overlay");
}
