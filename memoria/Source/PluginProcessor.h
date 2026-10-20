#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "MemEngine.h"

namespace memoria
{
struct Preset
{
    const char* name;
    int mode, size;
    float decay, warmth, dynamics, spread, drift, mix;
    int lowcut, highcut;
    float layers; int seed; float pingpong; bool blur;
};
const std::vector<Preset>& presets();
}

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
    void diceLayers();
    mem::Engine& getEngine() { return engine; }

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout makeLayout();
    float val (const char* id) const { return apvts.getRawParameterValue (id)->load(); }

    mem::Engine engine;
    int curProgram = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MemoriaProcessor)
};
