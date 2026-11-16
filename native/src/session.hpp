// SPDX-License-Identifier: MIT
// PULSE's session: a heart for every car on the grid, fed from the live state (swofty/live.hpp, from the game's
// memory, no UDP), the game's collisions and overtakes, and what comes out of it: the data the screen effect reads
// (fx/pulse.hlsl), the view the monitor, the broadcast card and the report draw (monitor.hpp), and the log lines
// that tell how the live state reads in the game.
//
// Its life: idle (nothing seen yet) -> session (session.start, or a live session when the addon came in late) ->
// attempt (race.start: lights out; a second race.start is a new attempt) -> closed (session.end). A closed session
// ignores every snapshot the live state still holds until a new session.start, or until the live state's
// generation and session type both change.
//
// All calls but view() come from the game's threads with the plugin's lock held; view() is the render thread's copy.
#pragma once
#include <swofty/live.hpp>
#include <swofty/mixin.hpp>
#include <swofty/telemetry.hpp>

#include "heart.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace pulse {

namespace live = swofty::live;

enum class Phase : uint8_t { idle, session, attempt, closed };
// How much the screen moves: full; reduced (half the throb and blur, the tunnel eases in and out); still (nothing
// throbs, flashes, blurs or crawls: the edges only darken, slowly)
enum class Motion : uint8_t { full, reduced, still };

inline const char *phase_name(Phase p)
{
    return p == Phase::idle ? "idle" : p == Phase::session ? "session" : p == Phase::attempt ? "attempt" : "closed";
}

// The settings the plugin reads from the addon's options.
struct Settings {
    bool effects = true;    // the screen effect (its pass is also switched by the "effects" option itself)
    float strength = 0.6f;  // 0..1
    bool comfort = false;   // no blur, flash, heartbeat or grain; a softer tunnel; fatigue only dims a little
    Motion motion = Motion::full;
    bool tunnel = true, heartbeat = true, gforce = true, impact = true, fatigue = true;
    bool tv = true;         // the DRIVER BIOMETRICS card
    bool report = true;     // the PULSE REPORT at the end of a session
    bool diagnostics = true; // the probe lines in the log
};

// What fx/pulse.hlsl reads every frame (data_float at these byte offsets). Keep the two in step. Every channel is
// 0 when its effect is off: the shader multiplies each effect by its channel and adds nothing of its own.
struct FxData {
    float active = 0;   //  0: 1 while some channel is above 0
    float tunnel = 0;   //  4: tunnel vision, 0..1
    float beat = 0;     //  8: the heartbeat's throb now, 0..1
    float soft = 0;     // 12: fatigue's softness, 0..1
    float g_side = 0;   // 16: lateral G greyout, -1 (left edge) .. +1 (right edge)
    float g_brake = 0;  // 20: braking G, the top edge, 0..1
    float flash = 0;    // 24: the impact's white flash now, 0..1
    float blur = 0;     // 28: the impact's radial blur now, 0..1
    float desat = 0;    // 32: colour drained at the edges, 0..1
    float time = 0;     // 36: session seconds
    float grain = 0;    // 40: fatigue's grain, 0..1
    float dim = 0;      // 44: fatigue's dimming, 0..1
};
static_assert(sizeof(FxData) == 48, "fx/pulse.hlsl reads 48 bytes");

// A G reading on this side of zero darkens the right edge. The game's sign of g_lat is checked by the probe lines
// ("g lat"); flip this if the greyout shows on the inside of the corners.
constexpr float kGSideSign = 1.0f;
// Fatigue starts to show on screen from here (0..1).
constexpr float kFatigueVisual = 0.25f;
// The G greyout: lateral from 1.4 G, full at 3.4 G; braking from 2.2 G, full at 4.5 G. The game's G runs lower than a
// real car's (Montreal, 13 Nov: 2.2 G lateral most of the time, about 4 G under braking); from 2 and 3 G the edges
// hardly ever moved.
constexpr float kSideGFrom = 1.4f, kSideGSpan = 2.0f;
constexpr float kBrakeGFrom = 2.2f, kBrakeGSpan = 2.3f;

struct CardSide {
    float bpm = 0;
    Zone zone = Zone::calm;
    int position = 0;
    std::string label;      // "YOU", a driver's name, a team, or "P4"
    float ecg_phase = 0;
};

// The DRIVER BIOMETRICS broadcast card.
struct Card {
    bool show = false;
    float age = 0;          // seconds since it came up
    std::string title;      // "WHEEL TO WHEEL", "UNDER PRESSURE", "CONTACT"
    std::string mode;       // "ATTACKING", "DEFENDING", ""
    CardSide you, rival;
};

// The PULSE REPORT at the end of a session. It keeps its own wall clock: it goes after kReportSeconds of time not
// paused, whether or not a session is still there to tick.
struct Report {
    bool show = false;
    float age = 0;          // seconds shown (worked out by view())
    Kind kind = Kind::race;
    int score = 0;          // zone control, 0..100
    std::string word, grade;
    float peak = 0, average = 0;
    std::string peak_cause, peak_where; // "CONTACT", "LAP 1 · TURN 4"
    float zone_share[4] = {};           // of the counted time
    float seconds = 0;                  // counted
    float stamina = 1;                  // 1 - fatigue
    int spikes = 0;
    double opened = 0;                  // the wall clock when it came up
    float paused_for = 0;               // seconds of pause since
};

// What the render thread draws.
struct View {
    bool live = false;      // a session is on: the monitor shows (NO SIGNAL without `signal`)
    bool signal = false;    // the subject's car reads
    bool paused = false;
    bool spectating = false; // the heart is the watched car's
    Phase phase = Phase::idle;
    Kind kind = Kind::race;
    float bpm = 0;          // shown (with the beat-to-beat variation)
    Zone zone = Zone::calm;
    float fatigue = 0, stress = 0, adrenaline = 0;
    float phase_beat = 0;   // the beat (0 = the R spike), for the ECG trace
    float clock = 0;        // session seconds, for the trace's sweep
    Cause cause = Cause::none;
    std::string subject;    // "YOU" or the watched car's label
    Card card;
    Report report;
};

inline Kind kind_of_type(uint32_t type)
{
    if (type >= 1 && type <= 4)
        return Kind::practice;
    if (type >= 5 && type <= 14)
        return Kind::qualifying;
    if (type >= 15 && type <= 17)
        return Kind::race;
    return Kind::time_trial;
}

inline const char *kind_name(Kind k)
{
    return k == Kind::practice ? "PRACTICE" : k == Kind::qualifying ? "QUALIFYING" : k == Kind::race ? "RACE" : "TIME TRIAL";
}

// "ferrari_2026" -> "FERRARI", "red_bull_racing_2025" -> "RED BULL RACING"
inline std::string team_of_badge(std::string key)
{
    size_t cut = key.find_last_of('_');
    if (cut != std::string::npos && cut + 1 < key.size() && std::all_of(key.begin() + cut + 1, key.end(), ::isdigit))
        key.resize(cut);
    for (char &c : key)
        c = c == '_' ? ' ' : char(std::toupper(static_cast<unsigned char>(c)));
    return key;
}

inline double steady_seconds()
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

class Session {
public:
    using Sink = std::function<void(const FxData &)>;
    using WallClock = double (*)();

    void configure(const Settings &s) { set_ = s; }
    const Settings &settings() const { return set_; }
    void set_sink(Sink sink) { sink_ = std::move(sink); }
    void set_clock(WallClock clock) { now_ = clock ? clock : steady_seconds; }

    // session.start: every heart starts again once the live state says what kind of session it is; the last
    // session's report goes.
    void session_start() { begin("session.start"); }

    // race.start: lights out. A second one in the same session is a restart: a new attempt, where every heart starts
    // again but keeps beating where it is.
    void race_start()
    {
        if (phase_ == Phase::idle || phase_ == Phase::closed)
            begin("race.start");
        bool again = phase_ == Phase::attempt;
        if (again) {
            for (int v = 0; v < kCars; v++)
                if (seen_[v])
                    new_attempt(hearts_[v], kind_);
            clear_counts();
            close_report();
        }
        phase_ = Phase::attempt;
        race_clock_ = clock_;
        for (int v = 0; v < kCars; v++)
            if (seen_[v])
                lights_out(hearts_[v]);
        swofty::log("[pulse] %s: %d heart(s) racing", again ? "a new attempt (race.start again)" : "lights out",
                    count_seen());
    }

    // session.end. reason: 0 the session ended, 1 a restart, 2 the game unloaded with the session open. Everything
    // stops: the effect, the monitor and the card. The report comes unless it is a restart.
    void session_end(int reason)
    {
        if (phase_ == Phase::idle || phase_ == Phase::closed) {
            if (set_.diagnostics)
                swofty::log("[pulse] session.end (reason %d) with nothing open", reason);
            return;
        }
        if (set_.diagnostics && player_ >= 0 && seen_[player_]) {
            const Stats &s = hearts_[player_].stats;
            swofty::log("[pulse] session over (%s, reason %d): %.0f s counted, average %.0f, peak %.0f (%s%s%s), "
                        "zone control %d, fatigue %.2f, %d spike(s)",
                        kind_name(kind_), reason, s.time, s.time > 0 ? s.sum / s.time : 0.0, s.peak,
                        cause_name(s.peak_cause), peak_where_.empty() ? "" : ", ", peak_where_.c_str(), composure(s),
                        hearts_[player_].fatigue, s.spikes);
        }
        flush_flashback(true);
        if (reason != 1)
            make_report("the session ended");
        close("session.end");
    }

    // session.unload: closes what session.end left open (it should never be).
    void session_unload()
    {
        if (phase_ == Phase::session || phase_ == Phase::attempt)
            session_end(2);
    }

    // The game's collision: a and b are the bus's car ids (see vehicle_of).
    void collision(int64_t a, int64_t b, bool paused)
    {
        if (!accepting(paused))
            return;
        const char *how = "";
        int va = vehicle_of(a, &how), vb = vehicle_of(b, &how);
        if (probe_collision_ && set_.diagnostics)
            probe_event("collision", a, b, va, vb, how);
        probe_collision_ = false;
        bool mine = driving_ && (va == player_ || vb == player_);
        if (set_.diagnostics && (va == player_ || vb == player_))
            swofty::log("[pulse] collision: cars %d and %d (you are %d%s)", va, vb, player_,
                        driving_ ? "" : ", watching another car");
        for (int v : {va, vb}) {
            // the game sends a message for every touch of a scrape: one incident is 3 s of them
            if (!in_play(v) || clock_ - last_hit_[v] < 3.0f)
                continue;
            last_hit_[v] = clock_;
            add_spike(v, 0.6f, Cause::contact);
            if (v == player_ && mine) {
                float speed = car_ok_[v] ? cars_[v].speed : 0;
                impact_ = speed > 25 ? 1.0f : 0.6f;
                impact_flash_ = impact_;
                impact_age_ = 0;
                int other = v == va ? vb : va;
                if (in_play(other)) {
                    pending_contact_ = other;
                    contact_at_ = clock_;
                }
            }
        }
    }

    // a passed b (the bus's car ids). In a race only, after lights out. A pair counts once in kPairWindow, and the
    // place given back within it takes both spikes back: the game's classification flickers when two cars run side
    // by side.
    void overtake(int64_t a, int64_t b, bool paused)
    {
        if (!accepting(paused) || phase_ != Phase::attempt || clock_ < quiet_until_)
            return;
        const char *how = "";
        int va = vehicle_of(a, &how), vb = vehicle_of(b, &how);
        if (probe_overtake_ && set_.diagnostics)
            probe_event("overtake", a, b, va, vb, how);
        probe_overtake_ = false;
        if (!in_play(va) || !in_play(vb) || va == vb)
            return;
        int lo = std::min(va, vb), hi = std::max(va, vb);
        trim_pairs();
        for (Pair &p : pairs_) {
            if (p.lo != lo || p.hi != hi)
                continue;
            if (!p.cancelled && p.winner == vb) {
                undo_spike(p.spike_winner);
                undo_spike(p.spike_loser);
                p.cancelled = true;
                if (set_.diagnostics && (va == player_ || vb == player_))
                    swofty::log("[pulse] overtake given back within %.0f s: both spikes taken back", kPairWindow);
            }
            return;
        }
        Pair p;
        p.lo = lo;
        p.hi = hi;
        p.at = clock_;
        p.winner = va;
        p.spike_winner = add_spike(va, capped(va, 0.12f), Cause::gained_place);
        p.spike_loser = add_spike(vb, capped(vb, 0.22f), Cause::lost_place);
        pairs_.push_back(p);
    }

    // Every frame. paused: the game is paused (nothing moves, nothing draws).
    void tick(float raw_dt, bool paused)
    {
        if (paused)
            report_pause(std::clamp(raw_dt, 0.0f, 5.0f));
        float dt = std::clamp(raw_dt, 0.0f, 0.25f);
        paused_ = paused;
        bool available = live::available();
        auto session = available ? live::session() : std::nullopt;
        // type 0 is no session: the game shows it while a track loads, before session.start
        bool typed = session && (session->have & SWOFTY_LIVE_SHAVE_TYPE) && session->type >= 1;
        note_live(!available ? LiveRead::off : typed ? LiveRead::data : LiveRead::empty);
        if (typed && phase_ == Phase::idle)
            begin("a session in the live state");
        else if (typed && phase_ == Phase::closed && have_closed_ && session->generation != closed_gen_ &&
                 session->type != closed_type_)
            begin("a new session in the live state");
        if (typed) {
            last_gen_ = session->generation;
            last_type_ = session->type;
            have_last_ = true;
        }
        if (!typed || phase_ == Phase::idle || phase_ == Phase::closed) {
            rest(paused);
            return;
        }
        Kind kind = session->game_mode == SWOFTY_LIVE_MODE_TIME_TRIAL ? Kind::time_trial : kind_of_type(session->type);
        if (!kind_known_ || kind != kind_) {
            if (kind_known_ && set_.diagnostics)
                swofty::log("[pulse] the session is now %s: every heart starts again", kind_name(kind));
            kind_ = kind;
            kind_known_ = true;
            seen_.fill(false);
            progress_ = -1;
        }
        int player = (session->have & SWOFTY_LIVE_SHAVE_PLAYER) ? session->player : -1;
        if (player != player_)
            progress_ = -1;
        player_ = player;
        watched_ = (session->have & SWOFTY_LIVE_SHAVE_WATCHED) ? session->watched : -1;
        // one rule for whose heart this is: you, unless the spectator HUD watches another car
        bool spectating = watched_ >= 0 && watched_ != player_;
        driving_ = !spectating && player_ >= 0;
        int subject = spectating ? watched_ : player_;
        if (session->subject_generation != subject_gen_ || subject != subject_) {
            subject_gen_ = session->subject_generation;
            subject_ = subject;
            subject_label_.clear();
            label_next_ = 0;
            g_lat_ = g_long_ = g_brake_ = 0;
        }
        if (spectating)
            pending_contact_ = -1;
        if (paused) {
            publish_view(subject, true);
            publish_fx(false, 0);
            return;
        }
        clock_ += dt;
        impact_age_ = std::min(impact_age_ + dt, 99.0f);
        ai_clock_ += dt;
        bool ai_tick = ai_clock_ >= 0.1f;
        float ai_dt = ai_clock_;
        if (ai_tick)
            ai_clock_ = 0;
        read_cars(ai_tick, subject);
        float track = (session->have & SWOFTY_LIVE_SHAVE_TRACK_LENGTH) ? session->track_length_m : 0;
        int sc = (session->have & SWOFTY_LIVE_SHAVE_SAFETY_CAR) && session->safety_car != 0xff ? session->safety_car : 0;
        bool red = (session->have & SWOFTY_LIVE_SHAVE_RED_FLAG) && session->red_flag == 1;
        sc_ = sc;
        red_ = red;
        track_ = track;
        int racers = int(session->racers);
        if (probe_first_ && set_.diagnostics)
            probe_start(*session, track);
        watch_rewind(track);
        flush_flashback(false);

        for (int v = 0; v < kCars; v++) {
            if (!car_ok_[v])
                continue;
            bool every_frame = v == player_ || v == subject;
            if (!every_frame && !ai_tick)
                continue;
            if (!seen_[v]) {
                reset(hearts_[v], kind_, uint32_t(v * 7919 + 17));
                if (phase_ == Phase::attempt) {
                    if (clock_ - race_clock_ < 3)
                        lights_out(hearts_[v]);
                    hearts_[v].since_start = clock_ - race_clock_;
                }
                seen_[v] = true;
            }
            Inputs in = inputs_for(v, every_frame ? dt : ai_dt, track, sc, red, racers);
            in.count = v == player_;
            watch_flags(v);
            int before = hearts_[v].stats.spikes;
            update(hearts_[v], in);
            // the heart's own spikes (a moment) go in the ledger too, so a flashback can take them back
            if (hearts_[v].stats.spikes > before)
                record(v, 0.4f, hearts_[v].spike_cause);
        }
        if (player_ >= 0 && seen_[player_])
            after_player_update();
        update_card(dt);
        publish_view(subject, false);
        publish_fx(set_.effects && driving_ && car_ok_[player_] && seen_[player_], dt);
        if (set_.diagnostics)
            probe_tick(sc, red);
    }

    View view() const
    {
        std::lock_guard lock(view_mutex_);
        if (view_.report.show) {
            float age = float(now_() - view_.report.opened) - view_.report.paused_for;
            view_.report.age = std::max(0.0f, age);
            if (age > kReportSeconds)
                view_.report.show = false;
        }
        return view_;
    }

    // Closes the report early (the monitor's key, Esc).
    void dismiss_report()
    {
        std::lock_guard lock(view_mutex_);
        view_.report.show = false;
    }

    const Heart *heart(int vehicle) const
    {
        return vehicle >= 0 && vehicle < kCars && seen_[vehicle] ? &hearts_[vehicle] : nullptr;
    }
    int player() const { return player_; }
    const FxData &fx() const { return fx_; }
    Kind kind() const { return kind_; }
    Phase phase() const { return phase_; }

    static constexpr float kCardSeconds = 8.0f;
    static constexpr float kReportSeconds = 16.0f;
    static constexpr float kPairWindow = 30.0f;
    static constexpr float kOvertakeCap = 0.5f; // overtakes alone never take the adrenaline past this

private:
    static constexpr int kCars = SWOFTY_LIVE_MAX_CARS;

    enum class LiveRead : uint8_t { unknown, off, empty, data };

    // a spike given to a heart, kept so a flashback or a place given back can take it back
    struct SpikeRec {
        int id = 0;
        int vehicle = -1;
        float at = 0;        // clock_
        float progress = -1; // where your car was (lap x track + lap distance)
        float amount = 0;
        bool undone = false;
    };

    struct Pair {
        int lo = -1, hi = -1;
        float at = 0;
        int winner = -1;
        int spike_winner = -1, spike_loser = -1;
        bool cancelled = false;
    };

    Settings set_;
    Sink sink_;
    WallClock now_ = steady_seconds;
    Phase phase_ = Phase::idle;
    Kind kind_ = Kind::race;
    bool kind_known_ = false;
    bool paused_ = false;
    float clock_ = 0, ai_clock_ = 0, race_clock_ = 0;
    int player_ = -1, watched_ = -1, subject_ = -1;
    bool driving_ = false;
    uint32_t subject_gen_ = 0;
    std::string subject_label_;
    float label_next_ = 0;
    uint32_t last_gen_ = 0, last_type_ = 0, closed_gen_ = 0, closed_type_ = 0;
    bool have_last_ = false, have_closed_ = false;
    int sc_ = 0;
    bool red_ = false;
    float track_ = 0;
    std::array<Heart, kCars> hearts_{};
    std::array<bool, kCars> seen_{};
    std::array<live::Car, kCars> cars_{};
    std::array<bool, kCars> car_ok_{};
    std::array<float, kCars> read_at_{};
    std::array<int, kCars> ahead_id_{}, behind_id_{};
    std::array<float, kCars> ahead_s_{}, behind_s_{};
    std::array<int, kCars> last_warnings_{};
    std::array<float, kCars> last_penalty_{};
    std::array<float, kCars> last_hit_{};
    float impact_ = 0, impact_flash_ = 0, impact_age_ = 99;
    float g_lat_ = 0, g_long_ = 0, g_brake_ = 0, tunnel_s_ = 0;
    float battle_time_ = 0, last_card_ = -1000, card_before_ = -1000, card_progress_ = -1;
    int battle_rival_ = -1;
    bool was_in_pit_ = false;
    float pit_exit_at_ = -100;
    int pending_contact_ = -1;
    float contact_at_ = 0;
    int card_rival_ = -1;
    bool report_done_ = false, player_finished_ = false;
    float last_peak_ = 0;
    std::string peak_where_;
    int spikes_seen_ = 0;
    // flashbacks and overtakes
    float progress_ = -1, quiet_until_ = 0;
    int fb_steps_ = 0, fb_undone_ = 0; // the flashback being rewound, for its one log line
    float fb_back_ = 0, fb_last_ = 0;
    std::deque<SpikeRec> ledger_;
    int next_spike_ = 1;
    std::vector<Pair> pairs_;
    FxData fx_;
    bool fx_was_active_ = false;
    // the probe
    bool probe_first_ = true, probe_collision_ = true, probe_overtake_ = true;
    float probe_next_ = 0;
    LiveRead live_read_ = LiveRead::unknown;
    int live_changes_ = 0;

    mutable std::mutex view_mutex_;
    mutable View view_;

    int count_seen() const { return int(std::count(seen_.begin(), seen_.end(), true)); }

    // ---- the session's life ----

    void begin(const char *why)
    {
        phase_ = Phase::session;
        kind_known_ = false;
        clock_ = 0;
        seen_.fill(false);
        car_ok_.fill(false);
        last_warnings_.fill(-1);
        last_penalty_.fill(-1);
        player_ = watched_ = subject_ = -1;
        driving_ = false;
        clear_counts();
        probe_next_ = 0;
        probe_first_ = probe_collision_ = probe_overtake_ = true;
        close_report();
        {
            std::lock_guard lock(view_mutex_);
            view_.card = Card{};
        }
        swofty::log("[pulse] phase: session (%s)", why);
    }

    // what a new attempt starts again: the session's counts, the card, the flashback ledger
    void clear_counts()
    {
        last_hit_.fill(-100);
        impact_age_ = 99;
        impact_ = impact_flash_ = 0;
        g_lat_ = g_long_ = g_brake_ = tunnel_s_ = 0;
        battle_time_ = 0;
        battle_rival_ = -1;
        last_card_ = card_before_ = -1000;
        card_progress_ = -1;
        pending_contact_ = -1;
        card_rival_ = -1;
        report_done_ = false;
        player_finished_ = false;
        last_peak_ = 0;
        peak_where_.clear();
        spikes_seen_ = 0;
        was_in_pit_ = false;
        pit_exit_at_ = -100;
        progress_ = -1;
        quiet_until_ = 0;
        fb_steps_ = fb_undone_ = 0;
        fb_back_ = fb_last_ = 0;
        ledger_.clear();
        pairs_.clear();
        std::lock_guard lock(view_mutex_);
        view_.card = Card{};
    }

    void close(const char *why)
    {
        phase_ = Phase::closed;
        have_closed_ = have_last_;
        closed_gen_ = last_gen_;
        closed_type_ = last_type_;
        kind_known_ = false;
        pending_contact_ = -1;
        {
            std::lock_guard lock(view_mutex_);
            view_.live = false;
            view_.signal = false;
            view_.card = Card{};
        }
        publish_fx(false, 0);
        swofty::log("[pulse] phase: closed (%s)", why);
    }

    void close_report()
    {
        std::lock_guard lock(view_mutex_);
        view_.report = Report{};
    }

    void report_pause(float dt)
    {
        std::lock_guard lock(view_mutex_);
        if (view_.report.show)
            view_.report.paused_for += dt;
    }

    // no session to follow: nothing on screen but a report, NO SIGNAL while a session is open without data
    void rest(bool paused)
    {
        {
            std::lock_guard lock(view_mutex_);
            view_.live = phase_ == Phase::session || phase_ == Phase::attempt;
            view_.signal = false;
            view_.paused = paused;
            view_.phase = phase_;
            view_.card = Card{};
        }
        publish_fx(false, 0);
    }

    void note_live(LiveRead r)
    {
        if (r == live_read_)
            return;
        live_read_ = r;
        if (++live_changes_ > 40)
            return;
        const char *what = r == LiveRead::off ? "unavailable" : r == LiveRead::empty ? "available, no session data yet"
                                                                                     : "session data";
        swofty::log("[pulse] live: %s%s", what, live_changes_ == 40 ? " (further changes not logged)" : "");
    }

    bool accepting(bool paused) const
    {
        return !paused && !paused_ && kind_known_ && (phase_ == Phase::session || phase_ == Phase::attempt);
    }

    // a car that exists and is still in the session (running, or in the pit; not finished, retired or out)
    bool in_play(int v) const
    {
        if (v < 0 || v >= kCars || !seen_[v] || !car_ok_[v])
            return false;
        uint8_t s = cars_[v].result_status;
        return s == 0 || s == 2;
    }

    // ---- the bus's car ids ----

    // The events carry the core's car ids: the docs call them participant indices, the live state counts vehicle
    // ids, and the two are not always the same. With UDP on, the participant's place and lap distance say which
    // live car it is; without it, the id is taken as a vehicle id.
    int vehicle_of(int64_t id, const char **how) const
    {
        if (id < 0 || id >= kCars)
            return -1;
        swofty::telemetry::Participant p{};
        if (swofty::telemetry::participant(uint32_t(id), &p) == swofty::telemetry::Result::ok && p.position > 0) {
            int by_place = -1;
            for (int v = 0; v < kCars; v++) {
                if (!car_ok_[v] || cars_[v].position != p.position)
                    continue;
                by_place = v;
            }
            if (by_place == int(id) || (by_place < 0 && car_ok_[id])) {
                *how = "vehicle id (UDP agrees)";
                return int(id);
            }
            if (by_place >= 0) {
                *how = "UDP participant, matched by place";
                return by_place;
            }
        }
        *how = "vehicle id";
        return car_ok_[id] ? int(id) : -1;
    }

    // A driver's name from UDP, only when its participant is this car (the same place, the lap distance close);
    // else the team badge, else the place.
    std::string label_of(int v, std::string *why = nullptr) const
    {
        if (car_ok_[v] && cars_[v].position > 0) {
            for (uint32_t i = 0; i < 24; i++) {
                swofty::telemetry::Participant p{};
                if (swofty::telemetry::participant(i, &p) != swofty::telemetry::Result::ok || !p.name[0] ||
                    p.position != cars_[v].position)
                    continue;
                if (track_ > 0 && p.lap_distance > 0) {
                    float d = std::fabs(p.lap_distance - cars_[v].lap_m);
                    d = std::min(d, track_ - d);
                    if (d > 60)
                        continue;
                }
                std::string name(p.name, strnlen(p.name, sizeof p.name));
                for (char &ch : name)
                    ch = char(std::toupper(static_cast<unsigned char>(ch)));
                if (why)
                    *why = "UDP name, matched by place";
                return name;
            }
        }
        if (auto badge = live::team_badge(uint32_t(v))) {
            if (why)
                *why = "team badge (no UDP participant matched the car)";
            return team_of_badge(*badge);
        }
        if (why)
            *why = "place (no UDP name, no team badge)";
        if (car_ok_[v] && cars_[v].position > 0)
            return "P" + std::to_string(cars_[v].position);
        return "CAR " + std::to_string(v);
    }

    // ---- spikes, flashbacks ----

    float capped(int v, float amount) const { return std::min(amount, std::max(0.0f, kOvertakeCap - hearts_[v].adrenaline)); }

    int record(int v, float amount, Cause)
    {
        SpikeRec r;
        r.id = next_spike_++;
        r.vehicle = v;
        r.at = clock_;
        r.progress = progress_;
        r.amount = amount;
        ledger_.push_back(r);
        while (!ledger_.empty() && clock_ - ledger_.front().at > 120)
            ledger_.pop_front();
        return r.id;
    }

    int add_spike(int v, float amount, Cause cause)
    {
        if (v < 0 || v >= kCars || !seen_[v] || amount < 0.01f)
            return -1;
        spike(hearts_[v], amount, cause);
        return record(v, amount, cause);
    }

    void undo_spike(int id)
    {
        for (SpikeRec &r : ledger_)
            if (r.id == id && !r.undone) {
                r.undone = true;
                unspike(hearts_[r.vehicle], r.amount, clock_ - r.at);
            }
    }

    void trim_pairs()
    {
        pairs_.erase(std::remove_if(pairs_.begin(), pairs_.end(),
                                    [&](const Pair &p) { return clock_ - p.at >= kPairWindow; }),
                     pairs_.end());
    }

    // A flashback: your car's distance goes back. Everything that happened after the point it went back to never
    // happened: its spikes are taken back, the flash and the card go, and the overtakes the jump makes are ignored.
    void watch_rewind(float track)
    {
        int v = player_ >= 0 && car_ok_[player_] ? player_ : subject_;
        if (v < 0 || !car_ok_[v] || track <= 0 || !live::has(cars_[v], SWOFTY_LIVE_HAVE_PROGRESS)) {
            progress_ = -1;
            return;
        }
        float prog = float(cars_[v].lap) * track + cars_[v].lap_m;
        if (progress_ >= 0) {
            float back = progress_ - prog;
            if (back > 30 && back < std::min(1500.0f, track * 0.6f))
                flashback(prog, back);
        }
        progress_ = prog;
    }

    // The game rewinds in steps (8 or 9 a flashback in the game, 13 Nov): they add up to one flashback in the log,
    // written once the rewind has stood still for 1.5 s.
    void flush_flashback(bool now)
    {
        if (fb_steps_ == 0 || (!now && clock_ - fb_last_ < 1.5f))
            return;
        swofty::log("[pulse] flashback: back %.0f m in %d step(s), %d spike(s) taken back", fb_back_, fb_steps_,
                    fb_undone_);
        fb_steps_ = fb_undone_ = 0;
        fb_back_ = 0;
    }

    void flashback(float to, float back)
    {
        fb_steps_++;
        fb_back_ += back;
        fb_last_ = clock_;
        int undone = 0;
        for (SpikeRec &r : ledger_)
            if (!r.undone && r.progress > to + 5) {
                r.undone = true;
                unspike(hearts_[r.vehicle], r.amount, clock_ - r.at);
                undone++;
            }
        impact_ = impact_flash_ = 0;
        impact_age_ = 99;
        pending_contact_ = -1;
        battle_time_ = 0;
        pairs_.clear();
        quiet_until_ = clock_ + 2.0f;
        {
            std::lock_guard lock(view_mutex_);
            if (view_.card.show && card_progress_ > to + 5) {
                view_.card = Card{};
                last_card_ = card_before_;
            }
        }
        fb_undone_ += undone;
    }

    // ---- reading the cars ----

    // every car on the AI's 10 Hz tick; your car and the watched one every frame
    void read_cars(bool all, int subject)
    {
        for (int v = 0; v < kCars; v++) {
            if (!all && v != player_ && v != subject)
                continue;
            auto c = live::car(uint32_t(v));
            car_ok_[v] = c && live::is_racer(*c) && live::has(*c, SWOFTY_LIVE_HAVE_SPEED);
            if (car_ok_[v]) {
                cars_[v] = *c;
                read_at_[v] = clock_;
            }
        }
    }

    bool running(int v) const
    {
        const live::Car &c = cars_[v];
        if (live::in_pit(c))
            return false;
        return c.result_status == 0 || c.result_status == 2;
    }

    // a car's lap distance now (read up to 0.1 s ago)
    float lap_at(int v, float track) const
    {
        float m = cars_[v].lap_m + cars_[v].speed * std::max(0.0f, clock_ - read_at_[v]);
        return std::fmod(m + track, track);
    }

    // In a race, a car `d` metres ahead on the road (behind if `ahead` is false) that is not on your lap: lapped or
    // lapping. Laps read as the game gives them; a car just over the line counts its new lap.
    bool other_lap(int v, int o, float d, bool ahead) const
    {
        if (kind_ != Kind::race || !live::has(cars_[v], SWOFTY_LIVE_HAVE_PROGRESS) ||
            !live::has(cars_[o], SWOFTY_LIVE_HAVE_PROGRESS))
            return false;
        float mine = lap_at(v, track_);
        int expect = ahead ? cars_[v].lap + (mine + d >= track_ ? 1 : 0) : cars_[v].lap - (mine - d < 0 ? 1 : 0);
        return cars_[o].lap != expect;
    }

    void nearest(int v, float track)
    {
        ahead_id_[v] = behind_id_[v] = -1;
        ahead_s_[v] = behind_s_[v] = -1;
        // standing still (the grid, a stop on track) there are no gaps to speak of
        if (track <= 0 || !live::has(cars_[v], SWOFTY_LIVE_HAVE_PROGRESS) || !running(v) || cars_[v].speed < 10)
            return;
        float mine = lap_at(v, track);
        float best_ahead = track, best_behind = track;
        for (int o = 0; o < kCars; o++) {
            if (o == v || !car_ok_[o] || !running(o) || !live::has(cars_[o], SWOFTY_LIVE_HAVE_PROGRESS))
                continue;
            float d = std::fmod(lap_at(o, track) - mine + 2 * track, track); // how far ahead, 0..track
            if (d < best_ahead && !other_lap(v, o, d, true)) {
                best_ahead = d;
                ahead_id_[v] = o;
            }
            if (track - d < best_behind && !other_lap(v, o, track - d, false)) {
                best_behind = track - d;
                behind_id_[v] = o;
            }
        }
        // in seconds: the gap over the speed of the car that has to cover it
        if (ahead_id_[v] >= 0)
            ahead_s_[v] = best_ahead / std::max(cars_[v].speed, 20.0f);
        if (behind_id_[v] >= 0)
            behind_s_[v] = best_behind / std::max(cars_[behind_id_[v]].speed, 20.0f);
    }

    Inputs inputs_for(int v, float dt, float track, int sc, bool red, int racers)
    {
        const live::Car &c = cars_[v];
        nearest(v, track);
        Inputs in;
        in.dt = dt;
        in.kind = kind_;
        in.known = true;
        in.in_pit = live::in_pit(c);
        in.finished = c.result_status == 3;
        in.on_track = running(v);
        in.speed = c.speed;
        in.have_pedals = live::has(c, SWOFTY_LIVE_HAVE_PEDALS);
        in.brake = in.have_pedals ? c.brake : 0;
        in.have_g = live::has(c, SWOFTY_LIVE_HAVE_G);
        in.g_lat = c.g_lat;
        in.g_long = c.g_long;
        in.ahead_s = ahead_s_[v];
        in.behind_s = behind_s_[v];
        in.position = live::has(c, SWOFTY_LIVE_HAVE_CLASS) ? c.position : 0;
        in.cars = racers;
        in.safety_car = sc;
        in.red_flag = red;
        in.formation = kind_ == Kind::race && phase_ == Phase::session;
        return in;
    }

    // track limits and penalties: a spike when a count goes up (not on the first reading)
    void watch_flags(int v)
    {
        const live::Car &c = cars_[v];
        if (!live::has(c, SWOFTY_LIVE_HAVE_PENALTIES))
            return;
        int warnings = c.track_limit_warnings;
        float penalty = c.time_penalty_s + 10.0f * c.drive_through + 20.0f * c.stop_go;
        if (last_warnings_[v] >= 0 && warnings > last_warnings_[v])
            add_spike(v, 0.18f, Cause::track_limits);
        if (last_penalty_[v] >= 0 && penalty > last_penalty_[v] + 0.1f)
            add_spike(v, 0.35f, Cause::penalty);
        last_warnings_[v] = warnings;
        last_penalty_[v] = penalty;
    }

    std::string where_of(const live::Car &c) const
    {
        char b[48];
        auto turn = live::turn(c);
        if (turn && turn->distance_ahead_m < 120)
            std::snprintf(b, sizeof b, "LAP %d \xC2\xB7 TURN %s", c.lap + 1, turn->label);
        else
            std::snprintf(b, sizeof b, "LAP %d", c.lap + 1);
        return b;
    }

    void after_player_update()
    {
        Heart &h = hearts_[player_];
        const live::Car &c = cars_[player_];
        if (h.stats.peak > last_peak_ + 0.5f) {
            last_peak_ = h.stats.peak;
            peak_where_ = where_of(c);
        }
        // a moment (spin, off, a hit the bus did not report): the blur without the flash
        if (h.stats.spikes != spikes_seen_) {
            if (h.stats.spikes > spikes_seen_ && h.spike_cause == Cause::moment && impact_age_ > 1.0f && driving_) {
                impact_ = 0.45f;
                impact_flash_ = 0;
                impact_age_ = 0;
            }
            spikes_seen_ = h.stats.spikes;
        }
        // the pit: no fights until 5 s after the exit
        bool pit = live::in_pit(c);
        if (was_in_pit_ && !pit)
            pit_exit_at_ = clock_;
        was_in_pit_ = pit;
        // the race's report comes when you take the flag or your race ends (retired, disqualified, not classified)
        if (kind_ == Kind::race && c.result_status >= 3 && c.result_status <= 7 && !player_finished_) {
            player_finished_ = true;
            make_report(c.result_status == 3 ? "the flag" : "your race ended");
        }
    }

    CardSide side_of(int v, bool you, const std::string &label)
    {
        CardSide s;
        const Heart &h = hearts_[v];
        s.bpm = shown_bpm(h);
        s.zone = zone_of(h.bpm);
        s.position = car_ok_[v] ? cars_[v].position : 0;
        s.label = you ? "YOU" : label;
        s.ecg_phase = h.phase;
        return s;
    }

    void open_card(const char *title, const char *mode, int rival)
    {
        card_rival_ = rival;
        card_before_ = last_card_;
        last_card_ = clock_;
        card_progress_ = progress_;
        std::string why;
        std::string label = label_of(rival, &why);
        std::lock_guard lock(view_mutex_);
        view_.card = Card{};
        view_.card.show = true;
        view_.card.title = title;
        view_.card.mode = mode;
        view_.card.you = side_of(player_, true, "");
        view_.card.rival = side_of(rival, false, label);
        if (set_.diagnostics)
            swofty::log("[pulse] card: %s %s, you %.0f bpm, %s %.0f bpm (car %d named by %s)", title, mode,
                        view_.card.you.bpm, label.c_str(), view_.card.rival.bpm, rival, why.c_str());
    }

    // may the player fight `rival` now? not in the pit, not just out of it, not with a lapped car, not behind the
    // safety car or under a red flag
    bool can_fight(int rival) const
    {
        if (rival < 0 || !seen_[rival] || !car_ok_[rival] || sc_ != 0 || red_)
            return false;
        if (live::in_pit(cars_[player_]) || clock_ - pit_exit_at_ < 5)
            return false;
        return running(rival);
    }

    void update_card(float dt)
    {
        bool showing;
        {
            std::lock_guard lock(view_mutex_);
            showing = view_.card.show;
            if (showing) {
                view_.card.age += dt;
                if (view_.card.age > kCardSeconds || !set_.tv || !driving_) {
                    view_.card.show = false;
                    showing = false;
                } else if (player_ >= 0 && seen_[player_] && card_rival_ >= 0 && seen_[card_rival_]) {
                    std::string label = view_.card.rival.label; // asked once
                    view_.card.you = side_of(player_, true, "");
                    view_.card.rival = side_of(card_rival_, false, label);
                }
            }
        }
        if (!set_.tv || !driving_ || kind_ != Kind::race || player_ < 0 || !seen_[player_] ||
            phase_ != Phase::attempt) {
            battle_time_ = 0;
            battle_rival_ = -1;
            return;
        }
        const Heart &h = hearts_[player_];
        // a contact with another car: the card a moment later
        if (pending_contact_ >= 0 && clock_ - contact_at_ > 1.2f) {
            int other = pending_contact_;
            pending_contact_ = -1;
            if (!showing && seen_[other] && clock_ - last_card_ > 20) {
                open_card("CONTACT", "", other);
                return;
            }
        }
        // a fight of a few seconds with one rival: wheel to wheel, or under pressure from behind
        int rival = h.defending ? behind_id_[player_] : ahead_id_[player_];
        if (h.battle <= 0.55f || !can_fight(rival)) {
            battle_time_ = 0;
            battle_rival_ = -1;
            return;
        }
        if (rival != battle_rival_) {
            battle_rival_ = rival;
            battle_time_ = 0;
        }
        battle_time_ += dt;
        if (!showing && battle_time_ > 3 && clock_ - last_card_ > 75)
            open_card(h.defending ? "UNDER PRESSURE" : "WHEEL TO WHEEL", h.defending ? "DEFENDING" : "ATTACKING",
                      rival);
    }

    void make_report(const char *why)
    {
        if (report_done_ || !set_.report || player_ < 0 || !seen_[player_])
            return;
        const Heart &h = hearts_[player_];
        const Stats &s = h.stats;
        if (s.time < 20)
            return;
        report_done_ = true;
        Report r;
        r.show = true;
        r.kind = kind_;
        r.score = composure(s);
        r.word = composure_word(r.score);
        r.grade = composure_grade(r.score);
        r.peak = s.peak;
        r.average = float(s.sum / s.time);
        r.peak_cause = cause_name(s.peak_cause);
        r.peak_where = peak_where_;
        for (int i = 0; i < 4; i++)
            r.zone_share[i] = s.zone_time[i] / s.time;
        r.seconds = s.time;
        r.stamina = 1.0f - h.fatigue;
        r.spikes = s.spikes;
        r.opened = now_();
        if (set_.diagnostics)
            swofty::log("[pulse] report (%s): zone control %d, peak %.0f", why, r.score, r.peak);
        std::lock_guard lock(view_mutex_);
        view_.report = r;
    }

    void publish_view(int subject, bool paused)
    {
        // the watched car's label, asked again every 2 s (its place changes)
        bool ok = subject >= 0 && subject < kCars && seen_[subject] && car_ok_[subject];
        if (ok && !driving_ && clock_ >= label_next_) {
            subject_label_ = label_of(subject);
            label_next_ = clock_ + 2;
        }
        std::lock_guard lock(view_mutex_);
        view_.paused = paused;
        view_.kind = kind_;
        view_.phase = phase_;
        view_.live = true;
        view_.signal = ok;
        view_.spectating = !driving_ && subject >= 0;
        if (!ok)
            return;
        const Heart &h = hearts_[subject];
        view_.subject = driving_ ? "YOU" : subject_label_;
        view_.bpm = shown_bpm(h);
        view_.zone = zone_of(h.bpm);
        view_.fatigue = h.fatigue;
        view_.stress = stress(h);
        view_.adrenaline = h.adrenaline;
        view_.phase_beat = h.phase;
        view_.clock = clock_;
        view_.cause = h.cause;
    }

    static float ease(float dt, float tau) { return tau <= 0 ? 1.0f : 1.0f - std::exp(-dt / tau); }

    // The screen effect's data. Every channel is worked out from its own switch and comes out at exactly 0 when off;
    // active is 1 only while one of them shows.
    void publish_fx(bool on, float dt)
    {
        FxData f;
        f.time = clock_;
        if (on) {
            const Heart &h = hearts_[player_];
            const live::Car &c = cars_[player_];
            float k = std::clamp(set_.strength, 0.0f, 1.0f);
            bool comfort = set_.comfort;
            Motion m = set_.motion;
            // the G, smoothed by time (the same at 30 and 144 fps); with no G read it fades out in about 0.4 s
            float g_tau = m == Motion::full ? 0.13f : m == Motion::reduced ? 0.3f : 0.6f;
            bool have_g = live::has(c, SWOFTY_LIVE_HAVE_G);
            float lat = have_g ? c.g_lat : 0, lon = have_g ? c.g_long : 0;
            float a = ease(dt, have_g ? g_tau : 0.13f);
            g_lat_ += (lat - g_lat_) * a;
            g_long_ += (lon - g_long_) * a;
            // braking G only with the pedal read and pressed: a bump or a kerb is not braking
            bool braking = live::has(c, SWOFTY_LIVE_HAVE_PEDALS) && c.brake > 0.5f;
            // thresholds from the game's own G (Montreal: 2.2 G lateral most of the time, about 4 G under braking)
            float brake_to = braking ? std::clamp((std::fabs(g_long_) - kBrakeGFrom) / kBrakeGSpan, 0.0f, 1.0f) : 0.0f;
            g_brake_ += (brake_to - g_brake_) * a;

            float tunnel = set_.tunnel ? stress(h) * k * (comfort ? 0.6f : 1.0f) : 0;
            float t_tau = m == Motion::full ? 0 : m == Motion::reduced ? 0.8f : 2.0f;
            tunnel_s_ += (tunnel - tunnel_s_) * ease(dt, t_tau);
            f.tunnel = tunnel_s_;
            f.desat = f.tunnel * 0.8f;
            float throb = std::clamp((h.bpm - 160.0f) / 35.0f, 0.0f, 1.0f);
            if (set_.heartbeat && !comfort && m != Motion::still)
                f.beat = beat_wave(h.phase) * throb * k * (m == Motion::reduced ? 0.5f : 1.0f);
            if (set_.fatigue) {
                float tired = std::clamp((h.fatigue - kFatigueVisual) / (1 - kFatigueVisual), 0.0f, 1.0f) * k;
                f.soft = comfort ? 0 : tired;
                f.grain = comfort || m == Motion::still ? 0 : tired;
                f.dim = tired * (comfort ? 0.4f : 0.6f);
            }
            if (set_.gforce) {
                float side = std::clamp((std::fabs(g_lat_) - kSideGFrom) / kSideGSpan, 0.0f, 1.0f);
                f.g_side = (g_lat_ * kGSideSign >= 0 ? 1.0f : -1.0f) * side * k;
                f.g_brake = g_brake_ * k;
            }
            if (set_.impact && !comfort && m != Motion::still && impact_age_ < 1.5f) {
                float soft = m == Motion::reduced ? 0.5f : 1.0f;
                f.flash = impact_flash_ * std::max(0.0f, 1.0f - impact_age_ / 0.16f) * k * soft;
                float left = std::max(0.0f, 1.0f - impact_age_ / 1.1f);
                f.blur = impact_ * left * left * k * soft;
            }
            const float e = 0.001f;
            bool any = f.tunnel > e || f.beat > e || f.soft > e || f.grain > e || f.dim > e ||
                       std::fabs(f.g_side) > e || f.g_brake > e || f.flash > e || f.blur > e;
            f.active = any ? 1.0f : 0.0f;
        } else {
            g_lat_ = g_long_ = g_brake_ = tunnel_s_ = 0;
        }
        fx_ = f;
        bool active = f.active > 0;
        if (!active && !fx_was_active_)
            return;
        fx_was_active_ = active;
        if (sink_)
            sink_(f);
    }

    // ---- the probe: how the live state reads in the game (one line a second for two minutes, then every 10 s) ----
    void probe_start(const live::Session &s, float track)
    {
        probe_first_ = false;
        swofty::log("[pulse] probe: session type %u (%s), %u car(s), %u racer(s), player %d, watched %d, track '%s' "
                    "%.0f m, safety car %u, generation %u, have 0x%llx",
                    s.type, kind_name(kind_), s.cars, s.racers, s.player, s.watched,
                    (s.have & SWOFTY_LIVE_SHAVE_TRACK_NAME) ? s.track_name : "?", track, s.safety_car, s.generation,
                    (unsigned long long)s.have);
        if (player_ >= 0 && car_ok_[player_])
            swofty::log("[pulse] probe: your car %d has 0x%llx (speed, G %s, pedals %s, progress %s, class %s, pit %s, "
                        "penalties %s), participant field %u",
                        player_, (unsigned long long)cars_[player_].have, yes(SWOFTY_LIVE_HAVE_G),
                        yes(SWOFTY_LIVE_HAVE_PEDALS), yes(SWOFTY_LIVE_HAVE_PROGRESS), yes(SWOFTY_LIVE_HAVE_CLASS),
                        yes(SWOFTY_LIVE_HAVE_PIT), yes(SWOFTY_LIVE_HAVE_PENALTIES), cars_[player_].participant);
    }

    const char *yes(uint64_t bit) const
    {
        return player_ >= 0 && car_ok_[player_] && live::has(cars_[player_], bit) ? "yes" : "no";
    }

    std::string describe_id(int64_t id) const
    {
        char b[160];
        if (id < 0 || id >= kCars)
            return "out of range";
        int v = int(id);
        int n = 0;
        if (car_ok_[v])
            n = std::snprintf(b, sizeof b, "live car %d P%d at %.0f m, participant field %u", v, cars_[v].position,
                              cars_[v].lap_m, cars_[v].participant);
        else
            n = std::snprintf(b, sizeof b, "no live car %d", v);
        swofty::telemetry::Participant p{};
        auto r = swofty::telemetry::participant(uint32_t(v), &p);
        if (r == swofty::telemetry::Result::ok)
            std::snprintf(b + n, sizeof b - n, "; UDP participant %d: driver_id %u, P%d at %.0f m", v, p.driver_id,
                          p.position, p.lap_distance);
        else
            std::snprintf(b + n, sizeof b - n, "; UDP participant %d: %s", v, swofty::telemetry::name(r));
        return b;
    }

    void probe_event(const char *what, int64_t a, int64_t b, int va, int vb, const char *how)
    {
        swofty::log("[pulse] probe: first %s: bus ids %lld and %lld -> cars %d and %d (%s); you are %d", what,
                    (long long)a, (long long)b, va, vb, how, player_);
        swofty::log("[pulse] probe:   id %lld: %s", (long long)a, describe_id(a).c_str());
        swofty::log("[pulse] probe:   id %lld: %s", (long long)b, describe_id(b).c_str());
    }

    void probe_tick(int sc, bool red)
    {
        if (clock_ < probe_next_ || player_ < 0 || !seen_[player_] || !car_ok_[player_])
            return;
        probe_next_ = clock_ + (clock_ < 120 ? 1.0f : 10.0f);
        const Heart &h = hearts_[player_];
        const live::Car &c = cars_[player_];
        swofty::log("[pulse] probe t=%.0f: bpm %.0f -> %.0f (%s, %s), %.0f km/h, g lat %+.2f long %+.2f, brake %.2f, "
                    "ahead %.2f s, behind %.2f s, P%d, lap %d %.0f m, sc %d%s%s%s, fatigue %.2f, fx tunnel %.2f beat "
                    "%.2f g %+.2f/%.2f",
                    clock_, h.bpm, h.target, zone_name(zone_of(h.bpm)), cause_name(h.cause), c.speed * 3.6f, c.g_lat,
                    c.g_long, live::has(c, SWOFTY_LIVE_HAVE_PEDALS) ? c.brake : -1.0f, ahead_s_[player_],
                    behind_s_[player_], c.position, c.lap + 1, c.lap_m, sc, red ? ", red flag" : "",
                    live::in_pit(c) ? ", in the pit" : "", driving_ ? "" : ", watching another car", h.fatigue,
                    fx_.tunnel, fx_.beat, fx_.g_side, fx_.g_brake);
    }
};

} // namespace pulse
