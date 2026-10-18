#include "ar_math.h"

// The functions of fdlibm (src/third_party/fdlibm).
double fd_sin(double x);
double fd_cos(double x);
double fd_tan(double x);
double fd_atan(double x);
double __ieee754_asin(double x);
double __ieee754_acos(double x);
double __ieee754_atan2(double y, double x);
double __ieee754_exp(double x);
double __ieee754_log(double x);
double __ieee754_log10(double x);
double __ieee754_pow(double x, double y);

double ar_sin(double x) { return fd_sin(x); }
double ar_cos(double x) { return fd_cos(x); }
double ar_tan(double x) { return fd_tan(x); }
double ar_asin(double x) { return __ieee754_asin(x); }
double ar_acos(double x) { return __ieee754_acos(x); }
double ar_atan(double x) { return fd_atan(x); }
double ar_atan2(double y, double x) { return __ieee754_atan2(y, x); }
double ar_exp(double x) { return __ieee754_exp(x); }
double ar_log(double x) { return __ieee754_log(x); }
double ar_log10(double x) { return __ieee754_log10(x); }
double ar_pow(double x, double y) { return __ieee754_pow(x, y); }

// fdlibm has no log2. 1 / ln 2, as the nearest double.
double ar_log2(double x) { return __ieee754_log(x) * 1.4426950408889634074; }
