# FLTerm XR for DomeD

This fork ports the independent desktop/XR terminal model from
[Ghostty XR](https://github.com/jebot-git/ghostty-xr) onto
[FLTerm](https://github.com/yongchaofan/FLTerm)'s own FLTK terminal core.
The upstream source was fetched at `bd86c8e70cb08283efe6ee87feba72a7736ffc86`.
No libghostty-vt or Zig build is used.

## Build and run

Requires Linux, CMake 3.24+, a C++20 compiler, FLTK 1.4.5 with Wayland/OpenGL,
OpenXR, SDL2 (event translation only), EGL/OpenGL, FreeType, Fontconfig, zlib,
xkbcommon, GIO, Wayland client headers, wayland-scanner and nlohmann JSON.
DomeD's patched FLTK is selected automatically from `/opt/fltk-vr/toolkit`;
`FLTK_DIR` selects another compatible build. Raw BDF fonts are required.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local
cmake --build build -j4
ctest --test-dir build --output-on-failure
./build/flterm-xr --wayland              # Independent native FLTK Wayland window
./build/flterm-xr                       # Windowless native OpenXR overlay
./build/flterm-xr --probe               # Inspect the selected runtime
./build/flterm-xr --preview-only         # Diagnostic FLTK desktop window
./build/flterm-xr --headless --seconds 3 # PTY/control test, no XR or display
./build/flterm-xr --wayland -- ssh host # Any local command, including SSH
```

The overlay requires `XR_EXTX_overlay`, `XR_KHR_opengl_enable` and
`XR_MNDX_egl_enable`. It uses an EGL context without a desktop window and a
head-relative quad, at positive overlay placement 20. `--standalone` explicitly
creates a primary XR session. `--blend` selects a supported environment blend.
The transparent background uses premultiplied alpha; explicitly coloured ANSI
backgrounds remain opaque unless `--styled-background-opacity` changes them.
Hiding submits zero layers while PTYs and scrollback remain alive.
DomeD login prestarts the FLTerm overlay hidden, without grabbing keyboard or
pointer focus. Use the existing XR terminal shortcut to show it when needed.

## DomeD integration

DomeD's updated terminal service prefers FLTerm when it is installed and there
is no explicit terminal override. `DOME_TERMINAL_BACKEND=ghostty` retains Ghostty.
Set the session variables below to select this build explicitly:

```sh
export DOME_TERMINAL_BACKEND=flterm
export DOME_TERMINAL=/absolute/path/to/flterm-xr
# DOME_TERMINAL_MODE=wayland optionally uses compositor windows in both realms.
```

`DOME_TERMINAL` is an executable path, never a shell command. Its selected backend
also owns the control namespace; custom wrappers can use `DOME_TERMINAL_BACKEND`
to declare their protocol. The shipped desktop entries separately expose
**FLTerm (DomeD)** and **FLTerm XR**, including WiVRn's XR application category.

In the desktop input realm, **Super+T** and **Open Terminal** create independent
Wayland windows with their own shells, tabs and control sockets. They never
create an OpenXR session, grab input devices, or read/write XR layout settings.
In the XR realm, another launch creates/selects a tab in the existing overlay.
**Super+F12** shows that overlay without creating a tab, including when a desktop
window is focused. Bare **F12** hides the overlay while it owns XR input; bare
F12 remains an ordinary application key in desktop mode. Ghostty's live socket
is never contacted when FLTerm is selected.

The FLTK window exports its actual File/Edit/View commands through DBusMenu and
the Wayland appmenu protocol, so DomeD's universal menu and keyboard navigation
can activate them. Inside a DomeD session, the local menu bar is hidden and its
space is reclaimed by the tab selector and terminal. Outside DomeD, the local
menu bar remains available. Its OpenGL canvas uses nearest-neighbour bitmap pixels.
DomeD's saved font family, native strike, 1×/2× scale, light/dark palette and
accent update running desktop and XR terminals without replacing their PTYs.
CLI font and size overrides remain independent. Desktop layout and IME use the
Wayland seat. Native overlay input starts with DomeD's saved XKB layout/active
group; outside DomeD it retains Ghostty XR's environment/KDE configuration path.

## Bindings and control

- **Ctrl+Shift+T/W**: create/close a tab; **Ctrl+PageUp/PageDown**: switch tabs.
- **Ctrl+Shift+C/V/A**: copy, paste and select all. Ctrl+C remains shell input.
- Left-click positions the shell cursor on the current logical line (including
  soft wraps); drag selects text; middle-click pastes primary selection; wheel scrolls.
  Cursor positioning sends Left/Right editing keys and requires a shell line editor
  such as Bash/readline. Clicks past the text clamp to its end. Scrollback, other
  output lines, full-screen/application cursor modes and application mouse tracking
  requests do not trigger editing keys. Modified clicks and drags retain selection
  behavior. This applies to both the desktop window and the native XR overlay.
- XR **Ctrl+Alt+arrows** moves the quad; add Shift to resize width/height.
- XR **Ctrl+Alt+PageUp/PageDown** adjusts depth; **+/-** scales; **Home** resets.
- XR **Ctrl+Alt+B** toggles the canvas between transparent and 30% opaque.

The overlay's keyboard and relative pointer devices are acquired only while
shown and its XR session runs, released when hidden, and disabled entirely by
`--no-keyboard`. Access to the seat's input devices is required for native input.
The Wayland frontend never uses this capture path.

```sh
./xr/flterm-xrctl status
./xr/flterm-xrctl show
./xr/flterm-xrctl hide
./xr/flterm-xrctl tab-new
./xr/flterm-xrctl type $'printf "hello\\n"\n'
./xr/flterm-xrctl position 0.2 -0.1 -1.6
./xr/flterm-xrctl size 1.4 0.8
./xr/flterm-xrctl opacity 0.3
./xr/flterm-xrctl quit
```

The overlay owns `$XDG_RUNTIME_DIR/flterm-xr/control.sock`. Desktop instances print
`$XDG_RUNTIME_DIR/flterm-xr/wayland-PID.sock`; use `--socket` to address one.
`status` reports mode, shell/tab counts, geometry, Appearance, native frame counts
and desktop menu addresses. Direct launcher reactivation shows the existing
overlay. The XR position, physical width/height (including scaling), canvas
opacity and styled ANSI background opacity are saved automatically after a
250 ms pause in adjustments and flushed on exit, including while hidden.
They are restored from `$XDG_CONFIG_HOME/flterm-xr/layout.json` (or
`~/.config/flterm-xr/layout.json`) on the next overlay launch. Existing layout
files remain compatible. Keyboard adjustments and `flterm-xrctl` changes share
this persistence path. Explicit geometry or opacity launch options override
only their corresponding saved values and become the new saved settings.
`--layout-file FILE` uses a different settings file; `--no-save-layout` disables
both restoration and saving. Desktop instances do not touch this file.

## Validation and current limits

```sh
ctest --test-dir build --output-on-failure
uv run --no-project --with python-xlib --with pillow python xr/tests/integration.py
python3 xr/tests/xr-smoke.py
```

The integration test needs a current DomeD binary (`DOME_COMPOSITOR` overrides its
path), Xvfb, wl-copy, gdbus and system GTK resources for libdecor. It uses private
D-Bus/Wayland/X displays and never connects to the user's display or physical
input devices. Reports/screenshots are under `artifacts/integration/`.
The XR test copies Monado's service to a private runtime, starts its simulated
headset on Xvfb/Lavapipe, and exercises actual OpenXR frame submission, zero-layer
hidden frames, shell survival and a live BDF swapchain resize. An optional local
Khronos validation layer can be placed under `build/validation/layers`.
Its reports and terminal capture are under `artifacts/xr/`.

On 2026-10-22, all five CTest executables, 23 desktop/control integration checks
and 12 simulated OpenXR checks passed, including a loaded Khronos core validation
layer and a live font/swapchain resize. The terminal parser/PTY/raster test also
passed under AddressSanitizer and UndefinedBehaviorSanitizer (framework leak
reporting disabled). DomeD passed six terminal unit tests and both separate
backend/overlay integration tests.

Terminal emulation retains FLTerm's core, with indexed/RGB colors, UTF-8 cell
editing and cursor addressing, synchronized updates, cursor-position reports and
foreground/background color queries. Unicode redraws replace complete cells and
preserve adjacent rows, including in Codex's alternate screen.

The [Kitty keyboard protocol](https://sw.kovidgoyal.net/kitty/keyboard-protocol/)
supports all five progressive enhancements: disambiguated escape codes,
press/repeat/release events, alternate key identities, reporting all keys, and
associated Unicode text. Each tab maintains independent main/alternate-screen
mode stacks (bounded to 32 entries), with query, push/pop and replace/set/clear
commands. Native Wayland and XR input retain XKB layout identities, physical US
base keys, keypad keys and modifier/lock state. Wayland observes the compositor's
keymap while FLTK owns composition; XR uses its existing native input path.
Shell input retains legacy encodings until applications enable enhancements.
Terminal shortcuts listed above remain reserved by the frontend. The diagnostic
X11 preview uses the key information available from FLTK.

Kitty graphics uses per-tab, per-screen image storage and supports RGB, RGBA,
PNG, zlib, chunked direct transfers, regular/temporary files and POSIX shared
memory, queries and quiet/error replies, image IDs/numbers, crop/scale/offsets,
placements and deletion, z ordering, relative placement chains, Unicode
placeholders (including underline-color placement IDs), animation frames,
frame composition and timed playback. Tmux DCS passthrough is decoded. Both
frontends render the same premultiplied image canvas through OpenGL. Images
follow scrollback and page margins, and synchronized updates include images.
Pixel dimensions are available through CSI 14/16/18 t and the PTY window size.
Decoded images are limited to 128 MiB each, with a 320 MiB per-screen quota;
invalid commands return protocol errors rather than claiming success.
This implements application-facing keyboard/graphics interoperability; Kitty's
remote-control API, Python kittens and Kitty-specific window management are
not emulated. Full grapheme-cluster handling remains unsupported.
Desktop FLTK composition and clipboard use
the native seat; advanced IME conversion needs further interactive verification.
Native overlay IME and live keyboard-group changes during an input grab are not
yet synchronized. Core input/layout tests and a simulated XR runtime do not replace
physical-headset validation of appearance, passthrough and layer ordering.

The new frontend currently uses local PTYs and shell commands. FLTerm's original
network-connection, SFTP and scripting UI remains in `src/tiny2.cxx` with its
original Makefiles; it is not part of the new XR frontend. Both share the retained
FLTerm parser/widget sources.

## Sources and licensing

FLTerm is GPL-3.0; see the repository `LICENSE`. The new frontend uses
GPL-3.0-or-later code. The MIT-licensed graphics/OpenXR, layout, input, BDF and
clipboard helpers were ported from Ghostty XR; its notice is retained in
`xr/LICENSE.ghostty-xr`. DomeD's MIT-licensed Appearance/menu adapters retain their
SPDX headers. Exact source revisions are recorded in `xr/upstream.json`.
GNU Unifont's font licences and exception are included under `xr/fonts/`.

### Scrollback

Each tab retains up to 10,000 rows above the live screen. Mouse-wheel up shows
older output; wheel down moves toward live output (three rows per wheel step reported by the input backend), in
both desktop and XR modes. Shift+PageUp/PageDown moves by a page, Shift+Home
jumps to the oldest retained row, and Shift+End returns to live output. Typing
or pasting also returns to the live prompt. Ctrl+PageUp/PageDown still switches
tabs, and XR Ctrl+Alt+PageUp/PageDown still adjusts depth.

The history view stays anchored while new output arrives, until its rows are
evicted. Text selection works in history, retaining UTF-8 and color attributes.
Alternate-screen applications keep their own screen and leave normal-screen
history intact; the wheel does not expose history while they are active.
No scrollbar is drawn and no terminal columns are reserved for one.

The `scrollback` CTest covers bounds, output anchoring, selection, UTF-8/styles,
resize, alternate screens, scroll regions and scrollbar-free rendering. The
private DomeD integration test also checks actual wheel and scrollback shortcut
input; control-socket status includes `history_rows` and `scroll_offset`.

The `terminal-protocol` and `key-encoding` CTests cover fragmented Codex-style
Unicode redraws, erasing/scrolling without corrupting neighboring rows, protocol
negotiation, all keyboard enhancement flags, layout alternatives, keypad and
modifier events, and legacy fallback. The desktop integration test also verifies
actual Kitty bytes through the compositor and PTY; use `--keyboard-only` for its
focused keyboard checks.

Prompt icons use bundled BDF fallbacks (Cozette and Scientifica) so their glyphs
remain available when the desktop font collection changes. Their licenses and
pinned source checksums are installed alongside the fonts in
`share/flterm-xr/fonts`.

### Links and PRIMARY

Ctrl+left-click opens OSC 8 links and detected HTTP(S), FTP(S), SSH, file and
email URLs using the desktop's default handler. Hover reveals the target;
right-click offers Open link and Copy link address. Dragging still selects text.
Targets survive edits, soft wraps, scrollback and main/alternate-screen changes.
The XR overlay uses the same Ctrl+click handling.

DomeD's `fltk-1.4.5-primary-selection.patch` implements the native Wayland PRIMARY
protocol in the shared toolkit: `Fl::copy(..., 0)` publishes the selection and
`Fl::paste(..., 0)` receives it asynchronously. Explicit Copy/Paste remains
selection 1. FLTerm desktop windows use these toolkit APIs directly. The patch
applies to all applications using that shared FLTK build; applications linked
statically or to another FLTK installation need the same patch and a rebuild.
Existing processes keep their loaded toolkit until they are reopened normally.

### de-xr bitmap prompt

`xr/shell/starship.toml` adapts the existing two-line Catppuccin Macchiato prompt.
It preserves directory/Git status, command status and duration, and replaces
battery with **XR** / **DE**, supplied by each terminal's
`DOME_TERMINAL_REALM`. Icons use readable text labels; the remaining separators
are verified at native 1x/2x BDF sizes. No Nerd Font or emoji font is required.

Run `python3 scripts/install-user-prompt.py` to enable it in future interactive
Bash shells under DomeD or FLTerm. Other sessions keep their prompt. An explicit
`STARSHIP_CONFIG` takes precedence; `DOME_PROMPT=0` opts out. Bash falls back to a
readline-safe ASCII/color PS1 if Starship is unavailable.

Stage an optimized release and the shared toolkit, then install system files:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DFLTK_DIR=../fltk-vr/_toolkit/share/fltk \
  -DFLTERM_INSTALL_RPATH=/opt/fltk-vr/toolkit/lib
cmake --build build -j4
ctest --test-dir build --output-on-failure
python3 scripts/stage-local.py /absolute/path/to/stage
sudo python3 scripts/install-staged.py /absolute/path/to/stage
```

The installer verifies hashes, backs up existing files and atomically replaces
executables/libraries. It does not restart desktop or display-manager sessions.
`kitty-graphics` and `links` CTests cover the protocol/parser/rendering paths;
the private desktop integration also opens links through a test URL handler and
exchanges PRIMARY with independent FLTK clients and wl-clipboard.
