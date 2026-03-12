{
  description = "Logos Standalone App — generic Qt shell for loading and testing Logos UI plugins";

  inputs = {
    nixpkgs.follows = "logos-liblogos/nixpkgs";
    logos-cpp-sdk.url = "github:logos-co/logos-cpp-sdk";
    logos-liblogos.url = "github:logos-co/logos-liblogos";
  };

  outputs = { self, nixpkgs, logos-cpp-sdk, logos-liblogos }:
    let
      systems = [ "aarch64-darwin" "x86_64-darwin" "aarch64-linux" "x86_64-linux" ];
      forAllSystems = f: nixpkgs.lib.genAttrs systems (system: f {
        inherit system;
        pkgs = import nixpkgs { inherit system; };
        logosSdk = logos-cpp-sdk.packages.${system}.default;
        logosLiblogos = logos-liblogos.packages.${system}.default;
      });
    in
    {
      packages = forAllSystems ({ pkgs, logosSdk, logosLiblogos, ... }:
        let
          app = import ./nix/app.nix {
            inherit pkgs logosSdk logosLiblogos;
            src = ./.;
          };
        in
        {
          app = app;
          default = app;
        }
      );

      # `nix run github:logos-co/logos-standalone-app -- <plugin.so> [options]`
      apps = forAllSystems ({ pkgs, system, ... }:
        let
          app = self.packages.${system}.default;
        in
        {
          default = {
            type = "app";
            program = "${app}/bin/logos-standalone";
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
            echo "  LOGOS_CPP_SDK_ROOT = $LOGOS_CPP_SDK_ROOT"
            echo "  LOGOS_LIBLOGOS_ROOT = $LOGOS_LIBLOGOS_ROOT"
          '';
        };
      });
    };
}
