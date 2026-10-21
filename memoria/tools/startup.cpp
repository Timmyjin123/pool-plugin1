// Старт плагина: что выходит в первые секунды (как при добавлении на дорожку)
#include "../Source/PluginProcessor.h"
#include <cstdio>
int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    for (int sc = 0; sc < 6; ++sc)
    {
        const double sr = sc % 2 ? 44100 : 48000; const int B = sc >= 2 ? 512 : 128;
        MemoriaProcessor p;
        if (sc >= 4) { p.setPlayConfigDetails (2, 2, 48000, 1024); p.prepareToPlay (48000, 1024); }   // хост готовит дважды
        p.setPlayConfigDetails (2, 2, sr, B); p.prepareToPlay (sr, B);
        if (sc == 5) { juce::MemoryBlock mb; p.getStateInformation (mb); p.setStateInformation (mb.getData(), (int) mb.getSize()); }
        juce::AudioBuffer<float> buf (2, B); juce::MidiBuffer midi;
        double ph = 0; float pk[10] {}; int firstBig = -1;
        for (long k = 0; k < (long) (3 * sr); k += B)
        {
            for (int i = 0; i < B; ++i) { const float x = .25f * (float) std::sin (ph); ph += 2 * M_PI * 220 / sr; buf.setSample (0, i, x); buf.setSample (1, i, x); }
            p.processBlock (buf, midi);
            for (int i = 0; i < B; ++i) { const float a = std::max (std::fabs (buf.getSample (0, i)), std::fabs (buf.getSample (1, i)));
                const int seg = std::min (9, (int) ((k + i) / (.1 * sr))); pk[seg] = std::max (pk[seg], a); if (a > .6f && firstBig < 0) firstBig = (int) (k + i); }
        }
        std::printf ("sr %.0f блок %d %s: пики по 100 мс:", sr, B, sc >= 4 ? (sc == 5 ? "(2×prepare+state)" : "(2×prepare)") : "");
        for (float v : pk) std::printf (" %.2f", v);
        std::printf ("  первый >0.6 на %d\n", firstBig);
    }
    return 0;
}
