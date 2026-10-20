// Автономный тест движка Memoria (без JUCE):
//   g++ -O2 -std=c++17 memoria/tests/engine_test.cpp -o /tmp/mt && /tmp/mt
#include "../Source/MemEngine.h"
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
