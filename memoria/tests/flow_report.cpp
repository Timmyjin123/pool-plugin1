// «Текучесть» пресетов на последовательности аккордов:
//   g++ -O2 -std=c++17 memoria/tests/flow_report.cpp -o /tmp/fr && /tmp/fr [фильтр по имени]
// Метрики: rms во время игры и хвоста, неровность громкости (меньше — текучее),
// самый глубокий провал, центр спектра, доля саба.
#include "../Source/Presets.h"
#include <cstdio>
#include <string>

using namespace mem;

// Cmaj7 → Am9 → Fmaj7 → G6, по 3 с, «пианино»: быстрая атака, затухание, обертоны
static float progression (double t)
{
    static const double ch[4][4] = { { 130.81, 164.81, 196.0, 246.94 }, { 110.0, 164.81, 196.0, 246.94 },
                                     { 87.31, 130.81, 164.81, 220.0 },  { 98.0, 146.83, 196.0, 164.81 * 2 } };
    if (t >= 12) return 0;
    const int c = (int) (t / 3.0);
    const double tt = std::fmod (t, 3.0);
    double y = 0;
    for (int n = 0; n < 4; ++n)
    {
        const double on = n * .06;                  // арпеджированная атака
        if (tt < on) continue;
        const double e = std::exp (-(tt - on) * 1.1);
        for (int h = 1; h <= 8; ++h) y += std::sin (2 * M_PI * ch[c][n] * h * (tt - on)) * e / (h * 1.4);
    }
    return (float) (.09 * y);
}

int main (int argc, char** argv)
{
    const std::string filt = argc > 1 ? argv[1] : "";
    const double sr = 48000;
    const int B = 256;
    std::printf ("%-34s %6s %6s %7s %7s %6s %6s\n", "", "play", "tail", "неровн", "провал", "центр", "саб%");
    for (const auto& pr : memoria::presets())
    {
        if (! filt.empty() && std::string (pr.name).find (filt) == std::string::npos) continue;
        Engine e; e.prepare (sr);
        Params p = memoria::toParams (pr);
        std::vector<float> il (B), ol (B), orr (B), out;
        const long total = (long) (24 * sr);
        for (long n = 0; n < total; n += B)
        {
            for (int i = 0; i < B; ++i) il[(size_t) i] = progression ((double) (n + i) / sr);
            e.process (il.data(), il.data(), ol.data(), orr.data(), B, p);
            for (int i = 0; i < B; ++i) out.push_back (.5f * (ol[(size_t) i] + orr[(size_t) i]));
        }
        // огибающая по 100 мс
        std::vector<double> env;
        for (size_t s = 0; s + 4800 <= out.size(); s += 4800)
        {
            double a = 0; for (size_t i = s; i < s + 4800; ++i) a += (double) out[i] * out[i];
            env.push_back (std::sqrt (a / 4800));
        }
        double play = 0, tail = 0, flux = 0, minP = 1e9; int np = 0, nt = 0;
        for (size_t i = 0; i < env.size(); ++i)
        {
            const double t = (double) i * .1;
            if (t >= 1.5 && t < 12) { play += env[i]; ++np; minP = std::min (minP, env[i]); if (i > 0) flux += std::fabs (env[i] - env[i - 1]); }
            if (t >= 13 && t < 23) { tail += env[i]; ++nt; }
        }
        play /= std::max (1, np); tail /= std::max (1, nt); flux /= std::max (1, np) * std::max (1e-9, play);
        // центр спектра и саб — грубо через однополюсные фильтры
        double lp = 0, lpS = 0, e0 = 0, eS = 0, eH = 0;
        for (size_t i = (size_t) (1.5 * sr); i < (size_t) (12 * sr); ++i)
        {
            lp += (out[i] - lp) * .12; lpS += (out[i] - lpS) * .012;
            e0 += (double) out[i] * out[i]; eS += lpS * lpS; eH += (out[i] - lp) * (out[i] - lp);
        }
        const double cen = 1000.0 * std::sqrt (eH / std::max (1e-12, e0)) * 10;   // условная «яркость»
        std::printf ("%-34s %6.3f %6.3f %7.3f %7.2f %6.0f %6.1f\n", pr.name, play, tail, flux, minP / std::max (1e-9, play), cen, 100 * eS / std::max (1e-12, e0));
    }
    return 0;
}
