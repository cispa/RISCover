{ pkgs, arch }:

pkgs.pkgsCross.${if arch == "aarch64" then "aarch64-multiplatform" else "riscv64"}
