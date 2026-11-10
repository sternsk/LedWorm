#include "matrix_renderer.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>
#include <thread>
#include <initializer_list>
#include <utility>
#include <cstring>

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
    assert(updatePlayerFrames(21000000, frames, MAX_PLAYERS) == 1);
    const auto retained = frames[0];
    assert(!getPlayerState(17, state));
    assert(addPlayer(777));
    assert(getPlayerState(777, state));
    assert(state.player_id == retained.player_id);
    assert(state.x == retained.x && state.y == retained.y);
    assert(state.active_players == 1);
    removePlayer(777);
    assert(updatePlayerFrames(21020000, frames, MAX_PLAYERS) == 1);
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
    player_frame_t desaturated = {1, 8, 8, 86, 40};
    desaturated.saturation = 0;
    first.render(&desaturated, 1, {0, 0, 0});
    const auto gray = first.pixels()[matrixPixelIndex(8, 8)];
    assert(gray.r == 40 && gray.g == 40 && gray.b == 40);
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

static void marquee() {
    resetPlayersForTest();
    marquee_frame_t frame;
    assert(setMarqueeText("HI"));
    updateMarqueeFrame(1000000, frame);
    assert(frame.active && frame.x == MATRIX_WIDTH && strcmp(frame.text, "HI") == 0);
    updateMarqueeFrame(1120000, frame);
    assert(frame.x == MATRIX_WIDTH - 1);
    const unsigned width = 11, travel = MATRIX_WIDTH + width + 6;
    updateMarqueeFrame(1000000 + uint64_t(travel) * 120000, frame);
    assert(frame.x == MATRIX_WIDTH);
    assert(!setMarqueeText("Gr\xc3\xbc\xc3\x9f"));
    assert(!setMarqueeText("123456789012345678901234567890123"));

    assert(setMarqueeText("H"));
    updateMarqueeFrame(2000000, frame);
    updateMarqueeFrame(2000000 + uint64_t(MATRIX_WIDTH) * 120000, frame);
    assert(frame.x == 0);
    MatrixRenderer renderer;
    renderer.render(nullptr, 0, {0, 0, 0}, nullptr, &frame);
    for (uint8_t y = 4; y <= 10; ++y) {
        const auto pixel = renderer.pixels()[matrixPixelIndex(0, y)];
        assert(pixel.r == 96 && pixel.g == 96 && pixel.b == 96);
    }
    assert(renderer.pixels()[matrixPixelIndex(5, 7)].r == 0);
    assert(setMarqueeText(""));
    updateMarqueeFrame(3000000, frame);
    assert(!frame.active);
    renderer.render(nullptr, 0, {0, 0, 0}, nullptr, &frame);
    assert(renderer.pixels()[matrixPixelIndex(0, 7)].r == 0);
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
        assert(updateJoystick(202, 0, 0)); // Keep the second player under manual control.
        now += 20000;
        updatePlayerFrames(now, frames, MAX_PLAYERS, &vitamin);
    }
    assert(!vitamin.active);
    assert(getPlayerState(201, own) && own.score == 1 && own.length == 9);
    assert(getPlayerState(202, other) && other.score == 0 && other.length == 8);
    updatePlayerFrames(now, frames, MAX_PLAYERS, &vitamin);
    assert(getPlayerState(201, own) && own.score == 1);
    assert(!vitamin.active);
    removePlayer(201);
    assert(addPlayer(203));
    assert(getPlayerState(203, own) && own.score == 0 && own.length == 8);
    removePlayer(203);
    removePlayer(202);
    now += 10000000;
    assert(updatePlayerFrames(now, frames, MAX_PLAYERS, &vitamin) == 1);
    assert(!getPlayerState(202, own));

    MatrixRenderer renderer;
    vitamin_frame_t visible = {true, 3, 7, VITAMIN_LIFETIME_US / 2};
    const auto index = matrixPixelIndex(3, 7);
    renderer.render(nullptr, 0, {0, 0, 0}, &visible);
    assert(renderer.pixels()[index].r == 48 && renderer.pixels()[index].g == 48);
    visible.active = false;
    renderer.render(nullptr, 0, {0, 0, 0}, &visible);
    assert(renderer.pixels()[index].r == 0); // No vitamin trail.
    visible.active = true;
    player_frame_t head = {1, 3, 7, 0, 63};
    renderer.render(&head, 1, {0, 0, 0}, &visible);
    assert(renderer.pixels()[index].r == 63 && renderer.pixels()[index].g == 0);
    // Fade starts and ends at the underlying colored background, symmetrically.
    MatrixRenderer soft;
    for (unsigned i = 0; i < 100; ++i) soft.render(nullptr, 0, {8, 16, 24});
    visible.age_us = 0;
    soft.render(nullptr, 0, {8, 16, 24}, &visible);
    assert(soft.pixels()[index].r == 8 && soft.pixels()[index].b == 24);
    visible.age_us = VITAMIN_LIFETIME_US / 4;
    soft.render(nullptr, 0, {8, 16, 24}, &visible);
    const auto rising = soft.pixels()[index];
    assert(rising.r == 28 && rising.g == 32 && rising.b == 36);
    visible.age_us = VITAMIN_LIFETIME_US * 3 / 4;
    soft.render(nullptr, 0, {8, 16, 24}, &visible);
    assert(soft.pixels()[index].r == rising.r);
    visible.age_us = VITAMIN_LIFETIME_US;
    soft.render(nullptr, 0, {8, 16, 24}, &visible);
    assert(soft.pixels()[index].r == 8 && soft.pixels()[index].b == 24);
}

static void bodies() {
    LedWorm worm;
    assert(worm.init(1));
    worm.nominal_speed = 50;
    worm.length = 3;
    worm.x_position = 14;
    worm.y_position = 4;
    worm.move(1, 0, 0);
    worm.move(1, 0, 100000); // 14 -> 15 -> 0 -> 1 -> 2 -> 3.
    assert(worm.bodyCount() == 3);
    assert(worm.bodyCell(0) == 4 * MATRIX_WIDTH + 3);
    assert(worm.bodyCell(1) == 4 * MATRIX_WIDTH + 2);
    assert(worm.bodyCell(2) == 4 * MATRIX_WIDTH + 1);
    worm.length = 5;
    worm.move(1, 0, 140000);
    assert(worm.bodyCount() == 5);
    worm.move(0, 0, 160000);
    assert(worm.bodyCount() == 5); // Stationary heads do not add segments.
    player_frame_t frame = {1, worm.x_position, worm.y_position, 0, 63};
    frame.body_count = worm.bodyCount();
    for (unsigned i = 0; i < frame.body_count; ++i) frame.body[i] = worm.bodyCell(i);
    MatrixRenderer renderer;
    renderer.render(&frame, 1, {0, 0, 0});
    unsigned visible = 0;
    for (unsigned i = 0; i < PIXEL_COUNT; ++i) {
        if (renderer.pixels()[i].r) { ++visible; assert(renderer.pixels()[i].r == 63); }
    }
    assert(visible == 5);
    // Long histories wrap their storage without corrupting segment order.
    assert(worm.init(2));
    worm.nominal_speed = 50;
    worm.length = LedWorm::MAX_LENGTH;
    worm.move(0, 0, 0);
    uint64_t now = 0;
    // A serpentine Hamiltonian cycle visits all cells without self-intersection.
    for (unsigned i = 0; i < 600; ++i) {
        const unsigned row = i / MATRIX_WIDTH;
        const bool vertical = i % MATRIX_WIDTH == MATRIX_WIDTH - 1;
        now += 20000;
        worm.move(vertical ? 0 : (row % 2 ? -1 : 1), vertical ? 1 : 0, now);
    }
    assert(worm.bodyCount() == LedWorm::MAX_LENGTH);
    bool seen[PIXEL_COUNT] = {};
    for (unsigned i = 0; i < worm.bodyCount(); ++i) {
        assert(!seen[worm.bodyCell(i)]);
        seen[worm.bodyCell(i)] = true;
    }
    assert(worm.init(2) && worm.length == 8 && worm.bodyCount() == 0);
}

static void selfCuts() {
    for (int offset : {0, 12}) { // Repeat the same enclosure across both edges.
        LedWorm worm;
        assert(worm.init(1));
        worm.length = 20;
        worm.nominal_speed = 50;
        worm.x_position = (1 + offset) % MATRIX_WIDTH;
        worm.y_position = (3 + offset) % MATRIX_HEIGHT;
        uint64_t now = 0;
        worm.move(0, 0, now);
        auto walk = [&](int x, int y, unsigned cells) {
            for (unsigned i = 0; i < cells; ++i) { now += 20000; worm.move(x, y, now); }
        };
        auto cell = [&](int x, int y) {
            return unsigned((y + offset) % MATRIX_HEIGHT * MATRIX_WIDTH + (x + offset) % MATRIX_WIDTH);
        };
        auto filled = [&](unsigned c) { return (worm.claimedArea()[c / 8] & (1u << (c % 8))) != 0; };
        walk(1, 0, 4);
        walk(0, 1, 2);
        walk(-1, 0, 2);
        walk(0, -1, 2);
        assert(worm.length == 13 && worm.bodyCount() == 3);
        assert(worm.bodyCell(0) == cell(3, 3));
        assert(worm.bodyCell(1) == cell(2, 3));
        assert(worm.bodyCell(2) == cell(1, 3));
        assert(filled(cell(4, 4)));
        assert(!filled(cell(4, 3)) && !filled(cell(2, 4)));
        player_frame_t frame = {1, worm.x_position, worm.y_position, 86, 63};
        frame.body_count = worm.bodyCount();
        for (unsigned i = 0; i < frame.body_count; ++i) frame.body[i] = worm.bodyCell(i);
        for (unsigned i = 0; i < LedWorm::AREA_BYTES; ++i) {
            frame.area[i] = worm.claimedArea()[i]; frame.cut[i] = worm.cutCells()[i];
        }
        MatrixRenderer renderer;
        const unsigned removed = cell(4, 3);
        player_frame_t old = {1, float(removed % MATRIX_WIDTH), float(removed / MATRIX_WIDTH), 86, 63};
        renderer.render(&old, 1, {0, 0, 0});
        renderer.render(&frame, 1, {0, 0, 0});
        assert(renderer.pixels()[matrixPixelIndex(removed % MATRIX_WIDTH, removed / MATRIX_WIDTH)].g == 0);
        for (unsigned i = 0; i < 20; ++i) renderer.render(&frame, 1, {0, 0, 0});
        const unsigned logical = cell(4, 4);
        const auto color = renderer.pixels()[matrixPixelIndex(logical % MATRIX_WIDTH, logical / MATRIX_WIDTH)];
        assert(color.r == 0 && color.g == 5 && color.b == 0);
        const auto head = renderer.pixels()[matrixPixelIndex(uint8_t(worm.x_position), uint8_t(worm.y_position))];
        assert(head.g == 63);
        walk(0, 0, 2);
        assert(worm.length == 13 && filled(cell(4, 4)));
        assert(worm.init(2));
        for (unsigned i = 0; i < LedWorm::AREA_BYTES; ++i) assert(worm.claimedArea()[i] == 0);
    }
    // Reversing on a one-cell-wide path has no enclosed area and cannot underflow.
    LedWorm worm;
    worm.init(1); worm.nominal_speed = 50; worm.length = 2;
    worm.move(0, 0, 0); worm.move(1, 0, 20000); worm.move(-1, 0, 40000);
    assert(worm.length == 1 && worm.bodyCount() == 1);
    for (unsigned i = 0; i < LedWorm::AREA_BYTES; ++i) assert(worm.claimedArea()[i] == 0);
    worm.init(1); worm.length = 20;
    worm.move(0, 0, 0);
    for (unsigned i = 1; i <= MATRIX_WIDTH; ++i) worm.move(1, 0, i * 20000);
    assert(worm.length == 5 && worm.bodyCount() == 1);
    for (unsigned i = 0; i < LedWorm::AREA_BYTES; ++i) assert(worm.claimedArea()[i] == 0);

    // Verify that the network snapshot and renderer snapshot receive the cut.
    assert(addPlayer(301));
    player_frame_t frames[MAX_PLAYERS];
    uint64_t now = 0;
    updatePlayerFrames(now, frames, MAX_PLAYERS);
    for (const auto &direction : {std::pair<int,int>{1,0}, {0,1}, {-1,0}, {0,-1}}) {
        assert(updateJoystick(301, direction.first, direction.second));
        for (unsigned i = 0; i < 2; ++i) {
            now += 50000;
            updatePlayerFrames(now, frames, MAX_PLAYERS);
        }
    }
    player_state_t state;
    assert(getPlayerState(301, state) && state.length == 1 && state.score == 0);
    const unsigned interior = 5 * MATRIX_WIDTH + 3;
    assert(frames[0].area[interior / 8] & (1u << (interior % 8)));
    assert(frames[0].body_count == 1);
    removePlayer(301);
    assert(addPlayer(302));
    updatePlayerFrames(now, frames, MAX_PLAYERS);
    assert(getPlayerState(302, state) && state.length == 1);
    assert(frames[0].area[interior / 8] & (1u << (interior % 8)));
    removePlayer(302);
}

static void autopilot() {
    player_frame_t frames[MAX_PLAYERS];
    vitamin_frame_t vitamin;
    updatePlayerFrames(0, frames, MAX_PLAYERS, &vitamin);
    assert(addPlayer(401) && addPlayer(402));
    updatePlayerFrames(0, frames, MAX_PLAYERS, &vitamin);
    updatePlayerFrames(5000000, frames, MAX_PLAYERS, &vitamin);
    assert(vitamin.active);
    assert(updateJoystick(401, 0, 0) && updateJoystick(402, 0, 0));
    updatePlayerFrames(5000000, frames, MAX_PLAYERS, &vitamin);
    const float start_x = frames[0].x, start_y = frames[0].y;
    const float other_start_x = frames[1].x, other_start_y = frames[1].y;
    auto delta = [](float target, float current, float extent) {
        float d = target - current;
        if (d > extent / 2) d -= extent;
        if (d < -extent / 2) d += extent;
        return d;
    };
    const float dx = float(vitamin.x) - start_x;
    const float dy = float(vitamin.y) - start_y;
    const float distance = std::hypot(dx, dy);
    assert(updateJoystick(402, 0, 0));
    updatePlayerFrames(7980000, frames, MAX_PLAYERS, &vitamin);
    assert(frames[0].x == start_x && frames[0].y == start_y);
    assert(!updateJoystick(401, 2, 0)); // Invalid input does not reset inactivity.
    assert(updateBrightness(401, 25));
    player_state_t state;
    assert(getPlayerState(401, state));
    assert(updateJoystick(402, 0, 0));
    updatePlayerFrames(8000000, frames, MAX_PLAYERS, &vitamin);
    const float first_step = 8.0f * 0.02f * 0.02f;
    assert(std::fabs(delta(frames[0].x, start_x, MATRIX_WIDTH) - dx / distance * first_step) < 0.0001f);
    assert(std::fabs(delta(frames[0].y, start_y, MATRIX_HEIGHT) - dy / distance * first_step) < 0.0001f);
    assert(frames[1].x == other_start_x && frames[1].y == other_start_y); // Independent timer.
    const float auto_x = frames[0].x, auto_y = frames[0].y;
    assert(updateJoystick(401, 0, 0) && updateJoystick(402, 0, 0));
    updatePlayerFrames(8020000, frames, MAX_PLAYERS, &vitamin);
    assert(frames[0].x == auto_x && frames[0].y == auto_y); // Immediate takeover.
    assert(updateJoystick(401, 1, 0) && updateJoystick(402, 0, 0));
    updatePlayerFrames(8040000, frames, MAX_PLAYERS, &vitamin);
    assert(std::fabs(delta(frames[0].x, auto_x, MATRIX_WIDTH) - 0.4f) < 0.0001f);
    assert(frames[0].y == auto_y);
    assert(updateJoystick(401, 0, 0) && updateJoystick(402, 0, 0));
    updatePlayerFrames(8060000, frames, MAX_PLAYERS, &vitamin);
    uint64_t now = 11060000;
    assert(updateJoystick(402, 0, 0));
    updatePlayerFrames(now, frames, MAX_PLAYERS, &vitamin);
    assert(getPlayerState(401, state));
    for (unsigned step = 0; step < 600 && state.score == 0; ++step) {
        now += 20000;
        assert(updateJoystick(402, 0, 0));
        updatePlayerFrames(now, frames, MAX_PLAYERS, &vitamin);
        assert(frames[0].x >= 0 && frames[0].x <= MATRIX_WIDTH - 1);
        assert(frames[0].y >= 0 && frames[0].y <= MATRIX_HEIGHT - 1);
        assert(getPlayerState(401, state));
    }
    assert(!vitamin.active);
    assert(getPlayerState(401, state) && state.score == 1);
    assert(getPlayerState(402, state) && state.score == 0);
    float previous_x = frames[0].x, previous_y = frames[0].y;
    bool coasted = false;
    for (unsigned step = 0; step < 100; ++step) {
        now += 20000;
        assert(updateJoystick(402, 0, 0));
        updatePlayerFrames(now, frames, MAX_PLAYERS, &vitamin);
        const float travel = std::hypot(delta(frames[0].x, previous_x, MATRIX_WIDTH),
                                        delta(frames[0].y, previous_y, MATRIX_HEIGHT));
        assert(travel > 0 && travel <= 0.401f);
        assert(frames[0].x >= 0 && frames[0].x <= MATRIX_WIDTH - 1);
        assert(frames[0].y >= 0 && frames[0].y <= MATRIX_HEIGHT - 1);
        coasted |= travel > 0;
        previous_x = frames[0].x; previous_y = frames[0].y;
    }
    assert(coasted);
    now += 1000000; // The next vitamin appears after the three-second pause.
    assert(updateJoystick(402, 0, 0));
    updatePlayerFrames(now, frames, MAX_PLAYERS, &vitamin);
    assert(vitamin.active);
    const float rest_x = frames[0].x, rest_y = frames[0].y;
    now += 20000;
    assert(updateJoystick(402, 0, 0));
    updatePlayerFrames(now, frames, MAX_PLAYERS, &vitamin);
    const float restart_travel = std::hypot(delta(frames[0].x, rest_x, MATRIX_WIDTH),
                                            delta(frames[0].y, rest_y, MATRIX_HEIGHT));
    assert(restart_travel > 0 && restart_travel < 0.05f);
    const float next_x = frames[0].x, next_y = frames[0].y;
    now += 20000;
    assert(updateJoystick(402, 0, 0));
    updatePlayerFrames(now, frames, MAX_PLAYERS, &vitamin);
    const float accelerating_travel = std::hypot(delta(frames[0].x, next_x, MATRIX_WIDTH),
                                                 delta(frames[0].y, next_y, MATRIX_HEIGHT));
    assert(accelerating_travel > 0 && accelerating_travel < 0.06f);
    removePlayer(401); removePlayer(402);
    resetPlayersForTest();
    assert(addPlayer(403));
    updatePlayerFrames(now, frames, MAX_PLAYERS, &vitamin);
    assert(frames[0].x == 2 && frames[0].y == 4);
    // Restarted simulation clocks must not underflow the inactivity timer.
    updatePlayerFrames(0, frames, MAX_PLAYERS, &vitamin);
    assert(frames[0].x == 2 && frames[0].y == 4);
    removePlayer(403);
}

static void continuousAutopilot() {
    player_frame_t frames[MAX_PLAYERS];
    vitamin_frame_t vitamin;
    updatePlayerFrames(0, frames, MAX_PLAYERS, &vitamin);
    assert(addPlayer(501));
    updatePlayerFrames(0, frames, MAX_PLAYERS, &vitamin);
    float x = frames[0].x, y = frames[0].y;
    for (uint64_t now = 20000; now <= 24000000; now += 20000) {
        updatePlayerFrames(now, frames, MAX_PLAYERS, &vitamin);
        assert(frames[0].x >= 0 && frames[0].x <= MATRIX_WIDTH - 1);
        assert(frames[0].y >= 0 && frames[0].y <= MATRIX_HEIGHT - 1);
        const float travel = std::hypot(frames[0].x - x, frames[0].y - y);
        if (now >= 3000000) assert(travel > 0.000001f && travel <= 0.401f);
        x = frames[0].x; y = frames[0].y;
    }
    removePlayer(501);
}

static void automaticColorAnimation() {
    player_frame_t frames[MAX_PLAYERS];
    resetPlayersForTest();
    assert(addPlayer(601));
    updatePlayerFrames(0, frames, MAX_PLAYERS);
    assert(frames[0].hue == 0 && frames[0].saturation == 255);
    uint8_t previous_cell = uint8_t(int(frames[0].y + 0.5f) * MATRIX_WIDTH +
                                    int(frames[0].x + 0.5f));
    unsigned cell_steps = 0;
    uint64_t now = 3000000;
    while (cell_steps < 2) {
        updatePlayerFrames(now, frames, MAX_PLAYERS);
        const uint8_t cell = uint8_t(int(frames[0].y + 0.5f) * MATRIX_WIDTH +
                                     int(frames[0].x + 0.5f));
        if (cell != previous_cell) {
            ++cell_steps;
            assert(frames[0].hue == uint8_t(cell_steps * 8));
            assert(frames[0].saturation == uint8_t(255 + cell_steps));
            previous_cell = cell;
        }
        now += 20000;
        assert(now < 6000000);
    }
    // With two automatic worms, neither color animation advances.
    assert(addPlayer(602));
    const uint8_t first_hue = frames[0].hue, first_saturation = frames[0].saturation;
    updatePlayerFrames(now + 4000000, frames, MAX_PLAYERS);
    for (unsigned i = 0; i < 100; ++i) {
        now += 20000;
        updatePlayerFrames(now + 4000000, frames, MAX_PLAYERS);
    }
    assert(frames[0].hue == first_hue && frames[0].saturation == first_saturation);
    assert(frames[1].hue == 32 && frames[1].saturation == 255);
}

static void boundedMovement() {
    LedWorm worm;
    worm.init(1); worm.nominal_speed = 50;
    worm.x_position = MATRIX_WIDTH - 0.25f;
    worm.y_position = MATRIX_HEIGHT - 0.25f;
    worm.move(1, 1, 0, false);
    worm.move(1, 1, 100000, false);
    assert(worm.x_position == MATRIX_WIDTH - 1 && worm.y_position == MATRIX_HEIGHT - 1);
    for (unsigned age = 0; age < worm.bodyCount(); ++age) {
        assert(worm.bodyCell(age) % MATRIX_WIDTH == MATRIX_WIDTH - 1);
        assert(worm.bodyCell(age) / MATRIX_WIDTH == MATRIX_HEIGHT - 1);
    }
    worm.x_position = worm.y_position = 0.25f;
    worm.move(-1, -1, 200000, false);
    assert(worm.x_position == 0 && worm.y_position == 0);
    worm.move(-1, -1, 300000, false);
    assert(worm.x_position == 0 && worm.y_position == 0);
    worm.move(-1, -1, 320000); // Manual movement still wraps.
    assert(worm.x_position == MATRIX_WIDTH - 1 && worm.y_position == MATRIX_HEIGHT - 1);
}

int main() {
    movement(); puts("PASS: unabhängige Bewegung und Matrixgrenzen");
    players(); puts("PASS: 8 Spieler, Eingaben, Reconnect und parallele Zugriffe");
    resetPlayersForTest();
    rendering(); puts("PASS: Matrixmapping, mehrere Köpfe, Mischung und Fade");
    marquee(); puts("PASS: marquee validation, timing, centered 5x7 rendering and clearing");
    resetPlayersForTest();
    vitamins(); puts("PASS: vitamin timing, free spawn, pickup, per-player score, reconnect, overlay");
    resetPlayersForTest();
    bodies(); puts("PASS: growing bodies, stationary head, wraparound, ring storage and rendering");
    selfCuts(); puts("PASS: self-cut, retained tail, enclosed area value 5, edge crossing and reset");
    resetPlayersForTest();
    autopilot(); puts("PASS: three-second autopilot, direct direction, manual takeover, pickup, independent timers");
    resetPlayersForTest();
    boundedMovement(); puts("PASS: automatic edge bounds and manual wraparound");
    continuousAutopilot(); puts("PASS: continuous automatic movement over multiple vitamin cycles");
    automaticColorAnimation(); puts("PASS: solo automatic hue and saturation animation per LED step");
}
