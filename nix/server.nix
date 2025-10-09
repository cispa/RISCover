{ pkgs ? import <nixpkgs> {} }:

with pkgs;
mkShellNoCC {
  nativeBuildInputs = [
    (pkgs.python3.withPackages (pp: with pp; [
      capstone
      keystone-engine
      pyyaml
      tabulate
      numpy
      json5
      pwntools
      scipy
      gitpython
      xmltodict
    ]))
  ];

  # NOTE: This entire shell hook assumes that you start the environment from
  # the directory where the flake.nix resides. There is currently no other
  # way in pure mode to pass in the root of the framework.
  shellHook = ''
    source env.sh || exit 1
  '';
}
