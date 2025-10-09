# NOTE: args are just passed to ../default.nix so look there
{ target ? "diffuzz-client", pkgs ? (import ./get-pkgs.nix), ... }@args:

let
  binary = (pkgs.callPackage ../default.nix args);
  pkgsCross = (import ./get-pkgs-cross.nix { inherit pkgs; inherit (args) arch; });
in
pkgs.stdenv.mkDerivation {
  name = "portable-${target}";
  phases = [ "buildPhase" ];

  buildPhase = ''
    mkdir -p $out
    # Copy out fuzzer build artefacts too
    cp ${binary}/* $out

    echo 'base64 -d - <<EOF > ${target}' > $out/portable-${target}.sh
    base64 -w 120 ${binary}/${target}  >> $out/portable-${target}.sh
    echo 'EOF' >> $out/portable-${target}.sh
    echo 'chmod +x ${target}' >> $out/portable-${target}.sh

    echo 'base64 -d - <<EOF > lscpu' >> $out/portable-${target}.sh
    base64 -w 120 ${pkgsCross.pkgsStatic.util-linux}/bin/lscpu >> $out/portable-${target}.sh
    echo 'EOF' >> $out/portable-${target}.sh
    echo 'chmod +x lscpu' >> $out/portable-${target}.sh


    echo 'PATH=$PATH:`pwd` ./${target} $@' >> $out/portable-${target}.sh

    chmod +x $out/portable-${target}.sh
  '';
}
