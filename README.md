# RISCover — Differential CPU Fuzzing Framework

This repository contains the differential CPU fuzzing framework from the research paper "RISCover: Automatic Discovery of User-exploitable Architectural Security Vulnerabilities in Closed-Source RISC-V CPUs".

> [!NOTE]
> Find the artifacts for the paper [here](https://github.com/cispa/RISCover-artifacts).

> [!NOTE]
> This repository relies on [Git LFS](https://git-lfs.com/) to handle large files. Please install it **before** cloning the repository. If you cloned without having LFS installed or have other problems with LFS files run: `git lfs install && git lfs pull`.

The framework largely depends on Nix for package retrieval.
Refer to the [setup section](#setup) for instructions on how to install and load dependencies.

## Differential CPU Fuzzing

The differential fuzzing part of RISCover is split into [server](./server) and [client](./client).

### Server

The server generates inputs, receives outputs from the clients, compares them and logs reproducers on differences.
The server is implemented in Python and can be started like:
``` sh
python server/diffuzz-server.py --floats --seq-len 5 --generator RandomDiffFuzzGenerator
```

The server logs results in `results` as yaml reproducer files.
The file format is as follows:
```
results/reproducers/reproducer-00000100-000001218860.yaml
```
- `00000100`: 100th reproducer of the current run.
- `000001218860`: Difference (reproducer) occurred after 1218860 inputs.

To quickly skim over reproducers run:
``` sh
for dir in results/reproducers/*; do find "$dir" -maxdepth 1 -type f | head -n 1; done | xargs bat --style=header --line-range :50
```

### Client

The main client of RISCover is the `diffuzz-client`, implemented in [client/src/diffuzz-client.c](./client/src/diffuzz-client.c).
It runs instructions sequences and sends the resulting state to the server.

The client compilation depends on the flags used by the server.
To generate the set of build flags run:
```
python server/diffuzz-server.py --floats --seq-len 5 --generator RandomDiffFuzzGenerator --print-flags
```
This prints:
```
-DMAX_SEQ_LEN=5 -DWITH_REGS -DCOMPRESS_RECV -DFLOATS
```

This can be used to build the client like:
``` sh
build-client --build-flags "-DMAX_SEQ_LEN=5 -DWITH_REGS -DCOMPRESS_RECV -DFLOATS" --target diffuzz-client --out diffuzz-client
```
This produces a static binary `diffuzz-client` which can be copied to any Linux machine, including Android (e.g., via Termux).

The client can then be started like:
``` sh
./diffuzz-client <server-ip> <server-port>
```

### Reproducers

Reproducer files (yaml) can be compiled to static binaries for quick reproduction.
The framework provides two main ways to use these reproducers:
* `repro-runner`: A statically compiled binary that can load reproducers and re-execute them.
                  Good for quick testing, without compilation.
* C reproducers: C programs which can be modified and statically compiled.
                 Better for debugging a reproducer.

The reproducer binaries automatically pin to the correct microarchitecture, but inform about further actions to take.
They follow a similar command line interface pattern:
```
Usage: repro-runner/repro [core N | midr 0x.... | uarch NAME]

Arguments:
  core N       Use logical core index N (decimal).
  midr 0x....  Use cores with this MIDR (hex or decimal).
  uarch NAME   Use microarch by name (e.g., Cortex-X4).
```

#### Repro-runner

The `repro-runner` can be compiled like this (ensure that you match the arguments of fuzzer compilation):
``` sh
build-client --no-auto-map-mem --vector --seq-len 100 --target repro-runner --out repro-runner
```

You can then run and reproduce reproducers (yaml) using:
``` sh
./repro-runner exfilstate-results/4108d034/04_04/073_097_1_2_00315_00020.yaml
```

#### Statically Compiled C Reproducers

The framework provides multiple commands for making a C program out of a reproducer file and compiling that to a static binary.

1. `init-repro-template` produces a C program that can be compiled and run:
``` sh
init-repro-template results/reproducers/reproducer-00000100-000001218860.yaml
```
Creates a file `results/reproducers/reproducer-00000100-000001218860.c`.

2. `build-repro` compiles such C programs into static binaries:
``` sh
build-repro results/reproducers/reproducer-00000100-000001218860.c
```
This produces a static binary `results,reproducers,reproducer-00000100-000001218860`.

3. `build-repro-run-on` allows for quickly building and running on a machine using ssh:
``` sh
build-repro-run-on results/reproducers/reproducer-00000100-000001218860.c -- <ssh/hostname>
```

4. `init-build-run-on` allows for quickly initializing, building and running on a machine using ssh:
``` sh
init-build-run-on results/reproducers/reproducer-00000100-000001218860.yaml -- -- <ssh/hostname>
```

For all these scripts the following flags can be used:
- `--orig-seq`: Use the original bytes as sequence and not assemble the recovered assembly instructions.
- `--no-sig`: Build the reproducer without signal handling, i.e., plain.

### Other Flags

The server accepts various flags, which can be inspected by calling:
``` sh
python server/diffuzz-server.py --help
```

> [!IMPORTANT]
> For the offline sequence generation, a server that can execute RISC-V binaries is needed.
> binfmt is a good option for non-RISC-V machines.

The most relevant ones are:
- `--filter-thead`: Filter out halting T-Head sequences and GhostWrite to not crash the machines.
- `--print-flags`: Print the flags needed to be used to compile the client via `build-client --build-flags "<FLAGS>"`.
- `--seq-len`: The used sequence length.
- `--weighted`: If weighted generation should be used.
- `--floats`: If FP-support should be enabled.
- `--vector`: If vector-support should be enabled.
- `--seed`: The RNG seed.
- `--generator`: The generator, `RandomDiffFuzzGenerator` for on-server, `OfflineRandomDiffFuzzGenerator` for on-device generation.

## Undocumented Instruction Enumeration

The undocumented instruction enumeration pass of RISCover is implemented in [undoc-enum.c](client/src/undoc-enum.c).

Build the binary with the following command:
``` sh
build-client --no-check-mem --floats --vector --seq-len 1 --target undoc-enum --out undoc-enum
```

Then you can copy the static binary to a RISC-V machine and run the scan.
Results are logged to `undoc-enum-results`.
Each worker (core) stores discoveries into a compressed archive.

## Setup

The framework uses [Nix](https://nixos.org/) to easily provide the required packages for the various parts.
Nix is a declarative package manager, i.e., needed packages can be specified in a file, locked via a lock file and then loaded reproducibly.
Each directory contains a `flake.nix` or `shell.nix`, specifying the packages and environment.
Nix can be installed on a large range of Linux distributions.
If your distribution is not supported you can fall back to the Docker container from the [artifact repo](https://github.com/cispa/RISCover-artifacts).

### Installation

1. Please install [Nix](https://nixos.org/download/).
   We recommend the multi-user installation if root privileges are available.

2. We further use an experimental Nix feature called Flakes.
   This feature needs to be enabled by running the following command:
   ```sh
   mkdir -p ~/.config/nix && echo 'experimental-features = nix-command flakes' > ~/.config/nix/nix.conf
   ```

3. Quickly verify that Nix Flakes work by running:
   ```sh
   nix run 'nixpkgs#hello'
   ```
   This should print "Hello, world!".

4. For automatically loading the environment when you navigate to this repository we use [direnv](https://direnv.net/).
   With Nix already installed it can be simply installed by running:
   ```sh
   nix profile add 'nixpkgs#direnv'
   ```
   For other package managers refer to [this page](https://direnv.net/docs/installation.html).

5. Please also setup your shell to automatically load the environment as specified [here](https://direnv.net/docs/hook.html).

### Loading environment & Verification

Run  `direnv allow` to load the environment.
Note that this can take a while because we customize packages.
Once the environment is loaded, it is cached, so loading will be instant afterwards.
This should expand `FRAMEWORK_ROOT` and load the needed packages.

To verify the setup, run:
``` sh
echo $FRAMEWORK_ROOT # expected: /absolute/path/to/the/repository
echo $CC             # expected: riscv64-unknown-linux-musl-gcc
```
