{
  description = "Integration tests for logos-standalone-app";

  inputs = {
    # Locking this input copies the app's own flake.lock into ours, so the
    # integration build gets the same cpp-sdk / design system / protocol set as
    # `nix build .#default`. CI overrides the source anyway (`--override-input
    # logos-standalone-app path:.`) and, from Nix 2.26 on, that re-locks the
    # app's inputs from the checkout's flake.lock — so what is committed here
    # only governs a plain `nix build path:./tests#...`. Re-run `nix flake
    # update` when it drifts too far behind master.
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
