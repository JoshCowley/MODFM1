/* Freestanding <math.h> for fm1-base builds (see README.md): single precision, defined by the firmware. */
#pragma once
#ifdef __cplusplus
extern "C" {
#endif
#define M_PI 3.14159265358979323846
float sqrtf(float); float sinf(float); float cosf(float); float tanf(float); float expf(float); float exp2f(float);
float logf(float); float log2f(float); float log10f(float); float powf(float, float); float tanhf(float);
float floorf(float); float ceilf(float); float fabsf(float); float fmodf(float, float); float atanf(float);
float atan2f(float, float);
static inline float fminf(float a, float b) { return a < b ? a : b; }
static inline float fmaxf(float a, float b) { return a > b ? a : b; }
/* declared for <cmath>; not defined: a double in the code fails to link */
double sqrt(double); double sin(double); double cos(double); double tan(double); double exp(double);
double log(double); double log2(double); double log10(double); double pow(double, double); double tanh(double);
double floor(double); double ceil(double); double fabs(double); double fmod(double, double); double atan(double);
double atan2(double, double);
#ifdef __cplusplus
}
#endif
