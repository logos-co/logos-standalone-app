{
    description = "Logos Standalone App — generic Qt shell for loading and testing Logos UI plugins";

    inputs = {
      logos-nix.url = "github:logos-co/logos-nix";
      nixpkgs.follows = "logos-nix/nixpkgs";
      # Was rev-pinned to a04b278 on logos-cpp-sdk's feat/sdk-codegen-b3-d11,
      # because master then lacked logos_host_services.h and the cdylib grant
      # export. logos-cpp-sdk#138 ("split the SDK by capability, retire the
      # provider-header path, and harden the cdylib decode") MERGED that branch,
      # so master (95d7b3a) now ships cpp/logos_host_services.h and the rest of
      # the capability split. Gap closed - tracking master again.
      logos-cpp-sdk.url = "github:logos-co/logos-cpp-sdk";
      # Was rev-pinned to c8bab12 on feat/per-client-token-store, for the
      # per-client token store. logos-protocol#59 ("per-client token store, the
      # host-services C ABI, and a container shape-check") MERGED that branch, so
      # master (f4407ff) now carries TokenManager::forIdentity / isolateIdentity
      # and the lp_grant_host_services / lp_token_keys C ABI. Gap closed -
      # tracking master again.
      logos-protocol = {
        url = "github:logos-co/logos-protocol";
        inputs.logos-nix.follows = "logos-nix";
        inputs.nixpkgs.follows = "nixpkgs";
      };
      # The Qt HOST RUNTIME this app links: LogosAPI, LogosAPIProvider and the
      # provider objects. The B1 split moved them out of logos-qt-sdk into
      # logos-plugin-qt, which exports them as packages.<sys>.logos-qt-host with
      # the CMake target logos-qt-host::logos_qt_host.
      #
      # logos-qt-sdk is deliberately NOT an input any more: the host runtime was
      # the only thing this app ever took from it. It uses none of the surface
      # that stays behind there — no logos_ui_plugin_context.h (that is for
      # ui_qml module backends, not for the shell that loads them), no
      # logos_qt_lp_bridge.h / logos_qt_wire.h, and no logos-qt-generator.
      #
      # Was rev-pinned to cc24fa1c (tip of feat/b4-qt-host-windows-target)
      # because logos-qt-host did not exist on logos-plugin-qt's master yet.
      # logos-plugin-qt#19 ("the Qt host runtime and cdylib-glue generator")
      # MERGED that branch, so master (9b2c64e) publishes logos-qt-host keyed by
      # forAllTargets. Gap closed - tracking master again.
      #
      # The pin's OTHER half still matters and is NOT closed by that merge: this
      # must stay the same host runtime logos-liblogos builds, because the app
      # binary statically links the logos_qt_host archive while liblogos_core
      # carries its own copy in the SAME process. logos-liblogos is still pinned
      # at f2a15ef3, which pins cc24fa1c and does not follow this input, so the
      # two sides no longer name one rev, and the closure now carries two
      # logos-qt-host store paths where it carried one. That is safe today, and
      # it was checked rather than assumed: cpp/* and nix/qt-host.nix differ
      # between cc24fa1c and master in comments only, and the two built archives
      # compare byte-identical in every object member (the one delta in the whole
      # .a is the nixbld uid recorded in ar's __.SYMDEF header). The two copies
      # therefore differ in store path, not in code.
      # Re-check that if master's cpp/ moves ahead of what logos-liblogos pins -
      # a real skew there is a duplicate-TokenManager bug that builds clean and
      # only shows up at runtime.
      logos-plugin-qt = {
        url = "github:logos-co/logos-plugin-qt";
        inputs.logos-nix.follows = "logos-nix";
        inputs.nixpkgs.follows = "nixpkgs";
        inputs.logos-protocol.follows = "logos-protocol";
      };
      # Unpinned: fix/b4-align-protocol-with-qt-host merged (logos-liblogos#177),
      # so master carries the qt-host repoint and the include/ re-export this app
      # compiles against — the reason the rev pin existed.
      #
      # This also removes one of the two sources of a duplicate host runtime. The
      # app binary statically links logos-qt-host while liblogos_core.dylib
      # carries its own copy in the SAME process, and a dylib has no interposition
      # to merge them. While this pinned f2a15ef (which pinned logos-plugin-qt at
      # cc24fa1c) that was a second archive; liblogos master tracks plugin-qt
      # master now, so this path agrees with the root.
      #
      # logos-view-module-runtime was the other source and is unpinned below now
      # (its #25 merged), so every path resolves logos-plugin-qt to master and the
      # closure carries ONE logos-qt-host.
      logos-liblogos.url = "github:logos-co/logos-liblogos";
      logos-design-system.url = "github:logos-co/logos-design-system";
      # 3ef779c is on logos-view-module-runtime's feat/sdk-codegen-b4-qt-host,
      # with master merged in, so it carries the hot-reload fix as well as the
      # qt-host repoint — a branch rev, hence the URL pin.
      logos-view-module-runtime.url = "github:logos-co/logos-view-module-runtime";
      logos-qt-mcp.url = "github:logos-co/logos-qt-mcp";
      # NOTE: no logos-capability-module input, and no nix-bundle-lgx (which was
      # here only to bundle it into a .lgx for re-extraction below).
      #
      # capability_module belongs to liblogos, not to this app. logos_core loads
      # it itself — module_manager.cpp's initializeCapabilityModule() calls
      # loadModuleInternal("capability_module") — and liblogos ships it ready to
      # load: nix/bin.nix copies its modules/ into the package, and the default
      # output symlinkJoins that in. So ${logosLiblogos}/modules already holds
      # capability_module in exactly the layout logos_core expects, and this app
      # already depends on logos-liblogos for logos_host and lib/. Declaring
      # capability-module here built a second copy of the same module from the
      # same source, then packed and unpacked it through a .lgx to arrive at the
      # layout liblogos had already produced.
    };

    outputs = { self, nixpkgs, logos-nix, logos-cpp-sdk, logos-protocol, logos-plugin-qt, logos-liblogos, logos-design-system, logos-view-module-runtime, logos-qt-mcp }:
      let
        systems = [ "aarch64-darwin" "x86_64-darwin" "aarch64-linux" "x86_64-linux" ];
        forAllSystems = f: nixpkgs.lib.genAttrs systems (system: f {
          inherit system;
          pkgs = import nixpkgs { inherit system; };
          logosSdk = logos-cpp-sdk.packages.${system}.default;
          logosProtocolPkg = logos-protocol.packages.${system}.default;
          logosQtHost = logos-plugin-qt.packages.${system}.logos-qt-host;
          logosLiblogos = logos-liblogos.packages.${system}.default;
          logosDesignSystem = logos-design-system.packages.${system}.default;
          logosViewModuleRuntime = logos-view-module-runtime.packages.${system}.default;
          logosQtMcp = logos-qt-mcp.packages.${system}.default;
        });
      in
      {
        packages = forAllSystems ({ pkgs, logosSdk, logosProtocolPkg, logosQtHost, logosLiblogos, logosDesignSystem, logosViewModuleRuntime, logosQtMcp, ... }:
          let
            app = import ./nix/app.nix {
              inherit pkgs logosSdk logosProtocolPkg logosQtHost logosLiblogos logosDesignSystem logosViewModuleRuntime logosQtMcp;
              src = ./.;
            };
          in
          {
            inherit app;
            default = app;

            # Smoke test: validates binary starts and Qt/libs resolve correctly
            smoke-test = import ./nix/smoke-test.nix { inherit pkgs; appPkg = app; };

            # One-runtime symbol gate. This app loads THIRD-PARTY plugins
            # in-process, so a duplicate TokenManager here surfaces as refused
            # calls in someone else's plugin, with no build diagnostic.
            symbol-gate = import ./nix/symbol-gate.nix { inherit pkgs; appPkg = app; };

            # Negative control, shipped WITH the gate: plants a real duplicate
            # definer and asserts the gate rejects it. An absence assertion that
            # has never been seen to fail is indistinguishable from a broken one.
            symbol-gate-negative = import ./nix/symbol-gate.nix {
              inherit pkgs; appPkg = app; negativeControl = true;
            };

            # MCP server (Node.js) for connecting Claude Code / MCP clients
            mcp-server = logos-qt-mcp.packages.${pkgs.system}.mcp-server;

            # Full logos-qt-mcp package (includes test-framework, mcp-server, qt-plugin)
            # Use: nix build .#logos-qt-mcp -o result-mcp
            inherit logosQtMcp;
            logos-qt-mcp = logosQtMcp;
          }
        );

        checks = forAllSystems ({ pkgs, system, ... }: {
          smoke-test = self.packages.${system}.smoke-test;
          symbol-gate = self.packages.${system}.symbol-gate;
          symbol-gate-negative = self.packages.${system}.symbol-gate-negative;
        });

        apps = forAllSystems ({ pkgs, system, ... }:
          {
            default = {
              type = "app";
              program = "${self.packages.${system}.default}/bin/logos-standalone-app";
            };
          }
        );

        # Reusable test builder for UI plugin integration tests.
        # Usage: logos-standalone-app.lib.mkPluginTest { pkgs, pluginPkg, testFile, ... }
        lib = forAllSystems ({ pkgs, system, ... }:
          let
            standaloneApp = self.packages.${system}.default;
            logosQtMcp = logos-qt-mcp.packages.${system}.default;
          in {
            mkPluginTest = import ./nix/mkPluginTest.nix { inherit standaloneApp logosQtMcp; };
          }
        );

        devShells = forAllSystems ({ pkgs, logosSdk, logosProtocolPkg, logosQtHost, logosLiblogos, logosViewModuleRuntime, ... }: {
          default = pkgs.mkShell {
            nativeBuildInputs = [ pkgs.cmake pkgs.ninja pkgs.pkg-config ];
            buildInputs = [
              pkgs.qt6.qtbase
              pkgs.qt6.qtremoteobjects
              pkgs.zstd
              pkgs.krb5
              pkgs.abseil-cpp
            ];
            shellHook = ''
              export LOGOS_CPP_SDK_ROOT="${logosSdk}"
              export LOGOS_QT_HOST_ROOT="${logosQtHost}"
              export LOGOS_PROTOCOL_ROOT="${logosProtocolPkg}"
              export LOGOS_LIBLOGOS_ROOT="${logosLiblogos}"
              export LOGOS_VIEW_MODULE_RUNTIME_ROOT="${logosViewModuleRuntime}"
              echo "logos-standalone-app dev shell"
            '';
          };
        });
      };
  }
