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
      nix-bundle-lgx.url = "github:logos-co/nix-bundle-lgx";
      nix-bundle-lgx.inputs.nixpkgs.follows = "logos-nix/nixpkgs";
    };

    outputs = { self, nixpkgs, logos-nix, logos-cpp-sdk, logos-liblogos, logos-design-system, logos-capability-module, nix-bundle-lgx }:
      let
        systems = [ "aarch64-darwin" "x86_64-darwin" "aarch64-linux" "x86_64-linux" ];
        forAllSystems = f: nixpkgs.lib.genAttrs systems (system: f {
          inherit system;
          pkgs = import nixpkgs { inherit system; };
          logosSdk = logos-cpp-sdk.packages.${system}.default;
          logosLiblogos = logos-liblogos.packages.${system}.default;
          logosDesignSystem = logos-design-system.packages.${system}.default;
          logosCapabilityModule = logos-capability-module.packages.${system}.default;
          bundleLgx = nix-bundle-lgx.bundlers.${system}.default;
        });
      in
      {
        packages = forAllSystems ({ pkgs, logosSdk, logosLiblogos, logosDesignSystem, logosCapabilityModule, bundleLgx, ... }:
          let
            capabilityModuleLgx = bundleLgx logosCapabilityModule;
            app = import ./nix/app.nix {
              inherit pkgs logosSdk logosLiblogos logosDesignSystem capabilityModuleLgx;
              src = ./.;
            };
          in
          {
            inherit app;
            default = app;
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
