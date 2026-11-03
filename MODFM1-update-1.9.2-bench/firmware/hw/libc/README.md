# Freestanding C headers for fm1-base builds

fm1-base links no C library: its board layer has `memset`, `memcpy` and `memcmp` (felucca-bsp `chip/libc.c`), and
fm1-base firmwares do their maths with `fastmath.h`. The Mutable code and FM1 Rack's engine include the usual C
headers, so these declare what they use. The maths functions are defined in single precision by the firmware
(`fm1/bench/rt.cc`, on `fastmath.h`); the `double` ones are declared only so that `<cmath>` compiles, and linking
one fails, which is the point: the FM-1's FPU is single precision.
