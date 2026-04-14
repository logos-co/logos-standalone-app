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
#   };
{ standaloneApp, logosQtMcp }:

{ pkgs, pluginPkg, testFiles ? [], testFile ? null, timeoutSec ? 120, name ? "plugin-integration-test" }:

let
  # Support both testFile (single, backwards-compat) and testFiles (list)
  allTestFiles =
    if testFiles != [] then testFiles
    else if testFile != null then [ testFile ]
    else builtins.throw "mkPluginTest: either testFiles or testFile must be provided";

  launchScript = pkgs.writeShellScript "run-standalone" ''
    exec ${standaloneApp}/bin/logos-standalone-app -p ${pluginPkg}/lib "$@"
  '';

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
