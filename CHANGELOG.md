# Changelog

## 0.1.1
- **The monitor and the card are in the game's HUD editor** (the launcher's HUD slots, 1.10.21): move them, resize them, change their opacity or switch them off there like any other part of the HUD, and the game keeps where you put them. While the editor is open both show a preview. The Monitor and Monitor size settings are gone; the monitor key still shows and hides it.
- The monitor and the card only show while the game shows its race HUD: not in replays, cutscenes, photo mode or menus.
- Needs Swofty Launcher 1.10.21 or later. SDK headers updated to the launcher 1.10.35.
- From players' reports: the biometrics card names a rival by the launcher's own driver name first, and a team by its current name (a Sauber rival read "ALFA ROMEO", the game's old internal name).

## 0.1.0
First version.

- **A heart for every driver**, from what their car does: the session, the start, pace and G, the nearest car
  (defending counts more than attacking; never a lapped car), what is at stake, and spikes of adrenaline from contact,
  moments, places lost and gained, track limits and penalties. The safety car, a red flag, the pit lane and the flag
  calm it, and fatigue builds over a long session (about 0.5 after 90 minutes in clean air, about 0.7 with fights).
  Zones: CALM, FOCUSED, IN THE ZONE and RED ZONE.
- **The session's life:** everything stops at the session's end and stays off until the next session (a restart is a
  new attempt: the counts start again, the heart keeps beating). Nothing counts while paused. A flashback takes back
  the spikes after its point; a place given back within 30 s takes both spikes back; overtakes alone never push the
  adrenaline past half. A moment needs the pedals read.
- **Screen effect** (`fx/pulse.hlsl`, its formula in `fx/pulse_core.hlsli`): tunnel vision, a heartbeat throb, G
  greyout, the flash and blur of an impact, and fatigue. Every effect is off at exactly zero. Strength slider, Comfort
  style, Screen motion (Full, Reduced, Static), each effect on its own, and an optional key. The G is smoothed by time
  (the same at any frame rate); braking G needs the brake pedal.
- **Monitor:** heart rate, zone (a shape and a colour), a sweeping ECG trace and stamina, in a corner of your choice;
  a compact layout for Small and 720p; NO SIGNAL when the car can't be read. F9 shows and hides it.
- **DRIVER BIOMETRICS card** at the top of the screen: your heart against your rival's when you fight one car for a few
  seconds or touch, in a race. Not in or just out of the pit, behind the safety car, or while you watch another car.
- **PULSE REPORT** at the flag, at the end of your race or of a session (not after a restart): zone control score,
  peak and its cause, average, time in each zone, stamina, spikes and the time counted. F9 or Esc closes it; it goes
  after 16 s of time not paused.
- **Tuned on three real races** (Montreal, 13 Nov, replayed in the tests): a fight counts within 1 s and weighs less,
  standing on the grid is no fight, and the G greyout starts at the G the game really gives (1.4 G lateral, 2.2 G
  braking). The report sits on the left by default (**Report position**: left, right, centre), clear of the game's
  driver of the day; the monitor sits higher in the bottom corners, clear of the track.
- **Diagnostics in the log** (on by default): what PULSE reads from the game, to check it on every setup, including
  the live state's state, the session's phases and the ids the first collision and overtake come with.
