# Debug version with "make DEBUG=1"

# Read out git commit from an ELF:
# readelf -p .gitcommit <ELF>

ifeq ($(ARCH),aarch64)
# RISC-V and lab55 only support 39 bits (up to 0x8000000000 for user space)
# import random; hex(random.getrandbits(39) & ~(2**16-1))
GENERAL_BASE       = 0x153ced0000
# TODO
HEAP_BASE          = 0x163ced0000
# TODO: Move runner back to end? currently we have to position it after BASE and not at the end because of heap
RUNNER_ADDR        = 0x24de6e0000
STACK_ADDR         = 0x391af00000
ALT_STACK_ADDR     = 0x381af00000
RUNNER_END_ADDR_32 = 0067
RUNNER_END_ADDR_16 =     3c5e
else ifeq ($(ARCH),riscv64)
# TODO: debug why we need low addresses for riscv
GENERAL_BASE       = 0xed0000
HEAP_BASE          = 0x02de6e0000
RUNNER_ADDR        = 0x04de6e0000
STACK_ADDR         = 0x081af00000
ALT_STACK_ADDR     = 0x091af00000
RUNNER_END_ADDR_32 = 003f
# cant go higher than 0x3fffff0000 here
RUNNER_END_ADDR_16 =     3c5e
endif

FLAGS=-I./src -no-pie -Wall -Wextra -DRUNNER_END_ADDR_32=0x$(RUNNER_END_ADDR_32) -DRUNNER_END_ADDR_16=0x$(RUNNER_END_ADDR_16) -DRUNNER_END_ADDR=0x$(RUNNER_END_ADDR_32)$(RUNNER_END_ADDR_16)0000 -DSTACK_ADDR=$(STACK_ADDR) -DALT_STACK_ADDR=$(ALT_STACK_ADDR) -DHEAP_BASE=$(HEAP_BASE)
# Don't remove git_commit symbol
FLAGS += -Wl,-u,git_commit

EXTRACFLAGS=-pipe -Wall -Wextra -Wredundant-decls -Wshadow

ifneq ($(DEBUG),)
EXTRACFLAGS += -Og -g -DDEBUG
else
EXTRACFLAGS += -O3 -Werror
endif

ifeq ($(ARCH),riscv64)
# CFLAGS=-Og -g -march="rv64g"
# -fsanitize=address -fomit-frame-pointer
# NOTE: no-relax is here so that gp is not used (pc instead)
# this can't be fixed by clobbers for some reason, the compiler still tries to use gp
# NOTE: we move runner section here so that the instr address is reproducible in templates
# rv64gcv0p7
FLAGS+=-mno-relax -march="rv64gczve64x"
# NOTE: filtering does unfortunately not work, but we can just disable disassembly
# FLAGS=-fprofile-filter-files=diffuzz-client\.c -pg -Og -g -no-pie -Wall -Wextra -lcapstone -mno-relax
else ifeq ($(ARCH),aarch64)
# FLAGS+=-Wl,-z,common-page-size=16384 -Wl,-z,max-page-size=16384 -Wl,-z,norelro -Wl,-M
FLAGS+=-Wl,-z,common-page-size=16384 -Wl,-z,max-page-size=16384
# FLAGS+=-Wl,-z,common-page-size=16384 -Wl,-z,max-page-size=16384 -Wl,-M
# FLAGS+=-Wl,-M
# maybe needed at some point, then test which options we really need
FLAGS+=-march="armv8.1-a+sve" -fno-tree-vectorize -fno-tree-loop-vectorize
else
$(error Unsupported architecture "$(ARCH)". Pass one of riscv64 or aarch64 as ARCH.)
endif

CFLAGS := $(CFLAGS) $(FLAGS) $(EXTRACFLAGS) -static

ifeq ($(BINUTILS_A64_INC),)
$(error BINUTILS_A64_INC needs to be set and should be set by Nix)
endif
ifeq ($(BINUTILS_A64_LIB),)
$(error BINUTILS_A64_LIB needs to be set and should be set by Nix)
endif
LIBOPCODES_LDFLAGS = -lopcodes -lbfd -lsframe -liberty -lz -L$(BINUTILS_A64_LIB)
LIBOPCODES_CFLAGS  = -I$(BINUTILS_A64_INC)

LDFLAGS := -lz $(LIBOPCODES_LDFLAGS) -Wl,--require-defined=runner_end

ifeq ($(origin CC),default)
CC = gcc
endif

default: out/diffuzz-client

out/regs.o: src/lib/$(ARCH)/regs.c src/lib/$(ARCH)/regs.h
	mkdir -p out
	$(CC) -c src/lib/$(ARCH)/regs.c -o $@ $(CFLAGS)

# TODO: merge
out/runner.o: src/lib/runner.c src/lib/runner.h out/runner_arch_preprocessed.S
	mkdir -p out
	$(CC) -c src/lib/runner.c -o $@ $(CFLAGS)

# This is needed for .include statements in inline assembly.
# The preprocessor is not invoked there.
out/runner_arch_preprocessed.S: src/lib/$(ARCH)/runner.S
	mkdir -p out
	$(CC) -E $^ -o $@ $(CFLAGS)

out/runner-sandbox.o: src/lib/runner.S
	mkdir -p out
	$(CC) -c src/lib/runner.S -o $@ $(CFLAGS)

out/maps.o: src/lib/maps.c src/lib/maps.h
	mkdir -p out
	$(CC) -c src/lib/maps.c -o $@ $(CFLAGS)

out/util.o: src/lib/util.c src/lib/util.h
	mkdir -p out
	$(CC) -c src/lib/util.c -o $@ $(CFLAGS)

out/log.o: src/lib/log.c src/lib/log.h src/lib/util.h
	mkdir -p out
	$(CC) -c src/lib/log.c -o $@ $(CFLAGS)

out/extended_util.o: src/lib/extended_util.c src/lib/extended_util.h
	mkdir -p out
	$(CC) -c src/lib/extended_util.c -o $@ $(CFLAGS)

out/rng.o: src/lib/rng.c src/lib/rng.h
	mkdir -p out
	$(CC) -c src/lib/rng.c -o $@ $(CFLAGS)

out/connection.o: src/lib/connection.c src/lib/connection.h
	mkdir -p out
	$(CC) -c src/lib/connection.c -o $@ $(CFLAGS)

out/cpuinfo.o: src/lib/cpuinfo.c src/lib/cpuinfo.h
	mkdir -p out
	$(CC) -c src/lib/cpuinfo.c -o $@ $(CFLAGS)

out/fuzzing_value_map.o: src/lib/fuzzing_value_map.c src/lib/fuzzing_value_map.h
	mkdir -p out
	$(CC) -c src/lib/fuzzing_value_map.c -o $@ $(CFLAGS)

out/disasm_opcodes.o: src/lib/disasm_opcodes.c src/lib/disasm_opcodes.h
	mkdir -p out
	$(CC) $(LIBOPCODES_CFLAGS) -c src/lib/disasm_opcodes.c -o $@ $(CFLAGS)

out/vars.ld:
	mkdir -p out
	echo "GENERAL_BASE    = ${GENERAL_BASE};"                                  >  out/vars.ld
	echo "RUNNER_ADDR     = ${RUNNER_ADDR};"                                   >> out/vars.ld
	echo "RUNNER_END_ADDR = 0x${RUNNER_END_ADDR_32}${RUNNER_END_ADDR_16}0000;" >> out/vars.ld

src/$(ARCH)/default.ld: out/vars.ld

out/diffuzz-client: src/$(ARCH)/default.ld src/diffuzz-client.c src/lib/fuzzing_value_map.h out/regs.o out/runner.o out/disasm_opcodes.o out/runner-sandbox.o out/maps.o out/util.o out/log.o out/cpuinfo.o out/connection.o out/rng.o out/fuzzing_value_map.o
	mkdir -p out
	$(CC) -T src/$(ARCH)/default.ld src/diffuzz-client.c out/regs.o out/runner.o out/disasm_opcodes.o out/runner-sandbox.o out/maps.o out/util.o out/log.o out/cpuinfo.o out/connection.o out/rng.o out/fuzzing_value_map.o -o $@ $(CFLAGS) $(LDFLAGS)
	./embed_git_commit.sh $@
	./embed_hash.sh $@

out/input-generator: src/input-generator.c src/lib/fuzzing_value_map.h out/regs.o out/runner.o out/disasm_opcodes.o out/runner-sandbox.o out/maps.o out/util.o out/log.o out/cpuinfo.o out/rng.o out/fuzzing_value_map.o
	mkdir -p out
	$(CC) src/input-generator.c out/regs.o out/runner.o out/disasm_opcodes.o out/runner-sandbox.o out/maps.o out/util.o out/log.o out/cpuinfo.o out/rng.o out/fuzzing_value_map.o -o $@ $(CFLAGS) $(LDFLAGS)
	./embed_git_commit.sh $@

out/ranges.o: src/undocenum/$(ARCH)/ranges.gz
	mkdir -p out
	cp $^ out/ranges
	$(LD) -r -b binary out/ranges -o $@

out/undoc-enum: src/$(ARCH)/default.ld src/undoc-enum.c src/lib/fuzzing_value_map.h out/fuzzing_value_map.o out/rng.o out/regs.o out/runner.o out/disasm_opcodes.o out/runner-sandbox.o out/maps.o out/util.o out/log.o out/cpuinfo.o out/ranges.o
	mkdir -p out
	$(CC) -T src/$(ARCH)/default.ld src/undoc-enum.c out/fuzzing_value_map.o out/rng.o out/regs.o out/runner.o out/disasm_opcodes.o out/runner-sandbox.o out/maps.o out/util.o out/log.o out/cpuinfo.o out/ranges.o -o $@ $(CFLAGS) -larchive -llzma -lz -lxml2 -lssl -lcrypto -lbz2 -lzstd $(LDFLAGS)
	./embed_git_commit.sh $@

out/print-fuzing-value-map: src/print-fuzzing-value-map.c src/lib/fuzzing_value_map.h out/fuzzing_value_map.o out/rng.o
	mkdir -p out
	$(CC) src/print-fuzzing-value-map.c out/rng.o out/fuzzing_value_map.o -o $@ $(CFLAGS) $(LDFLAGS)

# TODO: we can use -MM here for resolving dependencies (headers are missing)
out/reg-spinner: src/reg-spinner.c
	mkdir -p out
	$(CC) $^ -o $@ $(CFLAGS) $(LDFLAGS)

out/repro-runner: src/$(ARCH)/default.ld src/repro-runner.c out/regs.o out/runner.o out/disasm_opcodes.o out/runner-sandbox.o out/maps.o out/util.o out/log.o out/cpuinfo.o out/rng.o out/fuzzing_value_map.o
	mkdir -p out
	$(CC) -T $^ -o $@ $(CFLAGS) $(LDFLAGS)
	./embed_git_commit.sh $@

out/repro: src/$(ARCH)/default.ld $(REPRO_FILE) out/regs.o out/runner.o out/disasm_opcodes.o out/runner-sandbox.o out/maps.o out/util.o out/log.o out/cpuinfo.o out/rng.o out/fuzzing_value_map.o
	mkdir -p out
									# NOTE: garbage collect unused library functions for non-sig repro
	$(CC) -T $^ -o $@ $(CFLAGS) -Wl,--gc-sections $(LDFLAGS)
	./embed_git_commit.sh $@

clean:
	rm -f out/* compile_commands.json
