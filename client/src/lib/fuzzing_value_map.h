#pragma once

#include "util.h"
#include "regs.h"
#include "rng.h"

extern uint64_t filler_64;
extern uint64_t fuzzing_value_map_gp[];
extern const size_t fuzzing_value_map_gp_size;
extern union fpv fuzzing_value_map_fp[];
extern const size_t fuzzing_value_map_fp_size;

uint64_t fuzzing_value_map_gp_val_or_rand(shared_rng* rng);
uint64_t fuzzing_value_map_fp_val_or_rand(shared_rng* rng);
uint64_t fuzzing_value_any_val(shared_rng* rng);
