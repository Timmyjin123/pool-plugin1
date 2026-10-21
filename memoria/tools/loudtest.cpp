// Громкий вход через процессор: где выход упирается в потолок и насколько сильно лимитер давит
#include "../Source/PluginProcessor.h"
#include <cstdio>
static float in (double t)
{
    const double tt = std::fmod (t, 2.0); double y = 0; const double f[3] = { 110, 138.6, 164.8 };
    const double e = std::min (1.0, tt / .003) * std::exp (-tt * 2.5);
    for (int k = 0; k < 3; ++k) for (int h = 1; h <= 6; ++h) y += std::sin (2 * M_PI * f[k] * h * tt) / h;
    const double kick = tt < .15 ? std::sin (2 * M_PI * (50 + 120 * std::exp (-tt * 30)) * tt) * std::exp (-tt * 20) : 0;
    return (float) (.32 * y * e + kick * .8);
}
int main()
{
    juce::ScopedJuceInitialiser_GUI gui;
    const double sr = 44100; const int B = 512;
    for (int pr = 0; pr <= 66; pr += (pr < 1 ? 1 : 5))
    {
        MemoriaProcessor p; p.setPlayConfigDetails (2, 2, sr, B); p.prepareToPlay (sr, B);
        if (pr > 0) p.setCurrentProgram (pr);
        juce::AudioBuffer<float> buf (2, B); juce::MidiBuffer midi;
        double inPk = 0, outPk = 0; long near = 0, tot = 0; double firstSec = 0;
        for (long k = 0; k < (long) (20 * sr); k += B)
        {
            for (int i = 0; i < B; ++i) { const float x = in ((double) (k + i) / sr); buf.setSample (0, i, x); buf.setSample (1, i, x); inPk = std::max (inPk, (double) std::fabs (x)); }
            p.processBlock (buf, midi);
            for (int i = 0; i < B; ++i) { const double a = std::max (std::fabs (buf.getSample (0, i)), std::fabs (buf.getSample (1, i))); outPk = std::max (outPk, a); if (a > .83) ++near; ++tot; if (k < sr) firstSec = std::max (firstSec, a); }
        }
        std::printf ("%-36s вход пик %.2f  выход пик %.2f  у потолка %.2f%%  первая секунда %.2f\n",
                     pr ? memoria::presets()[(size_t) pr - 1].name : "по умолчанию", inPk, outPk, 100.0 * near / tot, firstSec);
    }
}
