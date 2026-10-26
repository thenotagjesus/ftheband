// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Session Jam contributors
#pragma once
#include <vector>
#include <cmath>

namespace sj {
/** ITU-R BS.1770-4 integrated loudness (LUFS) of a stereo (or mono: pass same buffer twice) signal at 48 kHz. */
inline double integratedLufs (const float* L, const float* R, size_t n, double sr = 48000) {
    (void) sr;   // coefficients below are the 48 kHz reference values
    auto kweight = [] (const float* x, size_t n) {
        std::vector<double> y (n);
        double b0=1.53512485958697,b1=-2.69169618940638,b2=1.19839281085285,a1=-1.69065929318241,a2=0.73248077421585;
        double c0=1,c1=-2,c2=1,d1=-1.99004745483398,d2=0.99007225036621;
        double x1=0,x2=0,y1=0,y2=0,z1=0,z2=0,w1=0,w2=0;
        for (size_t i = 0; i < n; ++i) {
            double v = b0*x[i]+b1*x1+b2*x2-a1*y1-a2*y2; x2=x1; x1=x[i]; y2=y1; y1=v;
            double u = c0*v+c1*z1+c2*z2-d1*w1-d2*w2; z2=z1; z1=v; w2=w1; w1=u; y[i]=u;
        }
        return y;
    };
    auto l = kweight (L, n), r = kweight (R, n);
    const size_t blk = 19200, hop = 4800;
    std::vector<double> z;
    for (size_t s = 0; s + blk <= n; s += hop) {
        double a = 0; for (size_t i = s; i < s + blk; ++i) a += l[i]*l[i] + r[i]*r[i];
        z.push_back (a / blk);
    }
    auto lk = [] (double p) { return -0.691 + 10 * std::log10 (p + 1e-20); };
    double sum = 0; int c = 0;
    for (double p : z) if (lk (p) > -70) { sum += p; ++c; }
    if (c == 0) return -100;
    const double rel = lk (sum / c) - 10; sum = 0; c = 0;
    for (double p : z) if (lk (p) > -70 && lk (p) > rel) { sum += p; ++c; }
    return c ? lk (sum / c) : -100;
}
} // namespace sj
