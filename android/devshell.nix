{ pkgs }:

with pkgs;

mkShell {
  name = "android-building-shell";

  ANDROID_HOME="${android-sdk}/share/android-sdk";
  ANDROID_SDK_ROOT="${android-sdk}/share/android-sdk";
  JAVA_HOME = jdk11.home;

  packages = [
    android-sdk
    (pkgs.writeShellScriptBin "gradle" ''
      exec env "ORG_GRADLE_PROJECT_android.aapt2FromMavenOverride"="${android-sdk}/share/android-sdk/build-tools/32.0.0/aapt2" '${gradle}/bin/gradle' $@
      '')
  ];
}
