// SPDX-License-Identifier: MIT
// PULSE's heart: a driver's heart rate, strain, adrenaline and fatigue from what the car is doing. Plain C++, no game
// calls: the plugin feeds it what the live state says each tick, and the tests feed it made-up sessions.
//
// The heart chases a target: a base for the session (practice is calmer than a race), plus the pace, the sustained G,
// how close the nearest car is (defending counts more than attacking), what is at stake, the start, adrenaline from
// spikes (contact, a moment, a lost place, track limits, a penalty) and fatigue; the safety car, a red flag, the pit
// lane and the chequered flag bring it down. It rises in about 2 s and falls in about 8 s (slower when tired).
#pragma once
#include <cstdint>
#include <string>

namespace pulse {

enum class Kind : uint8_t { practice, qualifying, race, time_trial };
enum class Zone : uint8_t { calm, focused, zone, red };
enum class Cause : uint8_t {
    none,
    lights_out,
    contact,
    moment,
    wheel_to_wheel,
    defending,
    lost_place,
    gained_place,
    track_limits,
    penalty,
    g_load,
    pressure,
    restart,
};

// What one car does in one tick.
struct Inputs {
    float dt = 0;                 // seconds since the last update
    Kind kind = Kind::race;
    bool known = false;           // the car was read this tick; if not, the heart drifts towards its base
    bool on_track = true;         // running: not in the pit, not finished, not retired
    bool in_pit = false;
    bool finished = false;
    float speed = 0;              // m/s
    bool have_pedals = false;     // brake was read this tick (a moment needs it: without it, braking can't be told)
    float brake = 0;              // 0..1
    bool have_g = false;
    float g_lat = 0, g_long = 0;  // G
    float ahead_s = -1;           // time to the nearest car ahead on the road; < 0 none
    float behind_s = -1;          // time from the nearest car behind; < 0 none
    int position = 0, cars = 0;   // 1-based; 0 unknown
    int safety_car = 0;           // 0 none, 1 safety car, 2 virtual
    bool red_flag = false;
    bool formation = false;       // a race whose lights are not out yet
    bool count = false;           // add this tick to the session's numbers (the player's heart, not paused)
};

// A session's numbers for the report.
struct Stats {
    float time = 0;               // seconds counted
    double sum = 0;               // bpm x seconds
    float zone_time[4] = {};      // seconds in each zone
    float peak = 0;
    Cause peak_cause = Cause::none;
    float peak_at = 0;            // seconds into the count
    int spikes = 0;
};

struct Heart {
    float bpm = 0;                // the heart rate
    float target = 0;             // what it chases
    float strain = 0;             // sustained G (|G| low-passed over about 4 s)
    float adrenaline = 0;         // 0..1, spikes and fades in about 11 s
    float fatigue = 0;            // 0..1, builds over a long, hard session
    float battle = 0;             // 0..1, how close the nearest car is (smoothed)
    bool defending = false;       // the closer car is behind
    float phase = 0;              // where the beat is, 0..1 (0 = the R wave)
    float since_start = -1;       // seconds since lights out; < 0 before
    float last_speed = -1;
    float decel = 0;              // m/s² (smoothed), for the moments
    float moment_cooldown = 0;
    Cause cause = Cause::none;    // what drives the heart most right now
    Cause spike_cause = Cause::none;
    float spike_age = 99;         // seconds since the last spike
    uint32_t seed = 1;
    float clock = 0;              // seconds this heart has run
    Stats stats;
};

constexpr float kCalmBelow = 140, kZoneFrom = 160, kRedFrom = 178;

float base_of(Kind kind);
void reset(Heart &h, Kind kind, uint32_t seed);
// A spike of adrenaline (amount 0..1) and why.
void spike(Heart &h, float amount, Cause cause);
// Takes back a spike of `amount` given `age` seconds ago (what is left of it after the fade), and its count.
void unspike(Heart &h, float amount, float age);
// A new attempt at the same session (a restart): the heart keeps beating where it is, everything else starts again.
void new_attempt(Heart &h, Kind kind);
// Lights out: the start's surge.
void lights_out(Heart &h);
void update(Heart &h, const Inputs &in);

Zone zone_of(float bpm);
const char *zone_name(Zone z);   // "CALM", "FOCUSED", "IN THE ZONE", "RED ZONE"
const char *cause_name(Cause c); // "LIGHTS OUT", "CONTACT", ... ("" for none)

// The heart rate to show: the bpm with the small beat-to-beat variation of a real heart.
float shown_bpm(const Heart &h);
// How strained the driver is, 0..1: what the screen effects follow.
float stress(const Heart &h);
// The beat as a pulse 0..1 (the "lub-dub": a strong bump at phase 0 and a weaker one just after).
float beat_wave(float phase);
// One beat of an ECG trace at phase 0..1: about -0.25..1, the R spike at 0.
float ecg(float phase);

// 0..100: time in the zone counts most, then focused, then calm; the red zone counts nothing.
int composure(const Stats &s);
const char *composure_word(int score); // "ICE COLD", "COMPOSED", "STEADY", "ON EDGE", "RATTLED"
const char *composure_grade(int score); // "S", "A", "B", "C", "D"

} // namespace pulse
