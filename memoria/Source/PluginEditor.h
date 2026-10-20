#pragma once
#include "PluginProcessor.h"

namespace mui
{
struct Pal
{
    static juce::Colour bg()    { return juce::Colour (0xff04050a); }
    static juce::Colour panel() { return juce::Colour (0xff0b0d15); }
    static juce::Colour line()  { return juce::Colour (0xff1d2030); }
    static juce::Colour text()  { return juce::Colour (0xffe2e6f0); }
    static juce::Colour dim()   { return juce::Colour (0xff767c92); }
    static juce::Colour mode (int m)
    {
        static const juce::uint32 c[4] = { 0xffc8ff3d, 0xff3de0ff, 0xffa98bff, 0xffffb23d };
        return juce::Colour (c[juce::jlimit (0, 3, m)]);
    }
    static juce::Colour ice()   { return juce::Colour (0xff9fe7ff); }
    static juce::Colour blur()  { return juce::Colour (0xffff8bd8); }
    // цвет слоя: оттенок режима, сдвинутый по кругу
    static juce::Colour layer (int m, int j)
    {
        static const float sh[4] = { -.09f, .0f, .09f, .18f };
        const auto c = mode (m);
        return c.withHue (std::fmod (c.getHue() + sh[j & 3] + 1.0f, 1.0f)).withSaturation (juce::jmin (1.0f, c.getSaturation() * .9f + .1f));
    }
    static juce::Colour warn()  { return juce::Colour (0xffff5a7a); }
};

class Look : public juce::LookAndFeel_V4
{
public:
    Look();
    void drawRotarySlider (juce::Graphics&, int, int, int, int, float, float, float, juce::Slider&) override;
    void drawComboBox (juce::Graphics&, int, int, bool, int, int, int, int, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getPopupMenuFont() override;
};
}

// Ручка с подписью и значением
class Knob : public juce::Component, public juce::SettableTooltipClient
{
public:
    Knob (MemoriaProcessor&, const char* paramId, const juce::String& title, bool bipolar);
    ~Knob() override { slider.setLookAndFeel (nullptr); }
    void resized() override;
    void paint (juce::Graphics&) override;
    void setAccent (juce::Colour c) { slider.getProperties().set ("col", (int) c.getARGB()); repaint(); }
private:
    juce::Slider slider;
    juce::String title;
    juce::RangedAudioParameter* par;
    std::unique_ptr<juce::SliderParameterAttachment> att;
};

// Сегментный переключатель для параметра-выбора
class Segments : public juce::Component, public juce::SettableTooltipClient
{
public:
    Segments (MemoriaProcessor&, const char* paramId, juce::StringArray labels, bool modeColours);
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent& e) override { hover = idxAt (e.x); repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }
    void setAccent (juce::Colour c) { accent = c; repaint(); }
private:
    int idxAt (int x) const { return juce::jlimit (0, labels.size() - 1, x * labels.size() / juce::jmax (1, getWidth())); }
    juce::StringArray labels;
    bool modeCols;
    juce::Colour accent { juce::Colours::white };
    int cur = 0, hover = -1;
    std::unique_ptr<juce::ParameterAttachment> att;
};

// Кнопка-чип: переключатель, циклический выбор или действие
class Chip : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    enum Kind { Toggle, Cycle, Action };
    Chip (MemoriaProcessor&, const char* paramId, const juce::String& label, Kind kind, juce::Colour onColour);
    Chip (const juce::String& label, std::function<void()> action, juce::Colour onColour);   // действие
    ~Chip() override { stopTimer(); }
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hover = false; repaint(); }
    void setAccent (juce::Colour c) { col = c; repaint(); }
private:
    void timerCallback() override { flash = juce::jmax (0.0f, flash - .05f); repaint(); if (flash <= 0) stopTimer(); }
    juce::String label;
    Kind kind;
    juce::Colour col;
    std::function<void()> act;
    int cur = 0, count = 2;
    juce::StringArray choiceNames;
    bool hover = false;
    float flash = 0;
    std::unique_ptr<juce::ParameterAttachment> att;
};

// Кольцо памяти: линии как дуги, возраст материала, головки записи, гранулы, LFO-спутники
class MemoryRing : public juce::Component, private juce::Timer
{
public:
    explicit MemoryRing (MemoriaProcessor&);
    ~MemoryRing() override { stopTimer(); }
    void paint (juce::Graphics&) override;
    void resized() override;
private:
    void timerCallback() override;
    MemoriaProcessor& proc;
    static constexpr int kBins = 420;
    std::array<float, kBins> env {}, age {};
    std::array<int, kBins> lineOf {};
    int nBins = 0;
    float rot = 0, lvl = 0, inLvl = 0, t = 0;
    struct Star { float x, y, s, a, tw; };
    std::vector<Star> stars;
};

// Карточки слоёв: характер каждого слоя и его уровень
class LayerPanel : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    explicit LayerPanel (MemoriaProcessor& p) : proc (p) { startTimerHz (15); setInterceptsMouseClicks (true, false); }
    ~LayerPanel() override { stopTimer(); }
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override { proc.diceLayers(); }
private:
    void timerCallback() override { repaint(); }
    MemoriaProcessor& proc;
};

class MemoriaEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit MemoriaEditor (MemoriaProcessor&);
    ~MemoriaEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
private:
    void timerCallback() override;
    MemoriaProcessor& proc;
    mui::Look look;
    MemoryRing ring;
    LayerPanel layerPanel;
    Segments modeSeg, sizeSeg;
    juce::OwnedArray<Knob> knobs;
    juce::OwnedArray<Chip> chips;
    juce::ComboBox presetBox;
    juce::TooltipWindow tips { this, 500 };
    int lastMode = -1;
};
