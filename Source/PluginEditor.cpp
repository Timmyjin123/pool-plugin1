#include "PluginEditor.h"

using namespace pool;
using namespace poolui;

static juce::String U (const char* s) { return juce::String::fromUTF8 (s); }
static juce::Font F (float h, bool bold = false) { return juce::Font (juce::FontOptions (h, bold ? juce::Font::bold : juce::Font::plain)); }

static juce::Colour groupColour (int g)
{
    static const juce::uint32 c[kNumGroups] = { 0xffe8e8f0, 0xffc8ff3d, 0xff3de0ff, 0xffff3d8b, 0xffff7a3d, 0xff9d7aff, 0xffffb23d };
    return juce::Colour (c[g % kNumGroups]);
}

// ============================================================ LookAndFeel
PoolLook::PoolLook()
{
    setColour (juce::ResizableWindow::backgroundColourId, Palette::bg());
    setColour (juce::Label::textColourId, Palette::text());
    setColour (juce::Slider::textBoxTextColourId, Palette::dim());
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::ComboBox::backgroundColourId, Palette::panel());
    setColour (juce::ComboBox::textColourId, Palette::text());
    setColour (juce::ComboBox::outlineColourId, Palette::line());
    setColour (juce::PopupMenu::backgroundColourId, Palette::panel());
    setColour (juce::PopupMenu::textColourId, Palette::text());
    setColour (juce::PopupMenu::highlightedBackgroundColourId, Palette::line());
}

juce::Font PoolLook::getTextButtonFont (juce::TextButton&, int h) { return F (juce::jmin (14.0f, (float) h * .5f)); }
juce::Font PoolLook::getComboBoxFont (juce::ComboBox&) { return F (14.0f); }
juce::Font PoolLook::getLabelFont (juce::Label& l) { return F (l.getFont().getHeight()); }

void PoolLook::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider& s)
{
    const auto col = juce::Colour ((juce::uint32) (int) s.getProperties().getWithDefault ("col", (int) 0xffffffff));
    const float d = (float) juce::jmin (w, h) - 8.0f;
    const auto c = juce::Rectangle<float> ((float) x + (float) w * .5f - d * .5f, (float) y + (float) h * .5f - d * .5f, d, d);
    const float r = d * .5f, ang = a0 + pos * (a1 - a0);
    juce::Path track; track.addCentredArc (c.getCentreX(), c.getCentreY(), r, r, 0, a0, a1, true);
    g.setColour (Palette::line()); g.strokePath (track, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    juce::Path val; val.addCentredArc (c.getCentreX(), c.getCentreY(), r, r, 0, a0, ang, true);
    g.setColour (col.withAlpha (s.isEnabled() ? .95f : .3f));
    g.strokePath (val, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    // дрейф: оранжевая точка на эффективном значении
    const float eff = (float) (double) s.getProperties().getWithDefault ("eff", pos);
    if (std::fabs (eff - pos) > .004f)
    {
        const float ea = a0 + eff * (a1 - a0);
        g.setColour (Palette::drift());
        g.fillEllipse (c.getCentreX() + std::sin (ea) * (r + 6) - 3, c.getCentreY() - std::cos (ea) * (r + 6) - 3, 6, 6);
    }
    g.setColour (Palette::panel()); g.fillEllipse (c.reduced (7));
    g.setColour (col);
    juce::Path tick; tick.addRoundedRectangle (-1.5f, -r + 9, 3.0f, r * .38f, 1.5f);
    g.fillPath (tick, juce::AffineTransform::rotation (ang).translated (c.getCentreX(), c.getCentreY()));
}

void PoolLook::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool hi, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (.5f);
    const bool on = b.getToggleState();
    const auto accent = juce::Colour ((juce::uint32) (int) b.getProperties().getWithDefault ("col", (int) 0xff3de0ff));
    g.setColour (on ? accent.withAlpha (.22f) : Palette::panel().brighter (hi ? .1f : 0.f).brighter (down ? .1f : 0.f));
    g.fillRoundedRectangle (r, 4);
    g.setColour (on ? accent : Palette::line());
    g.drawRoundedRectangle (r, 4, 1.0f);
}

void PoolLook::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox&)
{
    g.setColour (Palette::panel()); g.fillRoundedRectangle (0, 0, (float) w, (float) h, 4);
    g.setColour (Palette::line()); g.drawRoundedRectangle (.5f, .5f, (float) w - 1, (float) h - 1, 4, 1);
    juce::Path p; p.addTriangle ((float) w - 20, (float) h * .4f, (float) w - 10, (float) h * .4f, (float) w - 15, (float) h * .62f);
    g.setColour (Palette::dim()); g.fillPath (p);
}

// ============================================================ Knob
KnobComp::KnobComp (PoolProcessor& p, int i, juce::Colour accent) : proc (p), idx (i)
{
    const auto& sp = paramSpecs()[i];
    auto* par = proc.apvts.getParameter (sp.id);
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.2f, juce::MathConstants<float>::pi * 2.8f, true);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 16);
    slider.setRange (0.0, 1.0);
    slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    slider.setColour (juce::Slider::textBoxTextColourId, Palette::text());
    slider.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    slider.setDoubleClickReturnValue (true, par->getDefaultValue());
    slider.textFromValueFunction = [par] (double v) { return par->getText ((float) v, 0); };
    slider.valueFromTextFunction = [par] (const juce::String& t) { return (double) par->getValueForText (t); };
    slider.getProperties().set ("col", (int) accent.getARGB());
    att = std::make_unique<juce::SliderParameterAttachment> (*par, slider, nullptr);
    addAndMakeVisible (slider);
    name.setText (U (sp.label), juce::dontSendNotification);
    name.setJustificationType (juce::Justification::centred);
    name.setFont (F (12.5f));
    name.setColour (juce::Label::textColourId, Palette::dim());
    name.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (name);
    setTooltip (U (sp.name));
}
void KnobComp::resized()
{
    auto r = getLocalBounds();
    name.setBounds (r.removeFromTop (18));
    slider.setBounds (r);
}
void KnobComp::paint (juce::Graphics&) {}

// ============================================================ Visual
VisualComp::VisualComp (PoolProcessor& p) : proc (p), fftBuf (2048, 0.f)
{
    for (int i = 0; i < 256; ++i)
    {
        const float x = (float) i / 255.0f;
        cmap.push_back (juce::Colour::fromHSV (.78f - .78f * x * .9f, 1.0f - .6f * x * x * x, juce::jlimit (0.f, 1.f, x * 1.4f), 1.0f));
    }
    setInterceptsMouseClicks (true, false);
    startTimerHz (30);
}

juce::Rectangle<float> VisualComp::xyArea() const { return getLocalBounds().toFloat().reduced (1); }

void VisualComp::drag (const juce::MouseEvent& e)
{
    const auto a = xyArea();
    proc.setBaseNotify (P_pos, (e.position.x - a.getX()) / a.getWidth());
    proc.setBaseNotify (P_size, (a.getBottom() - e.position.y) / a.getHeight());
}

void VisualComp::pushSpectrum()
{
    float x[1024];
    proc.copyScope (x, 1024);
    std::fill (fftBuf.begin(), fftBuf.end(), 0.f);
    std::copy (x, x + 1024, fftBuf.begin());
    window.multiplyWithWindowingTable (fftBuf.data(), 1024);
    fft.performFrequencyOnlyForwardTransform (fftBuf.data());

    juce::Image copy = spec.createCopy();
    juce::Graphics g (spec);
    g.setOpacity (1.0f);
    g.drawImageAt (copy, -1, 0);
    const int W = spec.getWidth(), H = spec.getHeight();
    for (int y = 0; y < H; ++y)
    {
        const float f = (float) (H - 1 - y) / (float) (H - 1);
        const float bin = 2.0f * std::pow (255.0f, f);              // 2..~510, логарифмическая шкала
        const int b0 = (int) bin;
        const float m = fftBuf[(size_t) juce::jmin (511, b0)] * (1 - (bin - (float) b0)) + fftBuf[(size_t) juce::jmin (511, b0 + 1)] * (bin - (float) b0);
        const float db = 20.0f * std::log10 (m / 512.0f + 1e-7f);
        const float v = juce::jlimit (0.f, 1.f, (db + 90.0f) / 70.0f);
        spec.setPixelAt (W - 1, y, cmap[(size_t) (v * 255)].withAlpha (juce::jlimit (0.f, 1.f, v * 1.6f)));
    }
}

void VisualComp::timerCallback()
{
    t += 1.0f / 30.0f;
    pushSpectrum();
    nGrains = proc.getEngine().getGrainViz (grains, Engine::kVizMax);
    const float tgt[3] = { proc.getEngine().levelGrain(), proc.getEngine().levelPad(), proc.getEngine().levelFar() };
    for (int i = 0; i < 3; ++i) smoothLvl[i] += (std::sqrt (tgt[i]) - smoothLvl[i]) * .2f;
    repaint();
}

void VisualComp::paint (juce::Graphics& g)
{
    const auto a = xyArea();
    const float W = a.getWidth(), H = a.getHeight();
    juce::Graphics::ScopedSaveState ss (g);
    g.reduceClipRegion (a.toNearestInt());
    g.fillAll (Palette::bg());

    // спектрограмма как «вода»
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    g.setOpacity (.85f);
    g.drawImage (spec, a, juce::RectanglePlacement::stretchToFit);

    // пэд: дышащие кольца снизу
    const float lp = juce::jlimit (0.f, 1.f, smoothLvl[1] * 3.5f);
    for (int i = 0; i < 5; ++i)
    {
        const float ph = std::fmod (t * .07f + (float) i / 5.0f, 1.0f);
        const float rad = (.1f + ph * .5f) * W * (.3f + lp * .6f);
        g.setColour (Palette::pad().withAlpha ((1.0f - ph) * (.05f + lp * .45f)));
        g.drawEllipse (a.getCentreX() - rad, a.getBottom() - rad * .35f - rad * .3f, rad * 2, rad * .7f, 1.5f + (1 - ph) * 3);
    }
    {
        juce::ColourGradient cg (Palette::pad().withAlpha (.05f + lp * .35f), a.getCentreX(), a.getBottom(), Palette::pad().withAlpha (0.f), a.getCentreX(), a.getBottom() - H * .6f, true);
        g.setGradientFill (cg); g.fillRect (a);
    }

    // далёкий голос: ленты
    const float fenv = proc.getFarEnv(), lf = juce::jlimit (0.f, 1.f, smoothLvl[2] * 4.0f) * fenv;
    const float reg = proc.getEffective (P_freg);
    const float baseY = a.getY() + H * (1.0f - reg) * .8f + H * .1f;
    for (int k = 0; k < 4; ++k)
    {
        juce::Path p;
        for (int xx = 0; xx <= (int) W; xx += 6)
        {
            const float fx = (float) xx / W;
            const float y = baseY + std::sin (fx * (3.0f + (float) k) + t * (.25f + .06f * (float) k) + (float) k) * H * .045f * (1 + lf * 2)
                          + std::sin (fx * 11.0f - t * .5f + (float) k * 2) * H * .008f;
            if (xx == 0) p.startNewSubPath (a.getX(), y); else p.lineTo (a.getX() + (float) xx, y);
        }
        g.setColour (Palette::far().withAlpha (.1f + lf * .55f - .08f * (float) k));
        g.strokePath (p, juce::PathStrokeType (1.0f + (float) (3 - k) * lf * 2.2f));
    }

    // облако зёрен: x = позиция в записи, y = панорама
    const float pos = proc.getEffective (P_pos), pj = paramToValue (paramSpecs()[P_pj], proc.getEffective (P_pj));
    g.setColour (Palette::grain().withAlpha (.05f));
    g.fillRect (juce::Rectangle<float> (a.getX() + juce::jmax (0.f, pos - pj * .9f) * W, a.getY(), juce::jmin (1.f, pos + pj * .9f) * W - juce::jmax (0.f, pos - pj * .9f) * W, H));
    for (int i = 0; i < nGrains; ++i)
    {
        const auto& gr = grains[i];
        const float env = std::sin (juce::MathConstants<float>::pi * gr.life);
        const float px = a.getX() + juce::jlimit (0.f, 1.f, gr.back) * W;
        const float py = a.getY() + (1.0f - gr.pan) * H * .9f + H * .05f;
        const float rad = 2.0f + std::sqrt (gr.size) * 10.0f;
        const auto col = juce::Colour::fromHSV (.22f + gr.semis * .012f, .8f, 1.0f, 1.0f);
        g.setColour (col.withAlpha (env * .12f)); g.fillEllipse (px - rad * 1.8f, py - rad * 1.8f, rad * 3.6f, rad * 3.6f);
        g.setColour (col.withAlpha (env * .85f)); g.fillEllipse (px - rad * .5f, py - rad * .5f, rad, rad);
    }

    // прицел XY
    const float xs = a.getX() + proc.getBase (P_pos) * W, ys = a.getBottom() - proc.getBase (P_size) * H;
    const float xe = a.getX() + pos * W, ye = a.getBottom() - proc.getEffective (P_size) * H;
    g.setColour (juce::Colours::white.withAlpha (.35f));
    g.drawLine (xs, a.getY(), xs, a.getBottom(), 1.0f); g.drawLine (a.getX(), ys, a.getRight(), ys, 1.0f);
    g.setColour (Palette::drift()); g.fillEllipse (xe - 4, ye - 4, 8, 8);
    g.setColour (juce::Colours::white); g.drawEllipse (xs - 7, ys - 7, 14, 14, 1.5f);

    if (proc.isFrozen())
    {
        g.setColour (Palette::pad().withAlpha (.12f)); g.fillRect (a);
        g.setColour (Palette::pad()); g.setFont (F (13.0f, true));
        g.drawText (U ("❄ ЗАМОРОЖЕНО"), a.reduced (10).toNearestInt(), juce::Justification::topRight);
    }
    g.setColour (Palette::dim()); g.setFont (F (11.5f));
    g.drawText (U ("← прошлое   позиция в записи   давнее →"), a.reduced (8, 6).toNearestInt(), juce::Justification::bottomLeft);
    g.drawText (U ("перетащите: позиция ⟷ размер зерна"), a.reduced (8, 6).toNearestInt(), juce::Justification::bottomRight);
}

// ============================================================ Editor
PoolEditor::PoolEditor (PoolProcessor& p) : AudioProcessorEditor (&p), proc (p), visual (p)
{
    setLookAndFeel (&look);
    setSize (1040, 660);
    addAndMakeVisible (visual);

    presets.addItem (U ("— свой звук —"), 1);
    for (size_t i = 0; i < presetDefs().size(); ++i) presets.addItem (U (presetDefs()[i].name), (int) i + 2);
    presets.setSelectedId (proc.getCurrentProgram() + 1, juce::dontSendNotification);
    presets.onChange = [this] { const int id = presets.getSelectedId(); if (id >= 2) proc.applyPreset (id - 2); };
    addAndMakeVisible (presets);
    prevBtn.setButtonText (U ("◀")); nextBtn.setButtonText (U ("▶"));
    auto step = [this] (int d)
    {
        const int n = (int) presetDefs().size();
        const int cur = proc.getCurrentProgram() - 1;
        const int nx = ((cur < 0 ? (d > 0 ? -1 : 0) : cur) + d + n) % n;
        proc.applyPreset (nx);
    };
    prevBtn.onClick = [step] { step (-1); };
    nextBtn.onClick = [step] { step (1); };
    addAndMakeVisible (prevBtn); addAndMakeVisible (nextBtn);

    freezeBtn.setButtonText (U ("Заморозка"));
    freezeBtn.setClickingTogglesState (true);
    freezeBtn.getProperties().set ("col", (int) Palette::pad().getARGB());
    freezeBtn.onClick = [this] { proc.setFrozen (freezeBtn.getToggleState()); };
    addAndMakeVisible (freezeBtn);

    randBtn.setButtonText (U ("Случайно"));
    randBtn.onClick = [this] { proc.randomise(); presets.setSelectedId (1, juce::dontSendNotification); };
    addAndMakeVisible (randBtn);

    for (int g = 0; g < kNumGroups; ++g)
    {
        auto* b = tabs.add (new juce::TextButton (U (groupNames()[g])));
        b->setClickingTogglesState (false);
        b->getProperties().set ("col", (int) groupColour (g).getARGB());
        b->onClick = [this, g] { showGroup (g); };
        addAndMakeVisible (b);
    }
    for (int i = 0; i < P_COUNT; ++i)
    {
        auto* k = knobs.add (new KnobComp (proc, i, groupColour (paramSpecs()[i].group)));
        addChildComponent (k);
    }
    showGroup (1);
    startTimerHz (20);
}

PoolEditor::~PoolEditor() { setLookAndFeel (nullptr); }

void PoolEditor::showGroup (int g)
{
    group = g;
    for (int i = 0; i < tabs.size(); ++i) tabs[i]->setToggleState (i == g, juce::dontSendNotification);
    for (int i = 0; i < knobs.size(); ++i) knobs[i]->setVisible (paramSpecs()[i].group == g);
    resized();
}

void PoolEditor::timerCallback()
{
    for (int i = 0; i < knobs.size(); ++i) if (knobs[i]->isVisible()) { knobs[i]->setEffective (proc.getEffective (i)); knobs[i]->repaint(); }
    if (freezeBtn.getToggleState() != proc.isFrozen()) freezeBtn.setToggleState (proc.isFrozen(), juce::dontSendNotification);
    const int id = proc.getCurrentProgram() + 1;
    if (presets.getSelectedId() != id && id >= 1) presets.setSelectedId (id, juce::dontSendNotification);
}

void PoolEditor::paint (juce::Graphics& g)
{
    g.fillAll (Palette::bg());
    g.setColour (Palette::text()); g.setFont (F (26.0f, true));
    g.drawText ("POOL", 20, 12, 140, 34, juce::Justification::centredLeft);
    g.setColour (Palette::dim()); g.setFont (F (12.0f));
    g.drawText (U ("текстуры из любого звука"), 108, 20, 220, 20, juce::Justification::centredLeft);

    const auto lvl = juce::Rectangle<float> (20, 628, 1000, 4);
    auto& e = proc.getEngine();
    const float v[3] = { e.levelGrain(), e.levelPad(), e.levelFar() };
    const juce::Colour c[3] = { Palette::grain(), Palette::pad(), Palette::far() };
    for (int i = 0; i < 3; ++i)
    {
        g.setColour (Palette::line()); g.fillRect (lvl.withY (lvl.getY() + (float) i * 7));
        g.setColour (c[i]); g.fillRect (lvl.withY (lvl.getY() + (float) i * 7).withWidth (lvl.getWidth() * juce::jlimit (0.f, 1.f, std::sqrt (v[i]) * 1.6f)));
    }
}

void PoolEditor::resized()
{
    auto r = getLocalBounds().reduced (20, 12);
    auto top = r.removeFromTop (36);
    randBtn.setBounds (top.removeFromRight (100)); top.removeFromRight (8);
    freezeBtn.setBounds (top.removeFromRight (110)); top.removeFromRight (8);
    nextBtn.setBounds (top.removeFromRight (36)); top.removeFromRight (4);
    presets.setBounds (top.removeFromRight (230)); top.removeFromRight (4);
    prevBtn.setBounds (top.removeFromRight (36));
    r.removeFromTop (10);
    visual.setBounds (r.removeFromTop (330));
    r.removeFromTop (10);
    auto tb = r.removeFromTop (30);
    const int tw = tb.getWidth() / juce::jmax (1, (int) tabs.size());
    for (auto* b : tabs) b->setBounds (tb.removeFromLeft (tw).reduced (2, 0));
    r.removeFromTop (8);
    auto kr = r.removeFromTop (150);
    int n = 0;
    for (auto* k : knobs) if (paramSpecs()[knobs.indexOf (k)].group == group) ++n;
    const int kw = juce::jmin (100, kr.getWidth() / juce::jmax (1, n));
    kr = kr.withSizeKeepingCentre (kw * n, kr.getHeight());
    for (auto* k : knobs) if (paramSpecs()[knobs.indexOf (k)].group == group) k->setBounds (kr.removeFromLeft (kw));
}
