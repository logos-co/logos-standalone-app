{
    description = "Logos Standalone App — generic Qt shell for loading and testing Logos UI plugins";

    inputs = {
      logos-nix.url = "github:logos-co/logos-nix";
      nixpkgs.follows = "logos-nix/nixpkgs";
      logos-cpp-sdk.url = "github:logos-co/logos-cpp-sdk";
      logos-liblogos.url = "github:logos-co/logos-liblogos";
      logos-liblogos.inputs.nixpkgs.follows = "logos-nix/nixpkgs";
      logos-design-system.url = "github:logos-co/logos-design-system";
      logos-capability-module.url = "github:logos-co/logos-capability-module";
      logos-capability-module.inputs.nixpkgs.follows = "logos-nix/nixpkgs";
      logos-capability-module.inputs.logos-cpp-sdk.follows = "logos-cpp-sdk";
      logos-view-module-runtime.url = "github:logos-co/logos-view-module-runtime";
      logos-view-module-runtime.inputs.nixpkgs.follows = "logos-nix/nixpkgs";
      logos-view-module-runtime.inputs.logos-cpp-sdk.follows = "logos-cpp-sdk";
      nix-bundle-lgx.url = "github:logos-co/nix-bundle-lgx";
      nix-bundle-lgx.inputs.nixpkgs.follows = "logos-nix/nixpkgs";
      logos-qt-mcp.url = "github:logos-co/logos-qt-mcp";
      logos-qt-mcp.inputs.nixpkgs.follows = "logos-nix/nixpkgs";
    };

    outputs = { self, nixpkgs, logos-nix, logos-cpp-sdk, logos-liblogos, logos-design-system, logos-capability-module, logos-view-module-runtime, nix-bundle-lgx, logos-qt-mcp }:
      let
        systems = [ "aarch64-darwin" "x86_64-darwin" "aarch64-linux" "x86_64-linux" ];
        forAllSystems = f: nixpkgs.lib.genAttrs systems (system: f {
          inherit system;
          pkgs = import nixpkgs { inherit system; };
          logosSdk = logos-cpp-sdk.packages.${system}.default;
          logosLiblogos = logos-liblogos.packages.${system}.default;
          logosDesignSystem = logos-design-system.packages.${system}.default;
          logosCapabilityModule = logos-capability-module.packages.${system}.default;
          logosViewModuleRuntime = logos-view-module-runtime.packages.${system}.default;
          logosQtMcp = logos-qt-mcp.packages.${system}.default;
          bundleLgx = nix-bundle-lgx.bundlers.${system}.default;
        });
      in
      {
        packages = forAllSystems ({ pkgs, logosSdk, logosLiblogos, logosDesignSystem, logosCapabilityModule, logosViewModuleRuntime, logosQtMcp, bundleLgx, ... }:
          let
            capabilityModuleLgx = bundleLgx logosCapabilityModule;
            app = import ./nix/app.nix {
              inherit pkgs logosSdk logosLiblogos logosDesignSystem logosViewModuleRuntime logosQtMcp capabilityModuleLgx;
              src = ./.;
            };
          in
          {
            inherit app;
            default = app;

            # MCP server (Node.js) for connecting Claude Code / MCP clients
            mcp-server = logos-qt-mcp.packages.${pkgs.system}.mcp-server;

            # Full logos-qt-mcp package (includes test-framework, mcp-server, qt-plugin)
            # Use: nix build .#logos-qt-mcp -o result-mcp
            inherit logosQtMcp;
            logos-qt-mcp = logosQtMcp;
          }
        );

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

        devShells = forAllSystems ({ pkgs, logosSdk, logosLiblogos, logosViewModuleRuntime, ... }: {
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
              export LOGOS_LIBLOGOS_ROOT="${logosLiblogos}"
              export LOGOS_VIEW_MODULE_RUNTIME_ROOT="${logosViewModuleRuntime}"
              echo "logos-standalone-app dev shell"
            '';
          };
        });
      };
  }
