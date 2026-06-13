{
  description = "Integration tests for logos-standalone-app";

  inputs = {
    # Pinned to this branch's head (not plain master) on purpose: the Qt-split
    # chain made the base SDK Qt-free, exporting logos-cpp-sdk::logos_headers.
    # master still pins the pre-split cpp-sdk (no such target), and a stale lock
    # here resolved that old SDK for the integration build — find_package then
    # failed to define logos_headers. This pin gives the committed lock the
    # correct (0.2.0) cpp-sdk regardless of whether `--override-input
    # logos-standalone-app path:.` re-locks transitive inputs (Nix-version
    # dependent). REVERT to "github:logos-co/logos-standalone-app" once this
    # branch is merged to master and master carries the Qt-split SDK pins.
    logos-standalone-app.url = "github:logos-co/logos-standalone-app/dd03047c28087c5c41a150e610ad75b4de6d81ab";
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
