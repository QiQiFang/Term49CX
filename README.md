# Term49

Term49 is a terminal emulator for BlackBerry 10. It is a continuation of the amazing [Term48](https://github.com/mordak/Term48) project by [mordak](https://github.com/mordak).

It implements (relevant parts of) the [ECMA-48 standard][ecma], but also includes some other control sequences to make it compliant with the `xterm-256color` terminfo specification. It is a work in progress, but is good enough for daily use. Pull requests, feature requests and bug reports are welcome.

The [current release](https://github.com/BerryFarm/Term49/releases) requires OS version >= 10.3.

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
So: tap Shift, arm metamode (double-tap right Shift, hold Space, or tap
the top-left corner), then `t` (the default Tab binding) — or tap
Shift, then pick Tab from your sym menu.

For custom shifted bindings, an uppercase entry in `metamode_keys` wins
when Shift is active, e.g. `("T", "...")` alongside `("t", "\x09")`.
Shift+Tab from the virtual keyboard also sends back-tab.

## Mouse support

Term49 implements xterm mouse reporting (DECSET 9/1000/1002/1003, plus
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

The default terminal font (Andale Mono) lacks many glyphs modern TUI
programs draw with: braille patterns (dot-matrix logos and spinners),
rounded box corners, block elements, powerline symbols. Characters the
main font cannot draw are rendered with a bundled fallback font
([Cascadia Mono](https://github.com/microsoft/cascadia-code), OFL
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
* **Remote clipboard (OSC 52)**: programs can set the phone's clipboard
  with an `OSC 52` escape — so `tmux` (`set -s set-clipboard on`), vim
  (`set clipboard=unnamed` with an OSC 52 plugin), and similar tools can
  copy to the BlackBerry clipboard even over SSH. Clipboard *reads* via
  OSC 52 are ignored for privacy.

## Cursor styles

Term49 honours `DECSCUSR` (`CSI Ps SP q`): applications can request a
block, underline, or bar cursor (vim, for example, uses this to show
insert vs normal mode). Blinking variants render steady.

## Scrollback

Term49 keeps scrollback history (`scrollback_lines` in `~/.term49rc`,
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

To compile Term49, you will need some additional libraries:

* [libSDL][libsdl]
* [Touch Control Overlay][tco]
* [libconfig][libconfig]

Prebuilt versions of these shared libraries are available in `external/lib` (see Makefile); to build from source you will need to check out the submodules (call `git clone` with the `--recursive` option) and build them with the Momentics IDE. Note that when compiling SDL, you must define `-D__PLAYBOOK__ -DRAW_KEYBOARD_EVENTS`.

### Building with Docker

If you don't have (or can't install) the BlackBerry NDK locally, you can
build with Docker using a community BB10 NDK 10.3.1 image:

* `./scripts/docker-build.sh` — builds `Device-Debug/Term49` and packages an
  unsigned `Term49C.bar`
* `./scripts/docker-build.sh sdl` — additionally rebuilds the patched
  `external/lib/libSDL12.so` from the `SDL` submodule (`term48` branch plus
  `patches/sdl-term48-trackpad.patch`)

The image (`delaya73/bbndk` by default, override with `BBNDK_IMAGE`) runs
under x86 emulation on arm64 hosts, which is slow but works. By using the
NDK you accept the BlackBerry SDK license. Sign or deploy the resulting bar
with your own keys/debug token as described below.

### Building locally

You can build and deploy Term49 without using Momentics IDE:

* Load the proper `bbndk-env` file
* Copy your debug token to `signing/debugtoken.bar` (or see the section below on generating a debug token)
* Populate the `BBIP` and `BBPASS` fields in `signing/bbpass` with your device's dev-mode IP address and device password
* Update the `<author>` and `<authorId>` tags in `bar-descriptor.xml` to match the `Package-Author` and `Package-Author-Id` for your debug token: `unzip -p signing/debugtoken.bar META-INF/MANIFEST.MF | grep 'Package-Author:\|Package-Author-Id:'`
* `make`
* `make deploy`

## Generating a Debug Token

* Use this form to obtain your `bbidtoken.csk` file: https://developer.blackberry.com/codesigning/
* Copy `bbidtoken.csk` to `signing/bbidtoken.csk`
* In `signing/bbpass`, fill in:
  - `CNNAME`: the Common Name for your signing cert (usually your name)
  - `KEYSTOREPASS`: CSK password you entered in step 1 signup
  - `BBPIN`: target device's PIN
  - `BBPASS`: target device's password
* Run `make` in `signing/Makefile` to request and deploy the token to your device.

Important: any symbols need to be escaped according to bash / Makefile rules e.g. backslashes before symbols `\!` and double dollar signs `\$$`.

## Signing the release

To distribute Term49, you need to sign the application bar with BlackBerry. To do that, run `make sign`.

## Debugging with GDB

To connect to the target device and enable debug tools such as GDB, the `blackberry-connect` tool must be started with the right arguments. For this, two terminals must have the correct `bbndk-env` environment loaded (or run the `make connect` command in the background).

### Terminal 1: `blackberry-connect`
* Start in the Term49 root directory.
* `cd signing`
* If the SSH key hasn't been generated yet, run `make ssh-key`.
* `make connect`
* Leave terminal running until done debugging.

### Terminal 2: `gdb`
* Start in the Term49 root directory.
* `make launch-debug`
* The package will be built, deployed to target device, and launched stopped. On host, `ntoarm-gdb` will start, connect to target device, and attach to the application process. To continue execution, run the GDB command `continue`. Further information on GDB can be found online.

## See also

* [Term48 in BlackBerry AppWorld](http://appworld.blackberry.com/webstore/content/26272878/)

[ecma]: http://www.ecma-international.org/publications/standards/Ecma-048.htm
[libsdl]: https://github.com/mordak/SDL/tree/term48
[tco]: https://github.com/blackberry/TouchControlOverlay
[libconfig]: http://www.hyperrealm.com/libconfig/
