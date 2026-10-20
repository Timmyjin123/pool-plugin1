#include "PluginProcessor.h"
#include "PluginEditor.h"

static juce::String U (const char* s) { return juce::String::fromUTF8 (s); }



juce::AudioProcessorValueTreeState::ParameterLayout MemoriaProcessor::makeLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> ps;
    auto pct = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v * 100)) + "%"; });
    auto f01 = [&] (const char* id, const char* name, float def)
    {
        ps.push_back (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, U (name), NormalisableRange<float> (0, 1), def, pct));
    };
    ps.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "mode", 1 }, U ("Режим"), StringArray { "Grains", "Diffuse", "Network", "Tapes" }, 0));
    ps.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "size", 1 }, U ("Размер памяти"), StringArray { "Long", "Mid", "Short" }, 0));
    f01 ("decay", "Затухание памяти", .7f);
    f01 ("warmth", "Тепло (сатурация)", .25f);
    ps.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "dynamics", 1 }, U ("Динамика: наплыв ↔ расцвет"), NormalisableRange<float> (0, 1), .5f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        {
            if (std::abs (v - .5f) < .02f) return U ("выкл");
            return v < .5f ? U ("наплыв ") + String (roundToInt ((.5f - v) * 200)) + "%"
                           : U ("расцвет ") + String (roundToInt ((v - .5f) * 200)) + "%";
        })));
    f01 ("spread", "Разброс / связь линий", .35f);
    f01 ("drift", "Дрейф", .5f);
    f01 ("mix", "Сухой / мокрый", .5f);
    ps.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "output", 1 }, U ("Выход"), NormalisableRange<float> (-24, 18), 0,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 1) + U (" дБ"); })));
    ps.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "lowcut", 1 }, U ("Срез низа в петле"), StringArray { U ("выкл"), "1", "2", "3" }, 1));
    ps.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "highcut", 1 }, U ("Срез верха в петле"), StringArray { U ("выкл"), "1", "2", "3" }, 2));
    ps.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "input", 1 }, U ("Запись с каналов"), StringArray { "L+R", "L", "R" }, 0));
    ps.push_back (std::make_unique<AudioParameterBool> (ParameterID { "freeze", 1 }, U ("Заморозка"), false));
    ps.push_back (std::make_unique<AudioParameterBool> (ParameterID { "reverse", 1 }, U ("Обратное направление"), false));
    ps.push_back (std::make_unique<AudioParameterBool> (ParameterID { "mono", 1 }, U ("Моно-выход"), false));
    f01 ("layers", "Слои: разная обработка каждого слоя", .4f);
    ps.push_back (std::make_unique<AudioParameterInt> (ParameterID { "layerSeed", 1 }, U ("Набор характеров слоёв"), 0, 999, 0));
    f01 ("pingpong", "Пинг-понг на слоях", .25f);
    ps.push_back (std::make_unique<AudioParameterBool> (ParameterID { "blur", 1 }, U ("Размыть (Blur)"), false));
    ps.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "tone", 1 }, U ("Тон: темнее ↔ ярче"), NormalisableRange<float> (0, 1), .48f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        {
            if (std::abs (v - .5f) < .02f) return U ("нейтр.");
            return v < .5f ? U ("темнее ") + String (roundToInt ((.5f - v) * 200)) + "%" : U ("ярче ") + String (roundToInt ((v - .5f) * 200)) + "%";
        })));
    f01 ("depth", "Глубина: саб на октаву ниже и низ", .4f);
    return { ps.begin(), ps.end() };
}

MemoriaProcessor::MemoriaProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                      .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "MEMORIA", makeLayout())
{
}

bool MemoriaProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    if (l.getMainOutputChannelSet() != juce::AudioChannelSet::stereo()) return false;
    const auto i = l.getMainInputChannelSet();
    return i == juce::AudioChannelSet::stereo() || i == juce::AudioChannelSet::mono();
}

void MemoriaProcessor::prepareToPlay (double sampleRate, int)
{
    engine.prepare (sampleRate);
    setLatencySamples (0);
}

void MemoriaProcessor::processBlock (juce::AudioBuffer<float>& buf, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals nd;
    const int n = buf.getNumSamples();
    const int nIn = getTotalNumInputChannels();
    for (int c = nIn; c < buf.getNumChannels(); ++c) buf.clear (c, 0, n);
    if (n == 0) return;

    mem::Params p;
    p.mode = (int) val ("mode");
    p.size = (int) val ("size");
    p.decay = val ("decay");
    p.warmth = val ("warmth");
    p.dynamics = val ("dynamics");
    p.spread = val ("spread");
    p.drift = val ("drift");
    p.mix = val ("mix");
    p.out = juce::Decibels::decibelsToGain (val ("output"));
    p.lowcut = (int) val ("lowcut");
    p.highcut = (int) val ("highcut");
    p.input = (int) val ("input");
    p.freeze = val ("freeze") > .5f;
    p.reverse = val ("reverse") > .5f;
    p.mono = val ("mono") > .5f;
    p.layers = val ("layers");
    p.layerSeed = (int) val ("layerSeed");
    p.pingpong = val ("pingpong");
    p.blur = val ("blur") > .5f;
    p.tone = val ("tone");
    p.depth = val ("depth");

    float* l = buf.getWritePointer (0);
    float* r = buf.getWritePointer (1);
    const float* inL = buf.getReadPointer (0);
    const float* inR = nIn > 1 ? buf.getReadPointer (1) : nullptr;
    // движок читает вход до записи выхода в тот же индекс, поэтому обработка на месте безопасна
    engine.process (inL, inR, l, r, n, p);
}

void MemoriaProcessor::setCurrentProgram (int i)
{
    curProgram = i;
    if (i < 1 || i > (int) memoria::presets().size()) return;
    const auto& pr = memoria::presets()[(size_t) i - 1];
    auto set = [this] (const char* id, float v)
    {
        if (auto* par = apvts.getParameter (id)) par->setValueNotifyingHost (par->convertTo0to1 (v));
    };
    set ("mode", (float) pr.mode); set ("size", (float) pr.size);
    set ("decay", pr.decay); set ("warmth", pr.warmth); set ("dynamics", pr.dynamics);
    set ("spread", pr.spread); set ("drift", pr.drift); set ("mix", pr.mix);
    set ("lowcut", (float) pr.lowcut); set ("highcut", (float) pr.highcut);
    set ("layers", pr.layers); set ("layerSeed", (float) pr.seed); set ("pingpong", pr.pingpong); set ("blur", pr.blur ? 1.0f : 0.0f);
    set ("tone", pr.tone); set ("depth", pr.depth); set ("reverse", pr.reverse ? 1.0f : 0.0f);
}

void MemoriaProcessor::diceLayers()
{
    if (auto* par = apvts.getParameter ("layerSeed"))
        par->setValueNotifyingHost (par->convertTo0to1 ((float) juce::Random::getSystemRandom().nextInt ({ 1, 1000 })));
}

const juce::String MemoriaProcessor::getProgramName (int i)
{
    if (i < 1 || i > (int) memoria::presets().size()) return U ("— свой —");
    return U (memoria::presets()[(size_t) i - 1].name);
}

void MemoriaProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    state.setProperty ("program", curProgram, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, dest);
}

void MemoriaProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto t = juce::ValueTree::fromXml (*xml);
            curProgram = (int) t.getProperty ("program", 0);
            apvts.replaceState (t);
        }
}

juce::AudioProcessorEditor* MemoriaProcessor::createEditor() { return new MemoriaEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new MemoriaProcessor(); }
