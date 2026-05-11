# Reusable integration test builder for Logos UI plugins.
#
# Launches logos-standalone-app with the given plugin (headless/offscreen),
# connects to the QML Inspector, and runs Node.js test files using the
# logos-qt-mcp test framework.
#
# Accepts either a single test file or a list of test files. Each file is
# run as a separate invocation (the test framework's run() exits the process).
#
# Usage from a module flake:
#   logos-standalone-app.lib.mkPluginTest {
#     inherit pkgs;
#     pluginPkg = myModulePackage;
#     testFiles = [ ./tests/smoke.mjs ./tests/interactions.mjs ];
#     moduleDeps = { package_manager = pkgs1.lgx; ... };  # optional
#   };
{ standaloneApp, logosQtMcp }:

{ pkgs, pluginPkg, testFiles ? [], testFile ? null, timeoutSec ? 120, name ? "plugin-integration-test"
, moduleDeps ? {}
}:

let
  # Support both testFile (single, backwards-compat) and testFiles (list)
  allTestFiles =
    if testFiles != [] then testFiles
    else if testFile != null then [ testFile ]
    else builtins.throw "mkPluginTest: either testFiles or testFile must be provided";

  hasModuleDeps = moduleDeps != {};

  # Merged modules directory: standalone-app's built-ins (capability_module)
  # plus each declared dependency extracted from its LGX package. Mirrors the
  # layout mkStandaloneApp uses so the plugin's metadata.json `dependencies`
  # field resolves at runtime.
  modulesDir = pkgs.runCommand "${name}-modules" {
    nativeBuildInputs = [ pkgs.python3 ];
  } ''
    mkdir -p $out

    if [ -d "${standaloneApp}/modules" ]; then
      cp -r ${standaloneApp}/modules/* $out/
    fi

    ${pkgs.lib.concatStringsSep "\n" (pkgs.lib.mapAttrsToList (depName: lgxPkg: ''
      lgx_file=$(find ${lgxPkg} -name '*.lgx' | head -1)
      if [ -n "$lgx_file" ]; then
        extract_dir=$(mktemp -d)
        tar -xzf "$lgx_file" -C "$extract_dir"

        variant_dir=""
        for v in darwin-arm64-dev darwin-amd64-dev darwin-x86_64-dev \
                 linux-x86_64-dev linux-amd64-dev linux-arm64-dev \
                 darwin-arm64 darwin-amd64 darwin-x86_64 \
                 linux-x86_64 linux-amd64 linux-arm64; do
          if [ -d "$extract_dir/variants/$v" ]; then
            variant_dir="$extract_dir/variants/$v"
            break
          fi
        done

        if [ -n "$variant_dir" ]; then
          module_name=$(python3 -c "
import json; f=open('$extract_dir/manifest.json'); print(json.load(f).get('name',str())); f.close()
")
          if [ -n "$module_name" ]; then
            mkdir -p "$out/$module_name"
            cp "$extract_dir/manifest.json" "$out/$module_name/"
            cp -r "$variant_dir"/* "$out/$module_name/"
            echo "Bundled $module_name from LGX into $out/$module_name/"
          else
            echo "Warning: could not read module name from LGX manifest for ${depName}" >&2
          fi
        else
          echo "Warning: no matching platform variant in LGX package for ${depName}" >&2
        fi
        rm -rf "$extract_dir"
      else
        echo "Warning: no .lgx file found for ${depName}" >&2
      fi
    '') moduleDeps)}
  '';

  launchScript = pkgs.writeShellScript "run-standalone" (if hasModuleDeps then ''
    exec ${standaloneApp}/bin/logos-standalone-app --modules-dir ${modulesDir} -p ${pluginPkg}/lib "$@"
  '' else ''
    exec ${standaloneApp}/bin/logos-standalone-app -p ${pluginPkg}/lib "$@"
  '');

  runOneTest = tf: ''
    echo "Running: ${builtins.baseNameOf (toString tf)}"
    timeout ${toString timeoutSec} \
      ${pkgs.nodejs}/bin/node ${tf} --ci ${launchScript} --verbose
  '';
in
pkgs.runCommand name {
  nativeBuildInputs = [ pkgs.coreutils pkgs.nodejs ]
    ++ pkgs.lib.optionals pkgs.stdenv.isLinux [
      pkgs.qt6.qtbase
      pkgs.libGL
      pkgs.libglvnd
    ];
} ''

  mkdir -p $out
  export LOGOS_DATA_DIR="$out/app-data"
  mkdir -p "$LOGOS_DATA_DIR"

  export QT_QPA_PLATFORM=offscreen
  export QT_FORCE_STDERR_LOGGING=1
  export QT_LOGGING_RULES="qt.*.debug=false;default.debug=true"

  ${pkgs.lib.optionalString pkgs.stdenv.isLinux ''
    export QT_PLUGIN_PATH="${pkgs.qt6.qtbase}/${pkgs.qt6.qtbase.qtPluginPrefix}"
    export LD_LIBRARY_PATH="${pkgs.libGL}/lib:${pkgs.libglvnd}/lib''${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
  ''}

  export LOGOS_QT_MCP="${logosQtMcp}"

  echo "Running integration tests: ${name} (${toString (builtins.length allTestFiles)} file(s), timeout: ${toString timeoutSec}s per file)..."

  ${pkgs.lib.concatMapStringsSep "\n" runOneTest allTestFiles}

  echo "All integration tests passed"
''
