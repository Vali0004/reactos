/*
 * PROJECT:     ReactOS CRT library
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Implementation of atan2 on top of atan
 * COPYRIGHT:   Copyright 2026 Jonathan McAvin <mcavinjohn@gmail.com>
 */

#include <math.h>

#define PI      3.14159265358979323846
#define PI_2    1.57079632679489661923
#define PI_4    0.78539816339744830962

static int
SignBit(double x)
{
    union { double d; unsigned long long u; } v = { x };
    return (int)(v.u >> 63);
}

/* only called once nan is ruled out */
static int
IsInf(double x)
{
    union { double d; unsigned long long u; } v = { x };
    return (v.u << 1) == 0xFFE0000000000000ULL;
}

double
__cdecl
atan2(double y, double x)
{
    int ny = SignBit(y), nx = SignBit(x);
    double r;

    if ((x != x) || (y != y))
        return x + y;

    if (y == 0.0)
    {
        /* the sign of x picks 0 or pi, the sign of y is kept */
        r = nx ? PI : 0.0;
        return ny ? -r : r;
    }

    if (x == 0.0)
        return ny ? -PI_2 : PI_2;

    if (IsInf(x))
    {
        if (IsInf(y))
            r = nx ? 3.0 * PI_4 : PI_4;
        else
            r = nx ? PI : 0.0;
        return ny ? -r : r;
    }

    if (IsInf(y))
        return ny ? -PI_2 : PI_2;

    r = atan(fabs(y / x));
    if (nx)
        r = PI - r;
    return ny ? -r : r;
}
