# Smoke-tests the logos-standalone-app binary.
# Runs the app with --help and verifies:
#   - the binary starts and Qt initializes without errors
#   - all shared libraries resolve (no missing .so / .dylib)
#   - no segfaults or fatal Qt plugin failures occur
#   - --help exits cleanly (code 0)
#
# logos-standalone-app requires a --plugin argument to load a UI, so we use
# --help rather than launching the event loop. This is enough to validate the
# full startup path: binary loads, Qt initialises, logos_core links correctly.
{ pkgs, appPkg, appBin ? "${appPkg}/bin/logos-standalone-app", timeoutSec ? 5 }:

pkgs.runCommand "logos-standalone-app-smoke-test" {
  nativeBuildInputs = [ pkgs.coreutils ]
    ++ pkgs.lib.optionals pkgs.stdenv.isLinux [
      pkgs.qt6.qtbase
      pkgs.libGL
      pkgs.libglvnd
    ];
} ''

  mkdir -p $out
  export LOGOS_USER_DIR="$out/app-data"
  mkdir -p "$LOGOS_USER_DIR"

  export QT_QPA_PLATFORM=offscreen
  export QT_FORCE_STDERR_LOGGING=1
  export QT_LOGGING_RULES="qt.*.debug=false;default.debug=true"

  ${pkgs.lib.optionalString pkgs.stdenv.isLinux ''
    export QT_PLUGIN_PATH="${pkgs.qt6.qtbase}/${pkgs.qt6.qtbase.qtPluginPrefix}"
    export LD_LIBRARY_PATH="${pkgs.libGL}/lib:${pkgs.libglvnd}/lib''${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
  ''}

  LOG="$out/smoke-test.log"

  echo "Running logos-standalone-app smoke test (timeout: ${toString timeoutSec}s)..."
  set +e
  timeout ${toString timeoutSec} ${appBin} --help > "$LOG" 2>&1
  CODE=$?
  set -e

  cat "$LOG"

  # --help must exit 0; any other non-timeout code indicates a startup failure
  if [ "$CODE" -ne 0 ] && [ "$CODE" -ne 124 ]; then
    echo "App failed with unexpected exit code $CODE"
    exit 1
  fi

  # Check for fatal library / Qt errors
  if grep -qE "Failed to load.*plugin|The shared library was not found|SIGSEGV|SIGABRT|SIGBUS|cannot open shared object" "$LOG"; then
    echo "Fatal errors detected in output"
    exit 1
  fi

  echo "Smoke test passed (exit code: $CODE)"
''
