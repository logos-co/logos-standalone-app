{ pkgs, src, logosSdk, logosLiblogos, logosDesignSystem }:

  pkgs.stdenv.mkDerivation rec {
    pname = "logos-standalone-app";
    version = "1.0.0";

    inherit src;

    nativeBuildInputs = [
      pkgs.cmake
      pkgs.ninja
      pkgs.pkg-config
      pkgs.qt6.wrapQtAppsHook
      logosSdk
      pkgs.patchelf
      pkgs.removeReferencesTo
    ];

    buildInputs = [
      pkgs.qt6.qtbase
      pkgs.qt6.qtremoteobjects
      pkgs.zstd
      pkgs.krb5
      pkgs.abseil-cpp
    ];

    qtLibPath = pkgs.lib.makeLibraryPath ([
      pkgs.qt6.qtbase
      pkgs.qt6.qtremoteobjects
      pkgs.zstd
      pkgs.krb5
      pkgs.zlib
      pkgs.glib
      pkgs.stdenv.cc.cc
      pkgs.freetype
      pkgs.fontconfig
    ] ++ pkgs.lib.optionals pkgs.stdenv.isLinux [
      pkgs.libglvnd
      pkgs.mesa.drivers
      pkgs.xorg.libX11
      pkgs.xorg.libXext
      pkgs.xorg.libXrender
      pkgs.xorg.libXrandr
      pkgs.xorg.libXcursor
      pkgs.xorg.libXi
      pkgs.xorg.libXfixes
      pkgs.xorg.libxcb
    ]);

    qtPluginPath = "${pkgs.qt6.qtbase}/lib/qt-6/plugins";
    # QML import path: bundled Logos.Theme/Controls + Qt declarative fallback
    qmlImportPath = "${placeholder "out"}/lib:${pkgs.qt6.qtdeclarative}/lib/qt-6/qml";

    dontStrip = true;

    qtWrapperArgs = [
      "--prefix" "LD_LIBRARY_PATH" ":" qtLibPath
      "--prefix" "QT_PLUGIN_PATH" ":" qtPluginPath
      "--prefix" "QML_IMPORT_PATH" ":" qmlImportPath
      "--prefix" "QML2_IMPORT_PATH" ":" qmlImportPath
    ];

    preConfigure = ''
      export MACOSX_DEPLOYMENT_TARGET=12.0

      mkdir -p logos-cpp-sdk/include/cpp logos-cpp-sdk/include/core logos-cpp-sdk/lib
      cp -r ${logosSdk}/include/cpp/* logos-cpp-sdk/include/cpp/
      cp -r ${logosSdk}/include/core/* logos-cpp-sdk/include/core/
      for ext in dylib so a; do
        f="${logosSdk}/lib/liblogos_sdk.$ext"
        [ -f "$f" ] && cp "$f" logos-cpp-sdk/lib/
      done
    '';

    preFixup = ''
      find $out -type f -executable -exec sh -c '
        if file "$1" | grep -q "ELF.*executable"; then
          if patchelf --print-rpath "$1" 2>/dev/null | grep -q "/build/"; then
            patchelf --remove-rpath "$1" 2>/dev/null || true
          fi
          if echo "$1" | grep -q "/logos-standalone$"; then
            patchelf --set-rpath "$out/lib" "$1" 2>/dev/null || true
          fi
        fi
      ' _ {} \;
      find $out -name "*.so" -exec sh -c '
        if patchelf --print-rpath "$1" 2>/dev/null | grep -q "/build/"; then
          patchelf --remove-rpath "$1" 2>/dev/null || true
        fi
      ' _ {} \;
    '';

    configurePhase = ''
      runHook preConfigure

      cmake -S app -B build \
        -GNinja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_OSX_DEPLOYMENT_TARGET=12.0 \
        -DCMAKE_INSTALL_RPATH_USE_LINK_PATH=FALSE \
        -DCMAKE_INSTALL_RPATH="" \
        -DCMAKE_SKIP_BUILD_RPATH=TRUE \
        -DLOGOS_LIBLOGOS_ROOT=${logosLiblogos} \
        -DLOGOS_CPP_SDK_ROOT=$(pwd)/logos-cpp-sdk

      runHook postConfigure
    '';

    buildPhase = ''
      runHook preBuild
      cmake --build build
      runHook postBuild
    '';

    installPhase = ''
      runHook preInstall

      mkdir -p $out/bin $out/lib

      cp build/bin/logos-standalone "$out/bin/.logos-standalone-bin"

      # wrapQtAppsHook does not create shell wrappers on macOS, so we do it manually
      # to ensure QML_IMPORT_PATH is set before the QML engine initialises.
      cat > "$out/bin/logos-standalone" << EOF
#!/bin/sh
export QML_IMPORT_PATH="$out/lib:${pkgs.qt6.qtdeclarative}/lib/qt-6/qml\''${QML_IMPORT_PATH:+:\$QML_IMPORT_PATH}"
export QML2_IMPORT_PATH="\$QML_IMPORT_PATH"
exec "$out/bin/.logos-standalone-bin" "\$@"
EOF
      chmod +x "$out/bin/logos-standalone"

      [ -f "${logosLiblogos}/bin/logos_host" ] && cp -L "${logosLiblogos}/bin/logos_host" "$out/bin/"

      ls "${logosLiblogos}/lib/"liblogos_core.* >/dev/null 2>&1 && \
        cp -L "${logosLiblogos}/lib/"liblogos_core.* "$out/lib/" || true
      ls "${logosSdk}/lib/"liblogos_sdk.* >/dev/null 2>&1 && \
        cp -L "${logosSdk}/lib/"liblogos_sdk.* "$out/lib/" || true

      # Bundle Logos.Theme and Logos.Controls QML modules from the design system
      if [ -d "${logosDesignSystem}/lib/Logos" ]; then
        cp -r "${logosDesignSystem}/lib/Logos" "$out/lib/"
      fi

      runHook postInstall
    '';

    meta = {
      description = "Generic standalone Qt shell for loading and testing Logos UI plugins";
      platforms = pkgs.lib.platforms.unix;
    };
  }
