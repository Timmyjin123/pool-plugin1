#pragma once
#include "PluginProcessor.h"

namespace poolui
{
struct Palette
{
    static juce::Colour bg()     { return juce::Colour (0xff05060a); }
    static juce::Colour panel()  { return juce::Colour (0xff0b0d14); }
    static juce::Colour line()   { return juce::Colour (0xff1c1f2b); }
    static juce::Colour text()   { return juce::Colour (0xffdfe3ee); }
    static juce::Colour dim()    { return juce::Colour (0xff7c8296); }
    static juce::Colour grain()  { return juce::Colour (0xffc8ff3d); }
    static juce::Colour pad()    { return juce::Colour (0xff3de0ff); }
    static juce::Colour far()    { return juce::Colour (0xffff3d8b); }
    static juce::Colour drift()  { return juce::Colour (0xffffb23d); }
    static juce::Colour teal()   { return juce::Colour (0xff4de8d0); }
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

// пустой вид для невидимых слайдеров под макро-ручками
class NullLook : public juce::LookAndFeel_V4
{
public:
    void drawRotarySlider (juce::Graphics&, int, int, int, int, float, float, float, juce::Slider&) override {}
};
}

// Обычная ручка (подробный режим)
class KnobComp : public juce::Component, public juce::SettableTooltipClient
{
public:
    KnobComp (PoolProcessor&, int paramIndex, juce::Colour accent);
    ~KnobComp() override { slider.setLookAndFeel (nullptr); }
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

// Большая макро-ручка главного экрана
class MacroKnob : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    enum Icon { Waves, Spiral, Dots, Circles };
    MacroKnob (PoolProcessor&, int paramIndex, const juce::String& title, Icon icon, juce::Colour accent);
    ~MacroKnob() override { slider.setLookAndFeel (nullptr); stopTimer(); }
    void resized() override;
    void paint (juce::Graphics&) override;
private:
    void timerCallback() override { repaint(); }
    PoolProcessor& proc;
    juce::Slider slider;
    poolui::NullLook nullLook;
    std::unique_ptr<juce::SliderParameterAttachment> att;
    juce::String title;
    Icon icon;
    juce::Colour col;
    float t = 0;
};

// Кнопка режима: иконка, подпись, точка
class ModeButton : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    enum Icon { Capture, Overdub, Freeze, Evolve };
    ModeButton (const juce::String& label, Icon i, std::function<bool()> isOn, std::function<void()> onClick);
    ~ModeButton() override { stopTimer(); }
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent& e) override { if (getLocalBounds().contains (e.getPosition())) { click(); pulse = 1.0f; } }
    void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hover = false; repaint(); }
private:
    void timerCallback() override { pulse = juce::jmax (0.0f, pulse - .04f); repaint(); }
    juce::String label;
    Icon icon;
    std::function<bool()> on;
    std::function<void()> click;
    bool hover = false;
    float pulse = 0;
};

// Измеритель с мини-осциллограммой
class MeterComp : public juce::Component
{
public:
    MeterComp (const juce::String& n, bool rightSide, juce::Colour c) : name (n), right (rightSide), col (c) { hist.fill (0); }
    void push (float level);
    void paint (juce::Graphics&) override;
private:
    juce::String name;
    bool right;
    juce::Colour col;
    std::array<float, 28> hist;
    float smooth = 0;
};

// Живая «жидкая» форма в центре
class BlobComp : public juce::Component, private juce::Timer
{
public:
    explicit BlobComp (PoolProcessor&);
    ~BlobComp() override { stopTimer(); }
    void setCompact (bool c) { compact = c; repaint(); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent& e) override { drag (e); }
    void mouseDrag (const juce::MouseEvent& e) override { drag (e); }
private:
    void timerCallback() override;
    void drag (const juce::MouseEvent&);
    juce::Path makeBlob (juce::Rectangle<float> a, float scale, float phase, float amp) const;

    PoolProcessor& proc;
    juce::dsp::FFT fft { 10 };
    juce::dsp::WindowingFunction<float> window { 1024, juce::dsp::WindowingFunction<float>::hann };
    std::vector<float> fftBuf;
    std::array<float, 32> bands;
    float lvl[3] { 0, 0, 0 };
    float t = 0;
    bool compact = false;
    pool::GrainCloud::VizGrain grains[pool::Engine::kVizMax];
    int nGrains = 0;
    struct Dust { float x, y, vx, vy, s, a; };
    std::array<Dust, 70> dust;
    struct Bubble { float x, y, r, ph, sp; };
    std::array<Bubble, 9> bubbles;
    juce::Rectangle<float> blobArea;
};

class PresetPill : public juce::Component
{
public:
    explicit PresetPill (PoolProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
private:
    PoolProcessor& proc;
};

class IconButton : public juce::Component, public juce::SettableTooltipClient
{
public:
    enum Type { Pulse, Gear };
    IconButton (Type t, std::function<void()> f) : type (t), fn (std::move (f)) {}
    void click() { fn(); }
    void setOn (bool b) { if (on != b) { on = b; repaint(); } }
    bool isOn() const { return on; }
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent& e) override { if (getLocalBounds().contains (e.getPosition())) fn(); }
    void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hover = false; repaint(); }
private:
    Type type;
    std::function<void()> fn;
    bool on = false, hover = false;
};

class ABComp : public juce::Component
{
public:
    explicit ABComp (PoolProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent& e) override { proc.toggleAB (e.x < getWidth() / 2 ? 0 : 1); repaint(); }
private:
    PoolProcessor& proc;
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
    void setDetail (bool d);

    PoolProcessor& proc;
    poolui::PoolLook look;
    BlobComp blob;
    PresetPill pill;
    IconButton randBtn, gearBtn;
    ABComp ab;
    juce::OwnedArray<ModeButton> modes;
    juce::OwnedArray<MacroKnob> macros;
    MeterComp inMeter { "IN", false, juce::Colour (0xff5aa8ff) }, outMeter { "OUT", true, juce::Colour (0xffbff4ff) };

    // подробный режим
    juce::OwnedArray<juce::TextButton> tabs, chips, playBtns, saveBtns, editBtns;
    juce::OwnedArray<KnobComp> knobs;
    juce::TextButton scnToggle;
    juce::Rectangle<int> chipRow, sceneRow, capSave, capEdit;
    juce::TooltipWindow tooltip { this, 600 };
    bool detail = false;
    int group = 1;
};
