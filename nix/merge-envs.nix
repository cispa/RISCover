# https://gist.github.com/adisbladis/2a44cded73e048458a815b5822eea195
# NOTE: we pass in packages so that we can use pkgsCross as base (so that CC is set)
pkgs: envs: pkgs.mkShell (builtins.foldl' (a: v: {
  buildInputs = (a.buildInputs or []) ++ (v.buildInputs or []);
  nativeBuildInputs = (a.nativeBuildInputs or []) ++ (v.nativeBuildInputs or []);
  propagatedBuildInputs = (a.propagatedBuildInputs or []) ++ (v.propagatedBuildInputs or []);
  propagatedNativeBuildInputs = (a.propagatedNativeBuildInputs or []) ++ (v.propagatedNativeBuildInputs or []);
  shellHook =
    (a.shellHook or "")
    + (if (a.shellHook or "") != "" && (v.shellHook or "") != "" then "\n" else "")
    + (v.shellHook or "");
  ARCH = a.ARCH or v.ARCH;

  # forward if set in any env
  BINUTILS_A64_INC = a.BINUTILS_A64_INC or v.BINUTILS_A64_INC;
  BINUTILS_A64_LIB = a.BINUTILS_A64_LIB or v.BINUTILS_A64_LIB;
}) (pkgs.mkShellNoCC {}) envs)
