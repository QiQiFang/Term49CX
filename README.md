# Term49C

**Term49C** is a terminal emulator for BlackBerry 10. The **C** stands for
**Classic**: this fork is tuned for the BlackBerry Classic (Q20) hardware —
physical keyboard, trackpad, and belt keys — while remaining usable on other
BB10 devices.

It continues the [Term49](https://github.com/BerryFarm/Term49) /
[Term48](https://github.com/mordak/Term48) line by
[mordak](https://github.com/mordak) and BerryFarm.

It implements (relevant parts of) the [ECMA-48 standard][ecma], plus extra
control sequences for the `xterm-256color` terminfo. It is a work in progress,
but good enough for daily use. Pull requests, feature requests, and bug reports
are welcome.

Requires OS version >= 10.3.

**Install note:** official BlackBerry signing/distribution is no longer
practical. Today Term49C can only be **sideloaded on a rooted BlackBerry 10
OS**. Unsigned bars will not install on a stock, non-rooted device.

## Trackpad support (BlackBerry Classic / Q20)

On devices with a trackpad, swiping the trackpad sends arrow keys (useful
for shell history and cursor movement, since the Classic has no arrow
keys), and clicking the trackpad sends Enter. Application cursor key mode
(DECCKM) is respected, so arrows also work in `vi`, `less`, etc.

Three settings in `~/.term49rc` control this behaviour:

* `trackpad_enabled` (default `true`): set to `false` to ignore the trackpad.
* `trackpad_sensitivity` (default `60`): trackpad displacement counts per
  arrow keystroke. Lower values mean more keystrokes per swipe.
* `trackpad_click_keys` (default `"kent"`, i.e. Enter): keystrokes sent on
  trackpad click. Accepts the same values as other keymaps, including
  terminfo names such as `"kcuu1"`; set to `""` to disable the click.

The trackpad arrives at the app as raw `SCREEN_EVENT_JOYSTICK` events via a
patched libSDL (see `patches/sdl-term48-trackpad.patch`, already applied to
the prebuilt `external/lib/libSDL12.so`).

## Belt keys (BlackBerry Classic / Q20)

* **Back** sends Esc. Configurable via `back_button_keys` in `~/.term49rc`
  (default `"\x1b"`; accepts the same values as other keymaps, e.g.
  terminfo names).
* **Menu (BlackBerry key)** toggles a persistent Alt mode: every key maps
  through the alt table (and Alt+trackpad scrolling is armed) until you
  press Menu again. The `a` indicator shows while it is active. This is
  different from the hardware Alt key, which stays one-shot. The
  swipe-down-from-bezel gesture does the same.
* Send and End keep their system behaviour.

## Shift+Tab (back-tab)

Key tables are shift-aware. With Shift active — held down, or armed via
the sticky shift key (`↑` indicator) — any metamode, sym-menu, or alt
key bound to Tab sends back-tab (`ESC [ Z`) instead, i.e. Shift+Tab.
So: arm Shift, then arm metamode (hold Space, or tap the top-left
corner), then `t` (the default Tab binding) — or arm Shift and pick
Tab from the sym menu.

**Note:** the optional double-tap right-Shift metamode toggle does
**not** work on BlackBerry Classic (Q20); the keyboard does not
report a distinct right-Shift event. Use hold-Space or the corner
hitbox on Classic.

For custom shifted bindings, an uppercase entry in `metamode_keys` wins
when Shift is active, e.g. `("T", "...")` alongside `("t", "\x09")`.
Shift+Tab from the virtual keyboard also sends back-tab.

## Mouse support

Term49C implements xterm mouse reporting (DECSET 9/1000/1002/1003, plus
SGR 1006 coordinates). When an application enables mouse tracking (tmux
with `set -g mouse on`, vim with `set mouse=a`, htop, mc, ...):

* the **trackpad** automatically switches from arrow keys to driving a
  mouse pointer (shown as an inverse `+`); trackpad click = left click.
  When the application turns mouse tracking off, the trackpad reverts to
  arrow keys — there is nothing to configure.
* **touching the screen** sends a click at the touched cell, and dragging
  reports mouse motion (e.g. selecting text in vim).
* **Alt + trackpad** swipes send wheel events (apps treat these as
  scrolling) instead of using the local scrollback.

Line drawing also works: SI/SO and the SCS charset designations
(`ESC ( 0` / `ESC ( B`) are honoured and DEC Special Graphics characters
are mapped to Unicode box-drawing glyphs, so ncurses borders and
separators render as real lines instead of letters.

## Fallback font

The default terminal font may lack glyphs modern TUI programs draw with:
braille patterns, rounded box corners, block elements, powerline symbols.
Characters the main font cannot draw are rendered with a bundled fallback
font ([Cascadia Mono](https://github.com/microsoft/cascadia-code), OFL
licensed), instead of showing empty squares. Configure with
`fallback_font_path` in `~/.term49rc` (set to `""` to disable).

## Copy and paste

* **Copy**: hold Alt (or turn on Menu's persistent Alt mode), then drag a
  finger across the screen to select text. Selected cells are highlighted;
  lifting your finger copies the text to the system clipboard (trailing
  spaces trimmed, rows joined with newlines). A plain tap clears the
  selection.
* **Paste**: the existing paste action (metamode `v`) writes the clipboard
  to the terminal. When an application enables **bracketed paste**
  (DECSET 2004, e.g. vim, zsh, bash), the paste is wrapped so the
  application treats it as literal text rather than typed commands.
* **System IME input (Q20)**: enter metamode (hold Space or tap the
  top-left corner), then press `i`. A native BB10 text field opens; compose
  Chinese or other IME text and press the physical Enter key (or tap Send)
  to write committed UTF-8 text to the terminal. While the prompt is open,
  hardware keystrokes are isolated from the terminal so composition keys do
  not leak into shells or full-screen TUI applications. Dialog responses are
  consumed synchronously in the SDL/BPS event pump, before their BPS payload
  becomes invalid.
* **Remote clipboard (OSC 52)**: programs can set the phone's clipboard
  with an `OSC 52` escape — so `tmux` (`set -s set-clipboard on`), vim
  (`set clipboard=unnamed` with an OSC 52 plugin), and similar tools can
  copy to the BlackBerry clipboard even over SSH. Clipboard *reads* via
  OSC 52 are ignored for privacy.

## Cursor styles

Term49C honours `DECSCUSR` (`CSI Ps SP q`): applications can request a
block, underline, or bar cursor (vim, for example, uses this to show
insert vs normal mode). Blinking variants render steady.

## Scrollback

Term49C keeps scrollback history (`scrollback_lines` in `~/.term49rc`,
default `500` extra lines; `0` disables). To scroll:

* **Touch**: drag a finger up/down on the screen — the content follows
  your finger.
* **Alt + trackpad**: press Alt (sticky), then swipe the trackpad up/down.
  Press Alt again to leave scroll mode.

While scrolled back, an inverse `▲` shows in the top-right corner, and the
view stays anchored even as new output arrives. Typing (or sending arrows
with the trackpad) snaps back to the live screen. Full-screen applications
(`less`, `vim`, ...) use the alternate screen and are unaffected.

## Development

To compile Term49C you will need:

* [libSDL][libsdl]
* [Touch Control Overlay][tco]
* [libconfig][libconfig]

Prebuilt shared libraries are available in `external/lib` (see Makefile). To
build them from source, check out the submodules (`git clone --recursive`) and
build with the Momentics IDE. When compiling SDL, define
`-D__PLAYBOOK__ -DRAW_KEYBOARD_EVENTS`.

**Deployment:** there is no supported signing path for BlackBerry World or
stock devices anymore. Build an unsigned bar and **sideload it onto a rooted
BB10 OS** (for example with community tools such as
[bb10d](https://github.com/jxw1102) / device-side package installers). Stock,
non-rooted devices will reject the package.

### Building with Docker

If you don't have (or can't install) the BlackBerry NDK locally, you can
build with Docker using a community BB10 NDK 10.3.1 image:

* `./scripts/docker-build.sh` — builds `Device-Debug/Term49` and packages an
  unsigned `Term49C.bar`
* `./scripts/docker-build.sh sdl` — additionally rebuilds the patched
  `external/lib/libSDL12.so` from the `SDL` tree (`term48` branch plus
  `patches/sdl-term48-trackpad.patch`)

The image (`delaya73/bbndk` by default, override with `BBNDK_IMAGE`) runs
under x86 emulation on arm64 hosts, which is slow but works. By using the
NDK you accept the BlackBerry SDK license. Sideload the resulting
`Term49C.bar` on a rooted device — do not expect stock install or store
signing to work.

### Building locally

You can build Term49C without Momentics:

* Load the proper `bbndk-env` file
* `make` — produces the binary under `Device-Debug/`
* Package with the Docker/container scripts, or your own `blackberry-nativepackager` invocation, to get `Term49C.bar`
* Sideload the bar on a **rooted** BB10 device

### Debugging with GDB

On a rooted device with a working SSH/debug channel:

* Load `bbndk-env` on the host
* Deploy and launch the app stopped if your tooling supports it
* Attach `ntoarm-gdb` to the process on the device

Exact steps depend on your root/debug setup; the historical Momentics
debug-token + `blackberry-connect` signing flow is no longer documented here.

## See also

* [BerryFarm Term49](https://github.com/BerryFarm/Term49) (upstream)
* [Term48](https://github.com/mordak/Term48) (original)
* [Term48 on BlackBerry AppWorld](http://appworld.blackberry.com/webstore/content/26272878/) (historical)

[ecma]: http://www.ecma-international.org/publications/standards/Ecma-048.htm
[libsdl]: https://github.com/mordak/SDL/tree/term48
[tco]: https://github.com/blackberry/TouchControlOverlay
[libconfig]: http://www.hyperrealm.com/libconfig/
