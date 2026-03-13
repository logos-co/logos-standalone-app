{
    description = "Logos Standalone App — generic Qt shell for loading and testing Logos UI plugins";

    inputs = {
      nixpkgs.follows = "logos-liblogos/nixpkgs";
      logos-cpp-sdk.url = "github:logos-co/logos-cpp-sdk";
      logos-liblogos.url = "github:logos-co/logos-liblogos";
      logos-design-system.url = "github:logos-co/logos-design-system";
      logos-capability-module.url = "github:logos-co/logos-capability-module";
      logos-capability-module.inputs.logos-liblogos.follows = "logos-liblogos";
      logos-capability-module.inputs.logos-cpp-sdk.follows = "logos-cpp-sdk";
    };

    outputs = { self, nixpkgs, logos-cpp-sdk, logos-liblogos, logos-design-system, logos-capability-module }:
      let
        systems = [ "aarch64-darwin" "x86_64-darwin" "aarch64-linux" "x86_64-linux" ];
        forAllSystems = f: nixpkgs.lib.genAttrs systems (system: f {
          inherit system;
          pkgs = import nixpkgs { inherit system; };
          logosSdk = logos-cpp-sdk.packages.${system}.default;
          logosLiblogos = logos-liblogos.packages.${system}.default;
          logosDesignSystem = logos-design-system.packages.${system}.default;
          logosCapabilityModule = logos-capability-module.packages.${system}.default;
        });
      in
      {
        packages = forAllSystems ({ pkgs, logosSdk, logosLiblogos, logosDesignSystem, logosCapabilityModule, ... }:
          let
            app = import ./nix/app.nix {
              inherit pkgs logosSdk logosLiblogos logosDesignSystem logosCapabilityModule;
              src = ./.;
            };
          in
          {
            app = app;
            default = app;
          }
        );

        apps = forAllSystems ({ pkgs, system, ... }:
          {
            default = {
              type = "app";
              program = "${self.packages.${system}.default}/bin/logos-standalone";
            };
          }
        );

        devShells = forAllSystems ({ pkgs, logosSdk, logosLiblogos, ... }: {
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
              echo "logos-standalone-app dev shell"
            '';
          };
        });
      };
  }
