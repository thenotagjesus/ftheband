// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 FtheBand contributors
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace sj {

/** Harmonic-aware note activations by non-negative least squares (NNLS), in the spirit of Mauch & Dixon's NNLS-Chroma
    (an original implementation: no code or data from Chordino/NNLS-Chroma is used, it only shares the idea).

    Model: the compressed magnitude spectrum X of one onset-aligned analysis window is explained as a non-negative sum
    of note templates. A template is a comb of partials (h = 1..H) at h*f0, each drawn with the main-lobe shape of the
    window actually used (so a 90 ms window at E1 is modelled as blurry as it really is), scaled by a spectral tilt and,
    optionally, by the magnitude response of the Bass VI strangle (low-cut) switch. Every note has several tilts
    ("dark", "bright", "no fundamental") so a weak E1 fundamental does not turn into an octave/fifth error: the
    harmonics are explained by the lowest note that explains them all. The solution is found jointly, so shared
    partials (a 5th shares every 3rd/2nd partial pair) are attributed instead of double counted. A small L1 term keeps it sparse.

    Real-time notes: runs on the analysis thread, not the audio callback. All storage is sized in prepare();
    analyse() allocates nothing. Cost per call is a few hundred thousand multiply-adds (about 0.2 ms). */
class NnlsNotes {
public:
    static constexpr int maxNotes = 64, nProf = 3, maxCols = maxNotes * nProf, maxPartials = 16;
    int lo = 28, hi = 76, nNotes = 0, nCols = 0;
    /** Per-note activation (sum over tilts), index = midi - lo. */
    std::array<float, maxNotes> act {};
    /** Fraction of the data energy left unexplained (0 = perfect fit). */
    float residual = 1.0f;
    /** Global tuning offset in cents applied to the templates (for an instrument tuned flat/sharp). */
    float tuningCents = 0.0f;

    void prepare (double sampleRate, int fftSize, int loMidi, int hiMidi) {
        sr = sampleRate; N = fftSize; binHz = (float) (sr / N); lo = loMidi; hi = std::min (hiMidi, loMidi + maxNotes - 1);
        nNotes = hi - lo + 1; nCols = nNotes * nProf;
        kMin = std::max (2, (int) (30.0f / binHz)); kMax = std::min (N / 2 - 2, (int) (4500.0f / binHz));
        const size_t cap = (size_t) maxCols * maxPartials * 20;
        cbin.assign (cap, 0); cval.assign (cap, 0.0f);
        colStart.assign ((size_t) maxCols + 1, 0);
        const size_t nb = (size_t) N / 2 + 2;
        binCount.assign (nb + 1, 0); binFill.assign (nb + 1, 0); tCol.assign (cap, 0); tVal.assign (cap, 0.0f);
        G.assign ((size_t) maxCols * maxCols, 0.0f); b.assign ((size_t) maxCols, 0.0f); a.assign ((size_t) maxCols, 0.0f);
        diag.assign ((size_t) maxCols, 1.0f);
        act.fill (0.0f);
    }

    /** X: sqrt-compressed magnitude spectrum (size >= N/2), `len` the window length in samples, lowCutHz > 0 models a
        low-cut switch (12 dB/oct), maxNote limits the loudest... returns the residual fraction. */
    float analyse (const float* X, int len, float lowCutHz) {
        buildColumns (len, lowCutHz);
        // b = D^T x,  energy of x
        float xx = 1e-12f;
        for (int k = kMin; k <= kMax; ++k) xx += X[k] * X[k];
        std::fill (b.begin(), b.begin() + nCols, 0.0f);
        for (int c = 0; c < nCols; ++c) { float s = 0; for (int i = colStart[(size_t) c]; i < colStart[(size_t) c + 1]; ++i) s += cval[(size_t) i] * X[cbin[(size_t) i]]; b[(size_t) c] = s; }
        gram();
        // projected coordinate descent with L1 sparsity
        float bmax = 0; for (int c = 0; c < nCols; ++c) bmax = std::max (bmax, b[(size_t) c] / std::sqrt (diag[(size_t) c]));
        const float l1 = sparsity * bmax;
        for (int c = 0; c < nCols; ++c) a[(size_t) c] = std::max (0.0f, (b[(size_t) c] - l1) / diag[(size_t) c]) * 0.5f;
        for (int it = 0; it < sweeps; ++it)
            for (int c = 0; c < nCols; ++c) {
                const float* g = &G[(size_t) c * maxCols];
                float s = b[(size_t) c] - l1;
                for (int j = 0; j < nCols; ++j) s -= g[j] * a[(size_t) j];
                s += g[c] * a[(size_t) c];
                a[(size_t) c] = std::max (0.0f, s / g[c]);
            }
        // residual = ||x - D a||^2 / ||x||^2 = 1 - (2 b.a - a.G.a)/xx
        float ba = 0, aga = 0;
        for (int c = 0; c < nCols; ++c) { ba += b[(size_t) c] * a[(size_t) c]; float s = 0; const float* g = &G[(size_t) c * maxCols]; for (int j = 0; j < nCols; ++j) s += g[j] * a[(size_t) j]; aga += a[(size_t) c] * s; }
        residual = std::max (0.0f, 1.0f - (2 * ba - aga) / xx);
        for (int n = 0; n < nNotes; ++n) { float s = 0; for (int q = 0; q < nProf; ++q) s += a[(size_t) (n * nProf + q)] * colNorm[(size_t) (n * nProf + q)]; act[(size_t) n] = s; }
        return residual;
    }

    int sweeps = 30; float sparsity = 0.04f; int nPartials = 14;
    /** Tilt of each template family: partial h has weight tilt^(h-1) (family 2 drops partial 1 entirely: a note whose fundamental is filtered). */
    std::array<float, nProf> tilt { 0.80f, 0.94f, 0.90f };
    float partialMaxHz = 4000.0f;

private:
    double sr = 48000; int N = 16384; float binHz = 2.93f; int kMin = 10, kMax = 1000;
    std::vector<int> cbin, colStart, binCount, binFill, tCol; std::vector<float> cval, tVal;
    std::vector<float> G, b, a, diag; std::array<float, maxCols> colNorm {};

    void buildColumns (int len, float lowCutHz) {
        const float hw = 2.0f * (float) sr / (float) len;         // Hann main-lobe half width (Hz)
        int w = 0;
        for (int n = 0; n < nNotes; ++n) {
            const float f0 = 440.0f * std::pow (2.0f, ((float) (lo + n) - 69.0f + tuningCents / 100.0f) / 12.0f);
            for (int q = 0; q < nProf; ++q) {
                const int c = n * nProf + q; colStart[(size_t) c] = w;
                float nrm = 0;
                const int start = w;
                for (int h = (q == 2 ? 2 : 1); h <= nPartials; ++h) {
                    const float f = f0 * (float) h; if (f > partialMaxHz) break;
                    float wt = std::pow (tilt[(size_t) q], (float) (h - 1));
                    if (lowCutHz > 0) { const float r = f / lowCutHz; wt *= std::sqrt (r * r / std::sqrt (1.0f + r * r * r * r)); }   // magnitude of a 2nd-order high-pass, compressed
                    const int k0 = std::max (kMin, (int) std::ceil ((f - hw) / binHz)), k1 = std::min (kMax, (int) std::floor ((f + hw) / binHz));
                    for (int k = k0; k <= k1; ++k) {
                        const float d = (float) k * binHz - f;
                        const float v = wt * std::cos (1.5707963f * d / hw);
                        if (v <= 1e-4f) continue;
                        cbin[(size_t) w] = k; cval[(size_t) w] = v; nrm += v * v; ++w;
                    }
                }
                nrm = std::sqrt (nrm) + 1e-9f; colNorm[(size_t) c] = 1.0f / nrm;   // unit-norm columns: activation = amplitude of the spectral pattern
                for (int i = start; i < w; ++i) cval[(size_t) i] /= nrm;
                // columns merge neighbouring partials into the same bin: keep the sum (bins appear once per partial); fine for a model of overlapping lobes
            }
        }
        colStart[(size_t) nCols] = w;
        for (int c = 0; c < nCols; ++c) colNorm[(size_t) c] = 1.0f;   // cval already unit norm; the amplitude of a column is its activation
    }

    void gram() {
        const size_t nb = (size_t) kMax + 2;
        std::fill (binCount.begin(), binCount.begin() + (long) nb + 1, 0);
        const int nnz = colStart[(size_t) nCols];
        for (int c = 0; c < nCols; ++c) for (int i = colStart[(size_t) c]; i < colStart[(size_t) c + 1]; ++i) ++binCount[(size_t) cbin[(size_t) i] + 1];
        for (size_t k = 1; k <= nb; ++k) binCount[k] += binCount[k - 1];
        for (size_t k = 0; k <= nb; ++k) binFill[k] = binCount[k];
        for (int c = 0; c < nCols; ++c) for (int i = colStart[(size_t) c]; i < colStart[(size_t) c + 1]; ++i) { const int pos = binFill[(size_t) cbin[(size_t) i]]++; tCol[(size_t) pos] = c; tVal[(size_t) pos] = cval[(size_t) i]; }
        (void) nnz;
        for (int c = 0; c < nCols; ++c) std::fill (G.begin() + (long) c * maxCols, G.begin() + (long) c * maxCols + nCols, 0.0f);
        for (size_t k = 0; k < nb; ++k) {
            const int s = binCount[k], e = binCount[k + 1];
            for (int i = s; i < e; ++i) { float* g = &G[(size_t) tCol[(size_t) i] * maxCols]; const float vi = tVal[(size_t) i];
                for (int j = s; j < e; ++j) g[tCol[(size_t) j]] += vi * tVal[(size_t) j]; }
        }
        for (int c = 0; c < nCols; ++c) { G[(size_t) c * maxCols + (size_t) c] += 1e-3f; diag[(size_t) c] = G[(size_t) c * maxCols + (size_t) c]; }
    }
};

} // namespace sj
