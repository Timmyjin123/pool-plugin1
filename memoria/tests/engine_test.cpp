// Автономный тест движка Memoria (без JUCE):
//   g++ -O2 -std=c++17 memoria/tests/engine_test.cpp -o /tmp/mt && /tmp/mt
#include "../Source/MemEngine.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>

using namespace mem;

static int fails = 0;
#define CHECK(c, msg) do { if (! (c)) { ++fails; std::printf ("FAIL: %s\n", msg); } else std::printf ("ok:   %s\n", msg); } while (0)

struct Run
{
    double sr;
    Engine e;
    Params p;
    double ph = 0;
    Rng rg { 7 };
    bool nan = false;
    float peak = 0;
    explicit Run (double s) : sr (s) { e.prepare (sr); }

    // seconds секунд; signal: 0 тишина, 1 тон 220 Гц + шум, 2 короткие щипки
    double run (double seconds, int signal, std::vector<float>* capL = nullptr)
    {
        const int B = 256;
        std::vector<float> il (B), ir (B), ol (B), orr (B);
        const int nb = (int) (seconds * sr / B);
        double rms = 0; long cnt = 0;
        for (int b = 0; b < nb; ++b)
        {
            for (int i = 0; i < B; ++i)
            {
                float x = 0;
                if (signal == 1) x = (float) (.3 * std::sin (ph) + .02 * rg.bi());
                if (signal == 3) x = .2f * rg.bi();   // белый шум
                if (signal == 2) { const double t = std::fmod (ph / (2 * M_PI * 220.0) , .5); x = (float) (.5 * std::exp (-t * 30) * std::sin (ph * 1.5)); }
                ph += 2 * M_PI * 220.0 / sr;
                il[(size_t) i] = x; ir[(size_t) i] = x * .8f;
            }
            e.process (il.data(), ir.data(), ol.data(), orr.data(), B, p);
            for (int i = 0; i < B; ++i)
            {
                const float a = ol[(size_t) i], c = orr[(size_t) i];
                if (! std::isfinite (a) || ! std::isfinite (c)) nan = true;
                peak = std::max (peak, std::max (std::fabs (a), std::fabs (c)));
                rms += (double) a * a + (double) c * c; cnt += 2;
                if (capL) capL->push_back (a);
            }
        }
        return std::sqrt (rms / std::max (1L, cnt));
    }
};

static double poolEnergy (const Engine& e)
{
    double s = 0; const float* d = e.poolData();
    for (int i = 0; i < e.getPoolLen(); i += 7) s += (double) d[i] * d[i];
    return s;
}

int main()
{
    const double sr = 48000;
    std::printf ("== все 12 алгоритмов: стабильность и уровень\n");
    double worstCpu = 0;
    for (int m = 0; m < 4; ++m)
        for (int s = 0; s < 3; ++s)
        {
            Run r (sr);
            r.p.mode = m; r.p.size = s; r.p.mix = 1; r.p.decay = .85f; r.p.warmth = .4f; r.p.dynamics = .8f;
            r.p.spread = .6f; r.p.drift = .7f; r.p.lowcut = 1; r.p.highcut = 2;
            const auto t0 = std::chrono::steady_clock::now();
            const double longest = (m == 3 && s == 0) ? 24.0 : 0.0;   // TAPES L: петли 21–23 с
            const double playing = r.run (12 + longest, 1);
            const double tail = r.run (8, 0);
            const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
            const double cpu = secs / (20.0 + longest) * 100.0;
            worstCpu = std::max (worstCpu, cpu);
            std::printf ("  %-8s %s: rms play %.3f tail %.3f peak %.3f  cpu %.2f%%\n", modeName (m), s == 0 ? "L" : s == 1 ? "M" : "S", playing, tail, r.peak, cpu);
            std::string tag = std::string (modeName (m)) + (s == 0 ? " L" : s == 1 ? " M" : " S");
            CHECK (! r.nan, (tag + ": нет NaN/Inf").c_str());
            CHECK (r.peak <= 1.0f, (tag + ": пик не выше 0 дБ").c_str());
            CHECK (playing > .01, (tag + ": мокрый сигнал есть").c_str());
            CHECK (tail > .003, (tag + ": память звучит после остановки").c_str());
        }
    std::printf ("  худший расход: %.2f%% одного ядра\n", worstCpu);
    CHECK (worstCpu < 15, "расход процессора умеренный");

    std::printf ("== слои + пинг-понг + blur на случайных характерах\n");
    double worstCpu2 = 0;
    for (int m = 0; m < 4; ++m)
        for (int seed : { 0, 17, 404, 999 })
        {
            Run r (sr);
            r.p.mode = m; r.p.size = seed % 3; r.p.mix = 1; r.p.decay = .95f; r.p.warmth = .5f; r.p.dynamics = .9f;
            r.p.spread = .7f; r.p.drift = .8f; r.p.layers = 1; r.p.layerSeed = seed; r.p.pingpong = 1; r.p.blur = true;
            const auto t0 = std::chrono::steady_clock::now();
            const double a = r.run (14, 1);
            const double t = r.run (10, 0);
            const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
            worstCpu2 = std::max (worstCpu2, secs / 24.0 * 100.0);
            char buf[160];
            std::snprintf (buf, sizeof buf, "%s seed %d: без NaN, пик <= 0 дБ, звучит (rms %.3f / хвост %.3f, пик %.3f)", modeName (m), seed, a, t, r.peak);
            CHECK (! r.nan && r.peak <= 1.0f && a > .01, buf);
        }
    std::printf ("  худший расход со всеми эффектами: %.2f%%\n", worstCpu2);
    CHECK (worstCpu2 < 20, "расход со слоями, пинг-понгом и blur умеренный");

    std::printf ("== тёмный слой −12 добавляет октаву вниз\n");
    {
        auto energyAt = [&] (float layers, double f)
        {
            Run r (sr); r.p.mode = 2; r.p.size = 2; r.p.mix = 1; r.p.decay = .6f; r.p.highcut = 0; r.p.lowcut = 0;
            r.p.layers = layers; r.p.layerSeed = 0; r.p.warmth = 0; r.p.depth = 0;   // мерим только слои
            std::vector<float> cap;
            r.run (6, 1);
            r.run (3, 1, &cap);
            double re = 0, im = 0;
            for (size_t i = 0; i < cap.size(); ++i) { const double w = 2 * M_PI * f * (double) i / sr; re += cap[i] * std::cos (w); im += cap[i] * std::sin (w); }
            return std::sqrt (re * re + im * im) / (double) cap.size();
        };
        const double off440 = energyAt (0, 110), on440 = energyAt (1, 110);
        std::printf ("  энергия на 110 Гц: слои выкл %.5f, вкл %.5f\n", off440, on440);
        CHECK (on440 > off440 * 3, "октава вниз появляется в спектре");
    }

    std::printf ("== CHAOS и RUIN на максимуме\n");
    for (int m = 0; m < 4; ++m)
    {
        Run r (sr);
        r.p.mode = m; r.p.size = 1; r.p.mix = 1; r.p.decay = .9f; r.p.layers = .8f; r.p.pingpong = .5f; r.p.blur = (m % 2) == 1;
        r.p.chaos = 1; r.p.ruin = 1;
        const auto t0 = std::chrono::steady_clock::now();
        const double a = r.run (25, 1);
        const double t = r.run (10, 0);
        const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        char buf[200];
        std::snprintf (buf, sizeof buf, "%s: без NaN, пик <= 0 дБ (rms %.3f / %.3f, пик %.3f, событий %ld, эрозий %d, cpu %.1f%%)",
                       modeName (m), a, t, r.peak, r.e.chaosEvents(), r.e.erosions(), secs / 35.0 * 100);
        CHECK (! r.nan && r.peak <= 1.0f, buf);
        CHECK (r.e.chaosEvents() >= 6, "хаос: события случаются (≥ 6 за 35 с; у DIFFUSE и TAPES события длиннее)");
        CHECK (r.e.erosions() >= 10, "руины: эрозия работает");
    }
    {
        Run r (sr); r.p.mode = 2; r.p.size = 2; r.p.mix = 1; r.p.decay = .85f; r.p.ruin = .8f;
        const double quiet = r.run (12, 0);
        std::printf ("  ruin .8 без входа: rms %.4f\n", quiet);
        CHECK (quiet > .002 && quiet < .2, "руины: шум и треск копятся в памяти даже без входа, но не разгоняются");
        Run z (sr); z.p.mode = 2; z.p.mix = 1; z.p.chaos = 0; z.p.ruin = 0;
        const double zero = z.run (6, 0);
        CHECK (zero < 1e-6, "при CHAOS = 0 и RUIN = 0 тишина остаётся тишиной");
    }

    std::printf ("== HARMONY: в режиме OCT слои не уводят из тональности\n");
    {
        // вход — ля (220 Гц) с тихим шумом
        auto pcEnergy = [&] (int harmony, int seed, double& inKey, double& off)
        {
            Run r (sr); r.p.mode = 2; r.p.size = 2; r.p.mix = 1; r.p.decay = .5f; r.p.warmth = 0; r.p.highcut = 0; r.p.lowcut = 0;
            r.p.layers = 1; r.p.layerSeed = seed; r.p.harmony = harmony; r.p.depth = 0; r.p.pingpong = 0;
            std::vector<float> cap; r.run (5, 1); r.run (3, 1, &cap);
            auto en = [&] (double f) { double re = 0, im = 0; for (size_t i = 0; i < cap.size(); ++i) { const double w = 2 * M_PI * f * (double) i / sr; re += cap[i] * std::cos (w); im += cap[i] * std::sin (w); } return std::sqrt (re * re + im * im) / (double) cap.size(); };
            inKey = en (110) + en (220) + en (440) + en (880);
            // чужие для ля классы высоты (не её натуральные обертоны): ре, соль, фа, соль-диез, си-бемоль
            off = en (146.83) + en (293.66) + en (587.33) + en (196.0) + en (392.0) + en (174.61) + en (349.23) + en (207.65) + en (415.30) + en (233.08) + en (466.16);
        };
        double worst = 0, freeWorst = 0;
        for (int seed : { 0, 9, 31, 42, 51, 101, 222, 404, 666, 999 })
        {
            double ik, of; pcEnergy (0, seed, ik, of); worst = std::max (worst, of / ik);
            pcEnergy (5, seed, ik, of); freeWorst = std::max (freeWorst, of / ik);
        }
        std::printf ("  доля чужих нот: OCT %.3f, FREE %.3f\n", worst, freeWorst);
        CHECK (worst < .05, "OCT: чужие ноты не появляются ни на одном наборе");
        CHECK (freeWorst > worst * 3, "FREE действительно добавляет другие интервалы (проверка теста)");
    }

    std::printf ("== SPACE, SHIMMER, PRISM\n");
    {
        // длина хвоста зала: щелчок → через сколько секунд уровень падает на 30 дБ (decay памяти = 0, чтобы мерить только зал)
        auto tail30 = [&] (float space)
        {
            Run r (sr); r.p.mode = 3; r.p.size = 2; r.p.mix = 1; r.p.decay = 0; r.p.space = space; r.p.depth = 0;
            Engine& e = r.e; const int B = 256;
            std::vector<float> il (B, 0.0f), ol (B), orr (B);
            for (int i = 0; i < 8; ++i) { std::fill (il.begin(), il.end(), 0.0f); if (i == 0) il[0] = .9f; e.process (il.data(), il.data(), ol.data(), orr.data(), B, r.p); }
            // энергия огибающей по 100 мс
            std::vector<double> env;
            std::fill (il.begin(), il.end(), 0.0f);
            double acc = 0; int n = 0;
            for (int b = 0; b < (int) (40 * sr / B); ++b)
            {
                e.process (il.data(), il.data(), ol.data(), orr.data(), B, r.p);
                for (int i = 0; i < B; ++i) { acc += (double) ol[(size_t) i] * ol[(size_t) i]; if (++n == (int) (.1 * sr)) { env.push_back (acc); acc = 0; n = 0; } }
            }
            double peak = 0; size_t pi = 0;
            for (size_t i = 0; i < env.size(); ++i) if (env[i] > peak) { peak = env[i]; pi = i; }
            for (size_t i = pi; i < env.size(); ++i) if (env[i] < peak * 1e-3) return (double) (i - pi) * .1;
            return 40.0;
        };
        const double t1 = tail30 (.2f), t2 = tail30 (.6f), t3 = tail30 (1.0f);
        std::printf ("  хвост −30 дБ: space .2 → %.1f с, .6 → %.1f с, 1.0 → %.1f с\n", t1, t2, t3);
        CHECK (t1 < t2 && t2 < t3 && t3 > 6, "SPACE удлиняет хвост, на максимуме — больше 6 секунд");

        Run r (sr); r.p.mode = 1; r.p.size = 0; r.p.mix = 1; r.p.decay = .95f; r.p.space = 1; r.p.shimmer = 1; r.p.prism = 1; r.p.layers = 1; r.p.blur = true;
        const auto t0 = std::chrono::steady_clock::now();
        r.run (10, 1);
        const double early = r.run (10, 0);
        const double late = r.run (20, 0);
        const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        std::printf ("  шиммер на максимуме: хвост %.3f → %.3f, пик %.3f, cpu %.1f%%\n", early, late, r.peak, secs / 40 * 100);
        CHECK (! r.nan && r.peak <= 1.0f, "всё на максимуме: без NaN, пик <= 0 дБ");
        CHECK (late < early * 1.2 + .01, "шиммер не разгоняется в бесконечный гул");
        CHECK (secs / 40 * 100 < 20, "расход с залом, шиммером и призмой умеренный");

        // PRISM двигает спектр: центр тяжести меняется во времени
        Run q (sr); q.p.mode = 2; q.p.mix = 1; q.p.decay = .9f; q.p.prism = 1; q.p.drift = 1;
        double cmin = 1e9, cmax = 0;
        for (int k = 0; k < 8; ++k)
        {
            std::vector<float> cap; q.run (2.5, 1, &cap);
            double lo = 0, hi = 0, lpS = 0;
            for (float v : cap) { lpS += (v - lpS) * .06; lo += lpS * lpS; hi += (v - lpS) * (v - lpS); }
            const double ratio = hi / std::max (1e-12, lo);
            cmin = std::min (cmin, ratio); cmax = std::max (cmax, ratio);
        }
        std::printf ("  prism: доля верха меняется %.3f … %.3f\n", cmin, cmax);
        CHECK (cmax > cmin * 1.3, "PRISM заметно двигает спектр во времени");
    }

    std::printf ("== TAIL: фильтр хвостов\n");
    {
        auto split = [&] (float lo, float hi, double& low, double& high)
        {
            Run r (sr); r.p.mode = 2; r.p.size = 2; r.p.mix = 1; r.p.decay = .8f; r.p.space = .5f; r.p.tailLo = lo; r.p.tailHi = hi;
            std::vector<float> cap; r.run (6, 3); r.run (3, 3, &cap);
            double lp = 0; low = high = 0;
            for (float v : cap) { lp += (v - lp) * .25; low += lp * lp; high += (v - lp) * (v - lp); }   // граница ~2 кГц
        };
        double l0, h0, l1, h1, l2, h2;
        split (20, 20000, l0, h0); split (20, 1200, l1, h1); split (1500, 20000, l2, h2);
        std::printf ("  верх: открыто %.2f → срез 1.2k %.2f; низ: открыто %.2f → срез 1.5k %.2f\n", h0, h1, l0, l2);
        CHECK (h1 < h0 * .4, "TAIL: правая точка убирает верх хвостов");
        CHECK (l2 < l0 * .5, "TAIL: левая точка убирает низ хвостов");
    }

    std::printf ("== DEPTH: глубокий упругий саб\n");
    {
        auto band = [&] (const std::vector<float>& cap, double f)
        { double re = 0, im = 0; for (size_t i = 0; i < cap.size(); ++i) { const double w = 2 * M_PI * f * (double) i / sr; re += cap[i] * std::cos (w); im += cap[i] * std::sin (w); } return std::sqrt (re * re + im * im) / (double) cap.size(); };
        auto measure = [&] (float depth, double& e110, double& e55)
        {
            Run r (sr); r.p.mode = 2; r.p.size = 2; r.p.mix = 1; r.p.decay = .6f; r.p.depth = depth; r.p.layers = 0;
            std::vector<float> cap; r.run (4, 1); r.run (3, 1, &cap);
            e110 = band (cap, 110); e55 = band (cap, 55);
        };
        double a110, a55, b110, b55, c110, c55;
        measure (0, a110, a55); measure (.4f, b110, b55); measure (1, c110, c55);
        std::printf ("  110 Гц: %.4f → %.4f → %.4f; 55 Гц: %.4f → %.4f → %.4f (depth 0 / .4 / 1)\n", a110, b110, c110, a55, b55, c55);
        CHECK (b110 > a110 * 3, "DEPTH: октава вниз (−12) появляется");
        CHECK (c55 > b55 * 3 && c55 > a55 * 5, "DEPTH на максимуме: вторая октава вниз (−24) появляется");
        // упругость: на щипках саб громче в момент ноты, чем между нотами
        Run q (sr); q.p.mode = 3; q.p.size = 2; q.p.mix = 1; q.p.decay = .3f; q.p.depth = 1; q.p.space = 0;
        std::vector<float> cap; q.run (4, 2); q.run (4, 2, &cap);
        // огибающая саба (ниже 150 Гц) по 10 мс
        double lp1 = 0, lp2 = 0; std::vector<double> env; double acc = 0; int n = 0;
        for (float v : cap) { lp1 += (v - lp1) * .02; lp2 += (lp1 - lp2) * .02; acc += lp2 * lp2; if (++n == 480) { env.push_back (acc); acc = 0; n = 0; } }
        std::vector<double> sorted = env; std::sort (sorted.begin(), sorted.end());
        const double hiE = sorted[sorted.size() * 9 / 10], loE = sorted[sorted.size() / 5];
        std::printf ("  саб на щипках: громко %.3g, между нотами %.3g\n", hiE, loE);
        CHECK (hiE > loE * 2.5, "DEPTH: саб упругий — пружинит с нотами, а не стоит ровным гулом");
        CHECK (! q.nan && q.peak <= 1.0f, "DEPTH на максимуме: без NaN и перегруза");
    }

    std::printf ("== decay = 0: память быстро гаснет\n");
    {
        Run r (sr); r.p.mode = 3; r.p.size = 2; r.p.mix = 1; r.p.decay = 0; r.p.highcut = 0;
        r.run (3, 1);
        const double t1 = r.run (2.0, 0);   // последний круг (2.1–2.6 с) ещё звучит
        r.run (1.0, 0);
        const double t2 = r.run (5, 0);
        std::printf ("  tail1 %.4f tail2 %.5f\n", t1, t2);
        CHECK (t2 < .002, "без обратной связи хвост пропадает после одного круга");
    }

    std::printf ("== freeze держит память, вход не пишется\n");
    {
        Run r (sr); r.p.mode = 2; r.p.size = 2; r.p.mix = 1; r.p.decay = .3f; r.p.highcut = 0; r.p.lowcut = 0;
        r.run (6, 1);
        r.p.freeze = true;
        const double a = r.run (6, 0);
        const double b = r.run (12, 0);
        std::printf ("  rms frozen early %.4f late %.4f\n", a, b);
        CHECK (b > a * .6, "заморозка почти не теряет энергию за 12 с");
        Run q (sr); q.p = r.p; q.p.freeze = true;
        q.run (4, 1);
        CHECK (poolEnergy (q.e) < 1e-6, "в заморозке вход не попадает в память");
    }

    std::printf ("== смена режима сохраняет память, erase стирает\n");
    {
        Run r (sr); r.p.mode = 0; r.p.size = 0; r.p.mix = 1; r.p.decay = .9f;
        r.run (8, 1);
        const double e0 = poolEnergy (r.e);
        r.p.mode = 3; r.p.size = 0;
        const double after = r.run (3, 0);
        std::printf ("  pool energy %.3f, tapes after switch rms %.4f\n", e0, after);
        CHECK (e0 > 1, "память набралась");
        CHECK (after > .003, "после смены режима старый материал звучит");
        r.e.requestErase();
        r.run (.2, 0);
        CHECK (poolEnergy (r.e) < 1e-9, "erase очищает память");
        const double silent = r.run (2, 0);
        CHECK (silent < 1e-5, "после erase тишина");
    }

    std::printf ("== reverse и переходы без щелчков\n");
    {
        Run r (sr); r.p.mode = 1; r.p.size = 1; r.p.mix = 1; r.p.decay = .8f;
        r.run (6, 1);
        std::vector<float> cap;
        r.p.reverse = true;
        r.run (1, 0, &cap);
        float maxJump = 0;
        for (size_t i = 1; i < cap.size(); ++i) maxJump = std::max (maxJump, std::fabs (cap[i] - cap[i - 1]));
        std::printf ("  max sample step around reverse %.4f\n", maxJump);
        CHECK (! r.nan, "reverse без NaN");
        CHECK (maxJump < .2f, "нет резких скачков при развороте");
    }

    std::printf ("== другие частоты дискретизации\n");
    for (double s2 : { 44100.0, 96000.0 })
    {
        Run r (s2); r.p.mode = 0; r.p.mix = 1; r.p.decay = .8f;
        const double a = r.run (6, 1);
        char buf[64]; std::snprintf (buf, sizeof buf, "%.0f Гц: работает", s2);
        CHECK (! r.nan && a > .01 && r.peak <= 1.0f, buf);
    }

    std::printf ("\n%s (%d ошибок)\n", fails ? "ЕСТЬ ОШИБКИ" : "ВСЁ ХОРОШО", fails);
    return fails ? 1 : 0;
}
