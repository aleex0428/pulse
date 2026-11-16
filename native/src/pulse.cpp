// SPDX-License-Identifier: MIT
// PULSE: the driver's heart. Every car gets a heart rate, adrenaline and fatigue from what it does on track (the live
// state from the game's memory, the game's collisions and overtakes). Yours shows on a bedside monitor and is felt on
// screen (fx/pulse.hlsl: tunnel vision, the heartbeat at the edges, G greyout, the flash and blur of an impact,
// fatigue); a broadcast card compares two drivers' hearts when they fight or touch, and a report sums the session up.
// This file wires the events, the options, the screen effect's data and the overlay to the session (session.hpp).
// Include mixin.hpp first: registry.hpp redefines SWOFTY_ADDON so the DLL exports SwoftyAddonRegister.
#include <swofty/mixin.hpp>

#include <swofty/events.hpp>
#include <swofty/live.hpp>
#include <swofty/overlay.hpp>
#include <swofty/postfx.hpp>
#include <swofty/registry.hpp>
#include <swofty/telemetry.hpp>
#include <swofty/view.hpp>

#include "heart.hpp"
#include "monitor.hpp"
#include "session.hpp"

#include <chrono>
#include <mutex>
#include <string>

#ifndef PULSE_VERSION
#define PULSE_VERSION "dev"
#endif

using namespace swofty;

namespace {

constexpr const char *kAddon = "alexs.pulse";
constexpr const char *kPass = "pulse";

std::mutex g_mutex;
pulse::Session g_session;
std::chrono::steady_clock::time_point g_last_frame, g_last_options;
bool g_have_frame = false;
postfx::Result g_fx_result = postfx::Result::invalid;
bool g_fx_logged = false;

const char *fx_name(postfx::Result r)
{
    switch (r) {
    case postfx::Result::ok:
        return "ok";
    case postfx::Result::no_pass:
        return "no_pass (the pass is off or not running yet)";
    case postfx::Result::unavailable:
        return "unavailable (this core takes no data for passes)";
    case postfx::Result::invalid:
        return "invalid";
    case postfx::Result::too_big:
        return "too_big";
    case postfx::Result::denied:
        return "denied";
    case postfx::Result::unknown_pass:
        return "unknown_pass (the profile's build left the pass out)";
    }
    return "?";
}

// The options, read when a session starts and every two seconds (they can change in ADDON SETTINGS in game).
void read_options()
{
    pulse::Settings s;
    s.effects = option<bool>("effects");
    s.strength = float(option<double>("effects_strength")) / 100.0f;
    s.comfort = option<std::string>("effects_style") == "comfort";
    std::string motion = option<std::string>("screen_motion");
    s.motion = motion == "reduced" ? pulse::Motion::reduced : motion == "static" ? pulse::Motion::still : pulse::Motion::full;
    s.tunnel = option<bool>("fx_tunnel");
    s.heartbeat = option<bool>("fx_heartbeat");
    s.gforce = option<bool>("fx_gforce");
    s.impact = option<bool>("fx_impact");
    s.fatigue = option<bool>("fx_fatigue");
    s.tv = option<bool>("tv_card");
    s.report = option<bool>("report");
    s.diagnostics = option<bool>("diagnostics");
    g_session.configure(s);
    pulse::monitor::set_corner(option<std::string>("monitor_corner"));
    pulse::monitor::set_size(option<std::string>("monitor_size"));
    pulse::monitor::set_key(option<std::string>("monitor_key"));
    pulse::monitor::set_report_at(option<std::string>("report_position"));
}

void send_fx(const pulse::FxData &f)
{
    postfx::Result r = postfx::set_data(kAddon, kPass, f);
    if (!g_fx_logged || r != g_fx_result) {
        g_fx_logged = true;
        g_fx_result = r;
        log("[pulse] screen effect data: %s", fx_name(r));
    }
}

void frame(double, void *)
{
    auto now = std::chrono::steady_clock::now();
    float dt = g_have_frame ? std::chrono::duration<float>(now - g_last_frame).count() : 0.0f;
    g_last_frame = now;
    g_have_frame = true;
    std::lock_guard lock(g_mutex);
    if (now - g_last_options > std::chrono::seconds(2)) {
        g_last_options = now;
        read_options();
    }
    g_session.tick(dt, view::paused());
}

} // namespace

SWOFTY_ON_LOAD
{
    log("[pulse] ready: version %s", PULSE_VERSION);
    {
        std::lock_guard lock(g_mutex);
        read_options();
        g_session.set_sink(send_fx);
    }
    log("[pulse] live state %s, screen effect data %s, overlay %s", live::available() ? "on" : "not yet",
        postfx::available() ? "available" : "unavailable", overlay::available() ? "available" : "unavailable");
    pulse::monitor::g_session = &g_session;
    events::on_frame(frame);
    if (overlay::available())
        log("[pulse] monitor: %s", overlay::name(overlay::on_paint(pulse::monitor::paint)));
    events::subscribe(SWOFTY_EVENT_SESSION_START, [](const SwoftyEvent *, void *) {
        std::lock_guard lock(g_mutex);
        read_options();
        g_session.session_start();
        log("[pulse] session.start");
    });
    events::subscribe(SWOFTY_EVENT_RACE_START, [](const SwoftyEvent *, void *) {
        std::lock_guard lock(g_mutex);
        g_session.race_start();
    });
    // e->a says why: 0 the session ended, 1 a restart (no report), 2 the game unloaded with the session open
    events::subscribe(SWOFTY_EVENT_SESSION_END, [](const SwoftyEvent *e, void *) {
        std::lock_guard lock(g_mutex);
        g_session.session_end(int(e->a));
    });
    events::subscribe(SWOFTY_EVENT_SESSION_UNLOAD, [](const SwoftyEvent *, void *) {
        std::lock_guard lock(g_mutex);
        g_session.session_unload();
    });
    events::subscribe(telemetry::event::collision, [](const SwoftyEvent *e, void *) {
        std::lock_guard lock(g_mutex);
        g_session.collision(e->a, e->b, view::paused());
    });
    events::subscribe(telemetry::event::overtake, [](const SwoftyEvent *e, void *) {
        std::lock_guard lock(g_mutex);
        g_session.overtake(e->a, e->b, view::paused());
    });
}

SWOFTY_ADDON()
