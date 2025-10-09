# Server

This is the source directory for all server related parts.
Note though that a lot of additional code lives in [../pyutils](../pyutils) for legacy reasons and because some of that code needs to be shared between CLI interfaces such as disassembly/assembly and the server.

The server needs to be started from the framework [root directory](../), therefore instructions on how to obtain a shell with dependencies can be found in that [top-level Readme](../README.md).

The server has multiple frontends (CLI) which all fulfill different tasks:
- [`diffuzz-server.py`](#difffuzz)
- [`undocfuzz-scan.py`](#undocfuzz-scan)
- [`run_on.py`](#run-on)
- [`list_unstable_instructions.py`](#list-unstable-instructions)
- [`docfuzz.py`](#docfuzz)

They are explained in more detail below.
These frontends are just plain python files that can be called with the python interpreter.
Note though that they have to be called from the top-level framework directory.

All those frontends share a common command line interface implemented in [`shared_logic.py`](../pyutils/shared_logic.py):
``` sh
python3 server/<interface>.py [-h] [--verbose] [--expect EXPECT] --port PORT
                              [--group-by {hostname+microarch,hostname,none}] [--print-flags] [--print-files] [--no-check-mem] [--no-auto-map-mem] [--vector]
                              [--floats] [--meta] [--single-client]
```

TODO: document what those do. @Daniel pls just ask if you have questions. Here is a quick explanation of the most important (non-trivial) ones:
- `expect`: on how many machine connections the server should wait. This is the number of machines, not the number of total cores. So if you have 3 phones that you want to wait on pass 3.
- `group-by`: we always use microarch (default). The others are only needed when we see weird behavior between machines of same microarch.
- `print-flags`: this is the flag that is used in the `start_clients.sh` script to get the flags to build the client.
- `no-check-mem`: if memory testing should be disabled
- `no-auto-map-mem`: if automatically mapping memory should be disabled
- `floats`, `vector`: just the respective client C preproceessor flags `VECTOR` and `FLOATS`. We leave those always on. I'm not even sure if the code currently works without those flags.
- `meta`: include performance counters (preprocessor flag `META`)

TLDR: you should use `--floats --vector --expect <num machines> --port <whatever you want>` (maybe also `--meta`)

## Server design

The mentioned CLI frontends all use shared logic mostly implemented in [`server.py`](../pyutils/server.py) and [`fuzzserver.py`](../pyutils/fuzzserver.py) which have the respective classes `Server` and `FuzzServer`.

### Server
The server class is the code that handles the connections to clients.
It listens on TCP connections from clients and spawns one process per connected client (one process per client core).
Once all cores of all `--expect` machines are connected, the server can be used to interface with these client processes (implemented in the `RealClient` class).
The basic functionality is `schedule_inputs`, `get_results` which both work concurrently.

Then, there are wrapper functions for these two which should be the only interface that code uses to interact with clients:
- `execute_inputs_on_clients`
- `execute_input_on_clients`

They should be self explanatory.

The server performs input merging, i.e. combining multiple inputs into one big network packet, so it's not so bad for performance if small batches of inputs (for example length 1) are scheduled.
The term batch just describes a list of inputs.

### FuzzServer
This class is only needed by code that does fuzzing.
It implements shared logic such as printing fuzzing progress or spawning multiple generator clients.

## Frontends

### Difffuzz
This is the most important interface to the fuzzer.
It currently implements basic differential fuzzing between all connected clients.
The sequence generation can currently be modified via CLI flags, though most of the flags currently do nothing.
Adding a switch for different sequence generation methods is TODO.
The most basic and important CLI flag here is `seq-len`.

Once the server is started and all clients are connected multiple worker processes are started by `DiffFuzzServer` (a child of [`FuzzServer`](#fuzzserver)).
The fuzz server prints progress in form of the current counter, elapsed time, executed inputs per second and number of logged reproducers.
The workers generate inputs, schedule them on the clients and then analyze the results.
The analysis stage is pretty involved:
- Custom filters can be written that filter out false positives.
  Those filters always work on a result to result comparison.
- An algorithm finds all the so called diff splits in a set of results.
  This algorithm starts at the first instruction of the sequence and gradually progresses to longer sequences.
  Every time at least two clients disagree on a result, a new diff split is recorded.
  After that, the clients in each branch of the split are treated isolated, since we already had a diff between these sets of clients.
  In the end, this algorithm produces the minimal set of differences found between lists of clients (microarchitectures).

Once a difference is found that is not filtered out it is logged in a so called reproducer file into `results/reproducers`.
Those are currently yaml files that include all the relevant data, together with a text representation of the diff at the top (as comment). 
Those files can nicely be inspected with simple Unix commands.
I like to use `bat`:
``` sh
bat --style=header ~/difffuzz-framework/results/reproducers/*
```

### Undocfuzz-Scan
This is a similar interface to the difffuzz interface.
Though here, the sequence generation produces instructions that are especially not in the machine-parsable specification to find undocumented instructions.
Further, this interface uses some other logging mechanisms and optimizations.

### Run On
This interface allows to re-run reproducers on clients or run custom instruction sequences passed from command line.

### List unstable instructions
This interface lists so called unstable instructions - instructions that produce different results on each invocation.
For example a random number generator.

### Docfuzz
NOTE: This interface is currently unfortunately broken

This interface executes all instructions in the machine-parsable specification on all clients and reports which instructions return which signal.

## Useful commands

List all clients that don't have `asimd`:
``` sh
yq '.[] | .clients[] | select(.microarchitecture.flags | contains(["asimd"]) | not)' clients.yaml
```

Inspect reproducers by scrolling through them:
``` sh
bat --style=header --line-range :50 ~/difffuzz-framework/results/reproducers/*.yaml
```

Remove classes and (re-)cluster:
``` sh
rm -rf results/reproducers/classes && python server/cluster.py results
```

Show one reproducer per class scrolling through:
``` sh
for dir in results/reproducers/classes/*/; do find "$dir" -maxdepth 1 -type f | head -n 1; done | xargs bat --style=header --line-range :50
```
