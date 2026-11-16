// SPDX-License-Identifier: MIT
// Draws PULSE's overlay into SVG files without the game: the monitor in each zone, corner and size, the DRIVER
// BIOMETRICS card and the PULSE REPORT. tools\render_svgs.py turns the SVG files into PNG.
//
//   preview.exe <out dir>
#include "svg_writer.hpp"

#include "../src/monitor.hpp"

#include <cstdio>
#include <filesystem>

using namespace pulse;
namespace mon = pulse::monitor;

namespace {

View view_at(float bpm, float fatigue, float clock)
{
    View v;
    v.live = true;
    v.kind = Kind::race;
    v.bpm = bpm;
    v.zone = zone_of(bpm);
    v.fatigue = fatigue;
    v.clock = clock;
    v.signal = true;
    v.phase_beat = 0.08f; // just after the R spike: the trace's head on its way down
    v.subject = "YOU";
    return v;
}

CardSide side(const char *label, int pos, float bpm, float phase)
{
    CardSide s;
    s.label = label;
    s.position = pos;
    s.bpm = bpm;
    s.zone = zone_of(bpm);
    s.ecg_phase = phase;
    return s;
}

// A mock-up of F1 25's own HUD (where its pieces usually sit, at 16:9), to see what the monitor and the card keep
// clear of. Grey boxes, not the game's art.
void hud_mockup(float w, float h)
{
    namespace ov = swofty::overlay;
    float k = std::min(w / 1920, h / 1080);
    ov::Rect safe = ov::safe_area(nullptr);
    auto box = [&](float x, float y, float bw, float bh, const char *label) {
        ov::rect(nullptr, x, y, bw, bh, 0x8090A030);
        ov::rect_outline(nullptr, x, y, bw, bh, 1, 0xB0C0D070);
        ov::text(nullptr, x + bw / 2, y + bh / 2, mon::ts(14 * k, 0xC8D2DCB0, ov::align::center | ov::align::middle, 2 * k),
                 label);
    };
    float cx = safe.x + safe.w / 2;
    box(safe.x, safe.y, 250 * k, 70 * k, "POS / LAP");
    box(safe.x, safe.y + 120 * k, 250 * k, 470 * k, "TIMING TOWER");
    box(safe.x + safe.w - 230 * k, safe.y, 230 * k, 230 * k, "MINI MAP");
    box(cx - 230 * k, safe.y + safe.h - 150 * k, 460 * k, 150 * k, "MFD");
    box(safe.x + safe.w - 330 * k, safe.y + safe.h - 160 * k, 330 * k, 160 * k, "REVS / SPEED / ERS");
    box(cx - 150 * k, safe.y + 300 * k, 300 * k, 50 * k, "GAME MESSAGES");
}

void monitor_shot(const std::filesystem::path &file, float w, float h, const View &v, mon::Corner corner,
                  float size = 1.0f, const Card *card = nullptr, bool hud = false)
{
    preview::W = w;
    preview::H = h;
    preview::begin();
    if (hud)
        hud_mockup(w, h);
    mon::g_size = size;
    bool compact = mon::compact_for(h);
    float k = std::min(w / 1920, h / 1080) * size;
    float x = 0, y = 0;
    mon::monitor_at(nullptr, corner, k, &x, &y, compact);
    mon::draw_monitor(nullptr, v, x, y, k, compact);
    if (card)
        mon::draw_card(nullptr, *card, w, h, k, v.clock);
    mon::g_size = 1.0f;
    preview::save(file);
    std::printf("  %s\n", file.filename().string().c_str());
}

void report_shot(const std::filesystem::path &file, const Report &r, bool pad = false, float w = 1920, float h = 1080,
                 mon::ReportAt at = mon::ReportAt::left, bool hud = false)
{
    preview::W = w;
    preview::H = h;
    preview::begin();
    if (hud)
        hud_mockup(w, h);
    mon::g_report_at = int(at);
    mon::draw_report(nullptr, r, w, h, std::min(w / 1920, h / 1080), 5, pad);
    mon::g_report_at = int(mon::ReportAt::left);
    preview::save(file);
    std::printf("  %s\n", file.filename().string().c_str());
}

} // namespace

int main(int argc, char **argv)
{
    if (argc < 2) {
        std::printf("preview <out dir>\n");
        return 2;
    }
    if (!LoadLibraryW(L"swofty_core.dll")) {
        std::printf("swofty_core.dll (the preview's stand-in) is missing next to the exe\n");
        return 1;
    }
    preview::install();
    std::filesystem::path dir = argv[1];
    std::filesystem::create_directories(dir);
    std::printf("preview: writing to %s\n", dir.string().c_str());

    monitor_shot(dir / "monitor_calm.svg", 1920, 1080, view_at(134, 0.05f, 1.2f), mon::Corner::bottom_left);
    monitor_shot(dir / "monitor_focused.svg", 1920, 1080, view_at(152, 0.18f, 2.1f), mon::Corner::bottom_left);
    monitor_shot(dir / "monitor_zone.svg", 1920, 1080, view_at(169, 0.32f, 0.7f), mon::Corner::bottom_left);
    monitor_shot(dir / "monitor_red.svg", 1920, 1080, view_at(188, 0.71f, 2.6f), mon::Corner::bottom_left);
    monitor_shot(dir / "monitor_top_right.svg", 1920, 1080, view_at(171, 0.4f, 1.6f), mon::Corner::top_right);
    monitor_shot(dir / "monitor_large.svg", 1920, 1080, view_at(176, 0.4f, 1.9f), mon::Corner::bottom_left, 1.2f);
    monitor_shot(dir / "monitor_1440p.svg", 2560, 1440, view_at(163, 0.2f, 1.1f), mon::Corner::bottom_left);
    monitor_shot(dir / "monitor_720p.svg", 1280, 720, view_at(163, 0.2f, 1.1f), mon::Corner::bottom_left, 0.85f);
    View spect = view_at(158, 0.3f, 1.4f);
    spect.subject = "RED BULL RACING";
    spect.spectating = true;
    monitor_shot(dir / "monitor_spectating.svg", 1920, 1080, spect, mon::Corner::bottom_left);
    // the compact layout (Small, 720p), and NO SIGNAL
    monitor_shot(dir / "monitor_small.svg", 1920, 1080, view_at(171, 0.3f, 1.3f), mon::Corner::bottom_left, 0.85f);
    monitor_shot(dir / "monitor_720p_normal.svg", 1280, 720, view_at(184, 0.6f, 2.2f), mon::Corner::bottom_left);
    View nosig = view_at(0, 0, 1.7f);
    nosig.signal = false;
    monitor_shot(dir / "monitor_no_signal.svg", 1920, 1080, nosig, mon::Corner::bottom_left);
    monitor_shot(dir / "monitor_no_signal_720p.svg", 1280, 720, nosig, mon::Corner::bottom_left);

    Card fight;
    fight.show = true;
    fight.age = 3;
    fight.title = "UNDER PRESSURE";
    fight.mode = "DEFENDING";
    fight.you = side("YOU", 4, 183, 0.1f);
    fight.rival = side("FERRARI", 5, 171, 0.55f);
    monitor_shot(dir / "card_defending.svg", 1920, 1080, view_at(183, 0.35f, 2.2f), mon::Corner::bottom_left, 1.0f, &fight);
    Card attack = fight;
    attack.title = "WHEEL TO WHEEL";
    attack.mode = "ATTACKING";
    attack.you = side("YOU", 7, 174, 0.3f);
    attack.rival = side("MCLAREN", 6, 166, 0.8f);
    monitor_shot(dir / "card_attacking.svg", 1920, 1080, view_at(174, 0.2f, 0.9f), mon::Corner::bottom_left, 1.0f, &attack);
    Card contact = fight;
    contact.title = "CONTACT";
    contact.mode = "";
    contact.you = side("YOU", 9, 194, 0.05f);
    contact.rival = side("P10", 10, 181, 0.4f);
    monitor_shot(dir / "card_contact.svg", 1920, 1080, view_at(194, 0.2f, 1.3f), mon::Corner::bottom_left, 1.0f, &contact);
    // the card with a long name, at 720p, 1440p and ultrawide
    Card longname = fight;
    longname.rival = side("ALEXANDER MAXIMILIAN SCHWARZENBERGER", 5, 171, 0.55f);
    monitor_shot(dir / "card_long_name.svg", 1920, 1080, view_at(183, 0.35f, 2.2f), mon::Corner::bottom_left, 1.0f, &longname);
    monitor_shot(dir / "card_720p.svg", 1280, 720, view_at(183, 0.35f, 2.2f), mon::Corner::bottom_left, 1.0f, &longname);
    monitor_shot(dir / "card_1440p.svg", 2560, 1440, view_at(183, 0.35f, 2.2f), mon::Corner::bottom_left, 1.0f, &fight);
    monitor_shot(dir / "card_ultrawide.svg", 3440, 1440, view_at(183, 0.35f, 2.2f), mon::Corner::bottom_left, 1.0f, &fight);
    // the four corners over a mock-up of the game's HUD, with the card
    monitor_shot(dir / "hud_bottom_left.svg", 1920, 1080, view_at(176, 0.3f, 1.2f), mon::Corner::bottom_left, 1.0f, &fight, true);
    monitor_shot(dir / "hud_top_left.svg", 1920, 1080, view_at(176, 0.3f, 1.2f), mon::Corner::top_left, 1.0f, &fight, true);
    monitor_shot(dir / "hud_top_right.svg", 1920, 1080, view_at(176, 0.3f, 1.2f), mon::Corner::top_right, 1.0f, &fight, true);
    monitor_shot(dir / "hud_bottom_right.svg", 1920, 1080, view_at(176, 0.3f, 1.2f), mon::Corner::bottom_right, 1.0f, &fight, true);

    Report race;
    race.show = true;
    race.age = 4;
    race.kind = Kind::race;
    race.score = 81;
    race.word = composure_word(81);
    race.grade = composure_grade(81);
    race.peak = 192;
    race.average = 167;
    race.peak_cause = "LIGHTS OUT";
    race.peak_where = "LAP 1 \xC2\xB7 TURN 1";
    race.zone_share[0] = 0.04f;
    race.zone_share[1] = 0.21f;
    race.zone_share[2] = 0.63f;
    race.zone_share[3] = 0.12f;
    race.seconds = 47 * 60 + 5;
    race.stamina = 0.58f;
    race.spikes = 9;
    report_shot(dir / "report_race.svg", race);
    Report rattled = race;
    rattled.score = 44;
    rattled.word = composure_word(44);
    rattled.grade = composure_grade(44);
    rattled.peak = 199;
    rattled.peak_cause = "CONTACT";
    rattled.peak_where = "LAP 12 \xC2\xB7 TURN 4";
    rattled.zone_share[0] = 0.02f;
    rattled.zone_share[1] = 0.12f;
    rattled.zone_share[2] = 0.38f;
    rattled.zone_share[3] = 0.48f;
    rattled.stamina = 0.22f;
    rattled.spikes = 17;
    report_shot(dir / "report_rattled.svg", rattled);
    Report quali = race;
    quali.kind = Kind::qualifying;
    quali.score = 92;
    quali.word = composure_word(92);
    quali.grade = composure_grade(92);
    quali.peak = 174;
    quali.average = 152;
    quali.peak_cause = "PRESSURE";
    quali.peak_where = "LAP 6 \xC2\xB7 TURN 13";
    quali.zone_share[0] = 0.18f;
    quali.zone_share[1] = 0.47f;
    quali.zone_share[2] = 0.35f;
    quali.zone_share[3] = 0;
    quali.seconds = 18 * 60 + 42;
    quali.stamina = 0.93f;
    quali.spikes = 2;
    report_shot(dir / "report_qualifying.svg", quali);
    Report brief = quali;
    brief.kind = Kind::practice;
    brief.seconds = 42;
    report_shot(dir / "report_short_pad.svg", brief, true);
    // the visual pass (15 Nov): 720p with long names, the red zone, the report in each place over the mock HUD
    View red720 = view_at(191, 0.55f, 2.0f);
    monitor_shot(dir / "monitor_720p_red.svg", 1280, 720, red720, mon::Corner::bottom_left);
    monitor_shot(dir / "monitor_red_1080p.svg", 1920, 1080, view_at(191, 0.55f, 2.0f), mon::Corner::bottom_left, 1.0f,
                 nullptr, true);
    Card longer = fight;
    longer.you = side("YOU", 12, 188, 0.1f);
    longer.rival = side("ALEXANDER MAXIMILIAN SCHWARZENBERGER", 11, 179, 0.55f);
    monitor_shot(dir / "card_720p_long_names.svg", 1280, 720, red720, mon::Corner::bottom_left, 1.0f, &longer, true);
    monitor_shot(dir / "card_1080p_hud.svg", 1920, 1080, view_at(176, 0.3f, 1.2f), mon::Corner::bottom_left, 1.0f, &longer,
                 true);
    report_shot(dir / "report_hud_left.svg", rattled, false, 1920, 1080, mon::ReportAt::left, true);
    report_shot(dir / "report_hud_right.svg", rattled, false, 1920, 1080, mon::ReportAt::right, true);
    report_shot(dir / "report_hud_centre.svg", rattled, false, 1920, 1080, mon::ReportAt::centre, true);
    report_shot(dir / "report_720p_hud.svg", race, false, 1280, 720, mon::ReportAt::left, true);
    report_shot(dir / "report_ultrawide_hud.svg", race, false, 3440, 1440, mon::ReportAt::left, true);
    return 0;
}
