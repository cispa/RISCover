# NOTE: nix build does not support arguments, therefore we import nixpkgs from the flake
import (builtins.getFlake (toString ./../..)).inputs.nixpkgs {}
