// ar_math: the math functions of the runtime. They come from fdlibm, which
// uses only the basic operations of IEEE 754, so they give the same bits on
// the Mac and on the phone. The math library of iOS 6 and the one of macOS
// give different results for sin, cos, atan2, pow, exp, and log, and then
// a replay of the Mac goes another way on the phone.

#ifndef AR_MATH_H
#define AR_MATH_H

double ar_sin(double x);
double ar_cos(double x);
double ar_tan(double x);
double ar_asin(double x);
double ar_acos(double x);
double ar_atan(double x);
double ar_atan2(double y, double x);
double ar_exp(double x);
double ar_log(double x);
double ar_log2(double x);
double ar_log10(double x);
double ar_pow(double x, double y);

#endif
