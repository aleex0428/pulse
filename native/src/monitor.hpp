// SPDX-License-Identifier: MIT
// PULSE on screen (the overlay, render thread): a bedside monitor in a corner (the heart rate, its zone, an ECG trace
// sweeping across a phosphor grid, the stamina left; NO SIGNAL while a session is on but the car can't be read), the
// DRIVER BIOMETRICS card a broadcast shows when two drivers fight or touch, and the PULSE REPORT at the end of a
// session. A key shows and hides the monitor and closes the report (Esc closes it too). Its own look: a medical
// monitor, black glass, phosphor green, amber and red alarms. Each zone has its own shape as well as its colour.
// Vector calls only.
#pragma once
#include <swofty/overlay.hpp>

#include "heart.hpp"
#include "session.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <string_view>

namespace pulse::monitor {

namespace ov = swofty::overlay;

// ---- palette (RGBA) ----
constexpr uint32_t kGlass = 0x030705EE;   // the monitor's screen
constexpr uint32_t kPanel = 0x06100BF2;   // the card's and the report's body
constexpr uint32_t kGridLine = 0x2BFF8812, kBezel = 0x2BFF8840, kRule = 0x2BFF8826;
constexpr uint32_t kText = 0xDDF5E8FF, kDim = 0x86AE9CFF, kMute = 0x4A6A5CFF;
constexpr uint32_t kCalm = 0x59C8FFFF, kFocused = 0x2BFF88FF, kZoneCol = 0xC6FF3DFF, kRed = 0xFF3B4EFF;
constexpr uint32_t kAmber = 0xFFB020FF;

inline uint32_t alpha(uint32_t rgba, float a)
{
    uint32_t al = static_cast<uint32_t>(std::clamp(a * static_cast<float>(rgba & 0xFF), 0.0f, 255.0f));
    return (rgba & 0xFFFFFF00u) | al;
}

inline uint32_t mix(uint32_t a, uint32_t b, float t)
{
    t = std::clamp(t, 0.0f, 1.0f);
    uint32_t out = 0;
    for (int s = 0; s < 32; s += 8) {
        float va = static_cast<float>((a >> s) & 0xFF), vb = static_cast<float>((b >> s) & 0xFF);
        out |= static_cast<uint32_t>(va + (vb - va) * t + 0.5f) << s;
    }
    return out;
}

inline uint32_t zone_colour(Zone z)
{
    return z == Zone::calm ? kCalm : z == Zone::focused ? kFocused : z == Zone::zone ? kZoneCol : kRed;
}

// the zone's short name, for tight spots
inline const char *zone_short(Zone z)
{
    return z == Zone::calm ? "CALM" : z == Zone::focused ? "FOCUSED" : z == Zone::zone ? "ZONE" : "RED";
}

inline ov::TextStyle ts(float size, uint32_t rgba = kText, uint32_t align = ov::align::left | ov::align::top,
                        float tracking = 0)
{
    ov::TextStyle s;
    s.size = size;
    s.rgba = rgba;
    s.align = align;
    s.tracking = tracking;
    return s;
}

inline float text_w(ov::Paint *p, const ov::TextStyle &s, std::string_view t)
{
    float w = 0, h = 0;
    ov::measure(p, s, t, &w, &h);
    return w;
}

// text cut to a width with "..."
inline std::string fit(ov::Paint *p, const ov::TextStyle &s, std::string t, float max_w)
{
    if (text_w(p, s, t) <= max_w)
        return t;
    while (!t.empty() && text_w(p, s, t + "...") > max_w)
        t.pop_back();
    while (!t.empty() && t.back() == ' ')
        t.pop_back();
    return t + "...";
}

inline float ease_out(float t)
{
    t = std::clamp(t, 0.0f, 1.0f);
    return 1 - (1 - t) * (1 - t) * (1 - t);
}

// ---- settings (set by the game's thread, read here) ----
enum class Corner { bottom_left, top_left, top_right, bottom_right, hidden };

struct Key {
    const char *id, *label;
    int vk;
};
constexpr Key kKeys[] = {{"f9", "F9", 0x78},   {"f4", "F4", 0x73},   {"f5", "F5", 0x74}, {"f8", "F8", 0x77},
                         {"f10", "F10", 0x79}, {"f11", "F11", 0x7A}, {"off", "", 0}};
constexpr int kEscape = 0x1B;

inline std::atomic<int> g_corner{int(Corner::bottom_left)};
inline std::atomic<float> g_size{1.0f};
inline std::atomic<int> g_key{0};
inline std::atomic<bool> g_shown{true}; // the key's toggle (render thread writes it)
inline std::atomic<bool> g_pad{false};  // the last input came from a pad: no keys to show
// Where the report goes. Left by default: the middle of the screen is the game's own after the flag (driver of the
// day, the podium), and the report covered it.
enum class ReportAt { left, right, centre };
inline std::atomic<int> g_report_at{int(ReportAt::left)};

inline void set_report_at(std::string_view id)
{
    g_report_at = int(id == "right" ? ReportAt::right : id == "centre" ? ReportAt::centre : ReportAt::left);
}
inline Session *g_session = nullptr;

inline void set_corner(std::string_view id)
{
    Corner c = id == "top_left"       ? Corner::top_left
               : id == "top_right"    ? Corner::top_right
               : id == "bottom_right" ? Corner::bottom_right
               : id == "hidden"       ? Corner::hidden
                                      : Corner::bottom_left;
    if (int(c) != g_corner.exchange(int(c)))
        g_shown = c != Corner::hidden;
}

inline void set_size(std::string_view id) { g_size = id == "small" ? 0.85f : id == "large" ? 1.2f : 1.0f; }

inline void set_key(std::string_view id)
{
    for (int i = 0; i < int(std::size(kKeys)); i++)
        if (id == kKeys[i].id)
            g_key = i;
}

inline const Key &key() { return kKeys[g_key.load(std::memory_order_relaxed)]; }

// ---- pieces ----

// A heart, centred at (cx, cy), about 2r wide.
inline void heart_glyph(ov::Paint *p, float cx, float cy, float r, uint32_t col)
{
    ov::circle(p, cx - r * 0.48f, cy - r * 0.22f, r * 0.52f, col);
    ov::circle(p, cx + r * 0.48f, cy - r * 0.22f, r * 0.52f, col);
    float pts[] = {cx - r * 0.98f, cy - r * 0.08f, cx + r * 0.98f, cy - r * 0.08f, cx, cy + r * 0.95f};
    ov::polygon(p, pts, 3, col);
}

// The zone's mark, centred at (cx, cy), about s wide: calm a circle, focused a square, in the zone a diamond, the
// red zone a warning triangle. Told apart by shape as well as colour.
inline void zone_icon(ov::Paint *p, float cx, float cy, float s, Zone z, uint32_t col)
{
    float h = s * 0.5f;
    switch (z) {
    case Zone::calm:
        ov::circle(p, cx, cy, h, col);
        break;
    case Zone::focused:
        ov::rect(p, cx - h * 0.9f, cy - h * 0.9f, h * 1.8f, h * 1.8f, col);
        break;
    case Zone::zone: {
        float pts[] = {cx, cy - h * 1.15f, cx + h * 1.15f, cy, cx, cy + h * 1.15f, cx - h * 1.15f, cy};
        ov::polygon(p, pts, 4, col);
        break;
    }
    case Zone::red: {
        float pts[] = {cx, cy - h * 1.2f, cx + h * 1.2f, cy + h, cx - h * 1.2f, cy + h};
        ov::polygon(p, pts, 3, col);
        break;
    }
    }
}

// a phosphor grid behind a trace
inline void grid(ov::Paint *p, float x, float y, float w, float h, float step)
{
    for (float gx = x; gx <= x + w + 0.5f; gx += step)
        ov::rect(p, gx, y, 1, h, kGridLine);
    for (float gy = y; gy <= y + h + 0.5f; gy += step)
        ov::rect(p, x, gy, w, 1, kGridLine);
}

// An ECG trace sweeping left to right, as a bedside monitor draws it: the line is written at the head, which moves
// across in `window` seconds, and a short gap ahead of the head wipes the old sweep. Worked out from the beat's phase
// now and the heart rate (steady over a few seconds), so it needs no memory between frames. With no signal, a flat
// line.
inline void ecg_sweep(ov::Paint *p, float x, float y, float w, float h, float bpm, float phase, float clock,
                      uint32_t col, float thickness, float window = 3.0f, bool signal = true)
{
    constexpr int kN = 160;
    float head = std::fmod(std::max(clock, 0.0f), window) / window * kN; // column the head is on
    int head_i = int(head);
    constexpr int kGap = 8;
    float pts[2 * (kN + 1)];
    int n = 0;
    auto flush = [&] {
        if (n >= 2)
            ov::polyline(p, pts, uint32_t(n), thickness, col);
        n = 0;
    };
    for (int i = 0; i <= kN; i++) {
        int back = (head_i - i + kN) % kN; // columns behind the head
        if (back > kN - kGap) {            // the wiped gap ahead of the head
            flush();
            continue;
        }
        float seconds = (back + (head - head_i)) * window / kN;
        float v = signal ? ecg(phase - seconds * bpm / 60.0f) : 0.0f;
        pts[2 * n] = x + w * i / kN;
        pts[2 * n + 1] = y + h * 0.62f - v * h * 0.52f;
        n++;
        if (i == head_i) {
            flush();
            ov::circle(p, x + w * i / kN, y + h * 0.62f - v * h * 0.52f, thickness * 1.6f, alpha(kText, 0.9f));
            pts[0] = x + w * i / kN;
            pts[1] = y + h * 0.62f - v * h * 0.52f;
            n = 1;
        }
    }
    flush();
}

// a small key cap; returns its width
inline float keycap(ov::Paint *p, float x, float y, std::string_view label, float k)
{
    ov::TextStyle s = ts(11 * k, kText, ov::align::center | ov::align::middle, 1.0f * k);
    float w = std::max(26 * k, text_w(p, s, label) + 12 * k), h = 20 * k;
    ov::rect(p, x, y, w, h, 0x2BFF880F);
    ov::rect_outline(p, x, y, w, h, 1 * k, kBezel);
    ov::text(p, x + w / 2, y + h / 2, s, label);
    return w;
}

// ---- the monitor ----
inline constexpr float kMonW = 318, kMonH = 112;
// the compact monitor (Small, or a 720p screen): the number and the zone big, the trace under them, thinner
inline constexpr float kMiniW = 224, kMiniH = 104;
// how far above the bottom of the safe area the monitor sits in a bottom corner (1080p pixels)
inline constexpr float kMonitorLift = 150;

// Small or a short screen: the compact layout, with text that never goes below a readable size.
inline bool compact_for(float H) { return g_size.load() < 0.9f || H <= 800; }

// the compact layout never scales below this, so its text stays readable on a 720p screen
inline float compact_scale(float k) { return std::max(k, 0.85f); }

inline void monitor_size(bool compact, float k, float *w, float *h)
{
    if (compact)
        k = compact_scale(k);
    *w = (compact ? kMiniW : kMonW) * k;
    *h = (compact ? kMiniH : kMonH) * k;
}

// the frame and the header every layout shares; returns the zone colour
inline uint32_t monitor_frame(ov::Paint *p, const View &v, float x, float y, float w, float h, float k, float label)
{
    uint32_t col = v.signal ? zone_colour(v.zone) : kAmber;
    bool alarm = v.signal && v.zone == Zone::red;
    float blink = alarm ? 0.5f + 0.5f * std::cos(v.clock * 6.2831853f * 2.0f) : 0.0f;
    ov::rect(p, x, y, w, h, kGlass);
    ov::rect_outline(p, x, y, w, h, 1 * k, alarm ? mix(kBezel, kRed, blink) : kBezel);
    ov::text(p, x + 12 * k, y + 8 * k, ts(label, kDim, ov::align::left | ov::align::top, 1.6f * k), "PULSE");
    ov::TextStyle who_s = ts(label, kDim, ov::align::right | ov::align::top, 1 * k);
    std::string who = fit(p, who_s, "HR \xC2\xB7 " + (v.subject.empty() ? std::string("YOU") : v.subject),
                          w - 92 * k);
    ov::text(p, x + w - 22 * k, y + 8 * k, who_s, who);
    float dot = v.signal ? 0.35f + 0.65f * beat_wave(v.phase_beat) : 0.3f + 0.7f * float(std::fmod(v.clock, 1.0f) < 0.5f);
    ov::circle(p, x + w - 12 * k, y + 8 * k + label * 0.5f, 3 * k, alpha(col, dot));
    ov::rect(p, x + 12 * k, y + 12 * k + label, w - 24 * k, 1 * k, kRule);
    return col;
}

inline void draw_no_signal(ov::Paint *p, const View &v, float tx, float ty, float tw, float th, float k, float size)
{
    ecg_sweep(p, tx, ty, tw, th, 0, 0, v.clock, alpha(kAmber, 0.6f), 1.5f * k, 3.0f, false);
    float blink = 0.7f + 0.3f * std::cos(v.clock * 6.2831853f * 0.8f);
    // the words cut the flat line, on the glass
    ov::TextStyle s = ts(size, kAmber, ov::align::center | ov::align::middle, 3 * k);
    float sw = text_w(p, s, "NO SIGNAL") + 14 * k;
    ov::rect(p, tx + tw / 2 - sw / 2, ty + th * 0.62f - size * 0.75f, sw, size * 1.5f, 0x030705FF);
    ov::text(p, tx + tw / 2, ty + th * 0.62f, ts(size, alpha(kAmber, blink), ov::align::center | ov::align::middle, 3 * k),
             "NO SIGNAL");
}

// the zone's name as a chip: its mark and its name on a tint of its colour, the text a step brighter than the colour so
// it reads on the glass. In the red zone the text stays solid; only the mark and the chip's edge pulse.
inline float zone_chip(ov::Paint *p, float x, float cy, float k, Zone z, const char *name, float size, float clock,
                       bool right_aligned)
{
    uint32_t col = zone_colour(z);
    uint32_t text_col = mix(col, 0xFFFFFFFF, 0.28f);
    ov::TextStyle s = ts(size, text_col, ov::align::left | ov::align::middle, 0.8f * k);
    float tw = text_w(p, s, name);
    float pad = 6 * k, icon = size * 0.62f;
    float w = pad + icon + 5 * k + tw + pad, h = size + 7 * k;
    float x0 = right_aligned ? x - w : x;
    ov::rect(p, x0, cy - h / 2, w, h, alpha(col, 0.16f));
    float beat = 1.0f;
    if (z == Zone::red) {
        beat = 0.45f + 0.55f * (0.5f + 0.5f * std::cos(clock * 6.2831853f * 2.0f));
        ov::rect_outline(p, x0, cy - h / 2, w, h, 1 * k, alpha(kRed, beat));
    }
    zone_icon(p, x0 + pad + icon / 2, cy, icon, z, alpha(col, beat));
    ov::text(p, x0 + pad + icon + 5 * k, cy, s, name);
    return w;
}

inline void draw_monitor(ov::Paint *p, const View &v, float x, float y, float k, bool compact = false)
{
    float w = 0, h = 0;
    monitor_size(compact, k, &w, &h);
    if (compact)
        k = compact_scale(k);
    float label = compact ? 10 * k : 9 * k;
    uint32_t col = monitor_frame(p, v, x, y, w, h, k, label);
    char bpm[8];
    if (v.signal)
        std::snprintf(bpm, sizeof bpm, "%.0f", std::clamp(v.bpm, 0.0f, 250.0f));
    else
        std::snprintf(bpm, sizeof bpm, "---");
    // the number never blinks: in the red zone it stays solid red, the alarm is the chip's mark and the bezel
    uint32_t ncol = v.signal ? col : kMute;
    float throb = v.signal ? 1.0f + 0.18f * beat_wave(v.phase_beat) : 1.0f;
    float left = std::clamp(1.0f - v.fatigue, 0.0f, 1.0f);
    uint32_t scol = left > 0.5f ? kFocused : left > 0.25f ? kAmber : kRed;
    char pct[16];
    std::snprintf(pct, sizeof pct, "%d%%", int(std::lround(left * 100)));

    if (compact) {
        // the number with BPM right beside it on the left; the zone chip and the stamina on the right; the trace below
        float top = y + 14 * k + label;
        heart_glyph(p, x + 20 * k, top + 19 * k, 8 * k * throb, ncol);
        ov::TextStyle big = ts(38 * k, ncol, ov::align::left | ov::align::top, 0.5f * k);
        ov::text(p, x + 34 * k, top, big, bpm);
        float nw = text_w(p, big, bpm);
        ov::text(p, x + 37 * k + nw, top + 23 * k, ts(10.5f * k, kDim, ov::align::left | ov::align::top, 0.8f * k), "BPM");
        if (v.signal) {
            zone_chip(p, x + w - 12 * k, top + 11 * k, k, v.zone, zone_short(v.zone), 12.5f * k, v.clock, true);
            ov::TextStyle val = ts(13 * k, scol, ov::align::right | ov::align::middle, 0.4f * k);
            ov::text(p, x + w - 12 * k, top + 33 * k, val, pct);
            ov::text(p, x + w - 16 * k - text_w(p, val, pct), top + 33 * k,
                     ts(9.5f * k, kDim, ov::align::right | ov::align::middle, 0.8f * k), "STAMINA");
        } else {
            ov::text(p, x + w - 12 * k, top + 11 * k, ts(label, kDim, ov::align::right | ov::align::middle, 1 * k), "NO DATA");
        }
        float tx = x + 12 * k, ty = y + h - 28 * k, tw = w - 24 * k, th = 20 * k;
        grid(p, tx, ty, tw, th, 10 * k);
        if (v.signal)
            ecg_sweep(p, tx, ty, tw, th, v.bpm, v.phase_beat, v.clock, col, 1.4f * k, 2.5f);
        else
            draw_no_signal(p, v, tx, ty, tw, th, k, 11 * k);
        return;
    }

    // the number, its heart, BPM tucked under it, and the zone chip
    heart_glyph(p, x + 22 * k, y + 46 * k, 8 * k * throb, ncol);
    ov::text(p, x + 38 * k, y + 27 * k, ts(42 * k, ncol, ov::align::left | ov::align::top, 0.5f * k), bpm);
    ov::text(p, x + 40 * k, y + 67 * k, ts(9.5f * k, kDim, ov::align::left | ov::align::top, 0.8f * k), "BPM");
    float tx = x + 134 * k, ty = y + 28 * k, tw = w - 146 * k, th = 52 * k;
    grid(p, tx, ty, tw, th, 13 * k);
    if (!v.signal) {
        draw_no_signal(p, v, tx, ty, tw, th, k, 12 * k);
        ov::text(p, x + 14 * k, y + 91 * k, ts(10 * k, kDim, ov::align::left | ov::align::middle, 1 * k), "NO DATA");
        return;
    }
    zone_chip(p, x + 12 * k, y + 92 * k, k, v.zone, zone_name(v.zone), 10.5f * k, v.clock, false);
    ecg_sweep(p, tx, ty, tw, th, v.bpm, v.phase_beat, v.clock, col, 1.8f * k);
    // stamina: its label, a bar, the value
    ov::text(p, x + 134 * k, y + 92 * k, ts(9 * k, kDim, ov::align::left | ov::align::middle, 0.8f * k), "STAMINA");
    float bx = x + 186 * k, bw = w - 236 * k;
    ov::rect(p, bx, y + 90 * k, bw, 4 * k, 0x2BFF881A);
    ov::rect(p, bx, y + 90 * k, bw * left, 4 * k, scol);
    ov::text(p, x + w - 12 * k, y + 92 * k, ts(11.5f * k, scol, ov::align::right | ov::align::middle, 0.4f * k), pct);
}

// ---- the DRIVER BIOMETRICS card ----
inline constexpr float kCardW = 600, kCardH = 132;
// the card and the report never shrink below this: at 720p their names and numbers stay readable
inline float panel_scale(float k) { return std::max(k, 0.8f); }

// The card's columns, the same for both rows so the two read side by side: the place, the name (as wide as the longer
// of the two, up to what the row allows), the ECG in whatever is left (it gives way before a name is cut), then the
// BPM and the zone at fixed places on the right.
struct CardCols {
    float pos, name, name_w, ecg, ecg_w, bpm, zone;
};

inline CardCols card_cols(ov::Paint *p, const Card &c, float x, float w, float k)
{
    CardCols cc;
    ov::TextStyle name = ts(17 * k, kText, ov::align::left | ov::align::middle, 0.8f * k);
    cc.pos = x + 14 * k;
    cc.name = x + 58 * k;
    cc.zone = x + w - 74 * k;
    cc.bpm = cc.zone - 40 * k; // the right edge of the number; "BPM" sits after it
    float right_of_ecg = cc.bpm - 70 * k;
    float want = std::max(text_w(p, name, c.you.label), text_w(p, name, c.rival.label));
    const float min_ecg = 56 * k, gap = 16 * k;
    float room = right_of_ecg - cc.name;
    cc.name_w = std::min(want, room - gap - min_ecg);
    cc.name_w = std::max(cc.name_w, 60 * k);
    cc.ecg = cc.name + cc.name_w + gap;
    cc.ecg_w = std::max(0.0f, right_of_ecg - cc.ecg);
    cc.ecg_w = std::min(cc.ecg_w, 170 * k);
    return cc;
}

inline void card_row(ov::Paint *p, const CardSide &s, const CardCols &cc, float x, float y, float w, float k,
                     float clock, bool you)
{
    uint32_t col = zone_colour(s.zone);
    float h = 44 * k, cy = y + h / 2;
    ov::rect(p, x, y, w, h, you ? 0x2BFF880E : 0x00000000);
    char pos[8];
    if (s.position > 0)
        std::snprintf(pos, sizeof pos, "P%d", s.position);
    else
        std::snprintf(pos, sizeof pos, "-");
    ov::text(p, cc.pos, cy, ts(16 * k, you ? kText : mix(kText, kDim, 0.3f), ov::align::left | ov::align::middle, 0.3f * k), pos);
    ov::TextStyle name = ts(17 * k, kText, ov::align::left | ov::align::middle, 0.8f * k);
    ov::text(p, cc.name, cy, name, fit(p, name, s.label, cc.name_w));
    // a narrower trace shows fewer seconds, so its beats keep their shape
    if (cc.ecg_w > 20 * k)
        ecg_sweep(p, cc.ecg, y + 7 * k, cc.ecg_w, h - 14 * k, s.bpm, s.ecg_phase, clock, alpha(col, 0.85f), 1.5f * k,
                  std::clamp(cc.ecg_w / (75 * k), 0.8f, 2.0f));
    char bpm[8];
    std::snprintf(bpm, sizeof bpm, "%.0f", s.bpm);
    ov::text(p, cc.bpm, cy, ts(26 * k, col, ov::align::right | ov::align::middle, 0.3f * k), bpm);
    ov::text(p, cc.bpm + 5 * k, cy + 4 * k, ts(9.5f * k, kDim, ov::align::left | ov::align::middle, 0.8f * k), "BPM");
    zone_chip(p, cc.zone, cy, k, s.zone, zone_short(s.zone), 10.5f * k, clock, false);
}

// Where the card goes: at the top, in the middle of the safe area, clear of the timing tower on the left and the
// MFD and the revs at the bottom.
inline void card_at(ov::Paint *p, float W, float H, float k, float *x, float *y)
{
    ov::Rect safe = ov::safe_area(p);
    if (safe.w <= 0 || safe.h <= 0)
        safe = ov::Rect{0, 0, W, H};
    *x = safe.x + safe.w * 0.5f - kCardW * k * 0.5f;
    *y = safe.y + 34 * k;
}

inline void draw_card(ov::Paint *p, const Card &c, float W, float H, float k_in, float clock)
{
    float in = ease_out(c.age / 0.45f);
    float out = std::clamp((Session::kCardSeconds - c.age) / 0.45f, 0.0f, 1.0f);
    float a = std::min(in, out);
    if (a <= 0)
        return;
    float k = panel_scale(k_in);
    float w = kCardW * k, h = kCardH * k;
    float x = 0, y = 0;
    card_at(p, W, H, k, &x, &y);
    y -= (1 - in) * 24 * k;
    ov::OpacityScope fade(p, a);
    ov::rect(p, x, y, w, h, kPanel);
    ov::rect(p, x, y, 3 * k, h, kFocused);
    // the header: LIVE, the card's name, and one phrase for what is going on
    float hh = 30 * k;
    ov::rect(p, x, y, w, hh, 0x0B1A12FF);
    float lx = x + 14 * k;
    ov::rect(p, lx, y + 8 * k, 44 * k, 14 * k, kRed);
    ov::circle(p, lx + 8 * k, y + 15 * k, 2.5f * k, alpha(0xFFFFFFFF, 0.5f + 0.5f * std::cos(clock * 6.28f)));
    ov::text(p, lx + 15 * k, y + 15 * k, ts(9 * k, 0xFFFFFFFF, ov::align::left | ov::align::middle, 1 * k), "LIVE");
    ov::text(p, lx + 56 * k, y + hh / 2, ts(11.5f * k, kDim, ov::align::left | ov::align::middle, 1.6f * k),
             "DRIVER BIOMETRICS");
    ov::text(p, x + w - 14 * k, y + hh / 2, ts(12 * k, kAmber, ov::align::right | ov::align::middle, 1.4f * k), c.title);
    CardCols cc = card_cols(p, c, x + 3 * k, w - 3 * k, k);
    card_row(p, c.you, cc, x + 3 * k, y + hh + 6 * k, w - 3 * k, k, clock, true);
    ov::rect(p, x + 14 * k, y + hh + 52 * k, w - 28 * k, 1 * k, kRule);
    card_row(p, c.rival, cc, x + 3 * k, y + hh + 54 * k, w - 3 * k, k, clock + 0.37f, false);
}

// ---- the PULSE REPORT ----
inline constexpr float kRepW = 600, kRepH = 352;

// a small figure: its label over its value
inline void stat_line(ov::Paint *p, float x, float y, float k, const char *label, const std::string &value,
                      uint32_t col)
{
    ov::text(p, x, y, ts(9 * k, kDim, ov::align::left | ov::align::top, 1.2f * k), label);
    ov::text(p, x, y + 13 * k, ts(19 * k, col, ov::align::left | ov::align::top, 0.4f * k), value);
}

// "0:42", "47:05"
inline std::string duration(float seconds)
{
    int s = std::max(0, int(seconds));
    char b[16];
    std::snprintf(b, sizeof b, "%d:%02d", s / 60, s % 60);
    return b;
}

// Where the report goes: on the left or the right of the safe area, half way down, or in the middle.
inline void report_at(ov::Paint *p, float W, float H, float k, float *x, float *y)
{
    ov::Rect safe = ov::safe_area(p);
    if (safe.w <= 0 || safe.h <= 0)
        safe = ov::Rect{0, 0, W, H};
    float w = kRepW * k, h = kRepH * k;
    *y = safe.y + (safe.h - h) * 0.5f;
    switch (ReportAt(g_report_at.load())) {
    case ReportAt::left:
        *x = safe.x;
        break;
    case ReportAt::right:
        *x = safe.x + safe.w - w;
        break;
    case ReportAt::centre:
        *x = W * 0.5f - w * 0.5f;
        *y = H * 0.5f - h * 0.5f;
        break;
    }
}

inline void draw_report(ov::Paint *p, const Report &r, float W, float H, float k_in, float clock, bool pad = false)
{
    float in = ease_out(r.age / 0.5f);
    float out = std::clamp((Session::kReportSeconds - r.age) / 0.5f, 0.0f, 1.0f);
    float a = std::min(in, out);
    if (a <= 0)
        return;
    float k = panel_scale(k_in);
    float w = kRepW * k, h = kRepH * k;
    float x = 0, y = 0;
    report_at(p, W, H, k, &x, &y);
    ReportAt where = ReportAt(g_report_at.load());
    // it slides in from its side (from below in the centre)
    if (where == ReportAt::left)
        x -= (1 - in) * 40 * k;
    else if (where == ReportAt::right)
        x += (1 - in) * 40 * k;
    else
        y += (1 - in) * 30 * k;
    ov::OpacityScope fade(p, a);
    ov::rect(p, x, y, w, h, kPanel);
    ov::rect_outline(p, x, y, w, h, 1 * k, kBezel);
    // the header, and a flat line that beats once: the report's signature, kept quiet
    ov::text(p, x + 22 * k, y + 18 * k, ts(20 * k, kText, ov::align::left | ov::align::top, 3.5f * k), "PULSE REPORT");
    ov::text(p, x + w - 22 * k, y + 24 * k, ts(11 * k, kDim, ov::align::right | ov::align::top, 2 * k),
             kind_name(r.kind));
    {
        float lx = x + 22 * k, lw = w - 44 * k, ly = y + 56 * k;
        float pts[2 * 80];
        for (int i = 0; i < 80; i++) {
            float f = i / 79.0f;
            pts[2 * i] = lx + lw * f;
            pts[2 * i + 1] = ly - ecg((f - 0.5f) * 1.2f) * 10 * k;
        }
        ov::polyline(p, pts, 80, 1.1f * k, alpha(kFocused, 0.45f));
    }
    // first focus: zone control in one reading, the score, its grade beside it and the word under them
    uint32_t ccol = r.score >= 78 ? kFocused : r.score >= 50 ? kAmber : kRed;
    float cx = x + 22 * k, cy = y + 78 * k;
    ov::text(p, cx, cy, ts(9.5f * k, kDim, ov::align::left | ov::align::top, 1.4f * k), "ZONE CONTROL");
    char score[8];
    std::snprintf(score, sizeof score, "%d", r.score);
    ov::TextStyle big = ts(64 * k, ccol, ov::align::left | ov::align::top, 0.5f * k);
    ov::text(p, cx, cy + 10 * k, big, score);
    float sx = cx + text_w(p, big, score) + 6 * k;
    ov::TextStyle of = ts(14 * k, kDim, ov::align::left | ov::align::top);
    ov::text(p, sx, cy + 50 * k, of, "/100");
    float gx = sx + text_w(p, of, "/100") + 14 * k;
    ov::rect(p, gx, cy + 20 * k, 46 * k, 46 * k, alpha(ccol, 0.12f));
    ov::rect_outline(p, gx, cy + 20 * k, 46 * k, 46 * k, 2 * k, ccol);
    ov::text(p, gx + 23 * k, cy + 43 * k, ts(28 * k, ccol, ov::align::center | ov::align::middle), r.grade);
    ov::text(p, cx, cy + 86 * k, ts(16 * k, ccol, ov::align::left | ov::align::top, 2 * k), r.word);
    ov::text(p, cx, cy + 108 * k, ts(10 * k, kDim, ov::align::left | ov::align::top, 0.6f * k),
             "TIME AT 160-178 BPM COUNTS MOST");
    // second focus: the peak, and what and where it was
    float px = x + 330 * k, py = cy;
    ov::text(p, px, py, ts(9.5f * k, kDim, ov::align::left | ov::align::top, 1.4f * k), "PEAK");
    char b[48];
    std::snprintf(b, sizeof b, "%.0f", r.peak);
    ov::TextStyle peak = ts(40 * k, zone_colour(zone_of(r.peak)), ov::align::left | ov::align::top, 0.5f * k);
    ov::text(p, px, py + 10 * k, peak, b);
    ov::text(p, px + text_w(p, peak, b) + 6 * k, py + 34 * k, ts(13 * k, kDim, ov::align::left | ov::align::top, 0.6f * k),
             "BPM");
    float room = x + w - 22 * k - px;
    if (!r.peak_cause.empty()) {
        ov::TextStyle cs = ts(14 * k, kText, ov::align::left | ov::align::top, 1.2f * k);
        ov::text(p, px, py + 60 * k, cs, fit(p, cs, r.peak_cause, room));
    }
    if (!r.peak_where.empty()) {
        ov::TextStyle ws = ts(12 * k, mix(kText, kDim, 0.45f), ov::align::left | ov::align::top, 0.8f * k);
        ov::text(p, px, py + 80 * k, ws, fit(p, ws, r.peak_where, room));
    }
    // the rest, smaller, in one row
    float ry = y + 216 * k, col_w = (w - 44 * k) / 4;
    ov::rect(p, x + 22 * k, ry - 10 * k, w - 44 * k, 1 * k, kRule);
    std::snprintf(b, sizeof b, "%.0f BPM", r.average);
    stat_line(p, x + 22 * k, ry, k, "AVERAGE", b, zone_colour(zone_of(r.average)));
    std::snprintf(b, sizeof b, "%d%%", int(std::lround(r.stamina * 100)));
    stat_line(p, x + 22 * k + col_w, ry, k, "STAMINA", b, r.stamina > 0.5f ? kFocused : r.stamina > 0.25f ? kAmber : kRed);
    stat_line(p, x + 22 * k + 2 * col_w, ry, k, "TIME", duration(r.seconds), kText);
    std::snprintf(b, sizeof b, "%d", r.spikes);
    stat_line(p, x + 22 * k + 3 * col_w, ry, k, "SPIKES", b, kText);
    // the time in each zone: the bar, and a legend in four even columns
    float zx = x + 22 * k, zy = y + 262 * k, zw = w - 44 * k;
    ov::text(p, zx, zy, ts(9 * k, kDim, ov::align::left | ov::align::top, 1.2f * k), "TIME IN EACH ZONE");
    float at = zx;
    for (int i = 0; i < 4; i++) {
        float seg = zw * std::clamp(r.zone_share[i], 0.0f, 1.0f);
        if (seg > 0.5f)
            ov::rect(p, at, zy + 16 * k, std::max(0.0f, seg - 2 * k), 8 * k, zone_colour(Zone(i)));
        at += seg;
    }
    for (int i = 0; i < 4; i++) {
        float lx = zx + zw * float(i) / 4;
        uint32_t zc = zone_colour(Zone(i));
        zone_icon(p, lx + 4 * k, zy + 38 * k, 8 * k, Zone(i), zc);
        char lab[40];
        std::snprintf(lab, sizeof lab, "%s", zone_short(Zone(i)));
        ov::TextStyle s = ts(11 * k, mix(zc, 0xFFFFFFFF, 0.35f), ov::align::left | ov::align::middle, 0.6f * k);
        ov::text(p, lx + 12 * k, zy + 38 * k, s, lab);
        char pc[8];
        std::snprintf(pc, sizeof pc, "%d%%", int(std::lround(r.zone_share[i] * 100)));
        ov::text(p, lx + 16 * k + text_w(p, s, lab), zy + 38 * k, ts(11 * k, kText, ov::align::left | ov::align::middle, 0.3f * k), pc);
    }
    // footer: the keys that close it (none on a pad: it goes by itself), and how long it stays
    float fy = y + h - 30 * k;
    ov::TextStyle fs = ts(10 * k, kDim, ov::align::left | ov::align::middle, 1.2f * k);
    if (pad) {
        ov::text(p, x + 22 * k, fy + 10 * k, fs, "CLOSES BY ITSELF");
    } else {
        float fx = x + 22 * k;
        if (key().vk) {
            fx += keycap(p, fx, fy, key().label, k) + 6 * k;
            ov::text(p, fx, fy + 10 * k, ts(10 * k, kMute, ov::align::left | ov::align::middle), "/");
            fx += 12 * k;
        }
        fx += keycap(p, fx, fy, "ESC", k) + 8 * k;
        ov::text(p, fx, fy + 10 * k, fs, "CLOSE");
    }
    float left = std::clamp(1 - r.age / Session::kReportSeconds, 0.0f, 1.0f);
    ov::rect(p, x, y + h - 2 * k, w * left, 2 * k, alpha(kFocused, 0.7f));
    (void)clock;
}

// Where the monitor goes for this corner, inside the safe area.
inline void monitor_at(ov::Paint *p, Corner c, float k, float *x, float *y, bool compact = false)
{
    ov::Rect safe = ov::safe_area(p);
    float w = 0, h = 0;
    monitor_size(compact, k, &w, &h);
    bool right = c == Corner::top_right || c == Corner::bottom_right;
    bool bottom = c == Corner::bottom_left || c == Corner::bottom_right;
    *x = right ? safe.x + safe.w - w : safe.x;
    // at the bottom, well above the edge: clear of the game's own bottom readouts and of the track in front of the car
    // (it sat 40 px up, and covered the road in the cockpit view)
    *y = bottom ? safe.y + safe.h - h - kMonitorLift * k : safe.y;
}

// The overlay's paint callback.
inline void paint(ov::Paint *p, void *)
{
    if (!g_session)
        return;
    View v = g_session->view();
    if (v.paused)
        return;
    float W = 0, H = 0;
    ov::size(p, &W, &H);
    float k = ov::ui_scale(p) * g_size.load();
    ov::Input in = ov::input(p);
    // a pad's buttons put the report's footer in pad mode; a key or a click takes it back
    if (in.pad_pressed || in.pad_buttons)
        g_pad = true;
    else if (in.pressed)
        g_pad = false;
    else
        for (uint8_t b : in.keys_pressed)
            if (b) {
                g_pad = false;
                break;
            }
    const Key &kk = key();
    if (v.report.show && (in.key_pressed(kEscape) || (kk.vk && in.key_pressed(uint32_t(kk.vk))))) {
        g_session->dismiss_report();
        v.report.show = false;
        swofty::log("[pulse] report closed");
    } else if (kk.vk && in.key_pressed(uint32_t(kk.vk))) {
        g_shown = !g_shown.load();
        swofty::log("[pulse] monitor %s", g_shown.load() ? "shown" : "hidden");
    }
    if (v.report.show) {
        draw_report(p, v.report, W, H, k, v.clock, g_pad.load());
        return;
    }
    if (!v.live)
        return;
    if (g_shown.load()) {
        bool compact = compact_for(H);
        float x = 0, y = 0;
        Corner c = Corner(g_corner.load());
        monitor_at(p, c == Corner::hidden ? Corner::bottom_left : c, k, &x, &y, compact);
        draw_monitor(p, v, x, y, k, compact);
    }
    if (v.card.show && !v.spectating)
        draw_card(p, v.card, W, H, k, v.clock);
}

} // namespace pulse::monitor
