// SPDX-License-Identifier: MIT
// PULSE's heart model (see heart.hpp).
#include "heart.hpp"

#include <algorithm>
#include <cmath>

namespace pulse {

namespace {

constexpr float kTwoPi = 6.2831853f;
constexpr float kAdrenalineFade = 11.0f; // seconds

float clamp01(float v) { return std::clamp(v, 0.0f, 1.0f); }

// the share of the way to the target after dt with time constant tau
float follow(float dt, float tau) { return 1.0f - std::exp(-dt / std::max(tau, 0.001f)); }

// how close a car is: 1 at 0.25 s or less, 0 at 1 s or more (tuned on three real races: from 1.3 s, a midfield race
// was a fight from start to flag)
float close_of(float seconds)
{
    if (seconds < 0)
        return 0;
    return clamp01((1.0f - seconds) / 0.75f);
}

float gauss(float x, float centre, float width)
{
    float d = (x - centre) / width;
    return std::exp(-d * d);
}

// a small hash of the seed, 0..1, for each heart's own rhythm
float seeded(uint32_t seed, uint32_t salt)
{
    uint32_t v = seed * 2654435761u ^ (salt * 2246822519u);
    v ^= v >> 15;
    v *= 2246822519u;
    v ^= v >> 13;
    return float(v & 0xFFFF) / 65535.0f;
}

} // namespace

float base_of(Kind kind)
{
    switch (kind) {
    case Kind::practice:
        return 130;
    case Kind::qualifying:
        return 140;
    case Kind::race:
        return 148;
    case Kind::time_trial:
        return 136;
    }
    return 135;
}

void reset(Heart &h, Kind kind, uint32_t seed)
{
    h = Heart{};
    h.seed = seed ? seed : 1;
    // sitting in the car before the session: a little under the session's base, each driver a little different
    h.bpm = base_of(kind) - 14 + 8 * seeded(h.seed, 1);
    h.target = h.bpm;
    h.phase = seeded(h.seed, 2);
}

void spike(Heart &h, float amount, Cause cause)
{
    h.adrenaline = clamp01(h.adrenaline + amount);
    h.spike_cause = cause;
    h.spike_age = 0;
    h.stats.spikes++;
}

void unspike(Heart &h, float amount, float age)
{
    float left = amount * std::exp(-std::max(age, 0.0f) / kAdrenalineFade);
    h.adrenaline = clamp01(h.adrenaline - left);
    h.stats.spikes = std::max(0, h.stats.spikes - 1);
}

void new_attempt(Heart &h, Kind kind)
{
    float bpm = h.bpm, phase = h.phase;
    uint32_t seed = h.seed;
    reset(h, kind, seed);
    h.bpm = h.target = bpm;
    h.phase = phase;
}

void lights_out(Heart &h)
{
    h.since_start = 0;
    spike(h, 0.35f, Cause::lights_out);
}

void update(Heart &h, const Inputs &in)
{
    float dt = std::clamp(in.dt, 0.0f, 0.5f);
    if (dt <= 0)
        return;
    h.clock += dt;
    float base = base_of(in.kind);

    // sustained G and the moments (a sudden loss of speed without braking hard: a spin, a hit, a trip off the road)
    if (in.known && in.have_g) {
        float g = std::sqrt(in.g_lat * in.g_lat + in.g_long * in.g_long);
        h.strain += (g - h.strain) * follow(dt, 4.0f);
    } else {
        h.strain += (0 - h.strain) * follow(dt, 4.0f);
    }
    if (in.known && h.last_speed >= 0) {
        float d = (h.last_speed - in.speed) / dt;
        h.decel += (d - h.decel) * follow(dt, 0.15f);
    }
    h.last_speed = in.known ? in.speed : -1;
    h.moment_cooldown = std::max(0.0f, h.moment_cooldown - dt);
    // only with the pedals read: without them hard braking looks the same as a spin
    if (in.known && in.have_pedals && in.on_track && h.decel > 28 && in.brake < 0.3f && in.speed > 6 &&
        h.moment_cooldown <= 0) {
        spike(h, 0.4f, Cause::moment);
        h.moment_cooldown = 4;
    }

    // how close the nearest car is (nobody fights in a time trial, behind the safety car or in the pit)
    float attack = close_of(in.ahead_s), defend = close_of(in.behind_s) * 1.15f;
    float raw = clamp01(std::max(attack, defend));
    // no fight on the grid or at a standstill either: the cars around you on the grid are not a battle
    if (!in.known || !in.on_track || in.kind == Kind::time_trial || in.safety_car != 0 || in.red_flag ||
        in.formation || in.speed < 10)
        raw = 0;
    h.battle += (raw - h.battle) * follow(dt, raw > h.battle ? 1.5f : 4.0f);
    h.defending = defend > attack;

    // the target, from its parts (each kept to name what drives the heart most)
    float pace = in.known ? clamp01(in.speed / 85.0f) * 9.0f : 0;
    float load = clamp01((h.strain - 1.2f) / 3.0f) * 15.0f;
    float fight = h.battle * 15.0f;
    float stakes = 0;
    if (in.known && in.on_track && in.kind == Kind::race && in.position > 0 && h.since_start >= 0) {
        stakes = in.position == 1 ? 7.0f : in.position <= 3 ? 4.0f : in.position <= 10 ? 1.5f : 0.0f;
    } else if (in.known && in.on_track && in.kind == Kind::qualifying && in.speed > 40) {
        stakes = 4.0f; // a lap that counts
    }
    float start = 0;
    if (in.formation)
        start = 10; // the grid: nerves before the lights
    if (h.since_start >= 0 && h.since_start < 45)
        start = std::max(start, 26.0f * std::exp(-h.since_start / 14.0f));
    float rush = 34.0f * h.adrenaline;
    float tired = 9.0f * h.fatigue;
    float target = base + pace + load + fight + stakes + start + rush + tired;

    // what brings it down
    if (in.known) {
        if (in.safety_car == 1)
            target -= 20;
        else if (in.safety_car == 2)
            target -= 14;
        if (in.in_pit)
            target -= in.speed < 1 ? 20 : 12;
        if (in.finished)
            target = base - 18 + rush * 0.5f;
        else if (!in.on_track && !in.in_pit)
            target = base - 10 + rush * 0.6f; // out of the session
    } else {
        target = base + rush * 0.5f;
    }
    if (in.red_flag)
        target = base - 22 + rush * 0.5f;
    h.target = std::clamp(target, 70.0f, 198.0f);

    // the cause: the biggest part, if it is a real one
    struct Part {
        float v;
        Cause c;
    } parts[] = {{start, Cause::lights_out},
                 {rush, h.spike_cause},
                 {fight, h.defending ? Cause::defending : Cause::wheel_to_wheel},
                 {load, Cause::g_load},
                 {stakes, Cause::pressure}};
    if (in.formation)
        parts[0].c = Cause::pressure;
    Part best{4.0f, Cause::none};
    for (const Part &p : parts)
        if (p.v > best.v)
            best = p;
    h.cause = best.c;

    // the heart follows: quick up, slow down, slower still when tired
    float tau = h.target > h.bpm ? 2.2f : 8.0f * (1.0f + h.fatigue);
    h.bpm += (h.target - h.bpm) * follow(dt, tau);
    h.bpm = std::clamp(h.bpm, 55.0f, 205.0f);

    // fades and clocks
    h.adrenaline *= std::exp(-dt / kAdrenalineFade);
    h.spike_age += dt;
    if (h.since_start >= 0)
        h.since_start += dt;
    // fatigue: racing itself wears a driver down (a base while driving at speed), a racing heart more. Tuned on
    // model_test's curve: about 0.5 at the end of 90 minutes in clean air, about 0.7 with fights now and then
    float effort = clamp01((h.bpm - 150.0f) / 35.0f) * (0.6f + 0.4f * clamp01(h.strain / 3.0f));
    float driving = in.known && in.on_track && in.speed > 20 ? 0.27f : 0.0f;
    h.fatigue = clamp01(h.fatigue + dt * (driving + effort) * 0.95f / 5400.0f);
    if (h.bpm < 130)
        h.fatigue = std::max(0.0f, h.fatigue - dt / 3600.0f);
    h.phase += dt * h.bpm / 60.0f;
    h.phase -= std::floor(h.phase);

    if (in.count) {
        Stats &s = h.stats;
        s.time += dt;
        s.sum += double(h.bpm) * dt;
        s.zone_time[int(zone_of(h.bpm))] += dt;
        if (h.bpm > s.peak) {
            s.peak = h.bpm;
            s.peak_at = s.time;
            s.peak_cause = h.cause;
        }
    }
}

Zone zone_of(float bpm)
{
    if (bpm >= kRedFrom)
        return Zone::red;
    if (bpm >= kZoneFrom)
        return Zone::zone;
    if (bpm >= kCalmBelow)
        return Zone::focused;
    return Zone::calm;
}

const char *zone_name(Zone z)
{
    switch (z) {
    case Zone::calm:
        return "CALM";
    case Zone::focused:
        return "FOCUSED";
    case Zone::zone:
        return "IN THE ZONE";
    case Zone::red:
        return "RED ZONE";
    }
    return "";
}

const char *cause_name(Cause c)
{
    switch (c) {
    case Cause::none:
        return "";
    case Cause::lights_out:
        return "LIGHTS OUT";
    case Cause::contact:
        return "CONTACT";
    case Cause::moment:
        return "A MOMENT";
    case Cause::wheel_to_wheel:
        return "WHEEL TO WHEEL";
    case Cause::defending:
        return "DEFENDING";
    case Cause::lost_place:
        return "LOST A PLACE";
    case Cause::gained_place:
        return "OVERTAKE";
    case Cause::track_limits:
        return "TRACK LIMITS";
    case Cause::penalty:
        return "PENALTY";
    case Cause::g_load:
        return "G-LOAD";
    case Cause::pressure:
        return "PRESSURE";
    case Cause::restart:
        return "RESTART";
    }
    return "";
}

float shown_bpm(const Heart &h)
{
    // breathing (about 15 breaths a minute) and a slower drift: a couple of beats either way
    float r = seeded(h.seed, 3);
    return h.bpm + 1.3f * std::sin(h.clock * kTwoPi / 4.1f + r * kTwoPi) +
           0.8f * std::sin(h.clock * kTwoPi / 11.7f + r * 9.0f);
}

float stress(const Heart &h)
{
    float s = std::clamp((h.bpm - 168.0f) / 26.0f, 0.0f, 1.0f);
    s = s * s * (3 - 2 * s);
    return clamp01(s * 0.85f + h.adrenaline * 0.35f);
}

float beat_wave(float phase)
{
    phase -= std::floor(phase);
    return clamp01(gauss(phase, 0.0f, 0.06f) + gauss(phase, 1.0f, 0.06f) + 0.55f * gauss(phase, 0.3f, 0.07f));
}

float ecg(float phase)
{
    phase -= std::floor(phase);
    // the R spike sits at 0 (and 1); P before it, T after it
    float p = phase > 0.5f ? phase - 1.0f : phase; // -0.5 .. 0.5
    // widths as a monitor draws them (a QRS wide enough to survive the trace's columns at any rate)
    return 0.12f * gauss(p, -0.2f, 0.04f)          // P
           - 0.14f * gauss(p, -0.05f, 0.018f)      // Q
           + 1.0f * gauss(p, 0.0f, 0.026f)         // R
           - 0.28f * gauss(p, 0.055f, 0.02f)       // S
           + 0.22f * gauss(p, 0.27f, 0.065f);      // T
}

int composure(const Stats &s)
{
    if (s.time <= 0)
        return 0;
    double good = s.zone_time[int(Zone::zone)] * 1.0 + s.zone_time[int(Zone::focused)] * 0.8 +
                  s.zone_time[int(Zone::calm)] * 0.55;
    return std::clamp(int(std::lround(100.0 * good / s.time)), 0, 100);
}

const char *composure_word(int score)
{
    return score >= 88 ? "ICE COLD" : score >= 78 ? "COMPOSED" : score >= 65 ? "STEADY" : score >= 50 ? "ON EDGE" : "RATTLED";
}

const char *composure_grade(int score)
{
    return score >= 88 ? "S" : score >= 78 ? "A" : score >= 65 ? "B" : score >= 50 ? "C" : "D";
}

} // namespace pulse
