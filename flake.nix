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
      logos-qt-sdk = {
        url = "github:logos-co/logos-qt-sdk";
        inputs.logos-nix.follows = "logos-nix";
        inputs.nixpkgs.follows = "nixpkgs";
        inputs.logos-protocol.follows = "logos-protocol";
        inputs.logos-cpp-sdk.follows = "logos-cpp-sdk";
      };
      logos-liblogos.url = "github:logos-co/logos-liblogos";
      logos-design-system.url = "github:logos-co/logos-design-system";
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

    outputs = { self, nixpkgs, logos-nix, logos-cpp-sdk, logos-protocol, logos-qt-sdk, logos-liblogos, logos-design-system, logos-view-module-runtime, logos-qt-mcp }:
      let
        systems = [ "aarch64-darwin" "x86_64-darwin" "aarch64-linux" "x86_64-linux" ];
        forAllSystems = f: nixpkgs.lib.genAttrs systems (system: f {
          inherit system;
          pkgs = import nixpkgs { inherit system; };
          logosSdk = logos-cpp-sdk.packages.${system}.default;
          logosProtocolPkg = logos-protocol.packages.${system}.default;
          logosQtSdk = logos-qt-sdk.packages.${system}.default;
          logosLiblogos = logos-liblogos.packages.${system}.default;
          logosDesignSystem = logos-design-system.packages.${system}.default;
          logosViewModuleRuntime = logos-view-module-runtime.packages.${system}.default;
          logosQtMcp = logos-qt-mcp.packages.${system}.default;
        });
      in
      {
        packages = forAllSystems ({ pkgs, logosSdk, logosProtocolPkg, logosQtSdk, logosLiblogos, logosDesignSystem, logosViewModuleRuntime, logosQtMcp, ... }:
          let
            app = import ./nix/app.nix {
              inherit pkgs logosSdk logosProtocolPkg logosQtSdk logosLiblogos logosDesignSystem logosViewModuleRuntime logosQtMcp;
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

        devShells = forAllSystems ({ pkgs, logosSdk, logosProtocolPkg, logosQtSdk, logosLiblogos, logosViewModuleRuntime, ... }: {
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
              export LOGOS_QT_SDK_ROOT="${logosQtSdk}"
              export LOGOS_PROTOCOL_ROOT="${logosProtocolPkg}"
              export LOGOS_LIBLOGOS_ROOT="${logosLiblogos}"
              export LOGOS_VIEW_MODULE_RUNTIME_ROOT="${logosViewModuleRuntime}"
              echo "logos-standalone-app dev shell"
            '';
          };
        });
      };
  }
