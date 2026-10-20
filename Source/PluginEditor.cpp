#include "PluginEditor.h"

using namespace pool;
using namespace poolui;

static juce::String U (const char* s) { return juce::String::fromUTF8 (s); }
static juce::Font F (float h, bool bold = false) { return juce::Font (juce::FontOptions (h, bold ? juce::Font::bold : juce::Font::plain)); }

static juce::Colour groupColour (int g)
{
    static const juce::uint32 c[kNumGroups] = { 0xffe8e8f0, 0xffc8ff3d, 0xff3de0ff, 0xffff3d8b, 0xffff7a3d, 0xff9d7aff, 0xffffb23d, 0xff7dffb0 };
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

// ============================================================ утилиты рисования
static juce::Font FK (float h, float kern, bool bold = false)
{
    return juce::Font (juce::FontOptions (h, bold ? juce::Font::bold : juce::Font::plain).withKerningFactor (kern));
}

static void drawSpaced (juce::Graphics& g, const juce::String& s, juce::Rectangle<int> r, juce::Justification j, float h, float kern, juce::Colour c, bool bold = false)
{
    g.setColour (c); g.setFont (FK (h, kern, bold));
    g.drawText (s, r, j, false);
}

static void iconWaves (juce::Graphics& g, juce::Point<float> c, float s, juce::Colour col)
{
    g.setColour (col);
    for (int k = -1; k <= 1; ++k)
    {
        juce::Path p;
        for (int i = 0; i <= 24; ++i)
        {
            const float x = c.x - s * .5f + s * (float) i / 24.0f;
            const float y = c.y + (float) k * s * .3f + std::sin ((float) i / 24.0f * 6.283f * 1.5f) * s * .1f;
            if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        g.strokePath (p, juce::PathStrokeType (1.4f));
    }
}
static void iconSpiral (juce::Graphics& g, juce::Point<float> c, float s, juce::Colour col)
{
    juce::Path p;
    for (int i = 0; i <= 80; ++i)
    {
        const float a = (float) i / 80.0f * 6.283f * 2.6f, r = (float) i / 80.0f * s * .5f;
        const float x = c.x + std::cos (a) * r, y = c.y + std::sin (a) * r;
        if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
    }
    g.setColour (col); g.strokePath (p, juce::PathStrokeType (1.4f));
}
static void iconDots (juce::Graphics& g, juce::Point<float> c, float s, juce::Colour col)
{
    g.setColour (col);
    for (int ring = 0; ring < 2; ++ring)
    {
        const int n = ring ? 9 : 16; const float r = s * (ring ? .22f : .5f);
        for (int i = 0; i < n; ++i)
        {
            const float a = (float) i / (float) n * 6.283f;
            g.fillEllipse (c.x + std::cos (a) * r - 1.2f, c.y + std::sin (a) * r - 1.2f, 2.4f, 2.4f);
        }
    }
}
static void iconCircles (juce::Graphics& g, juce::Point<float> c, float s, juce::Colour col)
{
    g.setColour (col);
    const float r = s * .3f;
    g.drawEllipse (c.x - r, c.y - r * 1.15f, r * 2, r * 2, 1.4f);
    g.drawEllipse (c.x - r * 1.5f, c.y + r * .1f, r * 2, r * 2, 1.4f);
    g.drawEllipse (c.x + r * -.5f, c.y + r * .1f, r * 2, r * 2, 1.4f);
}

// ============================================================ MacroKnob
MacroKnob::MacroKnob (PoolProcessor& p, int idx, const juce::String& ttl, Icon ic, juce::Colour c) : proc (p), title (ttl), icon (ic), col (c)
{
    auto* par = proc.apvts.getParameter (paramSpecs()[idx].id);
    slider.setLookAndFeel (&nullLook);
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setRange (0.0, 1.0);
    slider.setDoubleClickReturnValue (true, .5);
    slider.setMouseDragSensitivity (220);
    att = std::make_unique<juce::SliderParameterAttachment> (*par, slider, nullptr);
    addAndMakeVisible (slider);
    setTooltip (U (paramSpecs()[idx].name));
    startTimerHz (30);
}
void MacroKnob::resized() { slider.setBounds (getLocalBounds().removeFromTop (getHeight() - 38)); }

void MacroKnob::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().removeFromTop (getHeight() - 38).toFloat();
    const auto c = area.getCentre();
    const float R = juce::jmin (area.getWidth(), area.getHeight()) * .5f - 8.0f;
    const float a0 = juce::MathConstants<float>::pi * 1.2f, a1 = juce::MathConstants<float>::pi * 2.8f;
    const float v = (float) slider.getValue(), ang = a0 + v * (a1 - a0);
    juce::Path track; track.addCentredArc (c.x, c.y, R, R, 0, a0, a1, true);
    g.setColour (juce::Colours::white.withAlpha (.09f));
    g.strokePath (track, juce::PathStrokeType (2.0f));
    juce::Path val; val.addCentredArc (c.x, c.y, R, R, 0, a0, ang, true);
    const float act = slider.isMouseOverOrDragging() ? 1.0f : .0f;
    for (float w : { 14.0f, 9.0f, 5.0f })
    {
        g.setColour (col.withAlpha (.05f + act * .05f));
        g.strokePath (val, juce::PathStrokeType (w, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    juce::ColourGradient gr (col.brighter (.35f), c.x, c.y - R, col.darker (.15f), c.x, c.y + R, false);
    g.setGradientFill (gr);
    g.strokePath (val, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    // ползунок
    g.setColour (col.brighter (.6f));
    g.fillEllipse (c.x + std::sin (ang) * R - 3.5f, c.y - std::cos (ang) * R - 3.5f, 7, 7);
    // иконка
    const float s = R * .62f;
    const auto ic = col.withAlpha (.9f);
    switch (icon) { case Waves: iconWaves (g, c, s, ic); break; case Spiral: iconSpiral (g, c, s, ic); break; case Dots: iconDots (g, c, s, ic); break; case Circles: iconCircles (g, c, s, ic); break; }
    // подпись
    auto lab = getLocalBounds().removeFromBottom (38);
    const bool showVal = slider.isMouseButtonDown();
    drawSpaced (g, showVal ? juce::String (juce::roundToInt (v * 100)) + "%" : title, lab.removeFromTop (22), juce::Justification::centred, 13.5f, .32f, showVal ? col.brighter (.3f) : poolui::Palette::text(), false);
    const bool mod = std::fabs (v - .5f) > .02f;
    const float dx = (float) lab.getCentreX(), dy = (float) lab.getY() + 8.0f;
    if (mod) { g.setColour (col.withAlpha (.25f)); g.fillEllipse (dx - 6, dy - 6, 12, 12); }
    g.setColour (mod ? col : juce::Colours::white.withAlpha (.5f));
    g.fillEllipse (dx - 2.5f, dy - 2.5f, 5, 5);
}

// ============================================================ ModeButton
ModeButton::ModeButton (const juce::String& l, Icon i, std::function<bool()> o, std::function<void()> c) : label (l), icon (i), on (std::move (o)), click (std::move (c))
{
    startTimerHz (30);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void ModeButton::paint (juce::Graphics& g)
{
    const bool a = on();
    const auto teal = poolui::Palette::teal();
    const auto b = getLocalBounds().toFloat();
    const juce::Point<float> c (b.getX() + 30.0f, b.getY() + 24.0f);
    const float r = 14.0f;
    const auto ink = a ? teal : juce::Colours::white.withAlpha (hover ? .85f : .6f);
    if (a || pulse > .01f)
    {
        for (float w : { 16.0f, 10.0f, 5.0f }) { g.setColour (teal.withAlpha ((a ? .06f : 0.f) + pulse * .08f)); g.fillEllipse (c.x - r - w * .5f, c.y - r - w * .5f, (r + w * .5f) * 2, (r + w * .5f) * 2); }
    }
    g.setColour (ink); g.setOpacity (1.0f);
    switch (icon)
    {
        case Capture:
        {
            juce::Path p; p.addEllipse (c.x - r + 2, c.y - r + 2, 2 * r - 4, 2 * r - 4);
            juce::Path d; juce::PathStrokeType (1.3f).createDashedStroke (d, p, std::array<float, 2> { 2.5f, 3.0f }.data(), 2);
            g.fillPath (d);
            break;
        }
        case Overdub:
            g.drawEllipse (c.x - r + 1, c.y - r + 3, 2 * r - 6, 2 * r - 6, 1.3f);
            g.drawEllipse (c.x - r + 8, c.y - r + 3, 2 * r - 6, 2 * r - 6, 1.3f);
            break;
        case Freeze:
            for (int i = 0; i < 4; ++i)
            {
                const float ang = (float) i * juce::MathConstants<float>::pi / 4.0f;
                g.drawLine (c.x - std::cos (ang) * (r - 2), c.y - std::sin (ang) * (r - 2), c.x + std::cos (ang) * (r - 2), c.y + std::sin (ang) * (r - 2), 1.3f);
            }
            break;
        case Evolve:
            g.drawEllipse (c.x - r + 1, c.y - r + 1, 2 * r - 2, 2 * r - 2, 1.5f);
            g.fillEllipse (c.x - 4.5f, c.y - 4.5f, 9, 9);
            break;
    }
    drawSpaced (g, label, getLocalBounds().withTrimmedLeft (66).withHeight (48), juce::Justification::centredLeft, 13.0f, .3f, a ? teal : poolui::Palette::dim().brighter (.35f));
    g.setColour (a ? teal : juce::Colours::white.withAlpha (.35f));
    g.fillEllipse (c.x - 2, b.getY() + 58.0f, 4, 4);
}

// ============================================================ MeterComp
void MeterComp::push (float level)
{
    const float v = juce::jlimit (0.f, 1.f, std::sqrt (level) * 1.8f);
    smooth += (v - smooth) * (v > smooth ? .5f : .12f);
    for (size_t i = 0; i + 1 < hist.size(); ++i) hist[i] = hist[i + 1];
    hist.back() = smooth;
    repaint();
}

void MeterComp::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    const float y = b.getY() + 22.0f, w = b.getWidth();
    const float wfW = 64.0f;
    // линия и мини-осциллограмма (у IN волна справа, у OUT слева — как на макете)
    const float lx0 = right ? b.getX() + wfW : b.getX(), lx1 = right ? b.getRight() : b.getRight() - wfW;
    const float wx0 = right ? b.getX() : b.getRight() - wfW;
    g.setColour (juce::Colours::white.withAlpha (.1f));
    g.drawLine (lx0, y, lx1, y, 1.5f);
    const float fillW = (lx1 - lx0) * smooth;
    juce::ColourGradient gr (col.withAlpha (.2f), lx0, y, col, right ? lx0 + fillW : lx0 + fillW, y, false);
    g.setGradientFill (gr);
    if (right) g.drawLine (lx1 - fillW, y, lx1, y, 2.0f); else g.drawLine (lx0, y, lx0 + fillW, y, 2.0f);
    for (size_t i = 0; i < hist.size(); ++i)
    {
        const float x = wx0 + wfW * (float) i / (float) hist.size();
        const float h = 2.0f + hist[right ? hist.size() - 1 - i : i] * 24.0f;
        g.setColour (col.withAlpha (.25f + .5f * hist[right ? hist.size() - 1 - i : i]));
        g.drawLine (x, y - h * .5f, x, y + h * .5f, 1.3f);
    }
    drawSpaced (g, name, getLocalBounds().withTop ((int) y + 24), right ? juce::Justification::centredRight : juce::Justification::centredLeft, 11.5f, .45f, poolui::Palette::dim());
    juce::ignoreUnused (w);
}

// ============================================================ Header widgets
void PresetPill::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat().reduced (1);
    g.setColour (juce::Colours::white.withAlpha (.035f)); g.fillRoundedRectangle (b, b.getHeight() * .5f);
    g.setColour (juce::Colours::white.withAlpha (.16f)); g.drawRoundedRectangle (b, b.getHeight() * .5f, 1.0f);
    const int cur = proc.getCurrentProgram();
    const juce::String name = cur <= 0 ? U ("— свой звук —") : U (presetDefs()[(size_t) cur - 1].name);
    drawSpaced (g, name, getLocalBounds().reduced (44, 0), juce::Justification::centred, 15.0f, .12f, poolui::Palette::text());
    g.setColour (poolui::Palette::dim().brighter (.5f));
    for (int s = 0; s < 2; ++s)
    {
        const float cx = s == 0 ? 22.0f : (float) getWidth() - 22.0f, cy = (float) getHeight() * .5f, d = s == 0 ? -1.0f : 1.0f;
        juce::Path p; p.startNewSubPath (cx - 3 * d, cy - 6); p.lineTo (cx + 3 * d, cy); p.lineTo (cx - 3 * d, cy + 6);
        g.strokePath (p, juce::PathStrokeType (1.6f));
    }
}

void PresetPill::mouseUp (const juce::MouseEvent& e)
{
    const int n = (int) presetDefs().size();
    if (e.x < 44 || e.x > getWidth() - 44)
    {
        const int cur = proc.getCurrentProgram() - 1, d = e.x < 44 ? -1 : 1;
        proc.applyPreset (((cur < 0 ? (d > 0 ? -1 : 0) : cur) + d + n) % n);
    }
    else
    {
        juce::PopupMenu m;
        for (int i = 0; i < n; ++i) m.addItem (i + 1, U (presetDefs()[(size_t) i].name), true, proc.getCurrentProgram() == i + 1);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [this] (int r) { if (r > 0) proc.applyPreset (r - 1); });
    }
    repaint();
}

void IconButton::paint (juce::Graphics& g)
{
    const auto c = getLocalBounds().toFloat().getCentre();
    const auto col = on ? poolui::Palette::teal() : juce::Colours::white.withAlpha (hover ? .95f : .7f);
    g.setColour (col);
    if (type == Pulse)
    {
        juce::Path p; p.startNewSubPath (c.x - 12, c.y); p.lineTo (c.x - 6, c.y); p.lineTo (c.x - 3, c.y - 8); p.lineTo (c.x + 2, c.y + 9); p.lineTo (c.x + 5, c.y - 3); p.lineTo (c.x + 12, c.y - 3);
        g.strokePath (p, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved));
    }
    else
    {
        for (int i = 0; i < 8; ++i)
        {
            const float a = (float) i / 8.0f * juce::MathConstants<float>::twoPi;
            g.drawLine (c.x + std::cos (a) * 7.5f, c.y + std::sin (a) * 7.5f, c.x + std::cos (a) * 10.5f, c.y + std::sin (a) * 10.5f, 2.6f);
        }
        g.drawEllipse (c.x - 7.5f, c.y - 7.5f, 15, 15, 1.6f);
        g.drawEllipse (c.x - 3, c.y - 3, 6, 6, 1.4f);
    }
}

void ABComp::paint (juce::Graphics& g)
{
    const int cur = proc.getAB();
    auto r = getLocalBounds();
    drawSpaced (g, "A", r.removeFromLeft (r.getWidth() / 2 - 8), juce::Justification::centredRight, 14.0f, .2f, cur == 0 ? poolui::Palette::text() : poolui::Palette::dim(), cur == 0);
    drawSpaced (g, "/", r.removeFromLeft (16), juce::Justification::centred, 14.0f, 0, poolui::Palette::dim());
    drawSpaced (g, "B", r, juce::Justification::centredLeft, 14.0f, .2f, cur == 1 ? poolui::Palette::text() : poolui::Palette::dim(), cur == 1);
}

// ============================================================ BlobComp
BlobComp::BlobComp (PoolProcessor& p) : proc (p), fftBuf (2048, 0.f)
{
    bands.fill (0);
    juce::Random rnd (42);
    for (auto& d : dust) d = { rnd.nextFloat(), rnd.nextFloat(), (rnd.nextFloat() - .5f) * .0006f, (rnd.nextFloat() - .5f) * .0004f, .6f + rnd.nextFloat() * 1.6f, .15f + rnd.nextFloat() * .5f };
    for (auto& b : bubbles) b = { rnd.nextFloat(), rnd.nextFloat(), 3.0f + rnd.nextFloat() * 9.0f, rnd.nextFloat() * 6.28f, .2f + rnd.nextFloat() * .5f };
    setInterceptsMouseClicks (true, false);
    startTimerHz (30);
}

void BlobComp::drag (const juce::MouseEvent& e)
{
    const auto a = getLocalBounds().toFloat().reduced (30);
    proc.setBaseNotify (P_pos, (e.position.x - a.getX()) / a.getWidth());
    proc.setBaseNotify (P_size, (a.getBottom() - e.position.y) / a.getHeight());
}

void BlobComp::timerCallback()
{
    t += 1.0f / 30.0f;
    float x[1024];
    proc.copyScope (x, 1024);
    std::fill (fftBuf.begin(), fftBuf.end(), 0.f);
    std::copy (x, x + 1024, fftBuf.begin());
    window.multiplyWithWindowingTable (fftBuf.data(), 1024);
    fft.performFrequencyOnlyForwardTransform (fftBuf.data());
    for (size_t k = 0; k < bands.size(); ++k)
    {
        const int b0 = (int) (2.0f * std::pow (200.0f, (float) k / (float) bands.size())), b1 = juce::jmax (b0 + 1, (int) (2.0f * std::pow (200.0f, (float) (k + 1) / (float) bands.size())));
        float m = 0; for (int i = b0; i < juce::jmin (b1, 511); ++i) m = juce::jmax (m, fftBuf[(size_t) i]);
        const float db = 20.0f * std::log10 (m / 512.0f + 1e-7f);
        const float v = juce::jlimit (0.f, 1.f, (db + 85.0f) / 55.0f);
        bands[k] += (v - bands[k]) * (v > bands[k] ? .5f : .12f);
    }
    nGrains = proc.getEngine().getGrainViz (grains, pool::Engine::kVizMax);
    const float tg[3] = { proc.getEngine().levelGrain(), proc.getEngine().levelPad(), proc.getEngine().levelFar() };
    for (int i = 0; i < 3; ++i) lvl[i] += (std::sqrt (tg[i]) - lvl[i]) * .15f;
    for (auto& d : dust) { d.x += d.vx; d.y += d.vy; if (d.x < 0) d.x += 1; if (d.x > 1) d.x -= 1; if (d.y < 0) d.y += 1; if (d.y > 1) d.y -= 1; }
    for (auto& b : bubbles) { b.y -= b.sp * .0006f; if (b.y < -.05f) { b.y = 1.05f; } b.x += std::sin (t * .4f + b.ph) * .0003f; }
    repaint();
}

juce::Path BlobComp::makeBlob (juce::Rectangle<float> a, float scale, float phase, float amp) const
{
    const int N = 96;
    const auto c = a.getCentre();
    const float rx = a.getWidth() * .5f * scale, ry = a.getHeight() * .5f * scale;
    const float ph = t * .5f + phase;
    juce::Path p;
    std::array<juce::Point<float>, 96> pts;
    for (int i = 0; i < N; ++i)
    {
        const float th = (float) i / (float) N * juce::MathConstants<float>::twoPi;
        const float bandV = bands[(size_t) (((float) i / (float) N + phase * .13f) * (float) bands.size()) % bands.size()];
        float n = .10f * std::sin (2 * th + ph) + .07f * std::sin (3 * th - ph * 1.3f + 1.3f) + .05f * std::sin (5 * th + ph * 1.7f)
                + .035f * std::sin (8 * th - ph * 2.2f) * (.4f + lvl[1] * 2);
        n += bandV * .16f * amp + lvl[0] * .3f * std::sin (4 * th + ph * 2.0f) * .35f;
        const float r = 1.0f + n;
        // горизонтальная «складка»: форма вытянута и слегка скручена
        const float x = c.x + rx * std::cos (th) * r;
        const float y = c.y + ry * std::sin (th) * r + std::sin (th * 2 + ph) * ry * .12f * std::cos (th);
        pts[(size_t) i] = { x, y };
    }
    for (int i = 0; i < N; ++i)
    {
        const auto p0 = pts[(size_t) ((i + N - 1) % N)], p1 = pts[(size_t) i], p2 = pts[(size_t) ((i + 1) % N)], p3 = pts[(size_t) ((i + 2) % N)];
        if (i == 0) p.startNewSubPath (p1);
        // Catmull–Rom -> Безье
        const auto c1 = p1 + (p2 - p0) / 6.0f, c2 = p2 - (p3 - p1) / 6.0f;
        p.cubicTo (c1, c2, p2);
    }
    p.closeSubPath();
    return p;
}

void BlobComp::paint (juce::Graphics& g)
{
    const auto full = getLocalBounds().toFloat();
    const float totalLvl = juce::jlimit (0.f, 1.f, (lvl[0] + lvl[1] + lvl[2]) * 1.4f);
    const float breathe = 1.0f + .04f * std::sin (t * .6f) + totalLvl * .08f;
    blobArea = full.reduced (compact ? 300.0f : 250.0f, compact ? 40.0f : 46.0f);
    const auto c = blobArea.getCentre();

    // фоновое свечение
    {
        juce::ColourGradient bgGlow (juce::Colour (0xff1a4a7a).withAlpha (.10f + totalLvl * .12f), c.x - blobArea.getWidth() * .2f, c.y,
                                     juce::Colour (0x00000000), c.x - blobArea.getWidth() * .2f, c.y - blobArea.getHeight() * 1.1f, true);
        g.setGradientFill (bgGlow); g.fillRect (full);
        juce::ColourGradient bgGlow2 (juce::Colour (0xffd89a4a).withAlpha (.04f + lvl[2] * .4f), c.x + blobArea.getWidth() * .3f, c.y,
                                      juce::Colour (0x00000000), c.x + blobArea.getWidth() * .3f, c.y - blobArea.getHeight() * .9f, true);
        g.setGradientFill (bgGlow2); g.fillRect (full);
    }
    // тонкая орбита вокруг формы
    g.setColour (juce::Colours::white.withAlpha (.05f));
    g.drawEllipse (blobArea.expanded (compact ? 40.0f : 90.0f, compact ? 8.0f : 36.0f), 1.0f);

    // основной слой (+ второй, сдвинутый по фазе, даёт ощущение глубины)
    const auto outer = makeBlob (blobArea, breathe, 0.0f, 1.0f);
    const auto inner = makeBlob (blobArea, breathe * .82f, 2.1f, .8f);

    for (float w : { 22.0f, 14.0f, 8.0f, 4.0f })
    {
        g.setColour (juce::Colour (0xff3d9bff).withAlpha (.018f + totalLvl * .02f));
        g.strokePath (outer, juce::PathStrokeType (w, juce::PathStrokeType::curved));
    }
    const auto bb = outer.getBounds();
    const float amber = juce::jlimit (0.f, 1.f, .35f + lvl[2] * 3.0f);
    {
        juce::ColourGradient fill (juce::Colour (0xff1f56d8).withAlpha (.75f), bb.getX(), c.y, juce::Colour (0xffe8a55a).withAlpha (.35f + .45f * amber), bb.getRight(), c.y, false);
        fill.addColour (.28, juce::Colour (0xff2ea0e6).withAlpha (.65f));
        fill.addColour (.55, juce::Colour (0xff27c9a8).withAlpha (.60f));
        fill.addColour (.78, juce::Colour (0xff8ed49a).withAlpha (.50f));
        g.setGradientFill (fill); g.fillPath (outer);
        juce::ColourGradient shade (juce::Colour (0x00000000), c.x, bb.getY(), juce::Colour (0xff04060b).withAlpha (.72f), c.x, bb.getBottom(), false);
        g.setGradientFill (shade); g.fillPath (outer);
    }
    {
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (outer);
        // внутренний слой
        juce::ColourGradient in2 (juce::Colour (0xff6fc8ff).withAlpha (.30f), bb.getX(), bb.getY(), juce::Colour (0xff22d6b0).withAlpha (.12f), bb.getRight(), bb.getBottom(), false);
        g.setGradientFill (in2); g.fillPath (inner);
        // струящиеся линии
        for (int j = 0; j < 22; ++j)
        {
            const float fj = (float) j / 21.0f;
            const float y0 = bb.getY() + bb.getHeight() * (.05f + .9f * fj);
            juce::Path l;
            for (int i = 0; i <= 56; ++i)
            {
                const float fx = (float) i / 56.0f;
                const float x = bb.getX() + bb.getWidth() * fx;
                const float y = y0 + std::sin (fx * 6.0f + t * (.35f + .02f * (float) j) + (float) j * .6f) * bb.getHeight() * (.05f + lvl[1] * .08f)
                                   + std::sin (fx * 13.0f - t * .5f + (float) j) * bb.getHeight() * .012f * (1 + lvl[0] * 6);
                if (i == 0) l.startNewSubPath (x, y); else l.lineTo (x, y);
            }
            g.setColour (juce::Colours::white.withAlpha (.05f + .08f * std::sin (fj * 3.14f)));
            g.strokePath (l, juce::PathStrokeType (.8f));
        }
        // световые блики
        juce::ColourGradient spec (juce::Colours::white.withAlpha (.35f), bb.getX() + bb.getWidth() * .3f, bb.getY() + bb.getHeight() * .25f,
                                   juce::Colours::transparentWhite, bb.getX() + bb.getWidth() * .3f, bb.getY() + bb.getHeight() * .65f, true);
        g.setGradientFill (spec); g.fillPath (outer);
    }
    // контур с градиентом
    {
        juce::ColourGradient edge (juce::Colour (0xff8fd0ff).withAlpha (.85f), bb.getX(), c.y, juce::Colour (0xffffd39a).withAlpha (.45f + .5f * amber), bb.getRight(), c.y, false);
        edge.addColour (.5, juce::Colour (0xff7cf0d8).withAlpha (.6f));
        g.setGradientFill (edge);
        g.strokePath (outer, juce::PathStrokeType (1.3f));
    }
    // зёрна как искры внутри формы
    for (int i = 0; i < nGrains; ++i)
    {
        const auto& gr = grains[i];
        const float env = std::sin (juce::MathConstants<float>::pi * gr.life);
        const float px = bb.getX() + juce::jlimit (0.f, 1.f, gr.back) * bb.getWidth();
        const float py = bb.getY() + (1.0f - gr.pan) * bb.getHeight();
        const float rad = 1.2f + std::sqrt (gr.size) * 3.2f;
        const auto col = juce::Colour::fromHSV (.5f + gr.semis * .004f, .35f, 1.0f, 1.0f);
        g.setColour (col.withAlpha (env * .25f)); g.fillEllipse (px - rad * 2, py - rad * 2, rad * 4, rad * 4);
        g.setColour (juce::Colours::white.withAlpha (env * .9f)); g.fillEllipse (px - rad * .5f, py - rad * .5f, rad, rad);
    }
    // пылинки и пузыри
    for (auto& d : dust)
    {
        g.setColour (juce::Colours::white.withAlpha (d.a * (.4f + totalLvl)));
        g.fillEllipse (full.getX() + d.x * full.getWidth(), full.getY() + d.y * full.getHeight(), d.s, d.s);
    }
    for (auto& b : bubbles)
    {
        const float bx = full.getX() + (compact ? .22f + b.x * .56f : .12f + b.x * .76f) * full.getWidth(), by = full.getY() + b.y * full.getHeight(), r = b.r * (1 + lvl[0] * .5f);
        juce::ColourGradient bg (juce::Colours::white.withAlpha (.28f), bx - r * .3f, by - r * .35f, juce::Colour (0xff6fc8ff).withAlpha (.04f), bx + r, by + r, true);
        g.setGradientFill (bg); g.fillEllipse (bx - r, by - r, r * 2, r * 2);
        g.setColour (juce::Colours::white.withAlpha (.22f)); g.drawEllipse (bx - r, by - r, r * 2, r * 2, .8f);
    }
    // курсор позиции/размера (перетаскивание мышью)
    {
        const auto a = full.reduced (30);
        const float xs = a.getX() + proc.getBase (P_pos) * a.getWidth(), ys = a.getBottom() - proc.getBase (P_size) * a.getHeight();
        g.setColour (juce::Colours::white.withAlpha (.10f));
        g.drawLine (xs, a.getY(), xs, a.getBottom(), 1.0f); g.drawLine (a.getX(), ys, a.getRight(), ys, 1.0f);
        const float xe = a.getX() + proc.getEffective (P_pos) * a.getWidth(), ye = a.getBottom() - proc.getEffective (P_size) * a.getHeight();
        g.setColour (poolui::Palette::drift().withAlpha (.9f)); g.fillEllipse (xe - 3, ye - 3, 6, 6);
        g.setColour (juce::Colours::white.withAlpha (.65f)); g.drawEllipse (xs - 6, ys - 6, 12, 12, 1.2f);
    }
    if (proc.isFrozen())
    {
        g.setColour (juce::Colour (0xff9fe8ff).withAlpha (.07f)); g.fillRect (full);
    }
}

// ============================================================ Editor
PoolEditor::PoolEditor (PoolProcessor& p)
    : AudioProcessorEditor (&p), proc (p), blob (p), pill (p),
      randBtn (IconButton::Pulse, [this] { proc.randomise(); }),
      gearBtn (IconButton::Gear, [this] { setDetail (! detail); }),
      ab (p)
{
    setLookAndFeel (&look);
    setSize (1120, 680);
    addAndMakeVisible (blob);
    addAndMakeVisible (pill);
    addAndMakeVisible (randBtn); addAndMakeVisible (gearBtn); addAndMakeVisible (ab);
    randBtn.setTooltip (U ("Случайно: подмешать случайные значения"));
    gearBtn.setTooltip (U ("Подробные параметры"));

    modes.add (new ModeButton ("CAPTURE", ModeButton::Capture, [] { return false; }, [this] { proc.requestCapture(); }));
    modes.add (new ModeButton ("OVERDUB", ModeButton::Overdub, [this] { return proc.isOverdub(); }, [this] { proc.setOverdub (! proc.isOverdub()); }));
    modes.add (new ModeButton ("FREEZE", ModeButton::Freeze, [this] { return proc.isFrozen(); }, [this] { proc.setFrozen (! proc.isFrozen()); }));
    modes.add (new ModeButton ("EVOLVE", ModeButton::Evolve,
        [this] { return proc.isScenesOn() && proc.getBase (P_evo) > .02f; },
        [this]
        {
            if (proc.isScenesOn() && proc.getBase (P_evo) > .02f) proc.setScenesOn (false);
            else { if (proc.getBase (P_evo) <= .02f) proc.setBaseNotify (P_evo, .3f); proc.setScenesOn (true); }
        }));
    modes[0]->setTooltip (U ("Новая запись: очистить память зёрен и начать заново"));
    modes[1]->setTooltip (U ("Овердаб: выход возвращается в запись, звук наслаивается"));
    modes[2]->setTooltip (U ("Заморозка записи"));
    modes[3]->setTooltip (U ("Эволюция: плавные переходы между сценами A–D"));
    for (auto* m : modes) addAndMakeVisible (m);

    macros.add (new MacroKnob (proc, P_mtex, "TEXTURE", MacroKnob::Waves,   juce::Colour (0xff3d8bff)));
    macros.add (new MacroKnob (proc, P_mmot, "MOTION",  MacroKnob::Spiral,  juce::Colour (0xff3de8a0)));
    macros.add (new MacroKnob (proc, P_mspc, "SPACE",   MacroKnob::Dots,    juce::Colour (0xffb07dff)));
    macros.add (new MacroKnob (proc, P_mbld, "BLEND",   MacroKnob::Circles, juce::Colour (0xffffb23d)));
    for (auto* m : macros) addAndMakeVisible (m);
    addAndMakeVisible (inMeter); addAndMakeVisible (outMeter);

    // подробный режим
    {
        static const int steps[] = { -24, -12, -7, -5, -2, 0, 2, 5, 7, 12, 24 };
        for (int st : steps)
        {
            auto* c = chips.add (new juce::TextButton (st > 0 ? "+" + juce::String (st) : juce::String (st)));
            c->getProperties().set ("col", (int) 0xffe8e8f0);
            c->setTooltip (U ("Высота основного звука, полутонов"));
            c->onClick = [this, st] { proc.setBaseNotify (P_ipitch, paramFromValue (paramSpecs()[P_ipitch], (float) st)); };
            addChildComponent (c);
        }
    }
    scnToggle.setButtonText (U ("Сцены ВКЛ"));
    scnToggle.getProperties().set ("col", (int) 0xff7dffb0);
    scnToggle.onClick = [this] { proc.setScenesOn (! proc.isScenesOn()); };
    addChildComponent (scnToggle);
    for (int k = 0; k < 4; ++k)
    {
        const juce::String L = juce::String::charToString ((juce::juce_wchar) ('A' + k));
        auto mk = [this] (juce::OwnedArray<juce::TextButton>& arr, const juce::String& t, std::function<void()> fn)
        {
            auto* b = arr.add (new juce::TextButton (t));
            b->getProperties().set ("col", (int) 0xff7dffb0);
            b->onClick = std::move (fn);
            addChildComponent (b);
        };
        mk (playBtns, L, [this, k] { proc.playScene (k); });
        mk (saveBtns, juce::String (juce::CharPointer_UTF8 ("\xe2\x86\x92")) + L, [this, k] { proc.saveScene (k); });
        mk (editBtns, juce::String (juce::CharPointer_UTF8 ("\xe2\x86\x91")) + L, [this, k] { proc.recallSceneToKnobs (k); });
    }
    for (int g = 0; g < kNumGroups; ++g)
    {
        auto* b = tabs.add (new juce::TextButton (U (groupNames()[g])));
        b->getProperties().set ("col", (int) groupColour (g).getARGB());
        b->onClick = [this, g] { showGroup (g); };
        addChildComponent (b);
    }
    for (int i = 0; i < P_COUNT; ++i)
    {
        auto* k = knobs.add (new KnobComp (proc, i, groupColour (paramSpecs()[i].group)));
        addChildComponent (k);
    }
    showGroup (1);
    setDetail (false);
    startTimerHz (30);
}

PoolEditor::~PoolEditor() { setLookAndFeel (nullptr); }

void PoolEditor::setDetail (bool d)
{
    detail = d;
    gearBtn.setOn (d);
    blob.setCompact (d);
    for (auto* m : modes) m->setVisible (! d);
    for (auto* m : macros) m->setVisible (! d);
    inMeter.setVisible (! d); outMeter.setVisible (! d);
    for (auto* c : chips) c->setVisible (d);
    for (auto* t : tabs) t->setVisible (d);
    showGroup (group);
    resized();
    repaint();
}

void PoolEditor::showGroup (int g)
{
    group = g;
    for (int i = 0; i < tabs.size(); ++i) tabs[i]->setToggleState (i == g, juce::dontSendNotification);
    for (int i = 0; i < knobs.size(); ++i) knobs[i]->setVisible (detail && paramSpecs()[i].group == g);
    const bool sc = detail && (g == 7);
    scnToggle.setVisible (sc);
    for (auto* b : playBtns) b->setVisible (sc);
    for (auto* b : saveBtns) b->setVisible (sc);
    for (auto* b : editBtns) b->setVisible (sc);
    resized();
}

void PoolEditor::timerCallback()
{
    inMeter.push (proc.getInLevel());
    outMeter.push (proc.getEngine().levelOut());
    if (detail)
    {
        for (int i = 0; i < knobs.size(); ++i) if (knobs[i]->isVisible()) { knobs[i]->setEffective (proc.getEffective (i)); knobs[i]->repaint(); }
        const float ip = paramToValue (paramSpecs()[P_ipitch], proc.getBase (P_ipitch));
        for (auto* c : chips) c->setToggleState (std::fabs (c->getButtonText().getFloatValue() - ip) < .5f, juce::dontSendNotification);
        if (group == 7)
        {
            scnToggle.setToggleState (proc.isScenesOn(), juce::dontSendNotification);
            const int cur = (int) std::lround (proc.getScenePos());
            for (int k = 0; k < playBtns.size(); ++k) playBtns[k]->setToggleState (proc.isScenesOn() && k == cur, juce::dontSendNotification);
        }
    }
    pill.repaint();
}

void PoolEditor::paint (juce::Graphics& g)
{
    g.fillAll (poolui::Palette::bg());
    auto frame = getLocalBounds().toFloat().reduced (14);
    juce::ColourGradient bgG (juce::Colour (0xff0c1018), frame.getCentreX(), frame.getY(), juce::Colour (0xff05060a), frame.getCentreX(), frame.getBottom(), false);
    g.setGradientFill (bgG); g.fillRoundedRectangle (frame, 22);
    g.setColour (juce::Colours::white.withAlpha (.10f)); g.drawRoundedRectangle (frame, 22, 1.0f);
    // шапка
    g.setColour (juce::Colours::white.withAlpha (.07f));
    g.drawLine (frame.getX(), 82.0f, frame.getRight(), 82.0f, 1.0f);
    drawSpaced (g, "POOL", { 46, 28, 200, 40 }, juce::Justification::centredLeft, 30.0f, .7f, poolui::Palette::text());
    g.setColour (juce::Colours::white.withAlpha (.15f)); g.drawLine (228, 36, 228, 62, 1.0f);
    drawSpaced (g, U ("Текстуры из любого звука"), { 246, 28, 220, 40 }, juce::Justification::centredLeft, 12.0f, .12f, poolui::Palette::dim().brighter (.2f));

    if (! detail)
    {
        g.setColour (poolui::Palette::dim().brighter (.1f)); g.setFont (FK (12.5f, .55f));
        g.drawMultiLineText ("SOUND\nBECOMES\nMATTER", 70, 232, 200, juce::Justification::left, 8.0f);
        g.setColour (juce::Colours::white.withAlpha (.25f)); g.drawLine (72, 322, 98, 322, 1.0f);
        g.setFont (FK (13.0f, .3f));
        g.drawMultiLineText (U ("Звук\nстановится\nматерией"), 850, 232, 200, juce::Justification::right, 8.0f);
        g.drawLine (1022, 322, 1048, 322, 1.0f);
        g.setColour (juce::Colours::white.withAlpha (.07f));
        g.drawLine (frame.getX() + 28, 520.0f, frame.getRight() - 28, 520.0f, 1.0f);
        for (int k = 1; k < 4; ++k) { const float x = 248.0f + (float) k * 156.0f + 0; juce::ignoreUnused (x); }
    }
    else
    {
        g.setColour (poolui::Palette::dim()); g.setFont (F (12.5f));
        g.drawText (U ("Высота входа"), chipRow.withWidth (108), juce::Justification::centredLeft);
        if (! sceneRow.isEmpty())
        {
            g.drawText (U ("Записать:"), capSave, juce::Justification::centredRight);
            g.drawText (U ("В ручки:"), capEdit, juce::Justification::centredRight);
        }
    }
}

void PoolEditor::resized()
{
    auto b = getLocalBounds();
    // шапка
    pill.setBounds (b.getCentreX() + 20 - 135, 30, 270, 38);
    gearBtn.setBounds (b.getRight() - 90, 30, 36, 38);
    ab.setBounds (b.getRight() - 190, 30, 70, 38);
    randBtn.setBounds (b.getRight() - 260, 30, 44, 38);

    if (! detail)
    {
        blob.setBounds (30, 90, b.getWidth() - 60, 360);
        // режимы
        const int mw = 176, gap = 14, total = mw * 4 + gap * 3;
        int x = b.getCentreX() - total / 2;
        for (auto* m : modes) { m->setBounds (x, 450, mw, 66); x += mw + gap; }
        // макро-ручки
        const int kw = 130, kh = 160, kgap = 50, kt = kw * 4 + kgap * 3;
        x = b.getCentreX() - kt / 2;
        for (auto* k : macros) { k->setBounds (x, 524, kw, kh - 8); x += kw + kgap; }
        inMeter.setBounds (50, 572, 150, 70);
        outMeter.setBounds (b.getRight() - 200, 572, 150, 70);
        sceneRow = {}; chipRow = {};
    }
    else
    {
        blob.setBounds (30, 90, b.getWidth() - 60, 230);
        auto r = juce::Rectangle<int> (40, 330, b.getWidth() - 80, 330);
        chipRow = r.removeFromTop (26);
        {
            auto cr = chipRow; cr.removeFromLeft (110);
            const int cw = juce::jmin (52, cr.getWidth() / juce::jmax (1, chips.size()));
            for (auto* c : chips) c->setBounds (cr.removeFromLeft (cw).reduced (2, 0));
        }
        r.removeFromTop (8);
        auto tb = r.removeFromTop (30);
        const int tw = tb.getWidth() / juce::jmax (1, (int) tabs.size());
        for (auto* t : tabs) t->setBounds (tb.removeFromLeft (tw).reduced (2, 0));
        r.removeFromTop (8);
        if (group == 7)
        {
            sceneRow = r.removeFromTop (34);
            auto sr = sceneRow;
            scnToggle.setBounds (sr.removeFromLeft (110).reduced (0, 2)); sr.removeFromLeft (14);
            for (auto* bt : playBtns) bt->setBounds (sr.removeFromLeft (46).reduced (2, 2));
            sr.removeFromLeft (20);
            capSave = sr.removeFromLeft (78);
            for (auto* bt : saveBtns) bt->setBounds (sr.removeFromLeft (50).reduced (2, 2));
            sr.removeFromLeft (20);
            capEdit = sr.removeFromLeft (78);
            for (auto* bt : editBtns) bt->setBounds (sr.removeFromLeft (50).reduced (2, 2));
            r.removeFromTop (6);
        }
        else sceneRow = {};
        auto kr = r.removeFromTop (150);
        int n = 0;
        for (int i = 0; i < knobs.size(); ++i) if (paramSpecs()[i].group == group) ++n;
        const int kw = juce::jmin (100, kr.getWidth() / juce::jmax (1, n));
        kr = kr.withSizeKeepingCentre (kw * n, kr.getHeight());
        for (int i = 0; i < knobs.size(); ++i) if (paramSpecs()[i].group == group) knobs[i]->setBounds (kr.removeFromLeft (kw));
    }
}
