// Снимок окна плагина в PNG (для разработки под Linux/Xvfb):
//   memoria_snapshot out.png [mode] [seconds] [freeze] [reverse]
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::String out = argc > 1 ? argv[1] : "snapshot.png";
    const int mode = argc > 2 ? juce::String (argv[2]).getIntValue() : 0;
    const double secs = argc > 3 ? juce::String (argv[3]).getDoubleValue() : 6.0;
    const bool freeze = argc > 4 && juce::String (argv[4]) == "1";
    const bool reverse = argc > 5 && juce::String (argv[5]) == "1";
    const bool blur = argc > 6 && juce::String (argv[6]) == "1";

    MemoriaProcessor p;
    const double sr = 48000;
    const int B = 512;
    p.setPlayConfigDetails (2, 2, sr, B);
    p.prepareToPlay (sr, B);
    auto set = [&p] (const char* id, float v) { auto* par = p.apvts.getParameter (id); par->setValueNotifyingHost (par->convertTo0to1 (v)); };
    set ("mode", (float) mode);
    set ("mix", .6f); set ("decay", .85f); set ("drift", .7f); set ("spread", .5f);

    juce::AudioBuffer<float> buf (2, B);
    juce::MidiBuffer midi;
    double ph = 0;
    juce::Random rnd (5);
    const int nb = (int) (secs * sr / B);
    for (int b = 0; b < nb; ++b)
    {
        for (int i = 0; i < B; ++i)
        {
            const double env = std::fmod (ph / (2 * juce::MathConstants<double>::pi * 196.0), 1.3) < .8 ? .35 : .02;
            const float x = (float) (env * std::sin (ph) * (1 + .3 * std::sin (ph * .005)) + .01 * rnd.nextFloat());
            buf.setSample (0, i, x); buf.setSample (1, i, x * .9f);
            ph += 2 * juce::MathConstants<double>::pi * 196.0 / sr;
        }
        if (b == nb - nb / 6)
        {
            if (freeze) set ("freeze", 1);
            if (reverse) set ("reverse", 1);
            if (blur) { set ("blur", 1); set ("layers", 1); set ("pingpong", .7f); }
        }
        p.processBlock (buf, midi);
    }

    std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
    ed->setVisible (true);
    // дать таймерам интерфейса набрать данные
    for (int i = 0; i < 25; ++i)
    {
        p.processBlock (buf, midi);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (40);
    }
    auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 2.0f);
    juce::File f (juce::File::getCurrentWorkingDirectory().getChildFile (out));
    f.deleteFile();
    juce::FileOutputStream os (f);
    juce::PNGImageFormat png;
    png.writeImageToStream (img, os);
    std::printf ("wrote %s\n", f.getFullPathName().toRawUTF8());
    ed.reset();
    return 0;
}
