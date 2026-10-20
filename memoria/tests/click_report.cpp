// Детектор щелчков и зашкала по всем пресетам.
//   g++ -O2 -std=c++17 memoria/tests/click_report.cpp -o /tmp/cr && /tmp/cr [фильтр]
// Сценарий: три аккорда (9 с), затем 40 с почти тишины (шум интерфейса −80 дБ).
// Щелчок — скачок второй разности сигнала, в 12 раз выше её среднего уровня вокруг.
// «У потолка» — доля времени, когда |x| > 0.95.
#include "../Source/Presets.h"
#include <cstdio>
#include <string>

using namespace mem;

static float chords (double t, Rng& rng)
{
    static const double ch[3][3] = { { 130.81, 164.81, 196.0 }, { 110.0, 164.81, 220.0 }, { 87.31, 130.81, 174.61 } };
    float n = (float) (rng.bi() * 1e-4);   // шум интерфейса
    if (t >= 9) return n;
    const int c = (int) (t / 3.0); const double tt = std::fmod (t, 3.0);
    double y = 0;
    // мягкая атака 8 мс и затухание к концу аккорда — во входе самом нет щелчков
    const double env = std::min (1.0, tt / .008) * std::exp (-tt) * std::min (1.0, (3.0 - tt) / .05);
    for (int k = 0; k < 3; ++k) for (int h = 1; h <= 6; ++h) y += std::sin (2 * M_PI * ch[c][k] * h * tt) * env / h;
    return (float) (.1 * y) + n;
}

struct Res { int clicks = 0; double firstClick = -1, worstRatio = 0, peak = 0, nearTop = 0, silentRms = 0; };

static Res analyse (const std::vector<float>& x, double sr)
{
    Res r;
    const size_t W = 2048;
    std::vector<double> d2 (x.size(), 0.0);
    for (size_t i = 2; i < x.size(); ++i) d2[i] = (double) x[i] - 2.0 * x[i - 1] + x[i - 2];
    // скользящее среднее квадрата второй разности (окно W, без центра)
    double acc = 0; std::vector<double> loc (x.size(), 0.0);
    for (size_t i = 0; i < x.size(); ++i)
    {
        acc += d2[i] * d2[i];
        if (i >= W) acc -= d2[i - W] * d2[i - W];
        loc[i] = acc / (double) std::min (i + 1, W);
    }
    long last = -100000; long top = 0; double sil = 0; long nsil = 0;
    for (size_t i = W; i + W < x.size(); ++i)
    {
        const double a = std::fabs (x[i]);
        r.peak = std::max (r.peak, a);
        if (a > .95) ++top;
        const double around = std::sqrt (.5 * (loc[i - 64] + loc[std::min (x.size() - 1, i + W)])) + 1e-6;
        const double ratio = std::fabs (d2[i]) / around;
        if (std::fabs (d2[i]) > .02 && ratio > 12 && (long) i - last > (long) (.01 * sr))
        {
            ++r.clicks; last = (long) i;
            if (r.firstClick < 0) r.firstClick = (double) i / sr;
            r.worstRatio = std::max (r.worstRatio, ratio);
        }
        if ((double) i / sr > 30) { sil += a * a; ++nsil; }
    }
    r.nearTop = 100.0 * (double) top / (double) x.size();
    r.silentRms = std::sqrt (sil / std::max (1L, nsil));
    return r;
}

int main (int argc, char** argv)
{
    const std::string filt = argc > 1 ? argv[1] : "";
    const double sr = 48000;
    const int B = 256;
    int bad = 0, n = 0;
    std::printf ("%-34s %6s %8s %6s %7s %8s\n", "", "щелчк", "первый,с", "пик", "потолок%", "тишина rms");
    for (const auto& pr : memoria::presets())
    {
        if (! filt.empty() && std::string (pr.name).find (filt) == std::string::npos) continue;
        Engine e; e.prepare (sr);
        Params p = memoria::toParams (pr);
        Rng rng (5);
        std::vector<float> il (B), ir (B), ol (B), orr (B), outL, outR;
        const long total = (long) (49 * sr);
        for (long k = 0; k < total; k += B)
        {
            for (int i = 0; i < B; ++i) { il[(size_t) i] = chords ((double) (k + i) / sr, rng); ir[(size_t) i] = il[(size_t) i]; }
            e.process (il.data(), ir.data(), ol.data(), orr.data(), B, p);
            outL.insert (outL.end(), ol.begin(), ol.end()); outR.insert (outR.end(), orr.begin(), orr.end());
        }
        Res a = analyse (outL, sr), b = analyse (outR, sr);
        const int clicks = a.clicks + b.clicks;
        const double first = a.firstClick < 0 ? b.firstClick : (b.firstClick < 0 ? a.firstClick : std::min (a.firstClick, b.firstClick));
        const bool flag = clicks > 0 || a.nearTop + b.nearTop > .1;
        bad += flag; ++n;
        std::printf ("%-34s %6d %8.1f %6.3f %7.2f %8.4f %s\n", pr.name, clicks, first, std::max (a.peak, b.peak),
                     .5 * (a.nearTop + b.nearTop), .5 * (a.silentRms + b.silentRms), flag ? "  <<<" : "");
    }
    std::printf ("\nпроблемных пресетов: %d из %d\n", bad, n);
    return 0;
}
