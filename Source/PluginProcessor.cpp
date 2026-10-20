#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace pool;

juce::AudioProcessorValueTreeState::ParameterLayout PoolProcessor::makeLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> ps;
    const ParamSpec* S = paramSpecs();
    for (int i = 0; i < P_COUNT; ++i)
    {
        const ParamSpec s = S[i];
        auto attrs = juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction ([s] (float x, int)
            {
                const float v = paramToValue (s, x);
                if (juce::String (s.id) == "evo")
                {
                    if (v < .02f) return juce::String::fromUTF8 ("выкл");
                    const int sec = juce::roundToInt (15.0f * std::pow (32.0f, v));
                    return juce::String (sec / 60) + ":" + juce::String (sec % 60).paddedLeft ('0', 2);
                }
                if (juce::String (s.id) == "gsnap")
                {
                    static const char* nm[4] = { "свободно", "полутона", "окт+квинты", "октавы" };
                    return juce::String::fromUTF8 (nm[juce::jlimit (0, 3, (int) std::lround (v))]);
                }
                if (juce::String (s.id) == "pnote")
                {
                    static const char* nm[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
                    const int n = (int) std::lround (v);
                    if (n < 1) return juce::String::fromUTF8 ("выкл");
                    const int m = n + 23;
                    return juce::String (nm[m % 12]) + juce::String (m / 12 - 1);
                }
                const float a = std::fabs (v);
                juce::String t = (s.mn == 0 && s.mx == 1 && ! *s.unit) ? juce::String (juce::roundToInt (v * 100)) + "%"
                                  : juce::String (v, a >= 100 ? 0 : (a >= 10 ? 1 : 2));
                if (*s.unit && ! (s.mn == 0 && s.mx == 1)) t += juce::String::fromUTF8 (s.unit);
                return t;
            })
            .withValueFromStringFunction ([s] (const juce::String& t) { return paramFromValue (s, t.getFloatValue()); });
        ps.push_back (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { s.id, 1 }, juce::String::fromUTF8 (s.name),
                                                                   juce::NormalisableRange<float> (0.f, 1.f), paramFromValue (s, s.def), attrs));
    }
    ps.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "scnon", 1 }, juce::String::fromUTF8 ("Сцены включены"), false));
    ps.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "freeze", 1 }, juce::String::fromUTF8 ("Заморозка"), false));
    return { ps.begin(), ps.end() };
}

PoolProcessor::PoolProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                      .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "POOL", makeLayout())
{
    for (auto& e : eff) e.store (0);
    initScenes();
}

bool PoolProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto o = l.getMainOutputChannelSet();
    if (o != juce::AudioChannelSet::stereo()) return false;
    const auto i = l.getMainInputChannelSet();
    return i == juce::AudioChannelSet::stereo() || i == juce::AudioChannelSet::mono();
}

void PoolProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    engine.prepare (sr);
    engine.setOffline (isNonRealtime());
    for (int i = 0; i < P_COUNT; ++i) dr[i] = tg[i] = 0;
    inL.assign ((size_t) samplesPerBlock + 8, 0.f); inR = inL; outL2 = inL; outR2 = inL;
    setLatencySamples (0);
}

EngineParams PoolProcessor::makeParams (double dt, int)
{
    const ParamSpec* S = paramSpecs();
    const float amt = paramToValue (S[P_dr], getBase (P_dr)), spd = .15f + paramToValue (S[P_ds], getBase (P_ds)) * 2.5f;
    // --- сцены: смешивание слотов A–D (ручное или автоэволюция)
    const bool scOn = isScenesOn();
    int sa = 0, sb = 0; float sf = 0;
    if (scOn)
    {
        const float evoV = getBase (P_evo), manual = paramToValue (S[P_scn], getBase (P_scn));
        if (evoV > .02f)
        {
            if (! evoRun) { evoFrom = juce::jlimit (0, 3, (int) std::lround (manual)); evoTo = (evoFrom + 1 + (int) (rng.uni() * 3.0f)) % 4; evoT = 0; evoRun = true; }
            evoT += dt / (15.0 * std::pow (32.0, (double) evoV));
            if (evoT >= 1.0) { evoFrom = evoTo; evoTo = (evoFrom + 1 + (int) (rng.uni() * 3.0f)) % 4; evoT = 0; }
            sa = evoFrom; sb = evoTo;
            const float t = (float) evoT; sf = t * t * (3 - 2 * t);
            scnUi.store ((float) sa + ((float) sb - (float) sa) * sf);
        }
        else
        {
            evoRun = false;
            sa = juce::jlimit (0, 3, (int) std::floor (manual)); sb = juce::jmin (3, sa + 1); sf = manual - (float) sa;
            scnUi.store (manual);
        }
    }
    else evoRun = false;
    auto sceneExcluded = [] (int i) { return i == P_ipitch || i == P_ppitch || i == P_vol || i == P_dr || i == P_ds || i == P_scn || i == P_evo; };
    auto sceneStepped = [] (int i) { return i == P_pnote || i == P_gsnap; };

    float v[P_COUNT];
    for (int i = 0; i < P_COUNT; ++i)
    {
        float x = getBase (i);
        if (scOn && ! sceneExcluded (i))
            x = sceneStepped (i) ? sc[(size_t) (sf < .5f ? sa : sb)][(size_t) i].load()
                                 : sc[(size_t) sa][(size_t) i].load() * (1 - sf) + sc[(size_t) sb][(size_t) i].load() * sf;
        if (! S[i].noDrift)
        {
            dr[i] += (tg[i] - dr[i]) * (float) std::min (1.0, dt * spd);
            if (rng.uni() < dt * spd * .35) tg[i] = rng.bi();
            x = clampf (x + dr[i] * amt * .5f, 0.f, 1.f);
        }
        eff[(size_t) i].store (x);
        v[i] = paramToValue (S[i], x);
    }
    // огибающая далёкого голоса (два медленных LFO)
    const double T = 44.0 * std::pow (8.0 / 44.0, v[P_fsp]);
    fph1 = std::fmod (fph1 + dt / T, 1.0); fph2 = std::fmod (fph2 + dt / (T * 1.618), 1.0);
    const float x = .5f + .5f * (.62f * (float) std::sin (kTwoPi * fph1) + .38f * (float) std::sin (kTwoPi * fph2));
    const float t = clampf ((x - .3f) / .4f, 0.f, 1.f), smo = t * t * (3 - 2 * t);
    const float fenv = 1 - v[P_fsw] + v[P_fsw] * smo;
    farEnvUi.store (fenv);

    EngineParams e;
    e.ipitch = v[P_ipitch]; e.dw = v[P_dw]; e.size = v[P_size]; e.dens = v[P_dens]; e.pos = v[P_pos]; e.pj = v[P_pj];
    e.pitch = v[P_pitch]; e.ps = v[P_ps]; e.harm = v[P_harm]; e.rev = v[P_rev]; e.wid = v[P_wid]; e.glitch = v[P_glitch];
    e.crush = v[P_crush]; e.freeze = isFrozen() || (scOn && scFrz[(size_t) (sf < .5f ? sa : sb)].load() > .5f);
    e.nz = v[P_nz]; e.nvoc = v[P_nvoc]; e.ncol = v[P_ncol]; e.nfol = v[P_nfol];
    e.fbk = v[P_fbk]; e.rdec = v[P_rdec]; e.rsat = v[P_rsat]; e.rshim = v[P_rshim];
    if (scOn) eff[(size_t) P_scn].store (scnUi.load() / 3.0f);
    const float stab = v[P_pstab];
    e.ppos = clampf (v[P_pos] + (paramToValue (S[P_pos], getBase (P_pos)) - v[P_pos]) * stab, 0.f, 1.f);
    e.ppj = v[P_pj] * (1.0f - stab);
    e.pnote = v[P_pnote] < .5f ? 0 : (int) std::lround (v[P_pnote]) + 23;
    e.pstrict = v[P_pstrict];
    e.gsnap = (int) std::lround (v[P_gsnap]);
    e.pad = v[P_pad]; e.pdr = v[P_pdr]; e.pwid = v[P_pwid]; e.ppitch = v[P_ppitch]; e.pdark = v[P_pdark]; e.pwarm = v[P_pwarm];
    e.farLevel = v[P_far]; e.freg = v[P_freg]; e.fcl = v[P_fcl]; e.fdist = v[P_fdist]; e.fenv = fenv;
    e.drive = v[P_drive]; e.hp = v[P_hp]; e.lp = v[P_lp]; e.dt = v[P_dt]; e.df = v[P_df]; e.dm = v[P_dm]; e.rvm = v[P_rvm]; e.vol = v[P_vol];
    return e;
}

void PoolProcessor::processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals nd;
    const int n = b.getNumSamples(), nin = getTotalNumInputChannels();
    for (int c = nin; c < b.getNumChannels(); ++c) b.clear (c, 0, n);
    if ((int) inL.size() < n) { inL.resize ((size_t) n); inR.resize ((size_t) n); outL2.resize ((size_t) n); outR2.resize ((size_t) n); }
    engine.setOffline (isNonRealtime());

    const float* l = nin > 0 ? b.getReadPointer (0) : nullptr;
    const float* r = nin > 1 ? b.getReadPointer (1) : l;
    for (int i = 0; i < n; ++i) { inL[(size_t) i] = l ? l[i] : 0.f; inR[(size_t) i] = r ? r[i] : inL[(size_t) i]; }

    const EngineParams P = makeParams ((double) n / sr, n);
    engine.process (inL.data(), inR.data(), outL2.data(), outR2.data(), n, P);

    b.setSize (juce::jmax (2, b.getNumChannels()), n, true, false, true);
    std::memcpy (b.getWritePointer (0), outL2.data(), sizeof (float) * (size_t) n);
    std::memcpy (b.getWritePointer (1), outR2.data(), sizeof (float) * (size_t) n);

    int w = scopeW.load (std::memory_order_relaxed);
    for (int i = 0; i < n; ++i) { scope[w] = .5f * (outL2[(size_t) i] + outR2[(size_t) i]); w = (w + 1) & (kScope - 1); }
    scopeW.store (w, std::memory_order_release);
}

int PoolProcessor::copyScope (float* dst, int n) const
{
    n = juce::jmin (n, kScope);
    int w = scopeW.load (std::memory_order_acquire);
    for (int i = 0; i < n; ++i) dst[i] = scope[(w - n + i + kScope * 2) & (kScope - 1)];
    return n;
}

void PoolProcessor::setBaseNotify (int i, float x)
{
    if (auto* p = apvts.getParameter (paramSpecs()[i].id)) p->setValueNotifyingHost (clampf (x, 0.f, 1.f));
}

void PoolProcessor::setFrozen (bool b)
{
    if (auto* p = apvts.getParameter ("freeze")) p->setValueNotifyingHost (b ? 1.f : 0.f);
}

void PoolProcessor::applyPreset (int idx)
{
    const auto& all = presetDefs();
    if (idx < 0 || idx >= (int) all.size()) return;
    const auto& pr = all[(size_t) idx];
    for (int i = 0; i < P_COUNT; ++i)
    {
        if (i == P_ipitch || i == P_ppitch) continue;   // высоту источника пресеты не трогают
        setBaseNotify (i, paramFromValue (paramSpecs()[i], presetValue (pr, i)));
    }
    setFrozen (pr.freeze);
    setScenesOn (false);
    curProgram = idx + 1;
}

void PoolProcessor::randomise()
{
    for (int i = 0; i < P_COUNT; ++i)
    {
        const auto& s = paramSpecs()[i];
        if (s.noDrift || i == P_dw) continue;
        const float x = getBase (i), t = rng.uni();
        setBaseNotify (i, clampf (x + (t - x) * .55f, 0.f, 1.f));
    }
    curProgram = 0;
}

void PoolProcessor::initScenes()
{
    static const char* names[4] = { "Мягкая пыль", "Вечерний орган", "Собор в помехах", "Самоедство" };
    for (int k = 0; k < 4; ++k)
    {
        const PresetDef* pr = &presetDefs()[0];
        for (auto& d : presetDefs()) if (juce::String::fromUTF8 (d.name) == juce::String::fromUTF8 (names[k])) pr = &d;
        for (int i = 0; i < P_COUNT; ++i) sc[(size_t) k][(size_t) i].store (paramFromValue (paramSpecs()[i], presetValue (*pr, i)));
        scFrz[(size_t) k].store (pr->freeze ? 1.f : 0.f);
    }
}

void PoolProcessor::saveScene (int k)
{
    for (int i = 0; i < P_COUNT; ++i) sc[(size_t) k][(size_t) i].store (getBase (i));
    scFrz[(size_t) k].store (isFrozen() ? 1.f : 0.f);
}

void PoolProcessor::recallSceneToKnobs (int k)
{
    setScenesOn (false);
    for (int i = 0; i < P_COUNT; ++i)
        if (i != P_scn && i != P_evo && i != P_vol && i != P_dr && i != P_ds && i != P_ipitch && i != P_ppitch)
            setBaseNotify (i, sc[(size_t) k][(size_t) i].load());
    setFrozen (scFrz[(size_t) k].load() > .5f);
    curProgram = 0;
}

void PoolProcessor::playScene (int k)
{
    setBaseNotify (P_scn, (float) k / 3.0f);
    setScenesOn (true);
}

void PoolProcessor::setScenesOn (bool b)
{
    if (auto* p = apvts.getParameter ("scnon")) p->setValueNotifyingHost (b ? 1.f : 0.f);
}

void PoolProcessor::getStateInformation (juce::MemoryBlock& d)
{
    {
        juce::String s;
        for (int k = 0; k < 4; ++k)
        {
            for (int i = 0; i < P_COUNT; ++i) s << juce::String (sc[(size_t) k][(size_t) i].load(), 5) << ",";
            s << juce::String (scFrz[(size_t) k].load()) << ";";
        }
        apvts.state.setProperty ("scenes", s, nullptr);
    }
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, d);
}

void PoolProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            const auto rows = juce::StringArray::fromTokens (apvts.state.getProperty ("scenes").toString(), ";", "");
            if (rows.size() >= 4)
                for (int k = 0; k < 4; ++k)
                {
                    const auto vals = juce::StringArray::fromTokens (rows[k], ",", "");
                    if (vals.size() == P_COUNT + 1)
                    {
                        for (int i = 0; i < P_COUNT; ++i) sc[(size_t) k][(size_t) i].store (vals[i].getFloatValue());
                        scFrz[(size_t) k].store (vals[P_COUNT].getFloatValue());
                    }
                }
        }
}

juce::AudioProcessorEditor* PoolProcessor::createEditor() { return new PoolEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new PoolProcessor(); }
