# The one-runtime symbol gate.
#
# INVARIANT: across the images that share ONE process, each runtime type is
# DEFINED by exactly ONE image. A second definition is a second TokenManager:
# the host writes a capability token into one store, an in-process plugin reads
# an empty one, and every cross-module call is refused at runtime with NO build
# diagnostic. See logos-protocol/cpp/logos_shared_api.h.
#
# This app matters more than most for that invariant: it loads THIRD-PARTY
# plugins in-process through QPluginLoader (app/mainwindow.cpp, loadLegacyWidget
# / apiForPlugin), so the split-brain would surface in someone else's plugin.
#
# EXACTLY-ONE, rather than "liblogos_core is the provider". liblogos_core used
# to absorb both static archives and re-export them; since logos-liblogos#182 it
# imports the runtime like everyone else. liblogos_protocol owns TokenManager
# and LogosAPIClient, liblogos_qt_host owns LogosAPI. Naming an owner here would
# need editing every time ownership moves.
#
# THE IN-PROCESS IMAGE SET — this scoping IS the correctness of the gate:
#   IN   bin/logos-standalone-app     the app
#   IN   lib/liblogos_*.{dylib,so}    the runtime it links
#   OUT  bin/logos_host, bin/ui-host  SEPARATE PROCESSES. Measured here: they
#                                     define 98 and 101 runtime symbols
#                                     respectively, and that is CORRECT -- each
#                                     is its own process, so its own copy is the
#                                     right per-process singleton. Scanning them
#                                     reports a true fact about the wrong
#                                     question.
#   OUT  modules/**                   loaded by logos_host, out-of-process.
{ pkgs, appPkg, negativeControl ? false }:

let
  isDarwin  = pkgs.stdenv.isDarwin;
  isWindows = pkgs.stdenv.hostPlatform.isWindows;
  # "" natively, "x86_64-w64-mingw32-" for a Windows cross. The cross bintools
  # installs ONLY the prefixed names, so a bare `nm` / `c++filt` is not on PATH
  # in that derivation and every measurement reads nothing -- valid() then
  # refuses to assert over it. Fail-closed, but the gate could never run.
  tp = pkgs.stdenv.cc.targetPrefix;
  # Mach-O: -gU is defined externals. ELF: -D --defined-only. A PE has no ELF
  # dynamic symbol table, so -D reads NOTHING from a .dll.
  definedCmd = if isDarwin then "${tp}nm -gU"
               else if isWindows then "${tp}nm --defined-only"
               else "${tp}nm -D --defined-only";
  totalCmd   = if isDarwin then "${tp}nm -a"
               else if isWindows then "${tp}nm"
               else "${tp}nm -D";
in
pkgs.runCommand "logos-standalone-app-symbol-gate${pkgs.lib.optionalString negativeControl "-negative"}" {
  nativeBuildInputs = [ pkgs.coreutils pkgs.findutils pkgs.gnugrep pkgs.gnused pkgs.stdenv.cc.bintools ];
} ''
  set -uo pipefail
  export LC_ALL=C   # comm(1) in names() requires a byte-order sort
  FAIL=0
  note() { printf '  %-52s %s\n' "$1" "$2"; }
  bad()  { FAIL=1; printf '  %-52s %s\n' "$1" "$2"; }

  # nix wraps binaries two ways and BOTH defeat a naive measurement: a shell
  # wrapper that execs bin/.<name> (nm reads zero symbols), and
  # makeBinaryWrapper's compiled stub with the real image at bin/.<name>-wrapped
  # (nm reads ~10, so a "did nm read anything" guard does NOT catch it). Rule is
  # deterministic: if a hidden sibling exists, it IS the image.
  # Resolve wrappers ITERATIVELY. nix stacks them here three deep:
  #
  #   bin/logos-standalone-app              507B   shell script
  #   bin/.logos-standalone-app-bin       50456B   makeBinaryWrapper stub
  #   bin/..logos-standalone-app-bin-wrapped 1.5MB the real image
  #
  # and the script execs an ABSOLUTE store path rather than "$BINDIR/...".
  # A single-level resolver returns the script, nm reads zero symbols, and every
  # absence assertion below becomes vacuously true. The compiled stub is worse
  # than the script: it has ~50KB of real symbols, so a "did nm read anything"
  # guard passes on it.
  #
  # Deterministic and repeated, rather than a special case per shape: follow the
  # exec target if this is a script, else take a hidden <name>-wrapped sibling,
  # until neither applies.
  resolve_image() {
    local f="$1" d b t cand i
    for i in 1 2 3 4 5; do
      if head -c2 "$f" 2>/dev/null | grep -q '#!'; then
        t=$(grep -oE 'exec "[^"]+"' "$f" 2>/dev/null | tail -1 | sed 's/^exec "//; s/"$//')
        case "$t" in
          *'$BINDIR'*) t="$(dirname "$f")/''${t##*/}" ;;
        esac
        if [ -n "$t" ] && [ -e "$t" ]; then f="$t"; continue; fi
      fi
      d=$(dirname "$f"); b=$(basename "$f")
      cand=""
      for c in "$d/.$b-wrapped" "$d/.$b"; do
        [ -e "$c" ] && { cand="$c"; break; }
      done
      if [ -n "$cand" ]; then f="$cand"; continue; fi
      break
    done
    printf '%s\n' "$f"
  }

  ${if isWindows then ''
  # PE reports an import THUNK as a defined text symbol: ld synthesizes a .text
  # stub AND an __imp_<mangled> import-address-table slot per imported function,
  # and `nm --defined-only` shows the stub as `T`. Counting that alone reports
  # images as DEFINERS of types they merely import. The paired __imp_ entry is
  # the discriminator, and it is the right one -- a genuine second copy
  # statically linked in has no __imp_ slot and still counts. (The PE export
  # table would also hide the phantom, but it hides a real private copy too,
  # trading a false positive for a false NEGATIVE.)
  names() {
    local t; t=$(mktemp -d)
    ${definedCmd} "$1" 2>/dev/null | awk '{print $3}' | grep -v '^$' | sort -u > "$t/all"
    grep '^__imp_' "$t/all" | sed 's/^__imp_//' | sort -u > "$t/imp"
    grep -v '^__imp_' "$t/all" | sort -u > "$t/def"
    comm -23 "$t/def" "$t/imp" | ${tp}c++filt 2>/dev/null
    rm -rf "$t"
  }
  '' else ''
  names() { ${definedCmd} "$1" 2>/dev/null | ${tp}c++filt 2>/dev/null | sed -E 's/^[0-9a-fA-F]+ [A-Za-z] //'; }
  ''}
  valid() {
    local t; t=$(${totalCmd} "$1" 2>/dev/null | wc -l | tr -d ' ')
    [ "''${t:-0}" -gt 0 ] || { bad "$(basename "$1")" "ERROR: nm read 0 symbols — vacuous"; return 1; }
  }

  ROOT=$TMPDIR/bundle
  mkdir -p "$ROOT"; cp -R ${appPkg}/. "$ROOT"/ 2>/dev/null || true; chmod -R u+w "$ROOT"

  APP=$(resolve_image "$ROOT/bin/logos-standalone-app")
  [ -e "$APP" ] || { echo "FATAL: no app binary under $ROOT/bin"; exit 1; }

  # -L on the find: a nix output stages libs as SYMLINKS and `find -type f` does
  # not match a symlink, so without it the owner set comes back empty and every
  # assertion below passes over nothing.
  OWNERS=()
  while IFS= read -r p; do [ -n "$p" ] && OWNERS+=("$p"); done < <(
    find -L "$ROOT/lib" "$ROOT/bin" -maxdepth 1 -type f \
      \( -name 'liblogos_*.dylib' -o -name 'liblogos_*.so' -o -name 'liblogos_*.dll' \) \
      2>/dev/null || true)

  ${pkgs.lib.optionalString negativeControl ''
    # Plant a duplicate of a real DEFINER, not of liblogos_core: liblogos_core
    # defines zero runtime symbols since #182, so planting it plants NOTHING and
    # this control would silently stop testing anything.
    _def=""
    for c in "$ROOT/lib/liblogos_protocol.dylib" "$ROOT/lib/liblogos_protocol.so"; do
      [ -e "$c" ] && _def="$c" && break
    done
    [ -n "$_def" ] || { echo "NEGATIVE CONTROL: no liblogos_protocol to plant"; exit 1; }
    cp "$_def" "$ROOT/lib/liblogos_negative_control.''${_def##*.}"
    OWNERS+=("$ROOT/lib/liblogos_negative_control.''${_def##*.}")
    echo "NEGATIVE CONTROL: planted a duplicate definer; the gate MUST reject this tree."
  ''}

  echo "app    = ''${APP#$ROOT/}"
  printf 'owners = '; for o in "''${OWNERS[@]}"; do printf '%s ' "$(basename "$o")"; done; echo

  echo
  echo "== each runtime type is defined by EXACTLY ONE image =="
  valid "$APP" || exit 1
  for fam in TokenManager LogosAPI LogosAPIClient; do
    _n=0; _who=""
    for img in "$APP" "''${OWNERS[@]}"; do
      [ -e "$img" ] || continue
      c=$(names "$img" | grep -cE "^''${fam}::|^(vtable|typeinfo|typeinfo name|guard variable) for ''${fam}\b" || true)
      if [ "$c" -gt 0 ]; then _n=$((_n + 1)); _who="$_who $(basename "$img")($c)"; fi
    done
    if [ "$_n" -eq 1 ]; then note "$fam" "1 definer:$_who  OK"
    else bad "$fam" "$_n definers:$_who  EXPECTED exactly 1"; fi
  done

  # StoreRegistry is absent above on purpose: token_manager.cpp defines
  # `static StoreRegistry r;` inside registry(), so it has a LOCAL symbol and no
  # external one. Requiring one DEFINER of something never exported would fail
  # forever. Asserting the app does not define it is still meaningful, because
  # it should never become external.
  echo
  echo "== the app itself defines NONE of the runtime (expect 0) =="
  n=$(names "$APP" | grep -cE '^(TokenManager|StoreRegistry|LogosAPI|LogosAPIClient)::' || true)
  if [ "$n" -eq 0 ]; then note "$(basename "$APP")" "0  OK"
  else bad "$(basename "$APP")" "$n  SPLIT-BRAIN"; names "$APP" | grep -E '^(TokenManager|StoreRegistry|LogosAPI|LogosAPIClient)::' | sed 's/^/      /' | head -6; fi

  echo
  ${if negativeControl then ''
    if [ "$FAIL" -ne 0 ]; then echo "NEGATIVE CONTROL: PASS — gate rejected a planted duplicate."; mkdir -p $out; echo ok > $out/result; exit 0
    else echo "NEGATIVE CONTROL: FAIL — gate ACCEPTED a planted duplicate. It is vacuous."; exit 1; fi
  '' else ''
    if [ "$FAIL" -eq 0 ]; then echo "SYMBOL GATE: PASS"; mkdir -p $out; echo ok > $out/result; exit 0
    else echo "SYMBOL GATE: FAIL — see logos-protocol/cpp/logos_shared_api.h"; exit 1; fi
  ''}
''
