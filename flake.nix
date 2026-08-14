{
    description = "Logos Standalone App — generic Qt shell for loading and testing Logos UI plugins";

    inputs = {
      logos-nix.url = "github:logos-co/logos-nix";
      nixpkgs.follows = "logos-nix/nixpkgs";
      logos-cpp-sdk.url = "github:logos-co/logos-cpp-sdk";
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
      # Pinned to a rev rather than the branch head because logos-qt-host does
      # not exist on logos-plugin-qt's master yet (it arrives with B1). Drop the
      # rev once that merges. Same rev logos-liblogos pins, deliberately: the
      # app and liblogos_core share TokenManager and the transport ABI in ONE
      # process, so the host runtime must be a single copy across that boundary.
      logos-plugin-qt = {
        url = "github:logos-co/logos-plugin-qt/8ccb1fc81642ee52e843b69ac3f90a1ec7084299";
        inputs.logos-nix.follows = "logos-nix";
        inputs.nixpkgs.follows = "nixpkgs";
        inputs.logos-protocol.follows = "logos-protocol";
      };
      logos-liblogos.url = "github:logos-co/logos-liblogos";
      logos-design-system.url = "github:logos-co/logos-design-system";
      logos-capability-module.url = "github:logos-co/logos-capability-module";
      logos-view-module-runtime.url = "github:logos-co/logos-view-module-runtime";
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
