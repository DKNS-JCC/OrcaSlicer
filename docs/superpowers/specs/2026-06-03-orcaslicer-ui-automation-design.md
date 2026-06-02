# OrcaSlicer UI Automation — Design Spec

**Date:** 2026-06-03
**Status:** Approved design, pending implementation plan
**Topic:** Add an opt-in, externally-controllable UI automation interface to OrcaSlicer for automated GUI testing and future AI-agent control.

---

## 1. Overview

Add a localhost JSON-RPC server to a running OrcaSlicer GUI instance that lets an
**external** script (or AI agent) drive and observe the real GUI. "Driving" is done
the way a user would — simulated mouse/keyboard via `wxUIActionSimulator` — while
"observing" reads the live widget state and captures screenshots.

The interface must cover **three UI technologies** present in OrcaSlicer:

1. Native **wxWidgets** widgets (`wxWindow` hierarchy).
2. The **OpenGL 3D viewport** (`GLCanvas3D`) — screenshots via the existing
   framebuffer/thumbnail path.
3. **Dear ImGui** immediate-mode controls (gizmo panels, in-canvas overlays,
   notifications) — recorded as they are drawn, because there is no persistent tree.

## 2. Goals / Non-Goals

### Goals
- Let an external, language-agnostic client connect to a running instance and:
  introspect the UI, locate widgets by stable name, perform input actions, wait on
  conditions, and capture screenshots (including the 3D view as a separate image).
- Be safe by default: disabled unless explicitly enabled; bound to `127.0.0.1` only.
- Be testable in CI without a display (pure-logic units behind a mock backend).
- Ship a reference Python client, a runnable end-to-end example, protocol docs, and
  C++ unit tests.

### Non-Goals (v1)
- No headless/offscreen automation — OS input injection needs a focused, visible
  window (Linux CI requires a display, e.g. Xvfb).
- No auth token in v1 (documented future hardening; localhost-only is the boundary).
- No per-item coverage of raw-`ImGui::` gizmos (Emboss/SVG/Text). They get
  window-level coverage; per-item is future work.
- No new scripting language embedded in the app; control is purely external over JSON-RPC.
- We do **not** modify the existing auth `HttpServer`.

## 3. Background — existing infrastructure (verified)

- **`wxUIActionSimulator`** is compiled into the wx build and already used
  (`src/slic3r/GUI/GUI_ObjectList.cpp:211`). Cross-platform simulated input is available.
- **`GLCanvas3D::render_thumbnail()`** (`src/slic3r/GUI/GLCanvas3D.cpp:2210+`) →
  `render_thumbnail_framebuffer()` (`:6352`) renders the 3D scene into a
  `ThumbnailData` (RGBA) via an FBO + `glReadPixels`. `debug_output_thumbnail()`
  (`:6099`) shows the `ThumbnailData → wxImage → PNG` conversion. This is the
  separate-3D-screenshot path. `Plater::generate_thumbnail()`/`generate_thumbnails()`
  wrap it.
- **`ImGuiWrapper`** (`src/slic3r/GUI/ImGuiWrapper.hpp`) is the chokepoint for nearly
  all ImGui controls: `button`/`bbl_button`, `checkbox`/`bbl_checkbox`, `combo`,
  `slider_float`, `input_double`, `radio_button`, `menu_item_with_icon`, plus
  `begin`/`end` for windows. `imgui_internal.h` is in-tree, exposing
  `ImGui::GetCurrentContext()->Windows`, item rects, and hovered/active id.
- **`HttpServer`** (`src/slic3r/GUI/HttpServer.{hpp,cpp}`, boost::beast, port 13618)
  is used for cloud auth. **It cannot serve a POST body** — `session::read_body()`
  reads and discards the body and never replies (`HttpServer.cpp:57-65`). It is
  effectively GET-only. We will **not** reuse or modify it.
- **`OtherInstanceMessageHandler`** (`src/slic3r/GUI/InstanceCheck.{hpp,cpp}`) is the
  template for "start a localhost listener once the MainFrame exists, post events into
  the GUI." Useful as a structural reference.
- **CLI args** are parsed in `src/OrcaSlicer.cpp` (`CLI::setup`/`CLI::run`) and flow
  into the GUI run params consumed by `GUI_App::OnInit()`.

## 4. Architecture

```
External script / AI agent  ──►  Python client (orca_automation.py)
        │  HTTP POST /jsonrpc   (JSON-RPC 2.0)   on 127.0.0.1:<port>
        ▼
AutomationServer            (dedicated boost::beast listener; own thread;
        │                    started only when --automation-server is set)
        │  parse JSON-RPC envelope
        ▼
JsonRpcDispatcher           (pure logic — method registry; unit-testable)
        │  marshal each call to the GUI thread via wxGetApp().CallAfter
        │  + std::promise/future with a per-request timeout
        ▼
IUiBackend (interface)  ──►  WxUiBackend (real, GUI thread)   /   MockBackend (tests)
        ├─ Introspection : walk wxWindow tree + read ImGui item table → unified JSON
        ├─ Locator       : resolve automation-id / predicate → wx widget or ImGui item
        ├─ Actions       : raise window, then wxUIActionSimulator click/type/key
        ├─ Sync          : wait_for (poll condition) + app.state snapshot
        └─ Screenshots   : wx widget → wxDC→PNG ;  3D view → render_thumbnail()→PNG
```

### Components (new files unless noted)

All new code lives under `src/slic3r/GUI/Automation/`.

| Component | Responsibility |
|---|---|
| `AutomationServer.{hpp,cpp}` | Dedicated boost::beast HTTP listener with **POST + body** support; one `POST /jsonrpc` endpoint; returns `application/json`. Localhost-only. Own thread. |
| `JsonRpcDispatcher.{hpp,cpp}` | Parse JSON-RPC 2.0; route `method` → handler; build result/error. Depends only on `IUiBackend`. No wx/ImGui includes → unit-testable. |
| `IUiBackend.hpp` | Abstract interface: `dump_tree`, `find`, `get_widget`, `click`, `type`, `key`, `wait_for`, `app_state`, `screenshot_window`, `screenshot_viewport3d`. Uses plain structs (no wx types) so tests can mock it. |
| `WxUiBackend.{hpp,cpp}` | Real implementation. Runs on GUI thread. Walks `wxWindow` tree, reads the ImGui item table, drives `wxUIActionSimulator`, captures screenshots. |
| `MockUiBackend.{hpp,cpp}` (tests) | Deterministic fake tree + recorded actions for unit tests. |
| `AutomationRegistry.{hpp,cpp}` | Process-wide `wxWindow* → automation_id` map + reverse lookup; `set_automation_id(win, "id")` helper. Header is dependency-light so widget-construction code can call the helper unconditionally (it is a cheap no-op-safe registration). |
| `WidgetSerializer.{hpp,cpp}` | `wxWindow` → JSON node (name/id, class, label, screen-rect, enabled, shown, value via RTTI). |
| `ImGuiItemTable.{hpp,cpp}` | Per-frame recorder of ImGui items + live-window enumeration. Populated from `ImGuiWrapper`; read on GUI thread. |

### Touch points in existing files
- `src/slic3r/GUI/ImGuiWrapper.cpp` — add recording hooks inside the wrapped widget
  methods and `begin`/`end`, **guarded by an `is_automation_enabled()` flag** so there
  is zero overhead and zero behavior change when automation is off.
- `src/slic3r/GUI/GUI_App.{hpp,cpp}` — own the `AutomationServer`; start it in
  `OnInit()` only when the flag is set; stop it on exit. Expose
  `is_automation_enabled()`.
- `src/OrcaSlicer.cpp` — parse `--automation-server[=PORT]`; pass through GUI run params.
- A handful of widget-construction sites (Slice/Export buttons, preset combos, main
  tabs, common dialog OK/Cancel, the 3D canvas) — add `set_automation_id(...)` calls
  (~15-20 widgets in v1).
- CMake: add the new `Automation/` sources to the GUI target; add the unit-test target.

## 5. Transport & Protocol

- **Transport:** HTTP/1.1 on `127.0.0.1:<port>` (default **13619**, adjacent to the
  auth server's 13618). Single endpoint: `POST /jsonrpc`, body is a JSON-RPC 2.0
  request, response is a JSON-RPC 2.0 result/error. `GET /` returns a tiny health/version page.
- **Protocol:** JSON-RPC 2.0. `id`, `method`, `params`. Batch not required in v1.

### v1 methods

| Method | Params | Result |
|---|---|---|
| `automation.version` | — | `{version, protocol, capabilities[]}` |
| `tree.dump` | `{root?, max_depth?, visible_only?, include_imgui?}` | tree of nodes (wx + imgui) |
| `tree.find` | `{name?, class?, label?, value?, backend?}` | `[node...]` matches |
| `widget.get` | `{target}` | single node detail |
| `input.click` | `{target, button?=left, double?=false, modifiers?[]}` | `{ok}` |
| `input.type` | `{target?, text}` | `{ok}` |
| `input.key` | `{keys}` e.g. `"ctrl+s"` or `["ctrl","s"]` | `{ok}` |
| `sync.wait_for` | `{target, state: exists\|visible\|enabled\|value, value?, timeout_ms?=5000, poll_ms?=100}` | `{ok, elapsed_ms}` |
| `app.state` | — | `{active_tab, project_loaded, slicing, slice_progress, modal_dialog?, foreground}` |
| `screenshot.window` | `{target?}` (default main frame) | `{png_base64, width, height}` |
| `screenshot.viewport3d` | `{plate?, width?, height?}` | `{png_base64, width, height}` |

### Node shape (unified for wx and ImGui)
```json
{
  "backend": "wx" | "imgui",
  "id": "btn_slice",                 // automation id if set, else derived path id
  "path": "MainFrame/.../btn_slice", // stable-ish positional path
  "class": "Button",                 // wx class name or imgui item type
  "label": "Slice plate",
  "rect": { "x": 100, "y": 200, "w": 120, "h": 32 },  // screen coords
  "enabled": true,
  "visible": true,
  "value": "PLA",                    // when applicable (text/choice/check/slider)
  "children": [ ... ]                // wx only; imgui items are flat under their window
}
```

### Error model (JSON-RPC `error.code`)
- `-32700` parse error, `-32601` method not found, `-32602` invalid params (standard).
- Application codes: `1001` widget/target not found, `1002` target not actionable
  (disabled/hidden), `1003` wait timeout, `1004` GUI thread busy/timeout,
  `1005` screenshot failed, `1006` automation feature disabled.

## 6. Threading model

- `AutomationServer` runs on its own thread and accepts connections; the dispatcher
  parses on that thread.
- **Every** call touching wx/ImGui/GL is marshaled to the GUI thread with
  `wxGetApp().CallAfter([...]{ ... })`; the server thread blocks on a `std::future`
  with a per-request timeout (default 5 s; `wait_for` uses its own larger budget).
  Timeout → error `1004`.
- This is mandatory: wx widgets, the ImGui context, and the GL context are not
  thread-safe and are owned by the GUI thread.
- `CallAfter` is serviced even while modal dialogs run (nested event loop), so
  automation can interact with dialogs.

## 7. Widget locator & automation IDs (wxWidgets)

- **Stable IDs:** `set_automation_id(window, "btn_slice")` registers the widget in
  `AutomationRegistry`. Stored in a side map keyed by `wxWindow*` (not via `SetName`,
  to avoid any coupling with wx's name-based lookups). Registration is removed on
  widget destruction (bind to `wxEVT_DESTROY` or prune lazily on lookup).
- **Derived IDs:** for un-instrumented widgets, `WidgetSerializer` derives a positional
  `path` (e.g. `MainFrame/Panel[2]/Button[0]`) so an AI agent can still target anything.
  Named IDs are the preferred, stable path.
- **Locator resolution order:** exact automation id → exact path → predicate match
  (name/class/label/value). Ambiguous matches return the list via `tree.find`; action
  methods require a unique match or error `1001`.
- **v1 instrumented widgets (~15-20):** Slice/Export buttons, printer & filament preset
  combos, the main tab buttons (`tp3DEditor`/`tpPreview`/`tpMonitor`/…), Add/Import,
  common dialog OK/Cancel/Yes/No, the `GLCanvas3D` itself.

## 8. ImGui coverage (v1 = wrapper items + window introspection)

- **Item recording:** inside each `ImGuiWrapper` wrapped method, when
  `is_automation_enabled()` is true, append the just-drawn item to a per-frame
  `ImGuiItemTable` entry: `{window_name, label/id, type, rect, enabled, value}`.
  Item rect comes from `ImGui::GetItemRectMin/Max()` (ImGui display coords) mapped to
  **screen** coords via the `GLCanvas3D` client origin (`ClientToScreen`) and DPI scale.
- **Window enumeration:** via `imgui_internal.h`, enumerate `GetCurrentContext()->Windows`
  for window name, rect, visibility, plus the global hovered/active item id.
- **Double-buffering:** the table is swapped at frame end (`ImGuiWrapper::render`) so
  readers see a complete frame. Reads happen on the GUI thread (after marshaling), same
  thread as rendering, so a simple front/back swap suffices.
- **Freshness:** because items exist only while drawn, before an ImGui tree read or
  action the backend forces a canvas refresh and flushes events so the latest frame is
  captured. `sync.wait_for` can poll for an ImGui item to appear (e.g. after opening a
  gizmo).
- **Actions:** an ImGui target resolves to its recorded screen rect; `input.click`/
  `input.type` use `wxUIActionSimulator` on that rect — identical action path to wx,
  different rect source. Typing into an ImGui input works because simulated keystrokes
  flow through the existing `ImGuiWrapper::update_key_data` bridge once the field is
  focused by a click.
- **Limitation (documented):** raw-`ImGui::` gizmos (Emboss, SVG, Text) are covered at
  the **window** level only in v1; per-item instrumentation is future work.

## 9. Screenshots

- **`screenshot.window`:** capture a `wxWindow` (default: main frame) via
  `wxClientDC`/`wxWindowDC` → `wxBitmap` → `wxImage` → PNG → base64. Works for native
  widgets but **not** for the GL canvas region (returns black there) — hence the
  separate 3D method.
- **`screenshot.viewport3d`:** reuse `GLCanvas3D::render_thumbnail()` (FBO +
  `glReadPixels`) → `ThumbnailData` → `wxImage` (per `debug_output_thumbnail`) → PNG →
  base64. Optional `plate`, `width`, `height` params. Runs on the GUI thread with the GL
  context current.

## 10. Activation & security

- **Off by default.** Enabled by CLI flag `--automation-server[=PORT]` (default port
  13619). (An app-config/Preferences toggle may be added later; v1 is flag-only.)
- **Bind `127.0.0.1` only.** No external interface.
- **No token in v1** (per decision); documented as a recommended future hardening,
  along with an optional `--automation-token`.
- When disabled: no listener, no thread, and all `ImGuiWrapper` recording hooks are
  skipped — **zero** runtime overhead and **zero** behavior change. This satisfies the
  project's "features gated by options must not affect existing behavior when disabled"
  constraint.

## 11. Testability

- `JsonRpcDispatcher` depends only on `IUiBackend` and has **no** wx/ImGui/GL includes.
- **C++ unit tests (Catch2), display-free, run in CI:**
  - JSON-RPC envelope parse/validate/dispatch (good + malformed input, error codes).
  - Method routing and param validation for every v1 method against `MockUiBackend`.
  - `WidgetSerializer` node shape (fed a synthetic node model, not real wx widgets).
  - Locator resolution: exact id, path, predicate, ambiguity, not-found.
- The only piece needing a real GUI is `WxUiBackend`; it is exercised by the manual
  end-to-end example, not by CI unit tests.

## 12. Deliverables

- **C++:** the `Automation/` components, `ImGuiWrapper` recording hooks, widget
  instrumentation, CLI flag plumbing, `GUI_App` lifecycle, CMake wiring.
- **`tools/automation/orca_automation.py`:** reference Python client wrapping the
  JSON-RPC calls (`connect`, `version`, `dump_tree`, `find`, `click`, `type`, `key`,
  `wait_for`, `app_state`, `screenshot`, `screenshot_3d`).
- **`tools/automation/example_slice.py`:** runnable end-to-end flow — launch OrcaSlicer
  with the flag, load a model, click Slice, `wait_for` completion, save a 3D-preview PNG.
  Doubles as a manual smoke test.
- **`doc/automation.md`:** protocol reference (methods, params, results, error codes),
  node shape, automation-id naming conventions, ImGui notes, platform/display caveats.
- **`tests/`:** Catch2 unit-test target for the dispatch/serialize/locator logic.

## 13. New / changed file inventory

**New**
- `src/slic3r/GUI/Automation/AutomationServer.{hpp,cpp}`
- `src/slic3r/GUI/Automation/JsonRpcDispatcher.{hpp,cpp}`
- `src/slic3r/GUI/Automation/IUiBackend.hpp`
- `src/slic3r/GUI/Automation/WxUiBackend.{hpp,cpp}`
- `src/slic3r/GUI/Automation/AutomationRegistry.{hpp,cpp}`
- `src/slic3r/GUI/Automation/WidgetSerializer.{hpp,cpp}`
- `src/slic3r/GUI/Automation/ImGuiItemTable.{hpp,cpp}`
- `tools/automation/orca_automation.py`
- `tools/automation/example_slice.py`
- `doc/automation.md`
- `tests/automation/` (Catch2 target) + `MockUiBackend.{hpp,cpp}`

**Changed**
- `src/slic3r/GUI/ImGuiWrapper.cpp` (guarded recording hooks)
- `src/slic3r/GUI/GUI_App.{hpp,cpp}` (server lifecycle, `is_automation_enabled()`)
- `src/OrcaSlicer.cpp` (CLI flag)
- ~15-20 widget-construction sites (`set_automation_id`)
- `src/slic3r/GUI/CMakeLists.txt` + `tests/CMakeLists.txt`

## 14. Known constraints & limitations

- OS input injection requires the OrcaSlicer window **focused and visible**; the backend
  raises/focuses the main window before injecting. Linux CI needs a display (Xvfb).
- Input is asynchronous at the OS level; correctness relies on `sync.wait_for` rather
  than fixed sleeps.
- ImGui items are only addressable while their host panel is drawn.
- Raw-`ImGui::` gizmos: window-level only in v1.
- Single-client assumption in v1 (serialized request handling); no concurrent sessions
  contract.

## 15. Future work (out of scope for v1)

- Optional auth token + Preferences toggle.
- WebSocket channel for server-push events (slice progress, dialog-appeared).
- Per-item instrumentation for raw-`ImGui::` gizmos.
- An MCP server wrapping the JSON-RPC client for direct AI-agent integration.
- Optional integration of Dear ImGui Test Engine for deterministic ImGui interaction.

## 16. Verification plan

- **CI:** Catch2 unit tests (dispatch/serialize/locator) pass with no display.
- **Manual / e2e:** run `tools/automation/example_slice.py` against a built OrcaSlicer
  launched with `--automation-server`; confirm model loads, Slice runs, `wait_for`
  returns on completion, and both a wx-window PNG and a 3D-viewport PNG are produced.
- **Regression:** build and run with automation **off**; confirm no new threads, no
  listener, and ImGui rendering is byte-for-byte unchanged (hooks compiled out of the
  hot path via the disabled flag).
