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
    f01 ("chaos", "Хаос: случайные сбои (warp, stutter, reverse, tape stop, dropout, shuffle)", 0);
    f01 ("ruin", "Руины: шипение, треск, эрозия памяти", 0);
    f01 ("space", "Зал: размер и громкость реверба", .3f);
    ps.push_back (std::make_unique<AudioParameterBool> (ParameterID { "vary", 1 }, U ("Вариативность: параметры аккуратно гуляют"), false));
    ps.push_back (std::make_unique<AudioParameterBool> (ParameterID { "shimmer", 1 }, U ("Шиммер: хвост поднимается на октаву"), false));
    {
        auto hz = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
            { return v >= 1000 ? String (v / 1000.0f, 1) + U (" кГц") : String (roundToInt (v)) + U (" Гц"); });
        NormalisableRange<float> lo (15, 4000); lo.setSkewForCentre (200);
        NormalisableRange<float> hi (300, 20000); hi.setSkewForCentre (3000);
        ps.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "tailLo", 1 }, U ("Хвосты: срез низа"), lo, 40, hz));
        ps.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "tailHi", 1 }, U ("Хвосты: срез верха"), hi, 16000, hz));
    }
    ps.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "harmony", 1 }, U ("Гармония слоёв: октавы / квинты / свободно"),
                                                          StringArray { "OCT", "5TH", "FREE" }, 0));
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
    sr = sampleRate;
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

    updateVary (n);
    mem::Params p;
    p.mode = (int) val ("mode");
    p.size = (int) val ("size");
    p.decay = val ("decay");
    p.warmth = varied (6, val ("warmth"));
    p.dynamics = varied (7, val ("dynamics"));
    p.spread = varied (0, val ("spread"));
    p.drift = varied (1, val ("drift"));
    p.mix = val ("mix");
    p.out = juce::Decibels::decibelsToGain (val ("output"));
    p.lowcut = (int) val ("lowcut");
    p.highcut = (int) val ("highcut");
    p.input = (int) val ("input");
    p.freeze = val ("freeze") > .5f;
    p.reverse = val ("reverse") > .5f;
    p.mono = val ("mono") > .5f;
    p.layers = varied (2, val ("layers"));
    p.layerSeed = (int) val ("layerSeed");
    p.pingpong = varied (3, val ("pingpong"));
    p.blur = val ("blur") > .5f;
    p.tone = varied (5, val ("tone"));
    p.depth = varied (8, val ("depth"));
    p.chaos = val ("chaos");
    p.ruin = val ("ruin");
    p.harmony = (int) val ("harmony");
    p.space = varied (4, val ("space"));
    p.shimmer = val ("shimmer") > .5f ? .6f : 0.0f;
    p.tailLo = val ("tailLo");
    p.tailHi = val ("tailHi");

    float* l = buf.getWritePointer (0);
    float* r = buf.getWritePointer (1);
    const float* inL = buf.getReadPointer (0);
    const float* inR = nIn > 1 ? buf.getReadPointer (1) : nullptr;
    // движок читает вход до записи выхода в тот же индекс, поэтому обработка на месте безопасна
    engine.process (inL, inR, l, r, n, p);
}

// ---------------------------------------------------------------- VARY
const char* MemoriaProcessor::varyId (int i)
{
    static const char* ids[kVary] = { "spread", "drift", "layers", "pingpong", "space", "tone", "warmth", "dynamics", "depth" };
    return ids[juce::jlimit (0, kVary - 1, i)];
}

// Раз в 8–16 с выбираются 2–3 параметра, их смещения плавно уходят к новым небольшим целям;
// остальные плавно возвращаются к своим значениям. Режим, размер, DECAY, MIX, OUTPUT не трогаются.
void MemoriaProcessor::updateVary (int n)
{
    static const float range[kVary] = { .20f, .25f, .25f, .20f, .15f, .08f, .12f, .12f, .10f };
    const bool on = val ("vary") > .5f;
    const double dt = (double) n / sr;
    if (on)
    {
        varyTimer -= dt;
        if (varyTimer <= 0)
        {
            varyTimer = 8.0 + 8.0 * varyRnd.nextDouble();
            for (auto& t : varyTgt) t = 0;
            const int count = 2 + varyRnd.nextInt (2);
            for (int c = 0; c < count; ++c)
            {
                const int i = varyRnd.nextInt (kVary);
                varyTgt[i] = (varyRnd.nextFloat() * 2 - 1) * range[i];
            }
        }
    }
    else
    {
        for (auto& t : varyTgt) t = 0;
        varyTimer = 0;
    }
    const float a = (float) (1.0 - std::exp (-dt / 4.0));   // переход ~4 с
    for (int i = 0; i < kVary; ++i) varyOff[i] += (varyTgt[i] - varyOff[i]) * a;
}

float MemoriaProcessor::varied (int i, float base)
{
    auto* par = apvts.getParameter (varyId (i));
    float norm = par->convertTo0to1 (base) + varyOff[i];
    // DYNAMICS не перескакивает через середину: «наплыв» и «расцвет» — разные характеры
    if (i == 7) { const float b0 = par->convertTo0to1 (base); norm = b0 < .5f ? std::min (norm, .49f) : std::max (norm, .51f); if (std::fabs (b0 - .5f) < .02f) norm = b0; }
    norm = juce::jlimit (0.0f, 1.0f, norm);
    varyEff[(size_t) i].store (norm);
    return par->convertFrom0to1 (norm);
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
    set ("chaos", pr.chaos); set ("ruin", pr.ruin); set ("harmony", (float) pr.harmony);
    set ("space", pr.space); set ("shimmer", pr.shimmer ? 1.0f : 0.0f); set ("tailLo", pr.tailLo); set ("tailHi", pr.tailHi);
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
