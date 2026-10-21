// Поиск щелчков через настоящий процессор (как в Ableton): 44.1 кГц, блок 512, VARY, смены режимов и пресетов.
//   memoria_clicks [фильтр]
#include "../Source/PluginProcessor.h"
#include <cstdio>

static float chord (double t, juce::Random& r)
{
    static const double ch[3][3] = { { 130.81, 164.81, 196.0 }, { 110.0, 164.81, 220.0 }, { 87.31, 130.81, 174.61 } };
    const double per = 4.0; const int c = ((int) (t / per)) % 3; const double tt = std::fmod (t, per);
    if (std::fmod (t, 24.0) > 14.0) return (float) (r.nextFloat() * 2e-5f);   // паузы: хвосты на тишине
    const double env = std::min (1.0, tt / .01) * std::exp (-tt * .7) * std::min (1.0, (per - tt) / .05) * std::min (1.0, (14.0 - std::fmod (t, 24.0)) / .05);
    double y = 0; for (int k = 0; k < 3; ++k) for (int h = 1; h <= 5; ++h) y += std::sin (2 * juce::MathConstants<double>::pi * ch[c][k] * h * tt) * env / h;
    return (float) (.12 * y);
}

// щелчок: вторая разность резко выше окружения (чувствительно: в 8 раз и не меньше −50 дБ)
static int clicks (const std::vector<float>& x, double sr, std::vector<double>& at)
{
    const size_t W = 1024; int c = 0; long last = -100000;
    std::vector<double> d2 (x.size(), 0.0), cum (x.size() + 1, 0.0);
    for (size_t i = 2; i < x.size(); ++i) d2[i] = (double) x[i] - 2.0 * x[i - 1] + x[i - 2];
    for (size_t i = 0; i < x.size(); ++i) cum[i + 1] = cum[i] + d2[i] * d2[i];
    for (size_t i = W + 8; i + W + 8 < x.size(); ++i)
    {
        const double before = (cum[i - 8] - cum[i - 8 - W]) / W, after = (cum[i + 8 + W] - cum[i + 8]) / W;
        const double around = std::sqrt (std::max (before, after)) + 1e-7;
        const double a = std::fabs (d2[i]);
        if (a > .003 && a / around > 8 && (long) i - last > (long) (.02 * sr))
        {
            ++c; last = (long) i; if (at.size() < 6) at.push_back ((double) i / sr);
            if (getenv ("DUMP")) { std::printf ("  @%zu (блок %zu):", i, i % 512); for (size_t j = i - 5; j < i + 5; ++j) std::printf (" %.4f", x[j]); std::printf ("\n"); }
        }
    }
    return c;
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::String filt = argc > 1 ? argv[1] : "";
    const double sr = 44100; const int B = 512;
    auto run = [&] (const juce::String& name, std::function<void (MemoriaProcessor&, double)> step, double secs, int preset)
    {
        if (filt.isNotEmpty() && ! name.contains (filt)) return 0;
        MemoriaProcessor p; p.setPlayConfigDetails (2, 2, sr, B); p.prepareToPlay (sr, B);
        if (preset > 0) p.setCurrentProgram (preset);
        juce::AudioBuffer<float> buf (2, B); juce::MidiBuffer midi; juce::Random r (3);
        std::vector<float> L, R; L.reserve ((size_t) (secs * sr));
        for (long k = 0; k < (long) (secs * sr); k += B)
        {
            step (p, (double) k / sr);
            for (int i = 0; i < B; ++i) { const float x = chord ((double) (k + i) / sr, r); buf.setSample (0, i, x); buf.setSample (1, i, x); }
            p.processBlock (buf, midi);
            for (int i = 0; i < B; ++i) { L.push_back (buf.getSample (0, i)); R.push_back (buf.getSample (1, i)); }
        }
        std::vector<double> at;
        int c = clicks (L, sr, at) + clicks (R, sr, at);
        // всплески: пик за 10 мс в 4 раза выше среднего уровня за прошлую секунду
        {
            const size_t w10 = (size_t) (.01 * sr); double env = 0; long last = -1000000;
            for (size_t s0 = 0; s0 + w10 < L.size(); s0 += w10)
            {
                double pk = 0, e = 0; for (size_t i = s0; i < s0 + w10; ++i) { pk = std::max (pk, (double) std::max (std::fabs (L[i]), std::fabs (R[i]))); e += L[i] * L[i]; }
                const double rms = std::sqrt (e / w10);
                if (s0 > (size_t) sr && std::fmod (s0 / sr + .03, 4.0) > .08 && pk > .3 && pk > 4.0 * std::sqrt (env) * 1.41 && (long) s0 - last > (long) sr)
                { ++c; last = (long) s0; std::printf ("   всплеск %.2f с: пик %.2f при уровне %.3f\n", s0 / sr, pk, std::sqrt (env)); }
                env += (rms * rms - env) * .1;
            }
        }
        juce::String ts; for (auto t : at) ts << juce::String (t, 2) << " ";
        std::printf ("%-52s щелчков %3d  %s\n", name.toRawUTF8(), c, ts.toRawUTF8());
        return c > 0 ? 1 : 0;
    };
    auto set = [] (MemoriaProcessor& p, const char* id, float v) { auto* par = p.apvts.getParameter (id); par->setValueNotifyingHost (par->convertTo0to1 (v)); };
    int bad = 0, n = 0;
    const int np = (int) memoria::presets().size();
    for (int i = 1; i <= np; ++i)
    {
        ++n; bad += run (juce::String ("ПРЕСЕТ+VARY ") + juce::String::fromUTF8 (memoria::presets()[(size_t) i - 1].name),
                         [&] (MemoriaProcessor& p, double t) { if (t == 0) set (p, "vary", 1); }, 30, i);
    }
    for (int m = 0; m < 4; ++m)
    {
        ++n; bad += run ("SPACE .9 режим " + juce::String (m), [&, m] (MemoriaProcessor& p, double t) { if (t == 0) { set (p, "mode", (float) m); set (p, "space", .9f); set (p, "shimmer", 0); } }, 30, 0);
        ++n; bad += run ("SPACE .9 + SHIMMER режим " + juce::String (m), [&, m] (MemoriaProcessor& p, double t) { if (t == 0) { set (p, "mode", (float) m); set (p, "space", .9f); set (p, "shimmer", 1); } }, 30, 0);
    }
    ++n; bad += run ("смена режимов каждые 3 с, SPACE .8", [&] (MemoriaProcessor& p, double t) { if (t == 0) set (p, "space", .8f); set (p, "mode", (float) (((int) (t / 3)) % 4)); }, 30, 0);
    ++n; bad += run ("смена размеров каждые 3 с", [&] (MemoriaProcessor& p, double t) { set (p, "size", (float) (((int) (t / 3)) % 3)); }, 30, 0);
    ++n; bad += run ("смена пресетов каждые 4 с", [&] (MemoriaProcessor& p, double t) { static int last = -1; const int k = (int) (t / 4); if (k != last) { last = k; p.setCurrentProgram (1 + (k * 7) % 66); } }, 40, 1);
    ++n; bad += run ("SPACE ручкой туда-сюда", [&] (MemoriaProcessor& p, double t) { set (p, "space", (float) (.5 + .45 * std::sin (t * .8))); }, 30, 0);
    std::printf ("\nсценариев со щелчками: %d из %d\n", bad, n);
    return 0;
}
