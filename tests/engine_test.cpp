// Автономный тест движка (без JUCE): g++ -O2 -std=c++17 -pthread engine_test.cpp -o t && ./t
#include "../Source/Engine.h"
#include <cstdio>

using namespace pool;

static double goertzel (const std::vector<float>& x, double f, double sr)
{
    double re = 0, im = 0;
    for (size_t i = 0; i < x.size(); ++i)
    {
        const double w = kTwoPi * f * (double) i / sr;
        re += x[i] * std::cos (w); im += x[i] * std::sin (w);
    }
    return std::sqrt (re * re + im * im) / (double) x.size();
}

static int fails = 0;
#define CHECK(c, msg) do { if (! (c)) { ++fails; std::printf ("FAIL: %s\n", msg); } else std::printf ("ok:   %s\n", msg); } while (0)

int main()
{
    const double sr = 48000;
    const int block = 512, nBlocks = (int) (sr * 14 / block);
    Engine e;
    e.prepare (sr);
    e.setOffline (true);

    EngineParams P;
    P.dw = 1.0f; P.drive = 1.0f; P.rvm = 0; P.dm = 0; P.farLevel = 0; P.pad = 0.8f; P.pdr = 0.4f;
    P.ppitch = 0; P.vol = .8f; P.hp = 20; P.lp = 18000; P.dens = 25; P.size = 300;

    std::vector<float> in (block), l (block), r (block), all;
    Rng rg (123);
    double ph = 0;
    bool nan = false;
    auto t0 = std::chrono::steady_clock::now();
    for (int b = 0; b < nBlocks; ++b)
    {
        for (int i = 0; i < block; ++i) { in[(size_t) i] = (float) (.3 * std::sin (ph) + .02 * rg.bi()); ph += kTwoPi * 220.0 / sr; }
        e.process (in.data(), nullptr, l.data(), r.data(), block, P);
        for (int i = 0; i < block; ++i)
        {
            if (! std::isfinite (l[(size_t) i]) || ! std::isfinite (r[(size_t) i])) nan = true;
            if (b > nBlocks / 2) all.push_back (l[(size_t) i]);
        }
    }
    const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    std::printf ("render 14 s audio in %.2f s (x%.1f realtime)\n", secs, 14.0 / secs);
    CHECK (! nan, "нет NaN/Inf");
    CHECK (e.underruns() == 0, "нет недобегов фонового потока (offline)");
    double e220 = goertzel (all, 220, sr), e330 = goertzel (all, 330.0, sr);
    std::printf ("  energy@220=%.5f @330=%.5f\n", e220, e330);
    CHECK (e220 > 0.001, "звук на основной высоте есть");

    // сдвиг высоты входа +12 → 440 Гц
    {
        Engine e2; e2.prepare (sr);
        EngineParams Q; Q.dw = 0; Q.drive = 1; Q.rvm = 0; Q.dm = 0; Q.pad = 0; Q.farLevel = 0; Q.ipitch = 12; Q.vol = .8f; Q.hp = 20; Q.lp = 18000;
        std::vector<float> out; double p2 = 0;
        for (int b = 0; b < (int) (sr * 3 / block); ++b)
        {
            for (int i = 0; i < block; ++i) { in[(size_t) i] = (float) (.4 * std::sin (p2)); p2 += kTwoPi * 220.0 / sr; }
            e2.process (in.data(), nullptr, l.data(), r.data(), block, Q);
            if (b > 60) out.insert (out.end(), l.begin(), l.end());
        }
        const double a440 = goertzel (out, 440, sr), a220 = goertzel (out, 220, sr);
        std::printf ("  pitch +12: @440=%.5f @220=%.5f\n", a440, a220);
        CHECK (a440 > 3 * a220, "сдвиг +12 даёт 440 Гц");
    }

    // далёкий голос на +12 над 220 → энергия около 440
    {
        Engine e3; e3.prepare (sr); e3.setOffline (true);
        EngineParams Q; Q.dw = 1; Q.dens = 0.05f; Q.drive = 1; Q.rvm = 0; Q.dm = 0; Q.pad = 0; Q.farLevel = 1; Q.freg = 12; Q.fdist = 0; Q.vol = .8f; Q.hp = 20; Q.lp = 18000;
        std::vector<float> out; double p3 = 0;
        for (int b = 0; b < (int) (sr * 12 / block); ++b)
        {
            for (int i = 0; i < block; ++i) { in[(size_t) i] = (float) (.4 * std::sin (p3)); p3 += kTwoPi * 220.0 / sr; }
            e3.process (in.data(), nullptr, l.data(), r.data(), block, Q);
            if (b > (int) (sr * 6 / block)) out.insert (out.end(), l.begin(), l.end());
        }
        const double a440 = goertzel (out, 440, sr), a220 = goertzel (out, 220, sr);
        std::printf ("  far +12: @440=%.5f @220=%.5f\n", a440, a220);
        CHECK (a440 > 2 * a220, "далёкий голос на октаву выше");
    }
    return fails ? 1 : 0;
}
