// (fm1/bench has its own copy, as it was built and tested.)
// What the C++ engine needs and fm1-base doesn't link: no C++ runtime and no maths library (fm1-base firmwares are
// C on its own fastmath.h). The maths functions here are fastmath.h's single-precision ones, so the engine runs
// entirely on the FPU; they are approximations (fastmath.h gives their accuracy), close enough for timing and
// sound, but not bit-identical to the computer's libm.
#include <stddef.h>
#include <stdint.h>
extern "C" {
#include "fastmath.h"
}

extern "C" {
// static objects with destructors register them; nothing is ever destroyed in a firmware (fm1-base's host
// simulator links the computer's C++ runtime, which has these)
#ifndef PLAT_HOST
void *__dso_handle = 0;
int __cxa_atexit(void (*)(void *), void *, void *) { return 0; }
#endif
// a pure virtual function called: never, in the modules
void __cxa_pure_virtual(void) { for (;;) { } }

float sqrtf(float x) { return fm_sqrtf(x); }
float sinf(float x) { return fm_sinf(x); }
float cosf(float x) { return fm_cosf(x); }
float tanf(float x) { return fm_tanf(x); }
float expf(float x) { return fm_expf(x); }
float exp2f(float x) { return fm_exp2f(x); }
float logf(float x) { return fm_logf(x); }
float log2f(float x) { return fm_log2f(x); }
float log10f(float x) { return fm_log2f(x) * 0.30102999566f; }
float powf(float x, float y) { return fm_powf(x, y); }
float tanhf(float x) { return fm_tanhf(x); }
float floorf(float x) { return fm_floorf(x); }
float ceilf(float x) { float f = fm_floorf(x); return f == x ? f : f + 1.0f; }
float fabsf(float x) { return fm_fabsf(x); }
float fmodf(float x, float y)
{
    if (y == 0.0f)
        return 0.0f;
    float q = x / y;
    q = q < 0.0f ? -fm_floorf(-q) : fm_floorf(q);   // toward zero, as C's fmodf
    return x - q * y;
}
float atanf(float x)   // |error| < 0.0015 rad (a rational fit), then folded for |x| > 1
{
    float a = fm_fabsf(x), r;
    if (a > 1.0f)
        r = 1.5707963f - (1.0f / a) / (1.0f + 0.28086f / (a * a));
    else
        r = a / (1.0f + 0.28086f * a * a);
    return x < 0.0f ? -r : r;
}
float atan2f(float y, float x)
{
    if (x > 0.0f)
        return atanf(y / x);
    if (x < 0.0f)
        return y >= 0.0f ? atanf(y / x) + 3.14159265f : atanf(y / x) - 3.14159265f;
    return y > 0.0f ? 1.5707963f : y < 0.0f ? -1.5707963f : 0.0f;
}
}

// virtual destructors refer to these; the engine never deletes (objects are placed in the arena)
void operator delete(void *) noexcept { }
void operator delete(void *, size_t) noexcept { }
