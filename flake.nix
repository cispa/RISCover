{
  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-24.11";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils, ... }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = nixpkgs.legacyPackages.${system};

        mergeEnvs = import ./nix/merge-envs.nix;

        tools = (pkgs.mkShellNoCC {
          # NOTE: You can add language servers or other tools here.
          nativeBuildInputs = with pkgs; [ python3Packages.python-lsp-server clang-tools yq bat ];
        });

        cross-env = pkgsCross: arch: (pkgsCross.mkShell {
          ARCH = arch;
        });

        makeDockerImage = drv: pkgs.dockerTools.streamNixShellImage {
          name = "diffuzz-client-shell";
          tag = "latest";
          inherit drv;
        };

        client-env = arch: (import ./client/shell.nix { inherit arch pkgs; } );
      in
      rec {
        devShells.tools = tools;

        devShells.server-python-only = (import ./nix/server.nix { inherit pkgs; });
        devShells.server-aarch64 = mergeEnvs pkgs.pkgsCross.aarch64-multiplatform (with devShells; [cross-env-aarch64 server-python-only]);
        devShells.server-riscv64 = mergeEnvs pkgs.pkgsCross.riscv64 (with devShells; [cross-env-riscv64 server-python-only]);

        devShells.all-aarch64 = mergeEnvs pkgs.pkgsCross.aarch64-multiplatform (with devShells; [client-env-aarch64 server-aarch64 tools]);
        devShells.all-riscv64 = mergeEnvs pkgs.pkgsCross.riscv64 (with devShells; [client-env-riscv64 server-riscv64 tools]);

        devShells.client-env-aarch64 = (client-env "aarch64");
        devShells.client-env-riscv64 = (client-env "riscv64");

        devShells.cross-env-aarch64 = (cross-env pkgs.pkgsCross.aarch64-multiplatform "aarch64");
        devShells.cross-env-riscv64 = (cross-env pkgs.pkgsCross.riscv64 "riscv64");

        packages.docker-server-aarch64 = makeDockerImage devShells.server-aarch64;
        packages.docker-server-riscv64 = makeDockerImage devShells.server-riscv64;
        packages.docker-all-aarch64 = makeDockerImage devShells.all-aarch64;
        packages.docker-all-riscv64 = makeDockerImage devShells.all-riscv64;

        devShells.default = devShells.all-riscv64;
        packages.default = packages.docker-all-riscv64;
      });
}
