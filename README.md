# logos-standalone-app

A generic Qt6 shell for loading and testing [Logos](https://logos.co) UI plugins in isolation, without requiring a full Logos node or the complete `logos-app` stack.

## Overview

`logos-standalone` is a minimal host application that:

1. Starts the Logos core backend (`logos-liblogos`)
2. Loads the backend modules a plugin declares as dependencies
3. Displays the plugin's UI in a window

It supports two plugin formats:

- **Dylib plugins** (`.dylib` / `.so` / `.dll`) — loaded via `QPluginLoader`, must export a `createWidget(LogosAPI*)` method. Can be passed as a raw file path or as a directory containing a `metadata.json`.
- **QML plugins** (`ui_qml`) — loaded into a `QQuickWidget`; the `logos` context property exposes a bridge for calling backend modules from QML

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
# Load a dylib plugin directly (raw .dylib/.so file)
logos-standalone ./result/lib/accounts_ui.dylib

# Load a plugin directory (positional argument)
logos-standalone ./chat_ui

# Load a dylib with backend modules
logos-standalone --plugin ./result/lib/accounts_ui.dylib --modules-dir ./modules --load capability_module

# Load a QML plugin with explicit modules
logos-standalone --plugin ./wallet_ui --load waku_module --load wallet

# Override the modules directory
logos-standalone --plugin ./chat_ui --modules-dir ./result/modules

# Run directly via Nix against a local plugin build
nix run github:logos-co/logos-standalone-app -- ./result/lib/chat_ui
```

## Plugin Metadata

When given a plugin directory, the app reads `metadata.json` (or `manifest.json`) to determine the plugin type and auto-load declared dependencies before the UI is shown.

```json
{
  "type": "ui_qml",
  "main": "Main.qml",
  "icon": "icons/calc.png",
  "dependencies": ["waku_module", "chat"]
}
```

For dylib plugins, omit `"type"` (or use `"ui"`) and the app will locate the shared library automatically.

When a raw `.dylib`/`.so`/`.dll` file is passed directly (instead of a directory), the app loads it via `QPluginLoader` without requiring a metadata file. It will still look for a `metadata.json` in the same directory or parent directory to read the `name` (for the window title) and `icon` fields.

### Icon support

If `metadata.json` contains an `"icon"` field with a relative file path, the app sets it as the window icon. The path is resolved relative to the directory containing `metadata.json`.

## Adding `nix run` to a UI module

`logos-standalone-app` is bundled inside `logos-module-builder`. UI modules (`type: ui` or `type: ui_qml`) automatically get `apps.default` wired up — no separate flake input needed.

```nix
{
  inputs = {
    logos-module-builder.url = "github:logos-co/logos-module-builder";
  };

  outputs = inputs@{ logos-module-builder, ... }:
    logos-module-builder.lib.mkLogosModule {
      src = ./.;
      configFile = ./metadata.json;
      flakeInputs = inputs;
    };
}
```

Dependencies listed in `metadata.json` are automatically bundled from their LGX packages and loaded at runtime.

Then build and run:

```bash
nix build && nix run .

# Pass options after --
nix run . -- --title "My Plugin" --width 1280 --height 800
```

To override the standalone app with a custom build, pass `logosStandalone` to `mkLogosModule` or `mkLogosQmlModule`.

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

## Architecture

```
app/
  main.cpp          # CLI argument parsing, core init, module loading
  mainwindow.h/cpp  # Plugin loading (dylib + QML) and window setup
nix/
  app.nix           # Nix derivation: build, install, Qt wrapping, library bundling, capability_module bundling
flake.nix           # Flake outputs: packages, apps, devShells (4 platforms); inputs include capability_module + nix-bundle-lgx
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
