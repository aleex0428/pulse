// SPDX-License-Identifier: MIT
// The session against the SDK's fake live state (swofty/fake_live.hpp) and a fake UDP participant table:
// - no live state; a race from the grid to the flag (the start, a car on your gearbox and its card, contact and its
//   flash, the pause, track limits, a lost place, the safety car, a spectator, the report);
// - the session's life: closed stays closed while the live state still holds the session, a restart is a new
//   attempt, the next session starts clean, a new generation and type reopen it;
// - events: none in the pause, ten exchanges of a place give at most one spike, the overtakes' cap, a flashback
//   takes back what came after it, contact in qualifying counts;
// - the G and the pedals: G lost, braking G without the pedal, the same at 30 and 144 fps;
// - the screen effect's channels: strength 0, every effect off, comfort, reduced and still motion;
// - the report: its own wall clock (with no session, the pause not counted), the next session, a restart, a
//   retirement;
// - the subject (watching your own car, watching another), the card (one rival at a time, the safety car, the pit
//   exit, a lapped car), the bus's ids and the names from UDP;
// - a practice too short for a report, qualifying's report, a time trial.
// The screen effect's data is caught where the plugin hands it to the core.
//
//   plugin_test.exe [verbose]
#include <swofty/mixin.hpp>

#include <swofty/fake_live.hpp>

#include "../src/session.hpp"

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <vector>

using namespace pulse;

namespace {

int g_checks = 0, g_failed = 0;
bool g_verbose = false;

void check(bool ok, const char *what, double got = NAN)
{
    g_checks++;
    if (!ok) {
        g_failed++;
        if (std::isnan(got))
            std::printf("FAIL: %s\n", what);
        else
            std::printf("FAIL: %s (got %.3f)\n", what, got);
    }
}

void log_(const char *fmt, ...)
{
    if (!g_verbose)
        return;
    va_list a;
    va_start(a, fmt);
    std::vprintf(fmt, a);
    std::printf("\n");
    va_end(a);
}

int no_setting(const char *, uint32_t, SwoftySettingValue *) { return SWOFTY_ASSET_UNAVAILABLE; }

// ---- the wall clock the report keeps ----
double g_wall = 1000;
double fake_now() { return g_wall; }

// ---- a fake UDP participant table ----
SwoftyParticipant g_parts[24];
bool g_udp = false;
int t_available() { return 1; }
int t_session(SwoftySessionState *) { return SWOFTY_ASSET_UNAVAILABLE; }
int t_car(uint32_t, SwoftyCarState *) { return SWOFTY_ASSET_UNAVAILABLE; }
int t_participant(uint32_t i, SwoftyParticipant *out)
{
    if (!g_udp || i >= 24 || g_parts[i].position <= 0)
        return SWOFTY_ASSET_UNAVAILABLE;
    *out = g_parts[i];
    return SWOFTY_ASSET_OK;
}
int t_shared(const char **) { return SWOFTY_ASSET_UNAVAILABLE; }
SwoftyTelemetry g_tel{t_available, t_session, t_car, t_participant, t_shared};
SwoftyApi g_api{};

constexpr float kTrack = 5000;
constexpr uint64_t kHave = SWOFTY_LIVE_HAVE_IDENT | SWOFTY_LIVE_HAVE_SPEED | SWOFTY_LIVE_HAVE_PEDALS |
                           SWOFTY_LIVE_HAVE_G | SWOFTY_LIVE_HAVE_PROGRESS | SWOFTY_LIVE_HAVE_CLASS |
                           SWOFTY_LIVE_HAVE_PIT | SWOFTY_LIVE_HAVE_PENALTIES;

// A grid of `n` cars, the player in car 0, everyone 120 m apart round the lap.
struct Grid {
    swofty::fake::Live live;
    std::vector<FxData> fx;
    Session session;
    int n;
    bool fixed_g = false; // every car pulls glat/glong instead of the made-up corners
    float glat = 0, glong = 0;
    int udp_shift = -1;   // >= 0: the UDP table follows the cars, participant i being car (i + shift) % n
    int udp_off = 0;      // UDP gives every participant a place this many off

    Grid(uint32_t type, int cars = 20, Settings set = Settings{}) : n(cars)
    {
        auto &s = live.session();
        s.type = type;
        s.racers = uint32_t(n);
        s.track_length_m = kTrack;
        s.safety_car = 0;
        s.red_flag = 0;
        s.generation = 1;
        s.have |= SWOFTY_LIVE_SHAVE_TRACK_LENGTH | SWOFTY_LIVE_SHAVE_SAFETY_CAR | SWOFTY_LIVE_SHAVE_RED_FLAG;
        std::vector<uint32_t> order;
        for (int v = 0; v < n; v++) {
            auto &c = live.add_car(uint32_t(v), 0);
            c.have = kHave;
            c.result_status = 2;
            c.speed = 0;
            c.lap_m = std::fmod(2000.0f - 120.0f * v + kTrack, kTrack);
            order.push_back(uint32_t((v + 4) % n));
        }
        live.set_order(order);
        live.set_player(0);
        live.set_badge(1, "ferrari_2026");
        live.install();
        session.configure(set);
        session.set_clock(fake_now);
        session.set_sink([this](const FxData &f) { fx.push_back(f); });
        session.session_start();
    }

    float total(int v) { return float(live.car(uint32_t(v)).lap) * kTrack + live.car(uint32_t(v)).lap_m; }

    // puts car v `offset` metres from car `from` along the road (the lap counted as the game counts it)
    void place(int v, float offset, int from = 0)
    {
        float t = total(from) + offset;
        auto &c = live.car(uint32_t(v));
        c.lap = int(std::floor(t / kTrack));
        c.lap_m = t - float(c.lap) * kTrack;
    }

    // everyone apart: no fights
    void spread()
    {
        for (int v = 1; v < n; v++)
            place(v, 240.0f * v);
    }

    // a flashback: every car back `metres`
    void rewind(float metres)
    {
        for (int v = 0; v < n; v++)
            place(v, -metres, v);
    }

    // the UDP table as the game would fill it
    void fill_udp()
    {
        std::memset(g_parts, 0, sizeof g_parts);
        for (int i = 0; i < n && i < 24; i++) {
            int v = (i + udp_shift) % n;
            auto &c = live.car(uint32_t(v));
            g_parts[i].version = 2;
            g_parts[i].position = (c.position - 1 + udp_off) % n + 1;
            g_parts[i].lap_distance = c.lap_m;
            g_parts[i].driver_id = uint32_t(100 + v);
            std::snprintf(g_parts[i].name, sizeof g_parts[i].name, "Driver%d", v);
        }
        g_udp = true;
    }

    // drives everyone for `seconds`: speed, corners, distance (the lap goes up at the line)
    void drive(float seconds, float speed = 70, bool paused = false, float dt = 1.0f / 60.0f)
    {
        for (float t = 0; t < seconds - 1e-4f; t += dt) {
            for (int v = 0; v < n; v++) {
                auto &c = live.car(uint32_t(v));
                if (c.result_status != 2)
                    continue;
                if (!paused) {
                    c.speed = speed;
                    c.lap_m += speed * dt;
                    if (c.lap_m >= kTrack) {
                        c.lap_m -= kTrack;
                        c.lap++;
                    }
                    if (fixed_g) {
                        c.g_lat = glat;
                        c.g_long = glong;
                    } else {
                        c.g_lat = speed > 0 ? 2.5f * std::sin(c.lap_m / 300.0f) : 0.0f;
                        c.g_long = speed > 0 ? 0.5f : 0.0f;
                    }
                }
            }
            if (udp_shift >= 0)
                fill_udp();
            live.advance();
            g_wall += dt;
            session.tick(dt, paused);
        }
    }

    // the wall clock goes on with no live session to tick (the menus after a session)
    void menus(float seconds, bool paused = false)
    {
        live.set_available(false);
        for (float t = 0; t < seconds; t += 1.0f / 30) {
            g_wall += 1.0f / 30;
            session.tick(1.0f / 30, paused);
        }
        live.set_available(true);
    }

    const Heart &me() const { return *session.heart(0); }
    bool any_active() const
    {
        for (const FxData &f : fx)
            if (f.active > 0)
                return true;
        return false;
    }
};

void no_live()
{
    swofty::fake::Live live;
    live.set_available(false);
    live.install();
    Session s;
    std::vector<FxData> fx;
    s.set_sink([&](const FxData &f) { fx.push_back(f); });
    s.session_start();
    for (int i = 0; i < 120; i++)
        s.tick(1.0f / 60, false);
    View v = s.view();
    check(v.live && !v.signal, "a session with no live state: NO SIGNAL");
    check(fx.empty(), "no live state: the effect gets nothing");
    check(s.heart(0) == nullptr, "no live state: no hearts");
    Session idle;
    for (int i = 0; i < 60; i++)
        idle.tick(1.0f / 60, false);
    check(!idle.view().live && idle.phase() == Phase::idle, "no session at all: nothing on screen");
}

void race()
{
    Grid g(15);
    g.drive(20, 0); // the grid, before the lights
    check(g.session.heart(19) != nullptr, "every car on the grid has a heart");
    float grid = g.me().bpm;
    check(grid > 145 && grid < 175, "on the grid: nerves", grid);
    check(g.fx.empty() || g.fx.back().active == 0, "on the grid, calm and still: the effect rests");
    check(g.me().battle < 0.01f, "standing on the grid, cars all round: no fight", g.me().battle);
    check(grid < 170, "on the grid: nerves, not the red zone", grid);
    g.session.race_start();
    check(g.session.phase() == Phase::attempt, "lights out: the attempt");
    g.drive(6);
    check(g.me().bpm > 175, "lights out: the surge", g.me().bpm);
    check(g.fx.back().active == 1 && g.fx.back().tunnel > 0.05f, "lights out: tunnel vision", g.fx.back().tunnel);
    // a long, lonely stint brings it down
    g.spread();
    g.drive(90);
    float alone = g.me().bpm;
    check(alone < 175 && alone > 150, "alone after the start: the race's rhythm", alone);
    check(!g.session.view().card.show, "alone: no card");

    // a Ferrari 30 m behind for a while: defending, and the card
    g.place(1, -30);
    g.drive(5);
    View v = g.session.view();
    check(g.me().bpm > alone + 5, "a car on your gearbox: the heart goes up", g.me().bpm - alone);
    check(v.card.show, "a fight of a few seconds: the biometrics card");
    check(v.card.mode == "DEFENDING" && v.card.title == "UNDER PRESSURE", "the card says you are defending");
    check(v.card.rival.label == "FERRARI", "the rival is named by its team badge without UDP");
    check(v.card.you.label == "YOU" && v.card.you.bpm > 100, "the card shows your heart");
    g.drive(Session::kCardSeconds + 1);
    check(!g.session.view().card.show, "the card goes after its time");

    // contact with the Ferrari: a spike, the flash and the blur
    float before = g.me().bpm;
    g.session.collision(0, 1, false);
    g.drive(0.05f);
    check(g.fx.back().flash > 0.3f && g.fx.back().blur > 0.3f, "contact: the flash and the blur",
          g.fx.back().flash);
    g.drive(1.5f);
    check(g.fx.back().flash == 0, "the flash is brief");
    check(g.me().bpm > before + 5, "contact: the heart jumps", g.me().bpm - before);
    int spikes = g.me().stats.spikes;
    g.session.collision(1, 0, false); // the same contact again, at once
    check(g.me().stats.spikes == spikes, "a repeated collision message counts once");

    // pause: nothing moves, the effect stops, the view says paused, the events are ignored
    float bpm = g.me().bpm;
    size_t sent = g.fx.size();
    g.drive(5, 70, true);
    check(g.me().bpm == bpm, "paused: the heart waits");
    check(g.fx.size() <= sent + 1 && g.fx.back().active == 0, "paused: the effect is told once to stop");
    check(g.session.view().paused, "paused: the view says so");
    g.session.collision(0, 2, true);
    g.session.overtake(2, 0, true);
    check(g.me().stats.spikes == spikes, "paused: a collision and an overtake count nothing");
    g.drive(1);
    check(g.fx.back().active == 1, "resumed: the effect is back");

    // track limits and a lost place
    g.live.car(0).track_limit_warnings = 1;
    g.drive(0.1f);
    check(g.me().spike_cause == Cause::track_limits, "a track limits warning: a spike");
    // right after the contact the adrenaline is past the overtakes' cap: a lost place adds nothing
    spikes = g.me().stats.spikes;
    g.session.overtake(1, 0, false);
    check(g.me().stats.spikes == spikes, "a lost place with the adrenaline past the cap: nothing more");

    // the safety car
    g.spread();
    g.drive(30);
    float racing = g.me().bpm;
    g.live.session().safety_car = 1;
    g.drive(40, 45);
    check(g.me().bpm < racing - 12, "the safety car calms the heart", racing - g.me().bpm);
    g.live.session().safety_car = 0;
    g.drive(Session::kPairWindow);
    g.session.overtake(2, 0, false);
    check(g.me().spike_cause == Cause::lost_place, "a lost place, calm again: a spike");

    // the spectator: the monitor follows the watched car, the effect stops
    g.live.set_watched(3);
    g.drive(1);
    v = g.session.view();
    check(v.spectating && v.subject != "YOU" && !v.subject.empty(), "spectating: the watched car's heart, labelled");
    check(g.fx.back().active == 0, "spectating: no effect on your screen");
    g.live.session().watched = -1;
    g.live.session().subject = 0;
    g.live.session().have &= ~uint64_t(SWOFTY_LIVE_SHAVE_WATCHED);
    g.drive(1);
    check(!g.session.view().spectating && g.session.view().subject == "YOU", "back in the car");

    // the flag: the report
    g.live.car(0).result_status = 3;
    g.drive(0.1f);
    v = g.session.view();
    check(v.report.show, "the flag: the report");
    check(v.report.peak > 175 && v.report.average > 140 && v.report.average < v.report.peak, "the report's numbers",
          v.report.peak);
    float share = 0;
    for (float z : v.report.zone_share)
        share += z;
    check(std::fabs(share - 1) < 0.01f, "the zones add up to the session", share);
    check(v.report.score >= 0 && v.report.score <= 100 && !v.report.word.empty() && !v.report.grade.empty(),
          "the report's zone control");
    check(v.report.seconds > 150 && v.report.seconds < 400, "the report counts the session's seconds", v.report.seconds);
    check(!v.report.peak_cause.empty() && v.report.peak_where.rfind("LAP ", 0) == 0, "the peak has a cause and a lap");
    g.session.dismiss_report();
    check(!g.session.view().report.show, "the key closes the report");
    g.session.session_end(0); // after the flag: no second report
    check(!g.session.view().report.show, "one report a session");
}

// P1: the session's life
void lifecycle()
{
    {
        Grid g(15);
        g.drive(5, 0);
        g.session.race_start();
        g.drive(30);
        g.session.session_end(0);
        check(g.session.phase() == Phase::closed, "session.end: closed");
        size_t sent = g.fx.size();
        g.drive(5); // the live state still holds the session
        View v = g.session.view();
        check(!v.live, "closed: no monitor while the live state lags behind");
        bool woke = false;
        for (size_t i = sent; i < g.fx.size(); i++)
            woke = woke || g.fx[i].active > 0;
        check(!woke, "closed: the effect never comes back");
        check(g.session.phase() == Phase::closed, "closed: the same snapshot does not reopen it");
        // a new generation alone is not enough; a new generation and a new type are
        g.live.session().generation++;
        g.drive(1);
        check(g.session.phase() == Phase::closed, "closed: a new generation of the same session type stays closed");
        g.live.session().type = 16;
        g.live.session().generation++;
        g.drive(1);
        check(g.session.phase() == Phase::session && g.session.view().live, "a new generation and type: a new session");
    }
    {
        // a restart: race.start again in the same session
        Grid g(15);
        g.drive(5, 0);
        g.session.race_start();
        g.drive(60);
        g.session.collision(0, 1, false);
        g.drive(3);
        float bpm = g.me().bpm;
        check(g.me().stats.spikes >= 2 && g.me().stats.time > 60, "before the restart: a minute counted");
        g.session.race_start();
        check(g.session.phase() == Phase::attempt, "a restart: a new attempt");
        check(g.me().stats.spikes == 1 && g.me().stats.time == 0, "a restart: the counts start again (one spike: the start)");
        check(std::fabs(g.me().bpm - bpm) < 0.01f, "a restart: the heart keeps beating where it was");
        check(g.me().fatigue == 0, "a restart: no fatigue carried over");
        g.drive(30);
        g.session.session_end(1);
        check(!g.session.view().report.show, "session.end for a restart: no report");
        g.session.session_start();
        check(g.session.heart(0) == nullptr && !g.session.view().report.show, "the next session starts clean");
        g.drive(1);
        check(g.session.heart(0) && g.me().stats.spikes == 0 && g.me().stats.time < 2, "the next session: fresh hearts");
    }
    {
        // the track loading before session.start: a type 0 snapshot is no session
        Grid z(0);
        Session loading;
        loading.set_clock(fake_now);
        for (int i = 0; i < 30; i++)
            loading.tick(1.0f / 60, false);
        check(loading.phase() == Phase::idle && !loading.view().live, "a type 0 snapshot (a track loading): no session");
    }
    {
        // the addon loaded while a session was already on: the live state opens it
        Grid g(1);
        Session late;
        late.set_clock(fake_now);
        for (int i = 0; i < 30; i++)
            late.tick(1.0f / 60, false);
        check(late.phase() == Phase::session && late.heart(0), "a session already on: the live state opens it");
        g.session.session_unload();
        check(g.session.phase() == Phase::closed, "session.unload closes an open session");
    }
}

// P2: events in the pause, duplicates, flashbacks
void events()
{
    {
        Grid g(15);
        g.drive(5, 0);
        g.session.race_start();
        g.spread();
        g.drive(40);
        int spikes = g.me().stats.spikes;
        float adrenaline = g.me().adrenaline;
        // ten exchanges of a place with the same car, half a second apart
        for (int i = 0; i < 10; i++) {
            if (i % 2 == 0)
                g.session.overtake(1, 0, false);
            else
                g.session.overtake(0, 1, false);
            g.drive(0.5f);
        }
        check(g.me().stats.spikes <= spikes + 1, "ten exchanges: at most one spike", g.me().stats.spikes - spikes);
        check(g.me().adrenaline <= adrenaline + 0.05f, "a place given back: its spike is taken back", g.me().adrenaline);
        g.drive(Session::kPairWindow);
        int before = g.me().stats.spikes;
        g.session.overtake(0, 3, false);
        check(g.me().stats.spikes == before + 1 && g.me().spike_cause == Cause::gained_place, "a real pass counts");
        // a train of cars past you: the adrenaline stops at the cap
        g.drive(1);
        for (int v = 4; v < 14; v++)
            g.session.overtake(v, 0, false);
        check(g.me().adrenaline <= Session::kOvertakeCap + 0.01f, "overtakes alone stop at the cap", g.me().adrenaline);
        // a car that is out counts nothing
        g.live.car(5).result_status = 7;
        g.drive(0.2f);
        int now = g.session.heart(5)->stats.spikes;
        g.session.collision(5, 6, false);
        check(g.session.heart(5)->stats.spikes == now, "a retired car: no spike");
    }
    {
        // a flashback after a contact
        Grid g(15);
        g.drive(5, 0);
        g.session.race_start();
        g.spread();
        g.drive(60);
        g.session.collision(0, 2, false); // well before the flashback point: kept
        g.drive(12);
        int kept = g.me().stats.spikes;
        float calm = g.me().adrenaline;
        g.drive(1);
        g.session.collision(0, 1, false); // then contact, and the player rewinds
        g.drive(2);
        check(g.me().stats.spikes == kept + 1, "the contact counted");
        g.rewind(300);
        g.drive(0.1f);
        check(g.me().stats.spikes == kept, "a flashback takes back the contact after its point", g.me().stats.spikes - kept);
        check(g.me().adrenaline < calm + 0.05f, "a flashback: the adrenaline as before the contact", g.me().adrenaline);
        check(g.fx.back().flash == 0 && g.fx.back().blur == 0, "a flashback: no flash, no blur");
        // the positions it shuffles are no overtakes
        int s = g.me().stats.spikes;
        g.session.overtake(4, 0, false);
        check(g.me().stats.spikes == s, "just after a flashback: the shuffled places are ignored");
    }
    {
        // contact in qualifying still counts
        Grid q(6);
        q.drive(20);
        int spikes = q.me().stats.spikes;
        q.session.collision(0, 1, false);
        q.drive(0.1f);
        check(q.me().stats.spikes == spikes + 1 && q.me().spike_cause == Cause::contact, "contact in qualifying: a spike");
        q.session.overtake(1, 0, false);
        check(q.me().stats.spikes == spikes + 1, "no overtakes outside a race");
    }
}

// P4: G and pedals
void forces()
{
    {
        Grid g(1);
        g.fixed_g = true;
        g.glat = 4.2f;
        g.drive(3);
        check(g.fx.back().g_side > 0.3f, "4.2 G to one side: the greyout", g.fx.back().g_side);
        g.live.car(0).have &= ~uint64_t(SWOFTY_LIVE_HAVE_G);
        g.drive(0.4f);
        check(std::fabs(g.session.fx().g_side) < 0.02f, "G no longer read: it fades out within 0.4 s", g.session.fx().g_side);
    }
    {
        Grid g(1);
        g.fixed_g = true;
        g.glong = -5.0f;
        g.drive(2);
        check(g.session.fx().g_brake == 0, "5 G along the car without the brake: not braking", g.session.fx().g_brake);
        g.live.car(0).brake = 0.9f;
        g.drive(1);
        check(g.session.fx().g_brake > 0.2f, "5 G with the brake pressed: the top edge", g.session.fx().g_brake);
        g.live.car(0).have &= ~uint64_t(SWOFTY_LIVE_HAVE_PEDALS);
        g.drive(1);
        check(g.session.fx().g_brake < 0.01f, "no pedals read: no braking G", g.session.fx().g_brake);
    }
    {
        // the same ramp of G at 30 and at 144 fps
        float got[2];
        float rates[2] = {30, 144};
        for (int i = 0; i < 2; i++) {
            Grid g(1);
            g.fixed_g = true;
            float dt = 1.0f / rates[i];
            for (float t = 0; t < 0.6f - 1e-4f; t += dt) {
                g.glat = 6.0f * std::min(t / 0.5f, 1.0f);
                g.drive(dt, 70, false, dt);
            }
            got[i] = g.session.fx().g_side;
        }
        check(std::fabs(got[0] - got[1]) < 0.04f, "the G smoothing: the same at 30 and 144 fps", got[0] - got[1]);
    }
}

// P3: the channels
void channels()
{
    auto racing = [](Grid &g) {
        g.drive(5, 0);
        g.session.race_start();
        g.drive(5);
        g.session.collision(0, 1, false);
        g.drive(0.05f);
        g.drive(2);
    };
    {
        Settings s;
        s.effects = false;
        Grid g(15, 20, s);
        racing(g);
        check(g.fx.empty(), "effects off: the effect gets nothing");
    }
    {
        Settings s;
        s.strength = 0;
        Grid g(15, 20, s);
        racing(g);
        check(!g.any_active(), "strength 0: the effect is never active");
    }
    {
        Settings s;
        s.tunnel = s.heartbeat = s.gforce = s.impact = s.fatigue = false;
        Grid g(15, 20, s);
        racing(g);
        check(!g.any_active(), "every effect off: never active");
    }
    {
        Settings s;
        s.comfort = true;
        Grid g(15, 20, s);
        racing(g);
        float beat = 0, blur = 0, flash = 0, grain = 0, soft = 0, tunnel = 0;
        for (const FxData &f : g.fx) {
            beat = std::max(beat, f.beat);
            blur = std::max(blur, f.blur);
            flash = std::max(flash, f.flash);
            grain = std::max(grain, f.grain);
            soft = std::max(soft, f.soft);
            tunnel = std::max(tunnel, f.tunnel);
        }
        check(beat == 0 && blur == 0 && flash == 0 && grain == 0 && soft == 0, "comfort: no throb, blur, flash, grain or softness");
        check(tunnel > 0, "comfort: still some tunnel vision");
    }
    {
        Settings s;
        s.motion = Motion::still;
        Grid g(15, 20, s);
        racing(g);
        float moving = 0;
        for (const FxData &f : g.fx)
            moving = std::max({moving, f.beat, f.blur, f.flash, f.grain});
        check(moving == 0, "still: nothing throbs, blurs, flashes or crawls");
    }
    {
        // one grid at a time: the fake live state is one
        float beat_a = 0, beat_b = 0, blur_a = 0, blur_b = 0;
        {
            Grid a(15, 20, Settings{});
            racing(a);
            for (const FxData &f : a.fx)
                beat_a = std::max(beat_a, f.beat), blur_a = std::max(blur_a, f.blur);
        }
        {
            Settings reduced;
            reduced.motion = Motion::reduced;
            Grid b(15, 20, reduced);
            racing(b);
            for (const FxData &f : b.fx)
                beat_b = std::max(beat_b, f.beat), blur_b = std::max(blur_b, f.blur);
        }
        check(blur_b > 0 && blur_b <= blur_a * 0.55f, "reduced: half the blur", blur_b / std::max(blur_a, 1e-6f));
        check(beat_b <= beat_a * 0.55f + 1e-6f, "reduced: half the throb", beat_b);
    }
}

// P5: the report's own clock
void report_clock()
{
    {
        Grid g(15);
        g.drive(5, 0);
        g.session.race_start();
        g.drive(40);
        g.live.car(0).result_status = 3;
        g.drive(1);
        check(g.session.view().report.show, "the report shows at the flag");
        g.menus(30, true); // paused straight after: the pause does not count
        check(g.session.view().report.show, "thirty seconds of pause: the report still waits");
        g.session.session_end(0);
        g.menus(Session::kReportSeconds - 2);
        check(g.session.view().report.show, "in the menu: still there before its time");
        g.menus(3);
        check(!g.session.view().report.show, "in the menu with no session: the report goes after its time");
    }
    {
        Grid g(6);
        g.drive(60);
        g.session.session_end(0);
        check(g.session.view().report.show && g.session.view().report.kind == Kind::qualifying,
              "a qualifying session's end: its report");
        g.session.session_start();
        check(!g.session.view().report.show, "the next session: the report goes");
    }
    {
        Grid g(15);
        g.drive(5, 0);
        g.session.race_start();
        g.drive(40);
        g.live.car(0).result_status = 7; // retired
        g.drive(0.1f);
        check(g.session.view().report.show, "your race ends early: the report");
    }
}

// the subject and the card
void subject_and_card()
{
    {
        // watching your own car is driving
        Grid g(15);
        g.drive(5, 0);
        g.session.race_start();
        g.live.set_watched(0);
        g.drive(3);
        check(!g.session.view().spectating && g.session.view().subject == "YOU", "watching your own car: still you");
        check(g.fx.back().active == 1, "watching your own car: the effect stays");
    }
    {
        // a contact, then the spectator HUD: no card, ever
        Grid g(15);
        g.drive(5, 0);
        g.session.race_start();
        g.spread();
        g.drive(30);
        g.session.collision(0, 1, false);
        g.live.set_watched(4);
        g.drive(3);
        check(!g.session.view().card.show, "spectating: a pending contact opens no card");
    }
    {
        // one rival at a time: two cars taking turns on your gearbox never add up to a fight
        Grid g(15);
        g.drive(5, 0);
        g.session.race_start();
        g.spread();
        g.drive(60);
        for (int i = 0; i < 4; i++) {
            g.spread();
            g.place(1 + i % 2, -30);
            g.drive(2.2f);
        }
        check(!g.session.view().card.show, "rivals taking turns: the count starts again with each");
        g.drive(1.5f);
        check(g.session.view().card.show, "the same rival for over 3 s: the card");
    }
    {
        // the safety car, the pit exit and a lapped car: no fight to show
        Grid g(15);
        g.drive(5, 0);
        g.session.race_start();
        g.spread();
        g.drive(80);
        g.live.session().safety_car = 2;
        g.place(1, -30);
        g.drive(6, 40);
        check(!g.session.view().card.show, "under the virtual safety car: no card");
        g.live.session().safety_car = 0;
        g.spread();
        g.drive(80);
        g.live.car(0).pit_state = 2;
        g.drive(2, 20);
        g.live.car(0).pit_state = 0;
        g.place(1, -30);
        g.drive(4);
        check(!g.session.view().card.show, "just out of the pit: no card");
        g.spread();
        g.drive(80);
        g.place(1, -30 - kTrack); // a lap down, on your gearbox
        g.drive(6);
        check(!g.session.view().card.show, "a lapped car behind you: no card");
        check(g.me().battle < 0.2f, "a lapped car behind you: no fight in the heart either", g.me().battle);
    }
}

// the bus's ids and the names from UDP
void identity()
{
    g_api.telemetry = &g_tel;
    {
        // UDP agrees with the live state: the event ids are vehicle ids, and the names are used
        Grid g(15);
        g.drive(5, 0);
        g.session.race_start();
        g.spread();
        g.drive(60);
        g.place(1, -30);
        g.udp_shift = 0;
        g.drive(5);
        View v = g.session.view();
        check(v.card.show && v.card.rival.label == "DRIVER1", "UDP on and matching: the rival's name", 0);
    }
    {
        // the bus counts participants another way: participant i is vehicle i + 3
        Grid g(15);
        g.drive(5, 0);
        g.session.race_start();
        g.spread();
        g.drive(30);
        g.udp_shift = 3;
        g.fill_udp();
        int spikes = g.session.heart(5)->stats.spikes;
        g.session.collision(2, 7, false); // participants 2 and 7: vehicles 5 and 10
        check(g.session.heart(5)->stats.spikes == spikes + 1, "a participant id is read as the car UDP says it is");
    }
    {
        // a name whose participant sits in another place is not used
        Grid g(15);
        g.drive(5, 0);
        g.session.race_start();
        g.spread();
        g.drive(60);
        g.place(1, -30);
        g.udp_shift = 0;
        g.udp_off = 5; // every participant five places off: the name at the rival's place is a car far away
        g.drive(5);
        check(g.session.view().card.rival.label == "FERRARI", "UDP that does not match: the team badge, not a wrong name");
    }
    g_udp = false;
    g_api.telemetry = nullptr;
}

void short_sessions()
{
    Grid g(1);
    g.drive(10);
    check(g.session.kind() == Kind::practice, "type 1 is practice");
    g.session.session_end(0);
    check(!g.session.view().report.show, "ten seconds of practice: no report");
}

void time_trial()
{
    Grid g(18, 1);
    g.live.session().game_mode = SWOFTY_LIVE_MODE_TIME_TRIAL;
    g.live.session().have |= SWOFTY_LIVE_SHAVE_GAME_MODE;
    g.drive(30);
    check(g.session.kind() == Kind::time_trial, "time trial");
    check(g.me().bpm < 165, "time trial alone: no fight, no race nerves", g.me().bpm);
    check(!g.session.view().card.show, "time trial: no card");
}

void badges()
{
    check(team_of_badge("ferrari_2026") == "FERRARI", "badge: ferrari_2026");
    check(team_of_badge("red_bull_racing_2025") == "RED BULL RACING", "badge: red_bull_racing_2025");
    check(team_of_badge("haas") == "HAAS", "badge without a year");
    check(kind_of_type(3) == Kind::practice && kind_of_type(8) == Kind::qualifying && kind_of_type(12) == Kind::qualifying &&
              kind_of_type(16) == Kind::race && kind_of_type(18) == Kind::time_trial,
          "session types");
}

} // namespace

int main(int argc, char **argv)
{
    g_verbose = argc > 1 && std::string(argv[1]) == "verbose";
    g_api.version = SWOFTY_API_VERSION;
    g_api.log = log_;
    g_api.setting = no_setting;
    swofty::api = &g_api;
    no_live();
    race();
    lifecycle();
    events();
    forces();
    channels();
    report_clock();
    subject_and_card();
    identity();
    short_sessions();
    time_trial();
    badges();
    if (g_failed) {
        std::printf("plugin_test: %d of %d checks FAILED\n", g_failed, g_checks);
        return 1;
    }
    std::printf("plugin_test: all %d checks passed\n", g_checks);
    return 0;
}
