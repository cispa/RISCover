Important: always make sure that the app installed correctly.
I had cases where the fuzzing client wasn't updated correctly.
You can just check the hash on the server.
Uninstalling the app and reinstalling it worked for me.
I suspect this to be due to no new version tag/timestamps being the same for the ELF or something.

## General design

The app is nothing fancy.
It just unpacks the packaged `lscpu` and fuzzing client binaries, adds `lscpu` to the path and executes the fuzzing client with a hardcoded ip address and port (we can also think of just hardcoding the server address and ip in the build stage of the client).
That can of course be changed.
The main code is `MainActivity.kt`.

## Building/testing the android app

Build the client beforehand. E.g.:

``` sh
cd .. # go to repository root
build-client --vector --floats --seq-len 1
# the client is now in result/diffuzz-client
cd android
```

### In dev environment (recommended)

``` sh
nix develop
# not sure if removing build directory is needed but doesn't hurt
rm -rf build && sudo cp ../result/diffuzz-client src/main/assets/diffuzz-client && gradle assembleDebug
                                                                         # or
                                                                         gradle installDebug
# the result is build/outputs/apk/debug/android-debug.apk
```

### With nix build (slow)

Slow because gradle sucks.
Every time a new gradle deamon needs to be started.

``` sh
# not sure if removing build directory is needed but doesn't hurt
rm -rf build && sudo cp ../result/diffuzz-client src/main/assets/diffuzz-client && nix build .#app
```

## Building lscpu (prebuilt included in repo)

``` sh
# update nixpkgs if needed
# nix flake update
nix build .#lscpu
sudo cp ./result src/main/assets/lscpu
```
