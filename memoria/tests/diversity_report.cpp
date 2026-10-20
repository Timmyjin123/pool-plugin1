// Разнообразие пресетов: насколько они отличаются друг от друга по звуку.
//   g++ -O2 -std=c++17 memoria/tests/diversity_report.cpp -o /tmp/dv && /tmp/dv
// Признаки пресета (на последовательности аккордов): доли энергии в 5 полосах (дБ), ширина стерео,
// неровность громкости, длина хвоста, пик-фактор, «шероховатость» спектра во времени.
// Каждый признак нормируется по разбросу между пресетами; дальше — расстояния между пресетами.
#include "../Source/Presets.h"
#include <complex>
#include <cstdio>
#include <string>

using namespace mem;

static float progression (double t)
{
    static const double ch[4][4] = { { 130.81, 164.81, 196.0, 246.94 }, { 110.0, 164.81, 196.0, 246.94 },
                                     { 87.31, 130.81, 164.81, 220.0 },  { 98.0, 146.83, 196.0, 329.63 } };
    if (t >= 12) return 0;
    const int c = (int) (t / 3.0); const double tt = std::fmod (t, 3.0);
    double y = 0;
    for (int n = 0; n < 4; ++n)
    {
        const double on = n * .06; if (tt < on) continue;
        const double e = std::min (1.0, (tt - on) / .005) * std::exp (-(tt - on) * 1.1) * std::min (1.0, (3.0 - tt) / .03);
        for (int h = 1; h <= 8; ++h) y += std::sin (2 * M_PI * ch[c][n] * h * (tt - on)) * e / (h * 1.4);
    }
    return (float) (.09 * y);
}

static void fft (std::vector<std::complex<double>>& a)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) { size_t bit = n >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit; if (i < j) std::swap (a[i], a[j]); }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = -2 * M_PI / (double) len; const std::complex<double> wl (std::cos (ang), std::sin (ang));
        for (size_t i = 0; i < n; i += len) { std::complex<double> w (1); for (size_t j = 0; j < len / 2; ++j) { auto u = a[i + j], v = a[i + j + len / 2] * w; a[i + j] = u + v; a[i + j + len / 2] = u - v; w *= wl; } }
    }
}

int main (int argc, char** argv)
{
    const bool verbose = argc > 1;
    const double sr = 48000; const int B = 256;
    const auto& P = memoria::presets();
    std::vector<std::vector<double>> F;
    const char* names[10] = { "sub", "low", "mid", "hi", "air", "ширина", "неровн", "хвост", "пик-ф", "шерох" };
    for (const auto& pr : P)
    {
        Engine e; e.prepare (sr);
        Params p = memoria::toParams (pr);
        std::vector<float> il (B), ol (B), orr (B), L, R;
        for (long n = 0; n < (long) (22 * sr); n += B)
        {
            for (int i = 0; i < B; ++i) il[(size_t) i] = progression ((double) (n + i) / sr);
            e.process (il.data(), il.data(), ol.data(), orr.data(), B, p);
            L.insert (L.end(), ol.begin(), ol.end()); R.insert (R.end(), orr.begin(), orr.end());
        }
        // спектр по кадрам (2…12 с): полосы и шероховатость
        const size_t N = 4096; std::vector<std::complex<double>> a (N);
        double band[5] {}; double rough = 0; int fr = 0; std::vector<double> prev (N / 2, 0.0);
        const double edges[6] = { 20, 120, 500, 2000, 6000, 20000 };
        for (size_t s = (size_t) (2 * sr); s + N <= (size_t) (12 * sr); s += N / 2, ++fr)
        {
            for (size_t i = 0; i < N; ++i) a[i] = .5 * (L[s + i] + R[s + i]) * (.5 - .5 * std::cos (2 * M_PI * i / N));
            fft (a);
            double tot = 0, diff = 0;
            for (size_t k = 1; k < N / 2; ++k)
            {
                const double m = std::abs (a[k]), f = (double) k * sr / N;
                for (int b = 0; b < 5; ++b) if (f >= edges[b] && f < edges[b + 1]) band[b] += m * m;
                diff += std::fabs (m - prev[k]); tot += m; prev[k] = m;
            }
            if (fr > 0) rough += diff / std::max (1e-9, tot);
        }
        double bt = 0; for (double b : band) bt += b;
        std::vector<double> f;
        for (double b : band) f.push_back (10 * std::log10 (b / std::max (1e-20, bt) + 1e-6));
        // ширина: энергия боков к центру (дБ)
        double em = 0, es = 0;
        for (size_t i = (size_t) (2 * sr); i < (size_t) (12 * sr); ++i) { const double m = .5 * (L[i] + R[i]), sd = .5 * (L[i] - R[i]); em += m * m; es += sd * sd; }
        f.push_back (10 * std::log10 (es / std::max (1e-20, em) + 1e-6));
        // огибающая по 100 мс: неровность и хвост
        std::vector<double> env;
        for (size_t s = 0; s + 4800 <= L.size(); s += 4800) { double q = 0; for (size_t i = s; i < s + 4800; ++i) q += .5 * (L[i] * L[i] + R[i] * R[i]); env.push_back (std::sqrt (q / 4800)); }
        double play = 0, flux = 0, tail = 0; int np = 0, nt = 0;
        for (size_t i = 15; i < 120 && i < env.size(); ++i) { play += env[i]; if (i > 15) flux += std::fabs (env[i] - env[i - 1]); ++np; }
        for (size_t i = 140; i < 210 && i < env.size(); ++i) { tail += env[i]; ++nt; }
        play /= std::max (1, np); tail /= std::max (1, nt);
        f.push_back (flux / std::max (1e-9, play * np));
        f.push_back (20 * std::log10 (tail / std::max (1e-9, play) + 1e-4));
        double pk = 0, rms = 0; for (size_t i = (size_t) (2 * sr); i < (size_t) (12 * sr); ++i) { pk = std::max (pk, (double) std::fabs (L[i])); rms += (double) L[i] * L[i]; }
        rms = std::sqrt (rms / (10 * sr));
        f.push_back (20 * std::log10 (pk / std::max (1e-9, rms)));
        f.push_back (rough / std::max (1, fr - 1));
        F.push_back (f);
        if (verbose) { std::printf ("%-34s", pr.name); for (double v : f) std::printf (" %7.2f", v); std::printf ("\n"); }
    }
    const size_t D = F[0].size(), n = F.size();
    std::vector<double> mean (D, 0), sd (D, 0);
    for (auto& f : F) for (size_t d = 0; d < D; ++d) mean[d] += f[d] / n;
    for (auto& f : F) for (size_t d = 0; d < D; ++d) sd[d] += (f[d] - mean[d]) * (f[d] - mean[d]) / n;
    std::printf ("разброс признаков между пресетами (стандартное отклонение):\n");
    for (size_t d = 0; d < D; ++d) std::printf ("  %-8s %.3f\n", names[d], std::sqrt (sd[d]));
    // расстояния в единицах «типичного» разброса (фиксированные шкалы, чтобы сравнивать версии между собой)
    const double scale[10] = { 3, 3, 3, 3, 3, 3, .05, 6, 3, .05 };
    double sum = 0, nn = 0; int close = 0;
    for (size_t i = 0; i < n; ++i)
    {
        double best = 1e9;
        for (size_t j = 0; j < n; ++j)
        {
            if (i == j) continue;
            double d2 = 0; for (size_t d = 0; d < D; ++d) { const double q = (F[i][d] - F[j][d]) / scale[d]; d2 += q * q; }
            const double dd = std::sqrt (d2);
            if (j > i) sum += dd;
            best = std::min (best, dd);
        }
        nn += best; if (best < .5) ++close;
    }
    std::printf ("\nсреднее расстояние между пресетами: %.2f\n", sum / (n * (n - 1) / 2.0));
    std::printf ("среднее расстояние до ближайшего соседа: %.2f\n", nn / n);
    std::printf ("почти-двойников (сосед ближе 0.5): %d из %zu\n", close, n);
    return 0;
}
