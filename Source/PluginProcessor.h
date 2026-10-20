#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "ParamSpecs.h"
#include "Engine.h"

class PoolProcessor : public juce::AudioProcessor
{
public:
    PoolProcessor();
    ~PoolProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& l) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Pool"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 6.0; }
    int getNumPrograms() override { return (int) pool::presetDefs().size() + 1; }
    int getCurrentProgram() override { return curProgram; }
    void setCurrentProgram (int i) override { applyPreset (i - 1); curProgram = i; }
    const juce::String getProgramName (int i) override
    {
        return i == 0 ? juce::String::fromUTF8 ("— свой —") : juce::String::fromUTF8 (pool::presetDefs()[(size_t) i - 1].name);
    }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // для интерфейса
    void applyPreset (int idx);   // idx < 0 — ничего
    void randomise();
    float getBase (int i) const { return apvts.getRawParameterValue (pool::paramSpecs()[i].id)->load(); }
    float getEffective (int i) const { return eff[(size_t) i].load(); }   // нормированное, с дрейфом
    void setBaseNotify (int i, float x);
    bool isFrozen() const { return apvts.getRawParameterValue ("freeze")->load() > .5f; }
    void setFrozen (bool b);
    int copyScope (float* dst, int n) const;   // последние n отсчётов выхода (моно)
    pool::Engine& getEngine() { return engine; }
    float getFarEnv() const { return farEnvUi.load(); }

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout makeLayout();
    pool::EngineParams makeParams (double dt, int n);

    pool::Engine engine;
    double sr = 48000;
    int curProgram = 0;

    float dr[pool::P_COUNT] {}, tg[pool::P_COUNT] {};
    pool::Rng rng { 777 };
    double fph1 = 0, fph2 = .37;
    std::array<std::atomic<float>, pool::P_COUNT> eff;
    std::atomic<float> farEnvUi { 1 };
    std::vector<float> inL, inR, outL2, outR2;

    static constexpr int kScope = 4096;
    float scope[kScope] {};
    std::atomic<int> scopeW { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PoolProcessor)
};
