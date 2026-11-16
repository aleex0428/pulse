// SPDX-License-Identifier: MIT
// The heart model on made-up sessions: practice alone, a race start, a fight (defending counts more than attacking),
// the safety car, a red flag, a contact, a moment, a long race's fatigue, the composure score, the beat and ECG
// shapes, and random inputs that never take the heart out of its range.
//
//   model_test.exe
#include "../src/heart.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

using namespace pulse;

namespace {

int g_checks = 0, g_failed = 0;

void check(bool ok, const char *what, double got = NAN)
{
    g_checks++;
    if (!ok) {
        g_failed++;
        if (std::isnan(got))
            std::printf("FAIL: %s\n", what);
        else
            std::printf("FAIL: %s (got %.2f)\n", what, got);
    }
}

// a lap of a made-up circuit: the speed swings between 85 m/s on the straights and 45 m/s in the corners (with
// the brake on while it falls), up to 3.5 G in the corners
Inputs driving(Kind kind, float t)
{
    Inputs in;
    in.dt = 0.05f;
    in.kind = kind;
    in.known = true;
    float corner = std::sin(t * 0.7f);
    in.speed = 65.0f - 20.0f * corner;
    in.have_pedals = true;
    in.brake = std::cos(t * 0.7f) > 0 ? 0.8f : 0.0f;
    in.have_g = true;
    float lat = std::clamp((corner - 0.2f) / 0.8f, 0.0f, 1.0f);
    in.g_lat = 3.5f * lat * (std::fmod(t, 18.0f) < 9 ? 1 : -1) + 0.2f;
    in.g_long = in.brake > 0 ? -2.0f * std::cos(t * 0.7f) : 0.8f;
    in.position = 8;
    in.cars = 20;
    in.count = true;
    return in;
}

// runs a heart for `seconds` with the inputs the function gives
template <class F> void run(Heart &h, float seconds, F make)
{
    for (float t = 0; t < seconds; t += 0.05f) {
        Inputs in = make(t);
        update(h, in);
    }
}

void practice_alone()
{
    Heart h;
    reset(h, Kind::practice, 3);
    run(h, 120, [](float t) { return driving(Kind::practice, t); });
    check(h.bpm > 128 && h.bpm < 158, "practice alone: a steady heart", h.bpm);
    check(zone_of(h.bpm) == Zone::calm || zone_of(h.bpm) == Zone::focused, "practice alone: calm or focused", h.bpm);
    check(h.battle < 0.01f, "practice alone: no fight");
}

void race_start()
{
    Heart h;
    reset(h, Kind::race, 5);
    run(h, 30, [](float) {
        Inputs in = driving(Kind::race, 0);
        in.speed = 0;
        in.g_lat = in.g_long = 0;
        in.formation = true;
        return in;
    });
    float grid = h.bpm;
    check(grid > 150 && grid < 172, "on the grid: nerves before the lights", grid);
    lights_out(h);
    float peak = 0;
    run(h, 8, [&](float t) {
        Inputs in = driving(Kind::race, t);
        in.behind_s = 0.4f;
        in.ahead_s = 0.5f;
        return in;
    });
    peak = h.bpm;
    check(peak > 180, "lights out: the surge", peak);
    check(h.cause == Cause::lights_out || h.cause == Cause::defending || h.cause == Cause::wheel_to_wheel,
          "lights out: named as the start or the fight");
    run(h, 120, [](float t) { return driving(Kind::race, t); });
    check(h.bpm < peak - 10 && h.bpm > 150, "two minutes later, alone: down to the race's rhythm", h.bpm);
    check(zone_of(h.bpm) == Zone::zone || zone_of(h.bpm) == Zone::focused, "a lonely race: focused or in the zone",
          h.bpm);
}

float settle(Kind kind, float ahead, float behind)
{
    Heart h;
    reset(h, kind, 9);
    h.since_start = 1000;
    run(h, 40, [&](float t) {
        Inputs in = driving(kind, t);
        in.ahead_s = ahead;
        in.behind_s = behind;
        return in;
    });
    return h.bpm;
}

void fights()
{
    float alone = settle(Kind::race, -1, -1);
    float attack = settle(Kind::race, 0.4f, -1);
    float defend = settle(Kind::race, -1, 0.4f);
    float far = settle(Kind::race, 2.0f, 2.5f);
    check(attack > alone + 12, "attacking at 0.4 s: the heart goes up", attack - alone);
    check(defend > attack, "defending counts more than attacking", defend - attack);
    check(std::fabs(far - alone) < 1.5f, "a car 2 s away is not a fight", far - alone);
    float tt = settle(Kind::time_trial, 0.4f, 0.4f);
    float tt_alone = settle(Kind::time_trial, -1, -1);
    check(std::fabs(tt - tt_alone) < 0.5f, "time trial: no fights", tt - tt_alone);
    Heart h;
    reset(h, Kind::race, 1);
    h.since_start = 1000;
    run(h, 20, [](float t) {
        Inputs in = driving(Kind::race, t);
        in.behind_s = 0.35f;
        return in;
    });
    check(h.cause == Cause::defending, "a car on your gearbox: the cause is defending");
}

void calming()
{
    Heart h;
    reset(h, Kind::race, 2);
    h.since_start = 1000;
    run(h, 40, [](float t) {
        Inputs in = driving(Kind::race, t);
        in.ahead_s = 0.6f;
        return in;
    });
    float racing = h.bpm;
    run(h, 40, [](float t) {
        Inputs in = driving(Kind::race, t);
        in.ahead_s = 0.6f;
        in.safety_car = 1;
        in.speed = 45;
        in.g_lat = 1.0f;
        return in;
    });
    check(h.bpm < racing - 15, "behind the safety car: the heart comes down", racing - h.bpm);
    check(h.battle < 0.05f, "behind the safety car: no fight");
    run(h, 60, [](float) {
        Inputs in = driving(Kind::race, 0);
        in.red_flag = true;
        in.speed = 0;
        in.in_pit = true;
        in.on_track = false;
        return in;
    });
    check(h.bpm < 135, "a red flag in the pit lane: down below the race's base", h.bpm);
}

void contact()
{
    Heart h;
    reset(h, Kind::race, 4);
    h.since_start = 1000;
    run(h, 40, [](float t) { return driving(Kind::race, t); });
    float before = h.bpm;
    spike(h, 0.6f, Cause::contact);
    run(h, 3, [](float t) { return driving(Kind::race, t); });
    check(h.bpm > before + 12, "contact: a jump within three seconds", h.bpm - before);
    check(h.cause == Cause::contact, "contact: named");
    run(h, 60, [](float t) { return driving(Kind::race, t); });
    check(h.adrenaline < 0.02f, "a minute later the adrenaline is gone", h.adrenaline);
    check(std::fabs(h.bpm - before) < 6, "a minute later the heart is back", h.bpm - before);
}

void moments()
{
    for (int braking = 0; braking < 2; braking++) {
        Heart h;
        reset(h, Kind::race, 6);
        h.since_start = 1000;
        run(h, 10, [](float t) { return driving(Kind::race, t); });
        int spikes = h.stats.spikes;
        float speed = 70;
        run(h, 1.0f, [&](float) {
            Inputs in = driving(Kind::race, 0);
            speed = std::max(15.0f, speed - 40 * 0.05f); // 40 m/s² for about a second
            in.speed = speed;
            in.brake = braking ? 1.0f : 0.0f;
            return in;
        });
        if (braking)
            check(h.stats.spikes == spikes, "hard braking is not a moment");
        else
            check(h.stats.spikes == spikes + 1 && h.spike_cause == Cause::moment, "a sudden loss of speed: a moment");
    }
}

void fatigue()
{
    Heart h;
    reset(h, Kind::race, 8);
    h.since_start = 1000;
    run(h, 90 * 60, [](float t) {
        Inputs in = driving(Kind::race, t);
        in.ahead_s = std::fmod(t, 60.0f) < 25 ? 0.5f : -1.0f; // a fight now and then
        return in;
    });
    check(h.fatigue > 0.3f && h.fatigue < 0.95f, "a 90-minute race tires the driver", h.fatigue);
    Heart fresh;
    reset(fresh, Kind::race, 8);
    fresh.since_start = 1000;
    run(fresh, 60, [](float t) { return driving(Kind::race, t); });
    run(h, 60, [](float t) { return driving(Kind::race, t); });
    check(h.bpm > fresh.bpm + 2, "a tired heart beats faster for the same work", h.bpm - fresh.bpm);
    check(h.stats.time > 90 * 60 && h.stats.peak >= h.stats.sum / h.stats.time, "the session's numbers add up");
}

// The fatigue of a 90-minute race at 25, 50 and 100 %, in clean air and with a fight now and then (printed with
// "model_test curve"). The aim: 0.45-0.55 in clean air at the flag, 0.65-0.8 with fights.
float g_curve[2][3];

void fatigue_curve(bool print)
{
    for (int fight = 0; fight < 2; fight++) {
        Heart h;
        reset(h, Kind::race, 8);
        h.since_start = 1000;
        float marks[3] = {0.25f * 5400, 0.5f * 5400, 5400};
        int m = 0;
        for (float t = 0; t < 5400 && m < 3; t += 0.05f) {
            Inputs in = driving(Kind::race, t);
            if (fight)
                in.ahead_s = std::fmod(t, 60.0f) < 25 ? 0.5f : -1.0f;
            update(h, in);
            if (t + 0.05f >= marks[m])
                g_curve[fight][m++] = h.fatigue;
        }
        if (print)
            std::printf("fatigue %s: 25%% %.3f, 50%% %.3f, 100%% %.3f (bpm %.0f)\n", fight ? "with fights" : "clean air",
                        g_curve[fight][0], g_curve[fight][1], g_curve[fight][2], h.bpm);
    }
    check(g_curve[0][2] >= 0.45f && g_curve[0][2] <= 0.55f, "90 minutes in clean air: fatigue 0.45-0.55", g_curve[0][2]);
    check(g_curve[1][2] >= 0.65f && g_curve[1][2] <= 0.8f, "90 minutes with fights: fatigue 0.65-0.8", g_curve[1][2]);
    check(g_curve[0][0] < g_curve[0][1] && g_curve[0][1] < g_curve[0][2], "fatigue builds through the race");
    check(g_curve[1][2] > g_curve[0][2] + 0.1f, "fights tire more than clean air", g_curve[1][2] - g_curve[0][2]);
}

void no_pedals()
{
    Heart h;
    reset(h, Kind::race, 6);
    h.since_start = 1000;
    run(h, 10, [](float t) { return driving(Kind::race, t); });
    int spikes = h.stats.spikes;
    float speed = 70;
    run(h, 1.0f, [&](float) {
        Inputs in = driving(Kind::race, 0);
        speed = std::max(15.0f, speed - 40 * 0.05f);
        in.speed = speed;
        in.have_pedals = false; // the brake can't be read: a hard stop could be braking
        in.brake = 0;
        return in;
    });
    check(h.stats.spikes == spikes, "no pedals read: a sudden stop is not called a moment");
}

// ---- three real races, replayed ----
// tests/fixtures/canada_3_races.csv (tools/pulse_fixture.py from a game session, 2026-11-13): three 5-lap races at
// Montreal in the midfield, the player's inputs second by second for the first two minutes and every 10 s after,
// lights out and every contact. The heart has to spend most of a busy race in the zone, not in the red.
struct Probe {
    int race = 0;
    float t = 0, speed = 0, glat = 0, glong = 0, brake = 0, ahead = -1, behind = -1;
    int pos = 0, sc = 0, pit = 0;
};
struct Event {
    char kind = 0;
    int race = 0;
    float t = 0;
};

bool load_fixture(std::vector<Probe> &probes, std::vector<Event> &events)
{
    FILE *f = std::fopen("tests/fixtures/canada_3_races.csv", "r");
    if (!f)
        return false;
    char line[256];
    while (std::fgets(line, sizeof line, f)) {
        if (line[0] == 'P') {
            Probe p;
            if (std::sscanf(line, "P,%d,%f,%f,%f,%f,%f,%f,%f,%d,%d,%d", &p.race, &p.t, &p.speed, &p.glat, &p.glong,
                            &p.brake, &p.ahead, &p.behind, &p.pos, &p.sc, &p.pit) == 11)
                probes.push_back(p);
        } else if (line[0]) {
            Event e;
            e.kind = line[0];
            if (std::sscanf(line + 1, ",%d,%f", &e.race, &e.t) == 2)
                events.push_back(e);
        }
    }
    std::fclose(f);
    return !probes.empty();
}

struct RaceResult {
    float average = 0, red = 0, zone = 0, peak = 0;
    int score = 0;
};

RaceResult replay(int race, const std::vector<Probe> &all, const std::vector<Event> &events)
{
    std::vector<Probe> ps;
    for (const Probe &p : all)
        if (p.race == race)
            ps.push_back(p);
    float lights = 1e9f, end = ps.back().t;
    std::vector<float> contacts;
    for (const Event &e : events) {
        if (e.race != race)
            continue;
        if (e.kind == 'L')
            lights = e.t;
        else if (e.kind == 'C')
            contacts.push_back(e.t);
        else if (e.kind == 'E')
            end = e.t;
    }
    Heart h;
    reset(h, Kind::race, 19);
    size_t i = 0, c = 0;
    bool out = false;
    const float dt = 0.05f;
    for (float t = 0; t < end; t += dt) {
        while (i + 1 < ps.size() && ps[i + 1].t <= t)
            i++;
        const Probe &a = ps[i];
        const Probe &b = i + 1 < ps.size() ? ps[i + 1] : a;
        float k = b.t > a.t ? std::clamp((t - a.t) / (b.t - a.t), 0.0f, 1.0f) : 0.0f;
        auto mix = [&](float x, float y) { return x + (y - x) * k; };
        if (!out && t >= lights) {
            lights_out(h);
            out = true;
        }
        while (c < contacts.size() && contacts[c] <= t) {
            spike(h, 0.6f, Cause::contact);
            c++;
        }
        Inputs in;
        in.dt = dt;
        in.kind = Kind::race;
        in.known = true;
        in.speed = mix(a.speed, b.speed);
        in.have_pedals = true;
        in.brake = mix(a.brake, b.brake);
        in.have_g = true;
        in.g_lat = mix(a.glat, b.glat);
        in.g_long = mix(a.glong, b.glong);
        // the session's rule: no gaps for a car standing still
        bool moving = in.speed >= 10;
        in.ahead_s = moving && a.ahead >= 0 ? (b.ahead >= 0 ? mix(a.ahead, b.ahead) : a.ahead) : -1;
        in.behind_s = moving && a.behind >= 0 ? (b.behind >= 0 ? mix(a.behind, b.behind) : a.behind) : -1;
        in.position = a.pos;
        in.cars = 20;
        in.safety_car = a.sc;
        in.in_pit = a.pit != 0;
        in.on_track = !in.in_pit;
        in.formation = !out;
        in.count = true;
        update(h, in);
    }
    RaceResult r;
    const Stats &s = h.stats;
    r.average = float(s.sum / s.time);
    r.red = s.zone_time[int(Zone::red)] / s.time;
    r.zone = s.zone_time[int(Zone::zone)] / s.time;
    r.peak = s.peak;
    r.score = composure(s);
    return r;
}

void real_races(bool print)
{
    std::vector<Probe> probes;
    std::vector<Event> events;
    if (!load_fixture(probes, events)) {
        check(false, "tests/fixtures/canada_3_races.csv can be read (run from native\\)");
        return;
    }
    for (int race = 1; race <= 3; race++) {
        RaceResult r = replay(race, probes, events);
        if (print)
            std::printf("race %d: average %.1f, peak %.0f, in the zone %.0f%%, red %.0f%%, zone control %d\n", race,
                        r.average, r.peak, r.zone * 100, r.red * 100, r.score);
        // the replay has no overtakes (the log does not keep them), so the game runs a little hotter than this
        check(r.average >= 163 && r.average <= 174, "a busy real race: an average in the zone", r.average);
        check(r.red <= 0.28f, "a busy real race: no more than a quarter of it in the red", r.red);
        check(r.zone >= 0.6f, "a busy real race: most of it in the zone", r.zone);
        check(r.peak >= kRedFrom, "a busy real race: the start or the contacts still reach the red", r.peak);
        check(r.score >= 70, "a busy real race: zone control 70 or more", r.score);
    }
}

void scoring()
{
    Stats zone;
    zone.time = 100;
    zone.zone_time[int(Zone::zone)] = 100;
    check(composure(zone) == 100 && std::string(composure_grade(100)) == "S", "all in the zone: 100, S");
    Stats red;
    red.time = 100;
    red.zone_time[int(Zone::red)] = 70;
    red.zone_time[int(Zone::zone)] = 30;
    check(composure(red) == 30 && std::string(composure_word(30)) == "RATTLED", "mostly red: rattled");
    Stats none;
    check(composure(none) == 0, "nothing counted: 0");
    check(zone_of(139) == Zone::calm && zone_of(140) == Zone::focused && zone_of(160) == Zone::zone &&
              zone_of(178) == Zone::red,
          "the zones' edges");
    for (int z = 0; z < 4; z++)
        check(zone_name(Zone(z))[0] != 0, "every zone has a name");
}

void shapes()
{
    check(std::fabs(ecg(0) - 1.0f) < 0.05f, "the ECG's R spike is at 0", ecg(0));
    check(ecg(0.06f) < 0, "the S dip follows the spike", ecg(0.06f));
    check(ecg(0.5f) < 0.05f && ecg(0.5f) > -0.05f, "between beats the line is flat", ecg(0.5f));
    float most = 0;
    for (float p = 0; p < 1; p += 0.01f) {
        float b = beat_wave(p);
        check(b >= 0 && b <= 1, "the beat stays in 0..1", b);
        most = std::max(most, b);
    }
    check(most > 0.95f, "the beat reaches 1");
    check(beat_wave(0.3f) > 0.4f && beat_wave(0.3f) < 0.7f, "the second bump is weaker", beat_wave(0.3f));
}

void random_inputs()
{
    std::mt19937 rng(42);
    std::uniform_real_distribution<float> u(0, 1);
    for (int run_i = 0; run_i < 20; run_i++) {
        Heart h;
        reset(h, Kind(run_i % 4), uint32_t(run_i));
        bool ok = true;
        for (int i = 0; i < 4000; i++) {
            Inputs in;
            in.dt = u(rng) * 0.6f;
            in.kind = Kind(run_i % 4);
            in.known = u(rng) > 0.1f;
            in.speed = u(rng) * 100;
            in.brake = u(rng);
            in.have_g = u(rng) > 0.2f;
            in.g_lat = (u(rng) - 0.5f) * 12;
            in.g_long = (u(rng) - 0.5f) * 12;
            in.ahead_s = u(rng) * 3 - 0.5f;
            in.behind_s = u(rng) * 3 - 0.5f;
            in.position = int(u(rng) * 22);
            in.safety_car = int(u(rng) * 3);
            in.red_flag = u(rng) > 0.97f;
            in.in_pit = u(rng) > 0.9f;
            in.finished = u(rng) > 0.95f;
            in.on_track = !in.in_pit && !in.finished;
            in.count = true;
            if (u(rng) > 0.99f)
                spike(h, u(rng), Cause::contact);
            update(h, in);
            ok = ok && h.bpm >= 55 && h.bpm <= 205 && std::isfinite(h.bpm) && h.fatigue >= 0 && h.fatigue <= 1 &&
                 h.adrenaline >= 0 && h.adrenaline <= 1 && h.phase >= 0 && h.phase < 1 && stress(h) >= 0 &&
                 stress(h) <= 1 && std::isfinite(shown_bpm(h));
        }
        check(ok, "random inputs keep the heart in range");
    }
}

} // namespace

int main(int argc, char **argv)
{
    bool curve = argc > 1 && std::string(argv[1]) == "curve";
    practice_alone();
    race_start();
    fights();
    calming();
    contact();
    moments();
    fatigue();
    fatigue_curve(curve);
    no_pedals();
    real_races(curve);
    scoring();
    shapes();
    random_inputs();
    if (g_failed) {
        std::printf("model_test: %d of %d checks FAILED\n", g_failed, g_checks);
        return 1;
    }
    std::printf("model_test: all %d checks passed\n", g_checks);
    return 0;
}
