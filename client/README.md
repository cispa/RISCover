# Client

This is the source directory of client-related code.
It contains the source files, a script to build the client and other files.

## Building

NOTE: details on how to build the android app are in [../android](../android/README.md).

To build the client:
``` sh
./build-client --seq-len 1 [--vector] [--verbose] [--build-flags "-DOTHER_FLAG"] [--repro repro.c]
```

To get an isolated shell with the build environment:
``` sh
nix-shell
```

To get your shell with the build environment:
``` sh
nix-shell --command "$SHELL"
```

### Test client reproducibility

``` sh
./build-client --seq-len 10
# This should then report the same nix store path hash and the same ELF hash
./build-client --seq-len 10 --check
```
