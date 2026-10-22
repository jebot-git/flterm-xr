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
- Desktop drag selects text; middle-click pastes primary selection; wheel scrolls.
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
overlay. Native size/position persist atomically in
`$XDG_CONFIG_HOME/flterm-xr/layout.json`, with `--layout-file` and
`--no-save-layout` overrides. Desktop instances do not touch this file.

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

This is an initial working port. Terminal emulation is FLTerm's older VT100 core:
it does not acquire Ghostty's truecolour, Kitty keyboard/graphics, grapheme or
modern terminal protocol coverage. Desktop FLTK composition and clipboard use
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
