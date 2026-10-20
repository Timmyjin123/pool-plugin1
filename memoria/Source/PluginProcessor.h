#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "MemEngine.h"
#include "Presets.h"


class MemoriaProcessor : public juce::AudioProcessor
{
public:
    MemoriaProcessor();
    ~MemoriaProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& l) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Memoria"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 30.0; }

    int getNumPrograms() override { return (int) memoria::presets().size() + 1; }
    int getCurrentProgram() override { return curProgram; }
    void setCurrentProgram (int i) override;
    const juce::String getProgramName (int i) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    void erase() { engine.requestErase(); }
    // VARY: какие параметры гуляют и их текущие (нормированные 0..1) значения
    static constexpr int kVary = 9;
    static const char* varyId (int i);
    float varyEffective (int i) const { return varyEff[(size_t) i].load(); }
    bool varyActive (int i) const { return std::fabs (varyOff[i]) > .004f; }
    void diceLayers();
    mem::Engine& getEngine() { return engine; }

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout makeLayout();
    float val (const char* id) const { return apvts.getRawParameterValue (id)->load(); }

    mem::Engine engine;
    // VARY: плавные смещения выбранных параметров
    float varyOff[kVary] {}, varyTgt[kVary] {};
    double varyTimer = 0, sr = 48000;
    juce::Random varyRnd;
    std::array<std::atomic<float>, kVary> varyEff {};
    void updateVary (int n);
    float varied (int i, float base);
    int curProgram = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MemoriaProcessor)
};
