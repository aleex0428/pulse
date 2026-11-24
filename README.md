# PULSE

`alexs.pulse` 0.1.1, by Alex Sánchez. MIT licence.

Feel the driver's heart. PULSE gives every driver a heart rate that lives with the race: it climbs on the grid,
surges when the lights go out, rises in a fight and jumps after contact, and comes down behind the safety car. You
**feel** yours on screen and **see** it on a bedside monitor. In a race, a TV-style card compares your heart with your
rival's, and a report sums your session up when you take the flag.

## What it does

### The heart
Every car on the grid gets a heart, worked out many times a second from what the car does, read from the game's
memory (no UDP needed):
- **The session:** practice is calmer than qualifying, and qualifying is calmer than a race. On the grid, the nerves
  come before the lights.
- **The start:** a surge at lights out that settles over the first minute.
- **The work:** pace and sustained G in the corners.
- **A fight:** the nearest car on the road ahead or behind, within a second. Defending counts more than attacking.
  Standing on the grid is no fight, and neither is a lapped car.
- **What is at stake:** leading or running on the podium in a race, a lap that counts in qualifying.
- **Spikes of adrenaline:**
  - contact with another car;
  - a moment (a spin, a trip off the road, a hit);
  - losing a place, and gaining one (a smaller spike);
  - a track limits warning;
  - a penalty.
- **What calms it:** the safety car (less behind the virtual one), a red flag, the pit lane and the chequered flag.
- **Fatigue:** it builds over a long session (about half way at the end of 90 minutes in clean air, more with
  fights), raises the heart for the same work and slows its recovery.

The heart rises in about two seconds and settles in about eight, slower when tired. Its zones are **CALM** (under
140), **FOCUSED**, **IN THE ZONE** (160 to 178, where a racing driver lives) and the **RED ZONE**.

### On screen: the effect
A post-process pass (`fx/pulse.hlsl`) works on the 3D picture, under the HUD:
- **Tunnel vision:** the edges darken and lose colour in the red zone.
- **Heartbeat:** a soft throb at the edges in time with the heart, when it beats fast.
- **G greyout:** the outside edge in fast corners, and the top edge under hard braking.
- **Impacts:** after contact, a white flash and a radial blur that clears in about a second. A moment gets a
  shorter blur.
- **Fatigue:** late in a long race the picture turns a little softer, grainier and dimmer.

The centre of the picture, where you look, always stays clear. Nothing shows while paused, in replays, photo mode,
menus or cutscenes, or while you watch another car. Every effect is off at exactly zero: with the strength at 0, or
every effect switched off, the picture is untouched.

### The monitor
A bedside monitor, bottom left by default, with:
- the heart rate, with a heart that beats with it;
- the zone;
- an ECG trace sweeping across a phosphor grid;
- the stamina left.

Each zone has its own mark as well as its colour (a circle, a square, a diamond, a warning triangle). It blinks red
in the red zone. While you watch another car, it follows that car, with its name or team. While a session is on but
the car can't be read, it says **NO SIGNAL**. Made small, or on a 720p screen, it takes a compact layout:
the number and the zone big, the trace under them. **F9** (or the **Monitor key** setting) shows and hides it.

The monitor and the DRIVER BIOMETRICS card are entries of the game's own **HUD editor** (Settings > HUD, from launcher 1.10.21): move them, resize them, change their opacity or switch them off there, and the game keeps where you put them. They only show while the game shows its race HUD.

### The DRIVER BIOMETRICS card
In a race, when you fight with a car for a few seconds, or touch one, a TV-style card compares your heart with your
rival's: positions, live ECG traces, BPM and zones. A fight is WHEEL TO WHEEL when you attack and UNDER PRESSURE when
you defend. It counts one rival at a time, and never shows for a lapped car, in or just out of the pit, or behind
the safety car. It sits at the top of the screen, in the middle. Your rival is named from the game's telemetry when
its UDP output is on and its entry matches the car, and by their team otherwise.

### The PULSE REPORT
It comes up when you take the flag or your race ends, or at the end of a qualifying or practice session (not after
a restart). It shows:
- a **zone control** score out of 100, with a word (ICE COLD, COMPOSED, STEADY, ON EDGE, RATTLED) and a grade: the
  time at 160-178 BPM counts most;
- your peak heart rate, with what caused it and where ("LIGHTS OUT · LAP 1 · TURN 1");
- your average heart rate;
- the time in each zone;
- the stamina left;
- the number of spikes;
- the time counted (minutes and seconds).

It sits on the left of the screen by default, so the game's own end of race (driver of the day, the podium) stays
clear; **Report position** puts it on the right or in the middle. The monitor key or Esc closes it; otherwise it goes
after 16 seconds (the pause does not count), and never carries into the next session.

### Restarts and flashbacks
A restart is a new attempt: the counts start again and the heart keeps beating where it was. A flashback takes back
every spike that came after the point it goes back to, and the places it shuffles are not overtakes. A place given
back within 30 seconds takes both spikes back.

## Settings (ADDON SETTINGS)
- **Screen effects**, **Effects strength** (0 to 100, changes straight away), **Effects style** (Full or Comfort:
  no blur, flash, throb or grain, a softer tunnel, and fatigue only dims a little), **Screen motion** (Full; Reduced:
  half the throb and blur, the tunnel eases in and out; Static: nothing throbs, flashes, blurs or crawls) and each
  effect on its own: **Tunnel vision**, **Heartbeat**, **G greyout**, **Impacts**, **Fatigue**.
- **Effects key:** a key that switches the screen effects off and on while you drive (None by default).
- **Monitor key** (where the monitor and the card sit is the game's HUD editor's).
- **Biometrics card**, **Pulse report** and **Report position** (left, right or centre).
- **Diagnostics in the log:** what PULSE reads from the game, in `loader.log`.

## Needs
- Swofty Launcher 1.10.21 or later (the live state, the screen effect's data from code and the HUD editor's slots).
  Steam. Checked on 24 Nov with Swofty Launcher 1.10.35.
- The experimental features `overlay`, `telemetry` and `postfx`.

## Build
```
native\build.bat test             :: the DLL, model_test, plugin_test and fx_test (the shader's formula as C++)
native\build.bat shader           :: compiles fx\pulse.hlsl the way the launcher does
native\build.bat preview <dir>    :: the monitor, the card and the report as SVG files
```
