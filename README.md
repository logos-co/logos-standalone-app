# logos-standalone-app

A generic Qt6 shell for loading and testing [Logos](https://logos.co) UI plugins in isolation, without requiring a full Logos node or the complete `logos-app` stack.

## Overview

`logos-standalone` is a minimal host application that:

1. Starts the Logos core backend (`logos-liblogos`)
2. Loads the backend modules a plugin declares as dependencies
3. Displays the plugin's UI in a window

It supports two plugin formats:

- **Dylib plugins** (`.dylib` / `.so` / `.dll`) — loaded via `QPluginLoader`, must export a `createWidget(LogosAPI*)` method
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
| `--title <title>` | `-t` | Window title (default: derived from plugin directory name) |
| `--width <px>` | | Window width in pixels (default: `1024`) |
| `--height <px>` | | Window height in pixels (default: `768`) |
| `--help` | `-h` | Show help and exit |

### Examples

```bash
# Load a dylib plugin (positional argument)
logos-standalone ./chat_ui

# Load a QML plugin with explicit modules
logos-standalone --plugin ./wallet_ui --load waku_module --load wallet

# Override the modules directory
logos-standalone --plugin ./chat_ui --modules-dir ./result/modules

# Run directly via Nix against a local plugin build
nix run github:logos-co/logos-standalone-app -- ./result/lib/chat_ui
```

## Plugin Metadata

Each plugin directory must contain a `metadata.json` (or `manifest.json`) file. The app reads it to determine the plugin type and auto-load declared dependencies before the UI is shown.

```json
{
  "type": "ui_qml",
  "main": "Main.qml",
  "dependencies": ["waku_module", "chat"]
}
```

For dylib plugins, omit `"type"` (or use `"ui"`) and the app will locate the shared library automatically.

## Adding `nix run` to an existing C++ plugin

Add `logos-standalone-app` as a flake input and an `apps` output to your plugin's `flake.nix`:

```nix
inputs.logos-standalone-app.url = "github:logos-co/logos-standalone-app";
```

```nix
apps = forAllSystems (system:
  let
    pkgs = import nixpkgs { inherit system; };
    standalone = logos-standalone-app.packages.${system}.default;
    plugin = self.packages.${system}.default;
    run = pkgs.writeShellScript "run-standalone" ''
      exec ${standalone}/bin/logos-standalone "${plugin}" "$@"
    '';
  in { default = { type = "app"; program = "${run}"; }; }
);
```

Then build and run:

```bash
nix build && nix run .

# Pass options after --
nix run . -- --title "My Plugin" --width 1280 --height 800
```

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
  app.nix           # Nix derivation: build, install, Qt wrapping, library bundling
flake.nix           # Flake outputs: packages, apps, devShells (4 platforms)
```

The app always loads `capability_module` first (required by all UI plugins), then any modules declared in the plugin's `metadata.json`, then any additional modules passed via `--load`.

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
