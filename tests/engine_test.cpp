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

    // фиксация ноты пэда: любой вход -> 440 Гц (MIDI 69)
    for (double fin : { 196.0, 300.0 })
    {
        Engine e4; e4.prepare (sr); e4.setOffline (true);
        EngineParams Q; Q.dw = 1; Q.dens = 0.05f; Q.drive = 1; Q.rvm = 0; Q.dm = 0; Q.farLevel = 0; Q.pad = 1; Q.pdr = 0; Q.pdark = 12000; Q.pwarm = 0; Q.pnote = 69; Q.vol = .8f; Q.hp = 20; Q.lp = 18000;
        std::vector<float> out; double p4 = 0;
        for (int b = 0; b < (int) (sr * 12 / block); ++b)
        {
            for (int i = 0; i < block; ++i) { in[(size_t) i] = (float) (.4 * std::sin (p4)); p4 += kTwoPi * fin / sr; }
            e4.process (in.data(), nullptr, l.data(), r.data(), block, Q);
            if (b > (int) (sr * 6 / block)) out.insert (out.end(), l.begin(), l.end());
        }
        const double a440 = goertzel (out, 440, sr), aIn = goertzel (out, fin, sr);
        std::printf ("  lock A4, in %.0f Hz: @440=%.5f @in=%.5f\n", fin, a440, aIn);
        CHECK (a440 > 4 * aIn, "пэд подтянут к выбранной ноте");
    }

    // строгость: чужой тон не должен оставаться вне гармоник ноты
    {
        double e0 = 0, e1 = 0;
        for (int mode = 0; mode < 2; ++mode)
        {
            Engine e5; e5.prepare (sr); e5.setOffline (true);
            EngineParams Q; Q.dw = 1; Q.dens = 0.05f; Q.drive = 1; Q.rvm = 0; Q.dm = 0; Q.farLevel = 0; Q.pad = 1; Q.pdr = 0; Q.pdark = 12000; Q.pwarm = 0; Q.pnote = 69; Q.pstrict = mode ? 1.0f : 0.0f; Q.vol = .8f; Q.hp = 20; Q.lp = 18000;
            std::vector<float> out; double pa = 0, pb = 0;
            for (int b = 0; b < (int) (sr * 12 / block); ++b)
            {
                for (int i = 0; i < block; ++i) { in[(size_t) i] = (float) (.4 * std::sin (pa) + .15 * std::sin (pb)); pa += kTwoPi * 200.0 / sr; pb += kTwoPi * 337.0 / sr; }
                e5.process (in.data(), nullptr, l.data(), r.data(), block, Q);
                if (b > (int) (sr * 6 / block)) out.insert (out.end(), l.begin(), l.end());
            }
            (mode ? e1 : e0) = goertzel (out, 337.0 * 440.0 / 200.0, sr);
        }
        std::printf ("  off-harmonic partial: strict0=%.5f strict1=%.5f\n", e0, e1);
        CHECK (e1 < e0 * .25, "строгость убирает негармоничные тона");
    }

    // шумовой вокодер: шум повторяет спектр (много энергии у 220 Гц, мало у 3 кГц) и не раздувается
    {
        Engine e6; e6.prepare (sr); e6.setOffline (true);
        EngineParams Q; Q.dw = 0; Q.dens = 0.05f; Q.drive = 1; Q.rvm = 0; Q.dm = 0; Q.pad = 0; Q.farLevel = 0; Q.nz = .5f; Q.nvoc = 1; Q.nfol = 1; Q.vol = .8f; Q.hp = 20; Q.lp = 18000;
        std::vector<float> out; double p6 = 0;
        for (int b = 0; b < (int) (sr * 4 / block); ++b)
        {
            for (int i = 0; i < block; ++i) { in[(size_t) i] = (float) (.3 * std::sin (p6)); p6 += kTwoPi * 220.0 / sr; }
            e6.process (in.data(), nullptr, l.data(), r.data(), block, Q);
            if (b > 40) out.insert (out.end(), l.begin(), l.end());
        }
        double rms = 0; for (float v : out) rms += v * v; rms = std::sqrt (rms / (double) out.size());
        auto band = [&] (double f0, double f1) { double s = 0; int n = 0; for (double f = f0; f <= f1; f += (f1 - f0) / 12) { const double a = goertzel (out, f, sr); s += a * a; ++n; } return s / n; };
        const double lo = band (180, 270), hi = band (2600, 3400);
        std::printf ("  noise vocoder: rms=%.4f  band220=%.3e band3k=%.3e\n", rms, lo, hi);
        CHECK (rms > .01 && rms < .8, "шум имеет разумную громкость");
        CHECK (lo > 10 * hi, "шумовой вокодер повторяет спектр источника");
    }

    // самоперезапись + реверб с насыщением и шиммером: устойчивость, нет NaN, звук ограничен
    {
        Engine e7; e7.prepare (sr);
        EngineParams Q; Q.dw = 1; Q.fbk = 1; Q.rvm = 1; Q.rdec = 24; Q.rsat = 1; Q.rshim = 1; Q.drive = 8; Q.dens = 60; Q.pad = 1; Q.farLevel = 1; Q.vol = 1; Q.nz = 1;
        bool bad = false; float pk = 0; double p7 = 0;
        for (int b = 0; b < (int) (sr * 20 / block); ++b)
        {
            for (int i = 0; i < block; ++i) { in[(size_t) i] = (b < 200 ? (float) (.5 * std::sin (p7)) : 0.0f); p7 += kTwoPi * 220.0 / sr; }
            e7.process (in.data(), nullptr, l.data(), r.data(), block, Q);
            for (int i = 0; i < block; ++i) { if (! std::isfinite (l[(size_t) i]) || ! std::isfinite (r[(size_t) i])) bad = true; pk = std::max (pk, std::max (std::fabs (l[(size_t) i]), std::fabs (r[(size_t) i]))); }
        }
        std::printf ("  extreme feedback: peak=%.3f\n", pk);
        CHECK (! bad && pk <= 1.01f, "экстремальная обратная связь устойчива и ограничена");
    }

    // шиммер: в хвосте реверба появляется октава вверх
    {
        double eOn = 0, eOff = 0;
        for (int m = 0; m < 2; ++m)
        {
            Engine e8; e8.prepare (sr); e8.setOffline (true);
            EngineParams Q; Q.dw = 0; Q.dens = 0.05f; Q.drive = 1; Q.dm = 0; Q.pad = 0; Q.farLevel = 0; Q.rvm = 1; Q.rdec = 12; Q.rshim = m ? 1.0f : 0.0f; Q.vol = 1; Q.hp = 20; Q.lp = 18000;
            std::vector<float> out; double p8 = 0;
            for (int b = 0; b < (int) (sr * 6 / block); ++b)
            {
                for (int i = 0; i < block; ++i) { in[(size_t) i] = (b < 40 ? (float) (.5 * std::sin (p8)) : 0.0f); p8 += kTwoPi * 220.0 / sr; }
                e8.process (in.data(), nullptr, l.data(), r.data(), block, Q);
                if (b > 90) out.insert (out.end(), l.begin(), l.end());
            }
            (m ? eOn : eOff) = goertzel (out, 440, sr);
        }
        std::printf ("  shimmer @440: off=%.6f on=%.6f\n", eOff, eOn);
        CHECK (eOn > 3 * eOff, "шиммер добавляет октаву вверх в хвост");
    }

    // авто-нота: тон 227 Гц подтягивается к ближайшему полутону (A#3 = 233.08 Гц)
    {
        Engine e9; e9.prepare (sr); e9.setOffline (true);
        EngineParams Q; Q.dw = 1; Q.dens = 0.05f; Q.drive = 1; Q.rvm = 0; Q.dm = 0; Q.farLevel = 0; Q.pad = 1; Q.pdr = 0; Q.pdark = 12000; Q.pwarm = 0; Q.pnote = -1; Q.pstab = 1; Q.vol = .8f; Q.hp = 20; Q.lp = 18000;
        std::vector<float> out; double p9 = 0;
        for (int b = 0; b < (int) (sr * 12 / block); ++b)
        {
            for (int i = 0; i < block; ++i) { in[(size_t) i] = (float) (.4 * std::sin (p9)); p9 += kTwoPi * 227.0 / sr; }
            e9.process (in.data(), nullptr, l.data(), r.data(), block, Q);
            if (b > (int) (sr * 6 / block)) out.insert (out.end(), l.begin(), l.end());
        }
        const double aT = goertzel (out, 233.08, sr), aS = goertzel (out, 227.0, sr);
        std::printf ("  auto note: @233.08=%.5f @227=%.5f\n", aT, aS);
        CHECK (aT > 4 * aS, "авто-нота подтягивает тон к ближайшему полутону");
    }
    return fails ? 1 : 0;
}
