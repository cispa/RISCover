#pragma once

#if defined(__riscv)
    #include "riscv64/randomdifffuzzgenerator.h"
#elif defined(__aarch64__)
    #include "aarch64/randomdifffuzzgenerator.h"
#endif
