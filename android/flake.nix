{
  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    nixpkgs-stable.url = "github:NixOS/nixpkgs/nixos-24.05";
    flake-utils.url = "github:numtide/flake-utils";
    android.url = "github:tadfisher/android-nixpkgs";
    gradle2nix.url = "github:expenses/gradle2nix?ref=overrides-fix";
  };

  outputs = { self, nixpkgs, nixpkgs-stable, flake-utils, android, gradle2nix }:
    {
      overlay = final: prev: {
        inherit (self.packages.${final.system}) android-sdk android-studio;
      };
    }
    //
    flake-utils.lib.eachSystem [ "x86_64-linux" ] (system:
      let
        pkgs = import nixpkgs {
          inherit system;
          config.allowUnfree = true;
          overlays = [
            self.overlay
          ];
        };

        pkgsStable = import nixpkgs-stable { inherit system; };

        android-sdk = android.sdk.${system} (sdkPkgs: with sdkPkgs; [
          # Useful packages for building and testing.
          build-tools-32-0-0
          cmdline-tools-latest
          emulator
          platform-tools
          platforms-android-32

          # Other useful packages for a development environment.
          # sources-android-30
          # system-images-android-30-google-apis
          # system-images-android-30-google-apis-playstore
        ]);

        gradle = ( pkgs.writeShellScriptBin "gradle" ''
              exec env "ORG_GRADLE_PROJECT_android.aapt2FromMavenOverride"="${android-sdk}/share/android-sdk/build-tools/32.0.0/aapt2" '${pkgs.gradle_7}/bin/gradle' $@
              '');
      in
      {
        packages = {
          inherit android-sdk;

          # android-studio = pkgs.androidStudioPackages.stable;
          # android-studio = pkgs.androidStudioPackages.beta;
          # android-studio = pkgs.androidStudioPackages.preview;
         # android-studio = pkgs.androidStudioPackage.canary;
        };

        devShell = import ./devshell.nix { inherit pkgs; };

        # NOTE: the lockfile can be generated with
        # nix run 'github:expenses/gradle2nix?ref=e54eff2fad6ef319e7d23b866900b3a1951bdbc7' -- --gradle-wrapper 7.4.2
        # (github:expenses/gradle2nix?ref=overrides-fix)
        packages.app = gradle2nix.builders.x86_64-linux.buildGradlePackage {
          gradlePackage = gradle;

          pname = "fuzzer-wrapped";
          version = "3.0.0";
          lockFile = ./gradle.lock;

          src = pkgs.lib.cleanSourceWith {
            filter = pkgs.lib.cleanSourceFilter;
            src = pkgs.lib.cleanSourceWith {
              filter =
                path: type:
                let
                  baseName = baseNameOf path;
                in
                !(
                  (type == "directory" && (baseName == "build" || baseName == ".idea" || baseName == ".gradle"))
                  || (pkgs.lib.hasSuffix ".iml" baseName)
                );
              src = ./.;
            };
          };

          ANDROID_HOME = "${android-sdk}/share/android-sdk";
          ANDROID_SDK_ROOT= "${android-sdk}/share/android-sdk";
          JAVA_HOME= pkgs.jdk11.home;

          buildInputs = [
            android-sdk
          ];

          # NOTE: this is in fixupPhase because installPhase is used by buildGradlePackage
          fixupPhase = ''
            mkdir -p $out
            cp build/outputs/apk/debug/source-debug.apk $out
          '';

          gradleInstallFlags = [ "assembleDebug" ];
        };

        packages.lscpu = pkgsStable.stdenvNoCC.mkDerivation {
          name = "lscpu";
          dontUnpack = true;
          buildPhase = ''
            cp ${pkgsStable.pkgsCross.aarch64-multiplatform.pkgsStatic.util-linux}/bin/lscpu $out
          '';
        };
      }
    );
}
