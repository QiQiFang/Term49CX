# TouchProbe

Probes for Passport capacitive keyboard soft-touch (CKB).

## Bars in `~/Public`

| Bar | What |
|-----|------|
| `TouchProbe.bar` | **v5 native** — `screen_get_event` primary (Cascades-style), CKB session, device bind |
| `TouchProbeCascades.bar` | **Official Cascades** `TouchKeyboardHandler` — if this works, soft-touch is app-visible via Cascades only |

Logs:
- Native → `Documents/TouchProbe.log`
- Cascades → `Documents/TouchProbeCascades.log` (screen goes **magenta** on keyboard soft-touch Down)

### Why v1–v4 failed (likely)

Cascades `ScreenEventThread` uses **`screen_get_event()`**, not BPS `screen_request_events`. v5 switches to that path. Soft-touch may still only be fully wired inside Cascades; the Cascades bar is the control experiment.

## On-screen HUD (v4)

| Colour | Meaning |
|--------|---------|
| **Green** | LCD — Synaptics `touch_display` |
| **Magenta** | CKB — Synaptics `touch_keypad` (soft-touch on keys) |
| **Blue** | Physical keys — `BlackBerry touch keypad` / qwerty |
| **Orange** | Other |
| **Dark** | Idle |

v4 classifies by **device product name**, not “device ≠ 0”.  
It also tries to bind the `touch_keypad` device to our CKB session (`WINDOW` / `SESSION` / `DEVICES`) and logs ok/FAIL for each.

**Success for soft-touch:** magenta screen + log lines with `role=CKB` and `product="touch_keypad"`.

## What it logs

Startup: all input devices with `role=LCD|CKB|PHY_KEY|…` and bind results.  
MTOUCH: `dev`, `product`, `role`, `session`, pos/src.

## Build

From this directory (Docker + BBNDK image — same as Term49):

```bash
cd probe
make docker
```

Produces:

- `Device-Debug/TouchProbe`
- `TouchProbe.bar`

Uses `../signing/debugtoken.bar` when present (same token as Term49).

### Deploy

With NDK tools on PATH and device in dev mode:

```bash
# edit BBIP / BBPASS in ../signing/bbpass if needed
make deploy
```

Or manually:

```bash
source ../signing/bbpass   # or export BBIP / BBPASS
blackberry-deploy -installApp $BBIP -password $BBPASS TouchProbe.bar
```

## Capture procedure on Passport

1. Launch **TouchProbe** (dark screen, cyan top bar).
2. Do the following **in order**, with a short pause between groups.
   Swipe down from the top bezel once between groups — that writes a `MARK` separator in the log.

| # | Action |
|---|--------|
| A | Tap several places on the **LCD** |
| B | Swipe vertically on the **LCD** |
| C | Swipe horizontally on the **LCD** |
| D | Swipe-down bezel → **MARK** |
| E | Lightly swipe **vertically across the physical keys** (do not press hard enough to type if you can help it) |
| F | Swipe **horizontally across the physical keys** |
| G | Two-finger swipe on the keyboard |
| H | Double-tap the keyboard |
| I | Swipe-down bezel → **MARK** |
| J | Type a few physical keys (`asdf`) |
| K | Rotate to landscape, swipe keyboard again, rotate back |

3. Exit the app (swipe up / close) so the log is flushed.

## Get the log off the device

Log is appended to (first writable path wins):

1. `/accounts/1000/shared/documents/TouchProbe.log`
2. `/accounts/1000/shared/misc/TouchProbe.log`
3. `$HOME/data/TouchProbe.log`

### Option A — File Manager on device

**File Manager → Device → Documents → `TouchProbe.log`**  
Share / copy to PC.

### Option B — scp (blackberry-connect running)

```bash
cd probe
# in another terminal: cd ../signing && make connect
make pull-log
```

Saves `./TouchProbe.log` next to this README.

## Send for analysis

Paste or attach `TouchProbe.log` (the whole session is fine). We only need the header + MTOUCH lines for keyboard vs LCD comparison.
