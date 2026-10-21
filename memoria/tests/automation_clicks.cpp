// Щелчки при изменении параметров во время звука: ручки, кнопки, смена пресетов.
//   g++ -O2 -std=c++17 memoria/tests/automation_clicks.cpp -o /tmp/ac && /tmp/ac
// Каждый сценарий: 12 с звучит пресет, в середине параметр меняется (скачком или плавно, как ручкой).
#include "../Source/Presets.h"
#include <cstdio>
#include <functional>
#include <string>

using namespace mem;

static float input (double t, Rng& rng)
{
    static const double f[3] = { 130.81, 164.81, 196.0 };
    const double tt = std::fmod (t, 2.5);
    const double env = std::min (1.0, tt / .01) * std::exp (-tt * .8) * std::min (1.0, (2.5 - tt) / .05);
    double y = 0;
    for (double fr : f) for (int h = 1; h <= 5; ++h) y += std::sin (2 * M_PI * fr * h * t) / h;
    return (float) (.08 * env * y + rng.bi() * 1e-4);
}

struct Scn { std::string name; int preset; std::function<void (Params&, double)> change; };

// число щелчков на выходе (скачок второй разности в 12 раз выше окружения)
static int countClicks (const std::vector<float>& x, double sr, double& worst, double& at)
{
    const size_t W = 2048; int c = 0; long last = -100000; worst = 0; at = -1;
    std::vector<double> d2 (x.size(), 0.0), loc (x.size(), 0.0);
    for (size_t i = 2; i < x.size(); ++i) d2[i] = (double) x[i] - 2.0 * x[i - 1] + x[i - 2];
    double acc = 0;
    for (size_t i = 0; i < x.size(); ++i) { acc += d2[i] * d2[i]; if (i >= W) acc -= d2[i - W] * d2[i - W]; loc[i] = acc / (double) std::min (i + 1, W); }
    for (size_t i = W; i + W < x.size(); ++i)
    {
        const double around = std::sqrt (.5 * (loc[i - 64] + loc[i + W])) + 1e-6;
        const double r = std::fabs (d2[i]) / around;
        if (std::fabs (d2[i]) > .02 && r > 12 && (long) i - last > (long) (.01 * sr))
        { ++c; last = (long) i; if (std::fabs (d2[i]) > worst) { worst = std::fabs (d2[i]); at = (double) i / sr; } }
    }
    return c;
}

int main()
{
    const double sr = 48000;
    const auto& P = memoria::presets();
    auto find = [&] (const char* n) { for (size_t i = 0; i < P.size(); ++i) if (std::string (P[i].name) == n) return (int) i; return 0; };
    const int drone = find ("Дрон · Бесконечный"), flow = find ("Течение · Тёплое мерцание"), echo = find ("Эхо · Сеть"), hall = find ("Космос · Бесконечный зал");
    auto ramp = [] (double t, double a, double b, double t0, double t1) { return t < t0 ? a : t > t1 ? b : a + (b - a) * (t - t0) / (t1 - t0); };

    std::vector<Scn> sc = {
        { "SPACE ручкой .2 → .9",          hall,  [&] (Params& p, double t) { p.space = (float) ramp (t, .2, .9, 5, 7); } },
        { "SPACE скачком .3 → .8",         hall,  [&] (Params& p, double t) { p.space = t < 6 ? .3f : .8f; } },
        { "CHAOS 0 → .4",                  flow,  [&] (Params& p, double t) { p.chaos = t < 6 ? 0.0f : .4f; } },
        { "CHAOS .4 → 0",                  flow,  [&] (Params& p, double t) { p.chaos = t < 6 ? .4f : 0.0f; } },
        { "DEPTH 0 → .6",                  drone, [&] (Params& p, double t) { p.depth = t < 6 ? 0.0f : .6f; } },
        { "LAYERS 0 → .7",                 drone, [&] (Params& p, double t) { p.layers = t < 6 ? 0.0f : .7f; } },
        { "SPACE 0 → .7",                  drone, [&] (Params& p, double t) { p.space = t < 6 ? 0.0f : .7f; } },
        { "PING-PONG 0 → .8",              echo,  [&] (Params& p, double t) { p.pingpong = t < 6 ? 0.0f : .8f; } },
        { "SHIMMER вкл",                   hall,  [&] (Params& p, double t) { p.shimmer = t < 6 ? 0.0f : .6f; } },
        { "HARMONY OCT → FREE",            drone, [&] (Params& p, double t) { p.harmony = t < 6 ? 0 : 2; } },
        { "HARMONY OCT → 5TH (шиммер)",    hall,  [&] (Params& p, double t) { p.shimmer = .6f; p.harmony = t < 6 ? 0 : 1; } },
        { "MODE Diffuse → Network",        drone, [&] (Params& p, double t) { p.mode = t < 6 ? 1 : 2; } },
        { "SIZE Long → Short",             echo,  [&] (Params& p, double t) { p.size = t < 6 ? 0 : 2; } },
        { "REVERSE вкл",                   flow,  [&] (Params& p, double t) { p.reverse = t >= 6; } },
        { "FREEZE вкл",                    flow,  [&] (Params& p, double t) { p.freeze = t >= 6; } },
        { "BLUR вкл",                      echo,  [&] (Params& p, double t) { p.blur = t >= 6; } },
        { "MONO вкл",                      echo,  [&] (Params& p, double t) { p.mono = t >= 6; } },
        { "TONE ручкой",                   flow,  [&] (Params& p, double t) { p.tone = (float) ramp (t, .2, .8, 5, 6); } },
        { "TAIL верх 16k → 800",           flow,  [&] (Params& p, double t) { p.tailHi = (float) ramp (t, 16000, 800, 5, 5.5); } },
        { "DECAY .5 → .98",                drone, [&] (Params& p, double t) { p.decay = t < 6 ? .5f : .98f; } },
        { "RUIN 0 → .7",                   flow,  [&] (Params& p, double t) { p.ruin = t < 6 ? 0.0f : .7f; } },
        { "LO-CUT 0 → 3, HI-CUT 0 → 3",    drone, [&] (Params& p, double t) { p.lowcut = t < 6 ? 0 : 3; p.highcut = t < 6 ? 0 : 3; } },
        { "DRIFT 0 → 1",                   flow,  [&] (Params& p, double t) { p.drift = t < 6 ? 0.0f : 1.0f; } },
        { "CORE ручкой 0 → 1 (Tapes)",     echo,  [&] (Params& p, double t) { p.mode = 3; p.core = (float) ramp (t, 0, 1, 5, 6); } },
        { "CORE скачком 0 → 1 (Grains)",   flow,  [&] (Params& p, double t) { p.mode = 0; p.core = t < 6 ? 0.0f : 1.0f; } },
        { "CORE скачком 1 → 0 (Diffuse)",  drone, [&] (Params& p, double t) { p.mode = 1; p.core = t < 6 ? 1.0f : 0.0f; } },
        { "CORE ручкой (Network)",         echo,  [&] (Params& p, double t) { p.mode = 2; p.core = (float) ramp (t, .1, .95, 5, 5.5); } },
        { "ORGANISM TAPE → ASH",           flow,  [&] (Params& p, double t) { p.organism = t < 6 ? 1 : 4; } },
        { "ORGANISM OFF → GLASS",          hall,  [&] (Params& p, double t) { p.organism = t < 6 ? 0 : 3; } },
        { "HARMONY OCT → 9TH",             drone, [&] (Params& p, double t) { p.layers = .8f; p.harmony = t < 6 ? 0 : 4; } },
        { "HARMONY SUS → 7TH (шиммер)",    hall,  [&] (Params& p, double t) { p.shimmer = .6f; p.harmony = t < 6 ? 2 : 3; } },
        { "DRIFT .9 автопанорама, смена режимов", drone, [&] (Params& p, double t) { p.drift = .9f; p.mode = t < 4 ? 0 : t < 7 ? 1 : t < 9 ? 2 : 3; } },
        { "TAIL верх 800 → 18k (мягкий)",  hall,  [&] (Params& p, double t) { p.tailHi = (float) ramp (t, 800, 18000, 5, 5.3); } },
        { "TAIL верх скачком 20k → 400",   flow,  [&] (Params& p, double t) { p.tailHi = t < 6 ? 20000.0f : 400.0f; } },
        { "SEED смена (DICE)",            drone, [&] (Params& p, double t) { p.layers = .8f; p.layerSeed = t < 6 ? 0 : 404; } },
    };
    // смена пресетов подряд
    for (int k = 0; k < 6; ++k)
    {
        const int a = (k * 11) % (int) P.size(), b = (k * 11 + 7) % (int) P.size();
        sc.push_back ({ std::string ("ПРЕСЕТ ") + P[(size_t) a].name + " → " + P[(size_t) b].name, a,
                        [&, b] (Params& p, double t) { if (t >= 6) p = memoria::toParams (P[(size_t) b]); } });
    }

    int bad = 0;
    for (auto& s : sc)
    {
        Engine e; e.prepare (sr); Rng rng (9);
        const int B = 256;
        std::vector<float> il (B), ol (B), orr (B), outL, outR;
        Params p = memoria::toParams (P[(size_t) s.preset]);
        for (long k = 0; k < (long) (12 * sr); k += B)
        {
            const double t = (double) k / sr;
            Params q = memoria::toParams (P[(size_t) s.preset]);
            q.chaos = 0;                 // случайные сбои мешают замеру; CHAOS проверяется отдельно
            s.change (q, t);
            for (int i = 0; i < B; ++i) il[(size_t) i] = input ((double) (k + i) / sr, rng);
            e.process (il.data(), il.data(), ol.data(), orr.data(), B, q);
            outL.insert (outL.end(), ol.begin(), ol.end()); outR.insert (outR.end(), orr.begin(), orr.end());
        }
        double w1, a1, w2, a2;
        const int c = countClicks (outL, sr, w1, a1) + countClicks (outR, sr, w2, a2);
        bad += c > 0;
        std::printf ("%-62s щелчков %2d  сильнейший %.3f на %.2f с%s\n", s.name.c_str(), c, std::max (w1, w2), w1 >= w2 ? a1 : a2, c ? "  <<<" : "");
    }
    std::printf ("\nсценариев со щелчками: %d из %zu\n", bad, sc.size());
    return 0;
}
