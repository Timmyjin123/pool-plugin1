// Тональный отчёт по пресетам: доли энергии по полосам, центр спектра, работа клиппера.
//   g++ -O2 -std=c++17 memoria/tests/tone_report.cpp -o /tmp/tr && /tmp/tr
#include "../Source/Presets.h"
#include <complex>
#include <cstdio>
#include <string>

using namespace mem;

static void fft (std::vector<std::complex<double>>& a)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap (a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = -2 * M_PI / (double) len;
        const std::complex<double> wl (std::cos (ang), std::sin (ang));
        for (size_t i = 0; i < n; i += len)
        {
            std::complex<double> w (1);
            for (size_t j = 0; j < len / 2; ++j)
            {
                const auto u = a[i + j], v = a[i + j + len / 2] * w;
                a[i + j] = u + v; a[i + j + len / 2] = u - v; w *= wl;
            }
        }
    }
}

struct Report { double band[5] {}; double abs[5] {}; double centroid = 0; double clip = 0; double rms = 0; double infra = 0; };

static Report analyse (const std::vector<float>& x, double sr)
{
    const size_t N = 4096;
    std::vector<double> pw (N / 2, 0.0);
    std::vector<std::complex<double>> a (N);
    int frames = 0;
    for (size_t s = 0; s + N <= x.size(); s += N / 2, ++frames)
    {
        for (size_t i = 0; i < N; ++i) a[i] = x[s + i] * (.5 - .5 * std::cos (2 * M_PI * (double) i / (double) N));
        fft (a);
        for (size_t k = 0; k < N / 2; ++k) pw[k] += std::norm (a[k]);
    }
    Report r;
    const double edges[6] = { 20, 120, 500, 2000, 6000, 20000 };
    double tot = 0, cen = 0;
    for (size_t k = 1; k < N / 2; ++k)
    {
        const double f = (double) k * sr / (double) N;
        for (int b = 0; b < 5; ++b) if (f >= edges[b] && f < edges[b + 1]) r.band[b] += pw[k];
        tot += pw[k]; cen += pw[k] * f;
        if (f < 20) r.infra += pw[k];
    }
    r.infra = 100 * r.infra / std::max (1e-20, tot);
    for (int b = 0; b < 5; ++b) { r.abs[b] = r.band[b] / std::max (1, frames); r.band[b] = 100 * r.band[b] / std::max (1e-20, tot); }
    r.centroid = cen / std::max (1e-20, tot);
    long c = 0; double e = 0;
    for (float v : x) { if (std::fabs (v) > .9f) ++c; e += (double) v * v; }
    r.clip = 100.0 * (double) c / (double) std::max<size_t> (1, x.size());
    r.rms = std::sqrt (e / (double) std::max<size_t> (1, x.size()));
    return r;
}

// яркий сигнал: аккорд с обертонами 1/h до 12 кГц + розовый шум
static float chord (double t)
{
    static const double f[3] = { 130.81, 196.0, 329.63 };
    static Rng rng (99);
    static double b0 = 0, b1 = 0, b2 = 0;
    const double env = std::min (1.0, t / .8) * (std::fmod (t, 4.0) < 3.2 ? 1.0 : .25);
    double y = 0;
    for (double fr : f)
        for (int h = 1; fr * h < 12000; ++h)
            y += std::sin (2 * M_PI * fr * h * t + h) / (double) h;
    const double w = rng.bi();
    b0 = .99765 * b0 + w * .099; b1 = .963 * b1 + w * .2965; b2 = .57 * b2 + w * 1.0527;
    const double pink = (b0 + b1 + b2 + w * .1848) * .05;
    return (float) (.05 * env * y + .3 * env * pink);
}

int main (int argc, char** argv)
{
    const double sr = 48000;
    const int B = 256;
    const bool brief = argc > 1;

    std::vector<float> ref;
    for (int i = 0; i < (int) (8 * sr); ++i) ref.push_back (chord (i / sr));
    const Report ri = analyse (ref, sr);
    std::printf ("Доля энергии по полосам (%%) и наклон относительно входа (дБ, после выравнивания по громкости)\n");
    std::printf ("%-28s  sub%%  low%%  mid%%  hi%%  air%% | dSub  dLow  dMid  dHi  dAir | centr clip%%  rms  <20Гц%%\n", "");
    std::printf ("%-28s %5.1f %5.1f %5.1f %5.1f %5.1f |                              | %5.0f %5.2f %5.3f\n", "ВХОД", ri.band[0], ri.band[1], ri.band[2], ri.band[3], ri.band[4], ri.centroid, ri.clip, ri.rms);
    double avgD[5] {};

    double avg[5] {}, avgC = 0, avgClip = 0;
    const auto& P = memoria::presets();
    for (const auto& pr : P)
    {
        Engine e; e.prepare (sr);
        Params p = memoria::toParams (pr);
        std::vector<float> il (B), ir (B), ol (B), orr (B), cap;
        long n = 0;
        const long total = (long) (26 * sr), skip = (long) (8 * sr);
        while (n < total)
        {
            for (int i = 0; i < B; ++i)
            {
                const double t = (double) (n + i) / sr;
                const float x = t < 18 ? chord (t) : 0.0f;
                il[(size_t) i] = x; ir[(size_t) i] = x;
            }
            e.process (il.data(), ir.data(), ol.data(), orr.data(), B, p);
            for (int i = 0; i < B; ++i) if (n + i >= skip) cap.push_back (.5f * (ol[(size_t) i] + orr[(size_t) i]));
            n += B;
        }
        const Report r = analyse (cap, sr);
        double d[5];
        for (int b = 0; b < 5; ++b) { d[b] = 10 * std::log10 ((r.band[b] + 1e-9) / (ri.band[b] + 1e-9)); avgD[b] += d[b]; }
        for (int b = 0; b < 5; ++b) avg[b] += r.band[b];
        avgC += r.centroid; avgClip += r.clip;
        if (! brief)
            std::printf ("%-28s %5.1f %5.1f %5.1f %5.1f %5.1f | %+5.1f %+5.1f %+5.1f %+5.1f %+5.1f | %5.0f %5.2f %5.3f %5.1f\n", pr.name, r.band[0], r.band[1], r.band[2], r.band[3], r.band[4], d[0], d[1], d[2], d[3], d[4], r.centroid, r.clip, r.rms, r.infra);
    }
    const double k = 1.0 / (double) P.size();
    std::printf ("%-28s %5.1f %5.1f %5.1f %5.1f %5.1f | %+5.1f %+5.1f %+5.1f %+5.1f %+5.1f | %5.0f %5.2f\n", "СРЕДНЕЕ", avg[0] * k, avg[1] * k, avg[2] * k, avg[3] * k, avg[4] * k,
                 avgD[0] * k, avgD[1] * k, avgD[2] * k, avgD[3] * k, avgD[4] * k, avgC * k, avgClip * k);
    return 0;
}
