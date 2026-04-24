# logos-standalone-app

A generic Qt6 shell for loading and testing [Logos](https://logos.co) UI plugins in isolation, without requiring a full Logos node or the complete `logos-app` stack.

## Overview

`logos-standalone` is a minimal host application that:

1. Starts the Logos core backend (`logos-liblogos`)
2. Loads the backend modules a plugin declares as dependencies
3. Displays the plugin's UI in a window

It supports three plugin formats:

- **View modules** (`type: "ui"` with `"view"` field) — process-isolated C++ backend + QML view. The C++ plugin runs in a separate `ui-host` process; the QML view is loaded in the standalone app and communicates with the backend via `logos.callModuleAsync()`.
- **QML plugins** (`type: "ui_qml"`) — pure QML UI loaded into a `QQuickWidget`; the `logos` context property exposes a bridge for calling backend modules from QML.
- **Legacy dylib plugins** (`type: "ui"`, no `"view"` field) — loaded via `QPluginLoader`, must export a `createWidget(LogosAPI*)` method. Can be passed as a raw file path or as a directory containing a `metadata.json`.

## Usage

```
logos-standalone [options] <plugin-path>
```

### Options

| Flag | Short | Description |
|------|-------|-------------|
| `--plugin <path>` | `-p` | Path to the plugin directory (alternative to positional argument) |
| `--modules-dir <dir>` | `-m` | Directory containing backend modules (default: `../modules` relative to the binary) |
| `--load <module>` | `-l` | Load a named backend module before showing the UI; can be repeated |
| `--title <title>` | `-t` | Window title (default: `name` from `metadata.json`, then plugin filename) |
| `--width <px>` | | Window width in pixels (default: `1024`) |
| `--height <px>` | | Window height in pixels (default: `768`) |
| `--help` | `-h` | Show help and exit |

### Examples

```bash
# Load a view module directory (C++ backend + QML view)
logos-standalone ./calc_ui_cpp

# Load a pure QML plugin directory
logos-standalone ./wallet_ui

# Load a legacy dylib plugin directly (raw .dylib/.so file)
logos-standalone ./result/lib/accounts_ui.dylib

# Load a plugin directory with backend modules
logos-standalone --plugin ./chat_ui --modules-dir ./modules --load capability_module

# Override the modules directory
logos-standalone --plugin ./chat_ui --modules-dir ./result/modules

# Run directly via Nix against a local plugin build
nix run github:logos-co/logos-standalone-app -- ./result/lib/chat_ui
```

## Plugin Metadata

When given a plugin directory, the app reads `metadata.json` (or `manifest.json`) to determine the plugin type and auto-load declared dependencies before the UI is shown.

**View module** (C++ backend + QML view, process-isolated):
```json
{
  "name": "calc_ui",
  "type": "ui",
  "view": "qml/Main.qml",
  "dependencies": ["calc_module"]
}
```

**Pure QML plugin**:
```json
{
  "type": "ui_qml",
  "main": "Main.qml",
  "dependencies": ["waku_module", "chat"]
}
```

**Legacy dylib plugin** (`type: "ui"` without `"view"`, or a raw `.dylib`/`.so` file):
```json
{
  "type": "ui",
  "dependencies": ["capability_module"]
}
```

The app determines the loading strategy from these fields:
1. `type: "ui"` + `"view"` present → view module (spawn `ui-host`, load QML view)
2. `type: "ui"` without `"view"` → legacy dylib (load via `QPluginLoader`, call `createWidget`)
3. `type: "ui_qml"` → pure QML (load into `QQuickWidget`)

When a raw `.dylib`/`.so`/`.dll` file is passed directly (instead of a directory), the app loads it via `QPluginLoader` without requiring a metadata file.

### Icon support

If `metadata.json` contains an `"icon"` field with a relative file path, the app sets it as the window icon. The path is resolved relative to the directory containing `metadata.json`.

### `DEV_QML_PATH` — iterate on QML without rebuilding

For `ui_qml` plugins, setting `DEV_QML_PATH` to a directory redirects the view entry file (named by `metadata.json`'s `view` field) to that directory. Edit QML in source, relaunch the app, see changes — no `nix build` needed.

```bash
# view: "qml/MyView.qml" → loaded from $DEV_QML_PATH/MyView.qml
export DEV_QML_PATH=$PWD/src/qml
logos-standalone ./result/lib/my_ui_module
```

The directory must contain the basename of the `view` entry. The engine's base URL is set to `DEV_QML_PATH`, so relative imports (sub-components, icons) resolve alongside the entry file. If the env var is unset, invalid, or missing the entry file, the installed view is used and a warning is logged.

## Adding `nix run` to a UI module

`logos-standalone-app` is bundled inside `logos-module-builder`. UI modules automatically get `apps.default` wired up — no separate flake input needed. Use the builder that matches your module type:

| Module type | Builder | When to use |
|-------------|---------|-------------|
| C++ + QML view | `mkViewModule` | Process-isolated UI: C++ backend in `ui-host`, QML view in host app |
| Pure QML | `mkLogosQmlModule` | QML-only UI, no C++ compilation |
| Legacy C++ widget | `mkLogosModule` | C++ plugin with `createWidget()` (legacy pattern) |

```nix
# View module (C++ + QML)
logos-module-builder.lib.mkViewModule {
  src = ./.;
  configFile = ./metadata.json;
  flakeInputs = inputs;
};

# Pure QML module
logos-module-builder.lib.mkLogosQmlModule {
  src = ./.;
  configFile = ./metadata.json;
  flakeInputs = inputs;
};

# Legacy C++ widget module (type: "ui", no "view" field)
logos-module-builder.lib.mkLogosModule {
  src = ./.;
  configFile = ./metadata.json;
  flakeInputs = inputs;
};
```

Dependencies listed in `metadata.json` are automatically bundled from their LGX packages and loaded at runtime.

Then build and run:

```bash
nix build && nix run .

# Pass options after --
nix run . -- --title "My Plugin" --width 1280 --height 800
```

To override the standalone app with a custom build, pass `logosStandalone` to `mkViewModule`, `mkLogosModule`, or `mkLogosQmlModule`.

## Building

### Nix (recommended)

```bash
nix build                # produces ./result/bin/logos-standalone
nix run -- <plugin-path> # build and run in one step
```

To use a local checkout of a dependency:

```bash
nix build --override-input logos-liblogos path:../logos-liblogos
nix build --override-input logos-cpp-sdk   path:../logos-cpp-sdk
nix build --override-input logos-capability-module path:../logos-capability-module
```

### Manual CMake

Requires Qt6, `logos-liblogos`, and `logos-cpp-sdk` to be available.

```bash
cmake -S app -B build -GNinja \
  -DLOGOS_LIBLOGOS_ROOT=/path/to/logos-liblogos \
  -DLOGOS_CPP_SDK_ROOT=/path/to/logos-cpp-sdk
cmake --build build
```

### Dev shell

```bash
nix develop   # sets LOGOS_CPP_SDK_ROOT and LOGOS_LIBLOGOS_ROOT automatically
```

## QML Inspector

The app includes an optional QML Inspector server ([logos-qt-mcp](https://github.com/logos-co/logos-qt-mcp)) that enables runtime UI introspection over TCP. This allows AI assistants (via MCP) and test frameworks to interact with the running UI — clicking buttons, reading properties, taking screenshots, etc.

The inspector is **enabled by default** in non-release builds. In nix builds it is controlled by the `enableInspector` parameter (default: `true`).

### Using the inspector interactively

```bash
# Build the app (inspector enabled by default)
nix build

# Build the MCP server + test framework (one-time)
nix build .#logos-qt-mcp -o result-mcp

# Run the app with a plugin — inspector starts on localhost:3768
./result/bin/logos-standalone-app ./my-plugin/result/lib

# Connect Claude Code via .mcp.json (automatic when working from this directory)
# Or connect any MCP client to localhost:3768
```

The inspector port defaults to `3768` and can be changed via the `QML_INSPECTOR_PORT` environment variable.

### Available MCP tools

| Tool | Description |
|------|-------------|
| `qml_screenshot` | Capture a screenshot of the current app state |
| `qml_find_and_click` | Find a UI element by text and click it |
| `qml_find_by_type` | Locate elements by QML type name |
| `qml_find_by_property` | Locate elements by property value |
| `qml_list_interactive` | List all clickable/interactive elements |
| `qml_get_tree` | Get the full QML element tree |

### Disabling the inspector

```bash
# Via CMake
cmake -DENABLE_QML_INSPECTOR=OFF ...

# Via nix (pass enableInspector = false to nix/app.nix)
```

### Plugin integration tests (`lib.mkPluginTest`)

logos-standalone-app exposes a reusable test builder that UI modules can use to run headless integration tests. This is wired up automatically by `logos-module-builder` — any UI module with `.mjs` files in its `tests/` directory gets a `nix build .#integration-test` output for free.

If you need to use it directly:

```nix
# In a module's flake.nix
integration-test = logos-standalone-app.lib.${system}.mkPluginTest {
  inherit pkgs;
  pluginPkg = myModulePackage;
  testFiles = [ ./tests/smoke.mjs ./tests/interactions.mjs ];
  name = "my-module-integration-test";
};
```

The builder launches the plugin in logos-standalone-app with `QT_QPA_PLATFORM=offscreen`, connects to the QML inspector, and runs each test file sequentially.

## Architecture

```
app/
  main.cpp          # CLI argument parsing, core init, module loading
  mainwindow.h/cpp  # Plugin loading (dylib + QML) and window setup
nix/
  app.nix           # Nix derivation: build, install, Qt wrapping, library bundling, capability_module bundling
  mkPluginTest.nix  # Reusable integration test builder for UI plugins
flake.nix           # Flake outputs: packages, apps, lib, devShells (4 platforms)
```

The nix build bundles `capability_module` (via `nix-bundle-lgx`) into the output's `modules/` directory so it is always available at runtime. The app loads `capability_module` first (required by all UI plugins), then any modules declared in the plugin's `metadata.json`, then any additional modules passed via `--load`. The default modules directory is `../modules` relative to the binary; use `--modules-dir` to override.

## Supported Platforms

- `aarch64-darwin` (Apple Silicon)
- `x86_64-darwin` (Intel Mac)
- `aarch64-linux`
- `x86_64-linux`

## Related

- [logos-liblogos](https://github.com/logos-co/logos-liblogos) — core C++ plugin system
- [logos-cpp-sdk](https://github.com/logos-co/logos-cpp-sdk) — code generation SDK for module wrappers
- [logos-app](https://github.com/logos-co/logos-app) — full Qt desktop application
- [logos-module-builder](https://github.com/logos-co/logos-module-builder) — Nix helpers and templates for creating modules
