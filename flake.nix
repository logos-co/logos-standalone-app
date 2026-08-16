{
    description = "Logos Standalone App — generic Qt shell for loading and testing Logos UI plugins";

    inputs = {
      logos-nix.url = "github:logos-co/logos-nix";
      nixpkgs.follows = "logos-nix/nixpkgs";
      # Rev-pinned, not branch-head: a04b278 lives on logos-cpp-sdk's
      # feat/sdk-codegen-b3-d11, not on its master. Leaving the URL unpinned
      # would let `nix flake update` silently walk this back to master and drop
      # logos_host_services.h plus the cdylib grant export.
      logos-cpp-sdk.url = "github:logos-co/logos-cpp-sdk/a04b27888e1d126578f639ed46dae0c777990a10";
      # Same reasoning: c8bab12 (the per-client token store) is a BRANCH rev,
      # feat/per-client-token-store, so the rev belongs in the URL. It was
      # already the locked rev; pinning it in the URL is what makes it stay.
      logos-protocol = {
        url = "github:logos-co/logos-protocol/c8bab12834dbf92155b483546875e6078d17c74e";
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
      # Pinned to a rev rather than the branch head because logos-qt-host does
      # not exist on logos-plugin-qt's master yet (it arrives with B1). Drop the
      # rev once that merges. Same rev logos-liblogos pins, deliberately: the
      # app and liblogos_core share TokenManager and the transport ABI in ONE
      # process, so the host runtime must be a single copy across that boundary.
      #
      # Raised 8ccb1fc -> cc24fa1c, tracking logos-liblogos: cc24fa1c is the tip
      # of logos-plugin-qt's feat/b4-qt-host-windows-target and is exactly what
      # logos-liblogos f2a15ef3 pins. The sibling branch
      # feat/b4-qt-host-windows-target-8ccb1fc (989f6ae) is a REDUCED
      # re-baselining of the same work onto 8ccb1fc and is NOT an ancestor of
      # cc24fa1c — taking it here would put a second logos-qt-host build in the
      # closure, i.e. two LogosAPI/TokenManager copies in one process.
      logos-plugin-qt = {
        url = "github:logos-co/logos-plugin-qt/cc24fa1c0c43b2d96c1dc165ee545a0321318b59";
        inputs.logos-nix.follows = "logos-nix";
        inputs.nixpkgs.follows = "nixpkgs";
        inputs.logos-protocol.follows = "logos-protocol";
      };
      # f2a15ef3 lives on logos-liblogos's fix/b4-align-protocol-with-qt-host:
      # it is the rev that takes the Qt host runtime from logos-qt-host too, so
      # this app and liblogos_core agree on the single host-runtime copy above.
      logos-liblogos.url = "github:logos-co/logos-liblogos/f2a15ef3022d8fb71dac3d612c8edec839fc51e7";
      logos-design-system.url = "github:logos-co/logos-design-system";
      # DELIBERATELY still master (0cb33fb), NOT feat/universal-capability's
      # fc39b1b — and this is the one input on this branch that is knowingly a
      # step behind. fc39b1b's src/capability_module_impl.cpp includes
      # <logos_host_services.h>, which exists only in logos-cpp-sdk b9d7641 and
      # later, i.e. only on feat/sdk-codegen-b3-d11. But capability_module's
      # sole input is an UNPINNED logos-module-builder, so it locks to builder
      # master (9d3b7cc) -> logos-cpp-sdk master (e3744fb), which predates that
      # header: it fails to compile, full stop. Forcing that nested cpp-sdk
      # forward does not help either — builder 9d3b7cc still invokes
      # `logos-cpp-generator --provider-header`, which cpp-sdk 1017aa5 removed.
      #
      # That is the capability-module -> module-builder -> standalone-app ->
      # capability-module cycle, and THIS repo is where it is broken: builder
      # takes logos-standalone-app as an input, so a red app here would keep
      # the builder red and the cycle would never close. So the app pins a
      # capability_module that builds, the builder repoints onto the new
      # cpp-sdk, and capability_module is re-pinned onto that builder after.
      # Raise this to fc39b1b (or its successor) once the builder has landed.
      #
      # 0cb33fb is also exactly what logos-liblogos f2a15ef3 pins, so the
      # bundled .lgx and liblogos agree on one capability_module.
      logos-capability-module.url = "github:logos-co/logos-capability-module";
      # 5510acd is on logos-view-module-runtime's feat/sdk-codegen-b4-qt-host —
      # a branch rev, hence the URL pin.
      logos-view-module-runtime.url = "github:logos-co/logos-view-module-runtime/5510acd9eb7fcd49e420c9e530679edfa8f315ab";
      nix-bundle-lgx.url = "github:logos-co/nix-bundle-lgx";
      logos-qt-mcp.url = "github:logos-co/logos-qt-mcp";
    };

    outputs = { self, nixpkgs, logos-nix, logos-cpp-sdk, logos-protocol, logos-plugin-qt, logos-liblogos, logos-design-system, logos-capability-module, logos-view-module-runtime, nix-bundle-lgx, logos-qt-mcp }:
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
          logosCapabilityModule = logos-capability-module.packages.${system}.default;
          logosViewModuleRuntime = logos-view-module-runtime.packages.${system}.default;
          logosQtMcp = logos-qt-mcp.packages.${system}.default;
          bundleLgx = nix-bundle-lgx.bundlers.${system}.default;
        });
      in
      {
        packages = forAllSystems ({ pkgs, logosSdk, logosProtocolPkg, logosQtHost, logosLiblogos, logosDesignSystem, logosCapabilityModule, logosViewModuleRuntime, logosQtMcp, bundleLgx, ... }:
          let
            capabilityModuleLgx = bundleLgx logosCapabilityModule;
            app = import ./nix/app.nix {
              inherit pkgs logosSdk logosProtocolPkg logosQtHost logosLiblogos logosDesignSystem logosViewModuleRuntime logosQtMcp capabilityModuleLgx;
              src = ./.;
            };
          in
          {
            inherit app;
            default = app;

            # Smoke test: validates binary starts and Qt/libs resolve correctly
            smoke-test = import ./nix/smoke-test.nix { inherit pkgs; appPkg = app; };

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
