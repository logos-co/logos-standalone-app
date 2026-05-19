{
  description = "Integration tests for logos-standalone-app";

  inputs = {
    logos-standalone-app.url = "github:logos-co/logos-standalone-app";
    nixpkgs.follows = "logos-standalone-app/nixpkgs";
    logos-nix.follows = "logos-standalone-app/logos-nix";

    counter_qml.url = "github:logos-co/counter_qml";
    counter_qml.inputs.logos-nix.follows = "logos-nix";
    counter_qml.inputs.nixpkgs.follows = "nixpkgs";
  };

  outputs = { self, nixpkgs, logos-standalone-app, logos-nix, counter_qml }:
    let
      systems = [ "aarch64-darwin" "x86_64-darwin" "aarch64-linux" "x86_64-linux" ];
      forAllSystems = f: nixpkgs.lib.genAttrs systems (system: f {
        inherit system;
        pkgs = import nixpkgs { inherit system; };
      });
    in {
      checks = forAllSystems ({ system, pkgs }: {
        integration-test = logos-standalone-app.lib.${system}.mkPluginTest {
          inherit pkgs;
          pluginPkg = counter_qml.packages.${system}.default;
          testFiles = [ ./test-counter-qml.mjs ];
          name = "logos-standalone-app-integration-test";
        };
      });
    };
}
