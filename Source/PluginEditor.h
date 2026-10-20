#pragma once
#include "PluginProcessor.h"

namespace poolui
{
struct Palette
{
    static juce::Colour bg()     { return juce::Colour (0xff060609); }
    static juce::Colour panel()  { return juce::Colour (0xff0d0d14); }
    static juce::Colour line()   { return juce::Colour (0xff23232f); }
    static juce::Colour text()   { return juce::Colour (0xffd9d9e4); }
    static juce::Colour dim()    { return juce::Colour (0xff77778a); }
    static juce::Colour grain()  { return juce::Colour (0xffc8ff3d); }
    static juce::Colour pad()    { return juce::Colour (0xff3de0ff); }
    static juce::Colour far()    { return juce::Colour (0xffff3d8b); }
    static juce::Colour drift()  { return juce::Colour (0xffffb23d); }
};

class PoolLook : public juce::LookAndFeel_V4
{
public:
    PoolLook();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool hi, bool down) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool, int, int, int, int, juce::ComboBox&) override;
    juce::Font getTextButtonFont (juce::TextButton&, int h) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getLabelFont (juce::Label&) override;
};
}

class KnobComp : public juce::Component, public juce::SettableTooltipClient
{
public:
    KnobComp (PoolProcessor&, int paramIndex, juce::Colour accent);
    void resized() override;
    void paint (juce::Graphics&) override;
    void setEffective (float x) { slider.getProperties().set ("eff", x); }
private:
    PoolProcessor& proc;
    int idx;
    juce::Slider slider;
    juce::Label name;
    std::unique_ptr<juce::SliderParameterAttachment> att;
};

class VisualComp : public juce::Component, private juce::Timer
{
public:
    explicit VisualComp (PoolProcessor&);
    ~VisualComp() override { stopTimer(); }
    void paint (juce::Graphics&) override;
    void resized() override {}
    void mouseDown (const juce::MouseEvent& e) override { drag (e); }
    void mouseDrag (const juce::MouseEvent& e) override { drag (e); }
private:
    void timerCallback() override;
    void drag (const juce::MouseEvent&);
    void pushSpectrum();

    PoolProcessor& proc;
    juce::dsp::FFT fft { 10 };
    juce::dsp::WindowingFunction<float> window { 1024, juce::dsp::WindowingFunction<float>::hann };
    std::vector<float> fftBuf;
    juce::Image spec { juce::Image::ARGB, 360, 120, true };
    std::vector<juce::Colour> cmap;
    float smoothLvl[3] { 0, 0, 0 };
    float t = 0;
    pool::GrainCloud::VizGrain grains[pool::Engine::kVizMax];
    int nGrains = 0;
    juce::Rectangle<float> xyArea() const;
};

class PoolEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit PoolEditor (PoolProcessor&);
    ~PoolEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
private:
    void timerCallback() override;
    void showGroup (int g);

    PoolProcessor& proc;
    poolui::PoolLook look;
    VisualComp visual;
    juce::ComboBox presets;
    juce::TextButton freezeBtn, randBtn;
    juce::OwnedArray<juce::TextButton> tabs;
    juce::OwnedArray<KnobComp> knobs;
    juce::TooltipWindow tooltip { this, 600 };
    int group = 0;
};
