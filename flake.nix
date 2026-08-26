{
    description = "Logos Standalone App — generic Qt shell for loading and testing Logos UI plugins";

    inputs = {
      logos-nix.url = "github:logos-co/logos-nix";
      nixpkgs.follows = "logos-nix/nixpkgs";
      logos-cpp-sdk.url = "github:logos-co/logos-cpp-sdk";
      logos-protocol.url = "github:logos-co/logos-protocol";
      logos-plugin-qt.url = "github:logos-co/logos-plugin-qt";
      logos-liblogos.url = "github:logos-co/logos-liblogos";
      logos-design-system.url = "github:logos-co/logos-design-system";
      logos-view-module-runtime.url = "github:logos-co/logos-view-module-runtime";
      logos-qt-mcp.url = "github:logos-co/logos-qt-mcp";
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
