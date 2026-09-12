# Term49CX

Term49CX is an enhanced fork of
[jxw1102/Term49C](https://github.com/jxw1102/Term49C) for BlackBerry 10,
developed and tested on the BlackBerry Classic Q20.

The project lineage is:

> [Term48](https://github.com/mordak/Term48) →
> [Term49](https://github.com/BerryFarm/Term49) →
> [Term49C](https://github.com/jxw1102/Term49C) → Term49CX

The **C** retains the BlackBerry **Classic** focus of Term49C. The **X**
represents the extended input, Unicode, rendering, and modern TUI compatibility
implemented by this fork.

For the original terminal features, keyboard controls, trackpad behavior,
configuration, and build background, see the
[Term49C README](https://github.com/jxw1102/Term49C#readme). This document only
describes the changes made by Term49CX.

## Term49CX changes

### Native BB10 IME input

Press **Meta+i** (hold Space to enter Meta mode, then press `i`) to open a
native BB10 text field. Chinese and other IME text can be composed with the
system input method and submitted with physical Enter or the Send button.

Term49CX reuses the native dialog reliably, routes its response through the
SDL/BPS event pump, and prevents composition keystrokes from leaking into the
shell or a full-screen TUI. Virtual-keyboard resize events and trailing input
after dismissal are also isolated.

### Long text submission

Term49C's small key-event buffer truncated UTF-8 IME submissions after roughly
13 Chinese characters. Term49CX dynamically sizes long PTY writes, allowing the
native input dialog to submit substantially longer messages without truncation.

### CJK and wide-character rendering

The terminal buffer tracks East Asian wide and full-width characters as two
cells so Chinese text and following terminal columns remain aligned.

### Monochrome emoji on the Q20

Term49CX adds supplementary-plane Unicode handling and a bundled monochrome
Noto Emoji fallback font.

The Q20's legacy FreeType/SDL_ttf stack does not reliably render cmap format 12.
To remain compatible, supported glyphs in U+1F000–U+1FAFF are mirrored into the
BMP Private Use Area and rendered through the device-tested UTF-16 path. The
compatibility font can be regenerated with `scripts/Build-Q20EmojiFont.py` from
the parent Q20 workspace.

### Modern TUI redraw compatibility

DEC synchronized-output mode 2026 is supported. Applications can bracket a
frame with `CSI ? 2026 h` and `CSI ? 2026 l`; Term49CX defers intermediate
paints until the frame completes, reducing visible scrolling and partial redraws
in modern full-screen terminal interfaces. A safety timeout prevents a malformed
or interrupted frame from freezing the display.

### Additional terminal fixes

Term49CX also includes the related compatibility work developed alongside these
features:

* reusable system IME invocation;
* physical Enter submission from the native input dialog;
* correct UTF-8 delivery to the PTY;
* CJK font fallback and double-width cell alignment;
* supplementary-plane-safe copy handling;
* fallback glyph rendering for modern terminal symbols;
* OSC 52 clipboard support;
* bracketed paste support;
* touch selection and scrollback improvements;
* xterm mouse reporting and cursor-style handling;
* DEC Special Graphics line drawing and Shift+Tab behavior.

## Compatibility and installation

Term49CX requires BlackBerry OS 10.3 or later. The current device-tested feature
baseline is version **1.0.12**; version **1.0.13** changes the displayed
application name to Term49CX while retaining Term49C's package ID so an existing
installation is upgraded in place and keeps its configuration. Version
**1.0.21** fixes native IME Send/Cancel dismissal hangs on the BlackBerry
Classic. The prompt continues to use its touchscreen Cancel button; unsupported
hardware Back-key cancellation experiments are not included.

Official BlackBerry signing and distribution are no longer practical. Current
builds are unsigned BAR packages and require a BB10 system configured to accept
unsigned applications. A stock, non-rooted device will reject the package.

## Upstream and licenses

Term49CX is based on [jxw1102/Term49C](https://github.com/jxw1102/Term49C) and
retains its original licensing terms and upstream history. Bundled fallback
fonts retain their respective license files in `external/fonts`.

Please report Term49CX-specific issues in this repository. For behavior inherited
unchanged from Term49C, consult the upstream project first.
