#pragma once
double pow(double x, double y);
float powf(float x, float y);
double log10(double x);
double log2(double x);
double exp2(double x);
float sqrtf(float x);
double sqrt(double x);
static inline double ceil(double x) { return __builtin_ceil(x); }
static inline double floor(double x) { return __builtin_floor(x); }
static inline float floorf(float x) { return __builtin_floorf(x); }
static inline float fabsf(float x) { return __builtin_fabsf(x); }
static inline double fabs(double x) { return __builtin_fabs(x); }
