{ arch, pkgs ? (import ./nix/get-pkgs.nix) }@args:

let
  pkgsCross = (import ./nix/get-pkgs-cross.nix { inherit pkgs arch; });
  pkgsCrossStatic = pkgsCross.pkgsStatic;
in
pkgsCrossStatic.mkShell {
  buildInputs = [
    (pkgsCrossStatic.musl.overrideAttrs (oldAttrs: {
      # patches = oldAttrs.patches or [] ++ [ ./musl.patch ];
    }))

    pkgsCrossStatic.libarchive
    pkgsCrossStatic.zlib
    pkgsCrossStatic.libiberty
  ];

  # bear for generating compile_commands.json
  nativeBuildInputs = [ pkgs.bear ];

  ARCH=arch;

  BINUTILS_A64_INC = "${pkgsCrossStatic.binutils-unwrapped.dev}/include";
  BINUTILS_A64_LIB = "${pkgsCrossStatic.binutils-unwrapped.lib}/lib";
}
