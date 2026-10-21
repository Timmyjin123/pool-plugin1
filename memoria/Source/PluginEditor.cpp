#include "PluginEditor.h"

using namespace mui;

static juce::String U (const char* s) { return juce::String::fromUTF8 (s); }
static juce::Font F (float h, bool bold = false, float kern = 0)
{
    return juce::Font (juce::FontOptions (h, bold ? juce::Font::bold : juce::Font::plain).withKerningFactor (kern));
}
static constexpr float kPi = juce::MathConstants<float>::pi;
static constexpr float kTau = juce::MathConstants<float>::twoPi;

// ============================================================ LookAndFeel
Look::Look()
{
    setColour (juce::ComboBox::backgroundColourId, Pal::panel());
    setColour (juce::ComboBox::textColourId, Pal::text());
    setColour (juce::ComboBox::outlineColourId, Pal::line());
    setColour (juce::PopupMenu::backgroundColourId, Pal::panel());
    setColour (juce::PopupMenu::textColourId, Pal::text());
    setColour (juce::PopupMenu::highlightedBackgroundColourId, Pal::line());
    setColour (juce::PopupMenu::highlightedTextColourId, Pal::text());
    setColour (juce::TooltipWindow::backgroundColourId, Pal::panel());
    setColour (juce::TooltipWindow::textColourId, Pal::text());
    setColour (juce::TooltipWindow::outlineColourId, Pal::line());
}

juce::Font Look::getComboBoxFont (juce::ComboBox&) { return F (14); }
juce::Font Look::getPopupMenuFont() { return F (15); }

void Look::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider& s)
{
    const auto col = juce::Colour ((juce::uint32) (int) s.getProperties().getWithDefault ("col", (int) 0xffffffff));
    const bool bip = (bool) s.getProperties().getWithDefault ("bip", false);
    const float d = (float) juce::jmin (w, h) - 10.0f;
    const auto c = juce::Rectangle<float> (d, d).withCentre ({ (float) x + (float) w * .5f, (float) y + (float) h * .5f });
    const float r = d * .5f, ang = a0 + pos * (a1 - a0), mid = (a0 + a1) * .5f;

    // кольцо из рисок
    for (int i = 0; i <= 32; ++i)
    {
        const float a = a0 + (a1 - a0) * (float) i / 32.0f;
        const bool lit = bip ? ((a >= juce::jmin (mid, ang) - 1e-3f) && (a <= juce::jmax (mid, ang) + 1e-3f)) : a <= ang + 1e-3f;
        const float r0 = r - (i % 4 == 0 ? 5.0f : 3.0f);
        g.setColour (lit ? col.withAlpha (.85f) : Pal::line().brighter (.05f));
        g.drawLine (c.getCentreX() + std::sin (a) * r0, c.getCentreY() - std::cos (a) * r0,
                    c.getCentreX() + std::sin (a) * r, c.getCentreY() - std::cos (a) * r, lit ? 1.3f : .8f);
    }
    // тело
    const auto body = c.reduced (r * .28f);
    juce::ColourGradient gr (Pal::panel().brighter (.12f), body.getCentreX(), body.getY(), Pal::bg(), body.getCentreX(), body.getBottom(), false);
    g.setGradientFill (gr);
    g.fillEllipse (body);
    g.setColour (Pal::line().withAlpha (.7f));
    g.drawEllipse (body, .6f);
    // указатель
    const float pr = body.getWidth() * .5f;
    g.setColour (col);
    g.drawLine (c.getCentreX() + std::sin (ang) * pr * .25f, c.getCentreY() - std::cos (ang) * pr * .25f,
                c.getCentreX() + std::sin (ang) * pr * .85f, c.getCentreY() - std::cos (ang) * pr * .85f, 1.6f);
    g.setColour (col.withAlpha (.2f));
    g.fillEllipse (juce::Rectangle<float> (5, 5).withCentre ({ c.getCentreX() + std::sin (ang) * (r + 5), c.getCentreY() - std::cos (ang) * (r + 5) }));
    // VARY: текущее значение гуляющего параметра
    const float eff = (float) (double) s.getProperties().getWithDefault ("eff", -1.0);
    if (eff >= 0 && std::fabs (eff - pos) > .003f)
    {
        const float ea = a0 + eff * (a1 - a0);
        g.setColour (Pal::layer (0, 3));
        g.fillEllipse (juce::Rectangle<float> (6, 6).withCentre ({ c.getCentreX() + std::sin (ea) * (r + 6), c.getCentreY() - std::cos (ea) * (r + 6) }));
    }
}

void Look::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox&)
{
    g.setColour (Pal::panel()); g.fillRoundedRectangle (0, 0, (float) w, (float) h, (float) h * .5f);
    g.setColour (Pal::line()); g.drawRoundedRectangle (.5f, .5f, (float) w - 1, (float) h - 1, (float) h * .5f, .7f);
    juce::Path p; p.addTriangle ((float) w - 22, (float) h * .42f, (float) w - 12, (float) h * .42f, (float) w - 17, (float) h * .62f);
    g.setColour (Pal::dim()); g.fillPath (p);
}

// ============================================================ Knob
Knob::Knob (MemoriaProcessor& p, const char* id, const juce::String& t, bool bipolar) : title (t), pid (id)
{
    par = p.apvts.getParameter (id);
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setRotaryParameters (kPi * 1.22f, kPi * 2.78f, true);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setRange (0.0, 1.0);
    slider.setMouseDragSensitivity (220);
    slider.setDoubleClickReturnValue (true, par->getDefaultValue());
    slider.getProperties().set ("bip", bipolar);
    slider.getProperties().set ("col", (int) 0xffffffff);
    slider.onValueChange = [this] { repaint(); };
    att = std::make_unique<juce::SliderParameterAttachment> (*par, slider, nullptr);
    addAndMakeVisible (slider);
    setTooltip (par->getName (100));
}
void Knob::resized() { slider.setBounds (getLocalBounds().withTrimmedTop (16).withTrimmedBottom (18)); }
void Knob::paint (juce::Graphics& g)
{
    g.setColour (Pal::dim());
    g.setFont (F (11.5f, true, .12f));
    g.drawText (title, getLocalBounds().removeFromTop (16), juce::Justification::centred);
    g.setColour (Pal::text());
    g.setFont (F (12.5f));
    g.drawText (par->getCurrentValueAsText(), getLocalBounds().removeFromBottom (18), juce::Justification::centred);
}

// ============================================================ Segments
Segments::Segments (MemoriaProcessor& p, const char* id, juce::StringArray l, bool mc) : labels (std::move (l)), modeCols (mc)
{
    auto* par = p.apvts.getParameter (id);
    att = std::make_unique<juce::ParameterAttachment> (*par, [this] (float v) { cur = juce::roundToInt (v); repaint(); }, nullptr);
    att->sendInitialUpdate();
    setTooltip (par->getName (100));
}
void Segments::mouseUp (const juce::MouseEvent& e)
{
    if (! getLocalBounds().contains (e.getPosition())) return;
    att->setValueAsCompleteGesture ((float) idxAt (e.x));
}
void Segments::paint (juce::Graphics& g)
{
    const int n = labels.size();
    const auto r = getLocalBounds().toFloat().reduced (.5f);
    g.setColour (Pal::panel()); g.fillRoundedRectangle (r, r.getHeight() * .5f);
    g.setColour (Pal::line()); g.drawRoundedRectangle (r, r.getHeight() * .5f, .7f);
    const float sw = r.getWidth() / (float) n;
    for (int i = 0; i < n; ++i)
    {
        auto cell = juce::Rectangle<float> (r.getX() + sw * (float) i, r.getY(), sw, r.getHeight()).reduced (3);
        const auto c = modeCols ? Pal::mode (i) : accent;
        if (i == cur)
        {
            g.setColour (c.withAlpha (.16f)); g.fillRoundedRectangle (cell, cell.getHeight() * .5f);
            g.setColour (c.withAlpha (.9f)); g.drawRoundedRectangle (cell, cell.getHeight() * .5f, .8f);
        }
        else if (i == hover)
        {
            g.setColour (Pal::line()); g.fillRoundedRectangle (cell, cell.getHeight() * .5f);
        }
        g.setColour (i == cur ? c : Pal::dim().brighter (i == hover ? .4f : 0));
        g.setFont (F (12.5f, true, .14f));
        g.drawText (labels[i], cell, juce::Justification::centred);
    }
}

// ============================================================ Chip
Chip::Chip (MemoriaProcessor& p, const char* id, const juce::String& l, Kind k, juce::Colour c) : label (l), kind (k), col (c)
{
    auto* par = p.apvts.getParameter (id);
    if (auto* ch = dynamic_cast<juce::AudioParameterChoice*> (par)) { count = ch->choices.size(); choiceNames = ch->choices; }
    att = std::make_unique<juce::ParameterAttachment> (*par, [this] (float v) { cur = juce::roundToInt (v); repaint(); }, nullptr);
    att->sendInitialUpdate();
    setTooltip (par->getName (100));
}
Chip::Chip (const juce::String& l, std::function<void()> a, juce::Colour c) : label (l), kind (Action), col (c), act (std::move (a)) {}

void Chip::mouseUp (const juce::MouseEvent& e)
{
    if (! getLocalBounds().contains (e.getPosition())) return;
    if (kind == Action) { if (act) act(); flash = 1; startTimerHz (40); repaint(); return; }
    if (kind == Toggle) att->setValueAsCompleteGesture (cur ? 0.0f : 1.0f);
    else att->setValueAsCompleteGesture ((float) ((cur + 1) % count));
}

void Chip::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (.5f);
    const bool on = kind == Action ? flash > 0 : cur > 0 || (kind == Cycle && label == "INPUT");
    const float a = kind == Action ? flash : 1.0f;
    g.setColour (on ? col.withAlpha (.14f * a + (hover ? .05f : 0)) : Pal::panel().brighter (hover ? .08f : 0));
    g.fillRoundedRectangle (r, 7);
    g.setColour (on ? col.withAlpha (.25f + .45f * a) : Pal::line().withAlpha (.8f));
    g.drawRoundedRectangle (r, 7, .7f);

    auto inner = r.reduced (10, 0);
    g.setFont (F (11, true, .08f));
    g.setColour (on ? col : Pal::dim().brighter (hover ? .4f : 0));
    if (kind == Cycle && count == 4)
    {
        g.drawText (label, inner.removeFromLeft (inner.getWidth() - 26), juce::Justification::centredLeft);
        for (int i = 0; i < 3; ++i)
        {
            const auto dot = juce::Rectangle<float> (5, 5).withCentre ({ inner.getX() + 4 + (float) i * 9, r.getCentreY() });
            g.setColour (i < cur ? col : Pal::line().brighter (.3f));
            g.fillEllipse (dot);
        }
    }
    else if (kind == Cycle)
    {
        g.drawText (label, inner, juce::Justification::centredLeft);
        g.setColour (col);
        g.drawText (choiceNames[cur], inner, juce::Justification::centredRight);
    }
    else
        g.drawText (label, r, juce::Justification::centred);
}

// ============================================================ MemoryRing
MemoryRing::MemoryRing (MemoriaProcessor& p) : proc (p)
{
    corePar = proc.apvts.getParameter ("core");
    coreAtt = std::make_unique<juce::ParameterAttachment> (*corePar, [] (float) {}, nullptr);
    startTimerHz (30);
}

void MemoryRing::resized()
{
    juce::Random rnd (31);
    stars.clear();
    for (int i = 0; i < 140; ++i)
        stars.push_back ({ rnd.nextFloat() * (float) getWidth(), rnd.nextFloat() * (float) getHeight(),
                           .6f + rnd.nextFloat() * 1.3f, .08f + rnd.nextFloat() * .35f, rnd.nextFloat() * kTau });
}

void MemoryRing::timerCallback()
{
    auto& e = proc.getEngine();
    auto& v = e.viz;
    const int n = v.n.load();
    const float* pool = e.poolData();
    const int poolLen = e.getPoolLen();
    t += 1.0f / 30.0f;
    rot += .0007f;
    lvl += (v.outLvl.load() - lvl) * .25f;
    inLvl += (v.inLvl.load() - inLvl) * .25f;
    if (n <= 0 || pool == nullptr || poolLen <= 0) { nBins = 0; repaint(); return; }

    long total = 0;
    for (int l = 0; l < n; ++l) total += v.len[(size_t) l].load();
    if (total <= 0) { nBins = 0; repaint(); return; }

    int b = 0;
    const int dir = v.dir.load();
    for (int l = 0; l < n && b < kBins; ++l)
    {
        const int off = v.off[(size_t) l].load(), len = v.len[(size_t) l].load(), w = v.w[(size_t) l].load();
        const int nb = juce::jmax (6, (int) std::lround ((double) kBins * len / (double) total) - (n > 1 ? 1 : 0));
        for (int j = 0; j < nb && b < kBins; ++j, ++b)
        {
            const int s0 = (int) ((long long) len * j / nb), s1 = (int) ((long long) len * (j + 1) / nb);
            const int stride = juce::jmax (1, (s1 - s0) / 28);
            float mx = 0;
            for (int s = s0; s < s1; s += stride)
            {
                const int idx = off + s;
                if (idx >= 0 && idx < poolLen) mx = juce::jmax (mx, std::fabs (pool[idx]));
            }
            env[(size_t) b] += (mx - env[(size_t) b]) * (mx > env[(size_t) b] ? .6f : .2f);
            const int mid = (s0 + s1) / 2;
            int d = (w - mid) * dir;
            d %= len; if (d < 0) d += len;
            age[(size_t) b] = (float) d / (float) len;
            lineOf[(size_t) b] = l;
        }
    }
    nBins = b;
    repaint();
}

void MemoryRing::paint (juce::Graphics& g)
{
    auto& v = proc.getEngine().viz;
    const int mode = v.mode.load();
    const float coreV = v.core.load();
    // CORE: насыщенность и яркость цвета кольца
    const auto accBase = Pal::mode (mode);
    const auto acc = accBase.withSaturation (accBase.getSaturation() * (.3f + .7f * coreV)).withBrightness (juce::jmin (1.0f, accBase.getBrightness() * (.72f + .4f * coreV)));
    const float W = (float) getWidth(), H = (float) getHeight();
    const float cx = W * .45f, cy = H * .5f;
    const float R = juce::jmin (H * .3f, 88.0f);
    const float xf = .25f + .75f * v.xf.load();
    const bool frozen = proc.apvts.getRawParameterValue ("freeze")->load() > .5f;
    const float drift = proc.apvts.getRawParameterValue ("drift")->load();
    const float la = v.layers.load(), bl = v.blur.load();

    // звёздная пыль
    for (auto& s : stars)
    {
        const float tw = .55f + .45f * std::sin (t * .7f + s.tw);
        g.setColour (juce::Colours::white.withAlpha (s.a * tw * .6f));
        g.fillEllipse (s.x, s.y, s.s, s.s);
    }
    // ореол
    {
        juce::ColourGradient halo (acc.withAlpha (.07f), cx, cy, acc.withAlpha (0.0f), cx + R * 1.9f, cy, true);
        g.setGradientFill (halo);
        g.fillEllipse (juce::Rectangle<float> (R * 3.8f, R * 3.8f).withCentre ({ cx, cy }));
    }

    const int n = v.n.load();
    long total = 0;
    for (int l = 0; l < n; ++l) total += v.len[(size_t) l].load();

    if (n > 0 && total > 0 && nBins > 0)
    {
        const float gap = n > 1 ? .07f : 0.0f;
        const float avail = kTau - gap * (float) n;
        float a0s[mem::kMaxLines] {}, spans[mem::kMaxLines] {};
        float a = -kPi * .5f + rot + gap * .5f;
        for (int l = 0; l < n; ++l)
        {
            a0s[l] = a;
            spans[l] = avail * (float) v.len[(size_t) l].load() / (float) total;
            a += spans[l] + gap;
        }
        auto P = [&] (float ang, float rad) { return juce::Point<float> (cx + std::cos (ang) * rad, cy + std::sin (ang) * rad); };

        // базовые дуги
        for (int l = 0; l < n; ++l)
        {
            juce::Path arc;
            arc.addCentredArc (cx, cy, R, R, 0, a0s[l] + kPi * .5f, a0s[l] + spans[l] + kPi * .5f, true);
            g.setColour (Pal::line().brighter (.35f));
            g.strokePath (arc, juce::PathStrokeType (1.2f));
        }

        // дымка размытия
        if (bl > .01f)
            for (int i = 0; i < 5; ++i)
            {
                const float rr = R + 6 + (float) i * 8;
                g.setColour (Pal::blur().withAlpha (.035f * bl * (float) (5 - i)));
                g.drawEllipse (juce::Rectangle<float> (rr * 2, rr * 2).withCentre ({ cx, cy }), 10.0f);
            }

        // материал: штрихи, тускнеющие с возрастом
        int b = 0;
        for (int l = 0; l < n; ++l)
        {
            int nb = 0;
            for (int k = b; k < nBins && lineOf[(size_t) k] == l; ++k) ++nb;
            for (int j = 0; j < nb; ++j, ++b)
            {
                const float ang = a0s[l] + ((float) j + .5f) / (float) nb * spans[l];
                const float amp = std::pow (juce::jmin (1.0f, env[(size_t) b] * 1.7f), .7f);
                const float fresh = std::pow (1.0f - age[(size_t) b], 1.6f);
                const float alpha = (.12f + .88f * fresh) * xf;
                const int layerIdx = mode == 1 ? l / 2 : (mode == 0 ? -1 : l);
                auto base = layerIdx >= 0 ? acc.interpolatedWith (Pal::layer (mode, layerIdx), la) : acc;
                const auto c = (frozen ? Pal::ice() : base).interpolatedWith (juce::Colours::white, fresh * .25f);
                g.setColour (c.withAlpha (alpha * (1 - .35f * bl)));
                g.drawLine (juce::Line<float> (P (ang, R + 3), P (ang, R + 4 + amp * 34)), 1.7f + bl * 3.0f);
                g.setColour (c.withAlpha (alpha * .3f));
                g.drawLine (juce::Line<float> (P (ang, R - 3), P (ang, R - 3 - amp * 12)), 1.4f);
            }
        }

        // головки записи
        for (int l = 0; l < n; ++l)
        {
            const float ang = a0s[l] + spans[l] * (float) v.w[(size_t) l].load() / (float) juce::jmax (1, v.len[(size_t) l].load());
            const auto hc = frozen ? Pal::ice() : juce::Colours::white;
            for (auto [wd, al] : { std::pair<float, float> { 7.0f, .06f }, { 3.5f, .16f }, { 1.3f, .95f } })
            {
                g.setColour (hc.withAlpha (al * xf));
                g.drawLine (juce::Line<float> (P (ang, R - 10), P (ang, R + 40)), wd);
            }
            g.setColour (hc.withAlpha (.9f * xf));
            g.fillEllipse (juce::Rectangle<float> (5, 5).withCentre (P (ang, R + 43)));
        }

        // гранулы (режим Grains)
        if (mode == 0)
        {
            const int off = v.off[0].load(), len = juce::jmax (1, v.len[0].load());
            for (int gi = 0; gi < mem::kGrainViz; ++gi)
            {
                const float pos = v.gPos[(size_t) gi].load(), am = v.gAmp[(size_t) gi].load();
                if (pos < 0 || am <= .01f) continue;
                const float ang = a0s[0] + spans[0] * (pos - (float) off) / (float) len;
                const float rr = R - 16 - (float) (gi / 4) * 8;
                g.setColour (acc.withAlpha (am * .25f));
                g.drawLine (juce::Line<float> (P (ang, rr), P (ang, R - 4)), 1.0f);
                const float sz = 2.5f + am * 6;
                g.setColour (acc.interpolatedWith (Pal::layer (0, gi / 4), la).brighter (.4f).withAlpha (.25f + .75f * am));
                g.fillEllipse (juce::Rectangle<float> (sz, sz).withCentre (P (ang, rr)));
            }
        }

        // спутники дрейфа
        for (int j = 0; j < 4; ++j)
        {
            const float lv = v.lfo[(size_t) j].load();
            const float rr = R + 47 + (float) j * 4;
            const float ang = -kPi * .5f + (float) j * kPi * .5f + (lv - .5f) * kPi * .9f;
            juce::Path trail;
            trail.addCentredArc (cx, cy, rr, rr, 0, ang + kPi * .5f - .35f, ang + kPi * .5f, true);
            g.setColour (acc.withAlpha (.12f + .25f * drift));
            g.strokePath (trail, juce::PathStrokeType (1.0f));
            g.setColour (acc.withAlpha (.35f + .6f * drift));
            g.fillEllipse (juce::Rectangle<float> (4.5f, 4.5f).withCentre (P (ang, rr)));
        }
    }

    // руины: пыль и шум вокруг кольца, вспышка эрозии
    const float ru = v.ruin.load();
    if (ru > .01f)
    {
        juce::Random rr ((juce::int64) (t * 30));
        const int nDots = (int) (ru * 140);
        for (int i = 0; i < nDots; ++i)
        {
            const float ang = rr.nextFloat() * kTau, rad = R * (.55f + rr.nextFloat() * 1.2f);
            g.setColour (juce::Colours::white.withAlpha (.08f + .25f * ru * rr.nextFloat()));
            g.fillRect (cx + std::cos (ang) * rad, cy + std::sin (ang) * rad, 1.4f, 1.4f);
        }
        const int ec = v.erodeCount.load();
        if (ec != lastErodeSeen) { lastErodeSeen = ec; erodeFlash = 1; erodePos = v.lastErode.load(); }
        if (erodeFlash > .01f && erodePos >= 0 && total > 0)
        {
            // найти линию и угол места эрозии
            const float gap = n > 1 ? .07f : 0.0f, avail = kTau - gap * (float) n;
            float a = -kPi * .5f + rot + gap * .5f;
            for (int l = 0; l < n; ++l)
            {
                const int off = v.off[(size_t) l].load(), len = v.len[(size_t) l].load();
                const float span = avail * (float) len / (float) total;
                if (erodePos >= (float) off && erodePos < (float) (off + len))
                {
                    const float ang = a + span * (erodePos - (float) off) / (float) len;
                    juce::Path arc;
                    arc.addCentredArc (cx, cy, R + 20, R + 20, 0, ang + kPi * .5f - .05f, ang + kPi * .5f + .05f, true);
                    g.setColour (Pal::warn().withAlpha (.8f * erodeFlash));
                    g.strokePath (arc, juce::PathStrokeType (10.0f));
                    break;
                }
                a += span + gap;
            }
            erodeFlash *= .9f;
        }
    }

    // ядро
    const float core = 40 + juce::jmin (1.0f, lvl) * 10;
    for (int i = 3; i >= 1; --i)
    {
        g.setColour (acc.withAlpha (.05f * (float) i));
        g.fillEllipse (juce::Rectangle<float> (core * (1 + .35f * (float) i) * 2, core * (1 + .35f * (float) i) * 2).withCentre ({ cx, cy }));
    }
    g.setColour (Pal::bg().withAlpha (.85f));
    g.fillEllipse (juce::Rectangle<float> (core * 2, core * 2).withCentre ({ cx, cy }));
    g.setColour (acc.withAlpha (.7f));
    g.drawEllipse (juce::Rectangle<float> (core * 2, core * 2).withCentre ({ cx, cy }), .9f);

    // мембрана организма: дышит вместе со звуком
    if (v.organism.load() > 0)
    {
        const float br = v.breath.load();
        const float rr = core * (1.55f + .25f * (1 - br));
        g.setColour (acc.withAlpha (.12f + .18f * (1 - br)));
        g.drawEllipse (juce::Rectangle<float> (rr * 2, rr * 2).withCentre ({ cx, cy }), 1.0f + 2.0f * (1 - br));
    }
    // AUTOPAN: точка внизу ядра показывает, куда сейчас перетекает звук (DRIFT выше ~0.25)
    {
        const float pan = v.pan.load();
        if (std::fabs (pan) > .01f)
        {
            const float px = cx + pan / .7f * core * .55f, py = cy + core * .62f;
            g.setColour (acc.withAlpha (.18f)); g.drawLine (cx - core * .55f, py, cx + core * .55f, py, .6f);
            g.setColour (acc.withAlpha (.85f)); g.fillEllipse (juce::Rectangle<float> (4.5f, 4.5f).withCentre ({ px, py }));
        }
    }
    // CORE: заливка ядра по силе
    g.setColour (acc.withAlpha (.08f + .3f * coreV));
    g.fillEllipse (juce::Rectangle<float> (core * 2 * (.4f + .6f * coreV), core * 2 * (.4f + .6f * coreV)).withCentre ({ cx, cy }));
    g.setColour (Pal::text());
    g.setFont (F (13, true, .16f));
    if (coreHover || coreDrag)
    {
        static const char* what[4] = { "RANDOM", "DENSITY", "SCATTER", "WOW" };
        g.drawText ("CORE " + juce::String (juce::roundToInt (coreV * 100)) + "%", juce::Rectangle<float> (140, 18).withCentre ({ cx, cy - 6 }), juce::Justification::centred);
        g.setColour (Pal::dim()); g.setFont (F (10, false, .08f));
        g.drawText (what[juce::jlimit (0, 3, mode)], juce::Rectangle<float> (140, 14).withCentre ({ cx, cy + 11 }), juce::Justification::centred);
    }
    else
    g.drawText (mem::modeName (mode), juce::Rectangle<float> (140, 18).withCentre ({ cx, cy - 6 }), juce::Justification::centred);
    static const char* sizes[3] = { "LONG", "MID", "SHORT" };
    if (! (coreHover || coreDrag)) {
    g.setColour (Pal::dim());
    g.setFont (F (10, false, .08f));
    g.drawText (juce::String (sizes[juce::jlimit (0, 2, v.size.load())]) + "  " + juce::String ((double) total / proc.getSampleRate(), 1) + " s",
                juce::Rectangle<float> (140, 14).withCentre ({ cx, cy + 11 }), juce::Justification::centred);
    }

    // статус
    auto badge = [&] (const juce::String& s, juce::Colour c, float y)
    {
        g.setFont (F (11, true, .2f));
        const auto r = juce::Rectangle<float> (96, 22).withCentre ({ 110.0f, y });
        g.setColour (c.withAlpha (.13f)); g.fillRoundedRectangle (r, 10);
        g.setColour (c); g.drawRoundedRectangle (r, 10, 1);
        g.drawText (s, r, juce::Justification::centred);
    };
    float by = 24;
    if (frozen) { badge ("FROZEN", Pal::ice(), by); by += 28; }
    if (v.dir.load() < 0) { badge (U ("◂ REVERSE"), acc, by); by += 28; }
    if (bl > .5f) { badge ("BLUR", Pal::blur(), by); by += 28; }
    // событие хаоса — вспышка над кольцом
    {
        const int ct = v.chaosType.load();
        const float ce = v.chaosEnv.load();
        if (ct > 0 && ce > .02f)
        {
            const auto cc = Pal::warn().interpolatedWith (juce::Colours::white, .2f);
            const int cb = v.chaosBand.load();
            const juce::String bn = mem::bandMaskName (cb);
            const auto r = juce::Rectangle<float> (bn.isEmpty() ? 130.0f : 190.0f, 24).withCentre ({ cx, 18 });
            g.setColour (cc.withAlpha (.15f * ce)); g.fillRoundedRectangle (r, 12);
            g.setColour (cc.withAlpha (ce)); g.drawRoundedRectangle (r, 12, 1.2f);
            g.setFont (F (11.5f, true, .2f));
            g.drawText (juce::String (mem::chaosName (ct)) + (bn.isEmpty() ? juce::String() : U (" · ") + bn), r, juce::Justification::centred);
        }
    }

    // уровни входа и выхода
    auto meter = [&] (float x, float level, const juce::String& name)
    {
        const float h = H * .5f, y0 = cy - h * .5f;
        g.setColour (Pal::line());
        g.fillRoundedRectangle (x - 1.5f, y0, 3, h, 1.5f);
        const float lv = juce::jlimit (0.0f, 1.0f, std::sqrt (level));
        g.setColour (acc.withAlpha (.85f));
        g.fillRoundedRectangle (x - 1.5f, y0 + h * (1 - lv), 3, h * lv, 1.5f);
        g.setColour (Pal::dim());
        g.setFont (F (10, true, .2f));
        g.drawText (name, juce::Rectangle<float> (40, 14).withCentre ({ x, y0 + h + 12 }), juce::Justification::centred);
    };
    meter (30, inLvl, "IN");
    meter (W - 30, lvl, "OUT");
}

// ============================================================ Editor
MemoriaEditor::MemoriaEditor (MemoriaProcessor& p)
    : AudioProcessorEditor (p), proc (p), ring (p), layerPanel (p), tailRange (p),
      modeSeg (p, "mode", { "GRAINS", "DIFFUSE", "NETWORK", "TAPES" }, true),
      sizeSeg (p, "size", { "LONG", "MID", "SHORT" }, false),
      harmStep (p, "harmony", "HARMONY"), orgStep (p, "organism", "ORGANISM")
{
    setLookAndFeel (&look);
    addAndMakeVisible (ring);
    addAndMakeVisible (modeSeg);
    addAndMakeVisible (sizeSeg);
    addAndMakeVisible (harmStep);
    addAndMakeVisible (orgStep);
    harmStep.setTooltip (U ("Гармония сдвигов слоёв, гранул и шиммера. OCT — только октавы, всегда в тональности. 5TH — квинта, SUS — секунда и кварта, 7TH — септима, 9TH — нона: окраска интервалом, отдельные ноты могут выйти из лада. FREE — всё сразу."));
    orgStep.setTooltip (U ("Организм — живая кожа звука: Mid/Side, тёплые аналоговые фильтры, дыхание (играешь — открыто, хвост — темнее и шире), частоты уступают новым нотам, хвосты нарастают. TAPE — тёплый низ, CONSOLE — плотная середина, GLASS — воздух и ширина, ASH — тёмный и густой."));
    modeSeg.setTooltip (U ("Режим: гранулы над петлёй · кольцо дрейфующих allpass · сеть из 4 линий · две ленты неравной длины"));
    sizeSeg.setTooltip (U ("Размер памяти: длинные, средние или короткие петли"));

    struct K { const char* id; const char* title; bool bip; };
    for (auto k : { K { "decay", "DECAY", false }, K { "warmth", "WARMTH", false }, K { "dynamics", "DYNAMICS", true },
                    K { "spread", "SPREAD", false }, K { "drift", "DRIFT", false }, K { "layers", "LAYERS", false },
                    K { "pingpong", "PING-PONG", false }, K { "space", "SPACE", false }, K { "mix", "MIX", false }, K { "output", "OUTPUT", false },
                    K { "tone", "TONE", true }, K { "depth", "DEPTH", false }, K { "chaos", "CHAOS", false }, K { "ruin", "RUIN", false } })
        addAndMakeVisible (knobs.add (new Knob (p, k.id, k.title, k.bip)));
    knobs[2]->setTooltip (U ("Влево — память приглушается, пока играешь, и наплывает, когда перестаёшь. Вправо — тихие хвосты распускаются в ровный дрон."));
    knobs[3]->setTooltip (U ("Grains — разброс позиций гранул. Network и Tapes — сколько звука перетекает между линиями."));
    knobs[4]->setTooltip (U ("Движение: медленные непериодичные LFO двигают гранулы, панораму линий и переливы спектра (PRISM)."));
    knobs[5]->setTooltip (U ("Слои: каждый слой получает свою полосу частот, сдвиг высоты, текстуру и место в стерео. В петле слои ещё и темнеют или светлеют по-своему."));
    knobs[6]->setTooltip (U ("Пинг-понг: у каждого слоя своя задержка, эхо прыгает между левым и правым каналом."));
    knobs[7]->setTooltip (U ("Зал: плотный реверб с хвостом до 20 секунд, низ звучит дольше верха. Частоты хвостов — полоса TAIL справа."));
    knobs[10]->setTooltip (U ("Тон мокрого сигнала: влево темнее и мягче, вправо ярче."));
    knobs[11]->setTooltip (U ("Глубина: саб-слой на октаву ниже и подъём низа."));
    knobs[12]->setTooltip (U ("Хаос: иногда вмешиваются случайные сбои — звук плывёт, заикается, играет назад, останавливается как плёнка, проваливается, слои перетасовываются."));
    knobs[13]->setTooltip (U ("Руины: шипение и треск записываются в память и стареют вместе с ней, куски памяти выветриваются, выше — петля теряет разрядность."));

    const auto modeC = Pal::mode (0);
    addAndMakeVisible (chips.add (new Chip (p, "lowcut", "LO-CUT", Chip::Cycle, modeC)));
    addAndMakeVisible (chips.add (new Chip (p, "highcut", "HI-CUT", Chip::Cycle, modeC)));
    addAndMakeVisible (chips.add (new Chip (p, "input", "INPUT", Chip::Cycle, modeC)));
    addAndMakeVisible (chips.add (new Chip (p, "freeze", "FREEZE", Chip::Toggle, Pal::ice())));
    addAndMakeVisible (chips.add (new Chip (p, "reverse", "REVERSE", Chip::Toggle, modeC)));
    addAndMakeVisible (chips.add (new Chip (p, "blur", "BLUR", Chip::Toggle, Pal::blur())));
    addAndMakeVisible (chips.add (new Chip (p, "shimmer", "SHIMMER", Chip::Toggle, Pal::ice())));
    addAndMakeVisible (chips.add (new Chip (p, "vary", "VARY", Chip::Toggle, Pal::layer (0, 3))));
    addAndMakeVisible (chips.add (new Chip ("ERASE", [this] { proc.erase(); }, Pal::warn())));
    chips[0]->setTooltip (U ("Срез низа в петле: каждый круг память становится тоньше (3 степени)"));
    chips[1]->setTooltip (U ("Срез верха с тёплой сатурацией в петле: каждый круг память темнеет (3 степени)"));
    chips[3]->setTooltip (U ("Заморозка: вход не пишется, память кружит без потерь"));
    chips[5]->setTooltip (U ("Размыть всё в облако. Размытое понемногу дописывается в память, и дрон со временем становится мягче."));
    chips[6]->setTooltip (U ("Шиммер: хвост зала поднимается на октаву (в 5TH — на квинту) и каскадом уходит вверх. Работает вместе с SPACE."));
    addAndMakeVisible (tailRange);
    chips[7]->setTooltip (U ("Вариативность: раз в 8–16 секунд 2–3 параметра (SPREAD, DRIFT, LAYERS, PING-PONG, SPACE, TONE, WARMTH, DYNAMICS, DEPTH) плавно и понемногу уходят в сторону. Точка на ручке — её текущее значение."));
    chips[8]->setTooltip (U ("Стереть всю память"));
    addAndMakeVisible (layerPanel);
    layerPanel.setTooltip (U ("Характеры слоёв. Клик — перетасовать."));

    for (int i = 1; i <= (int) memoria::presets().size(); ++i) presetBox.addItem (proc.getProgramName (i), i);
    presetBox.setTextWhenNothingSelected (U ("— свой —"));
    presetBox.onChange = [this] { if (presetBox.getSelectedId() > 0) proc.setCurrentProgram (presetBox.getSelectedId()); };
    addAndMakeVisible (presetBox);

    setSize (960, 600);
    startTimerHz (20);
    timerCallback();
}

MemoriaEditor::~MemoriaEditor() { stopTimer(); setLookAndFeel (nullptr); }

void MemoriaEditor::timerCallback()
{
    const int m = (int) proc.apvts.getRawParameterValue ("mode")->load();
    if (m != lastMode)
    {
        lastMode = m;
        const auto c = Pal::mode (m);
        for (auto* k : knobs) k->setAccent (c);
        sizeSeg.setAccent (c);
        harmStep.setAccent (c); orgStep.setAccent (c);
        for (int i : { 0, 1, 2, 4 }) chips[i]->setAccent (c);
        repaint();
    }
    // VARY: точки на ручках, которые сейчас гуляют
    for (int i = 0; i < MemoriaProcessor::kVary; ++i)
        for (auto* k : knobs)
            if (k->paramId() == MemoriaProcessor::varyId (i))
                k->setEffective (proc.varyActive (i) ? proc.varyEffective (i) : -1.0f);
    const int pr = proc.getCurrentProgram();
    if (presetBox.getSelectedId() != pr) presetBox.setSelectedId (pr, juce::dontSendNotification);
}

void MemoriaEditor::paint (juce::Graphics& g)
{
    g.fillAll (Pal::bg());
    const auto acc = Pal::mode (lastMode < 0 ? 0 : lastMode);
    g.setColour (Pal::text());
    g.setFont (F (22, true, .45f));
    g.drawText ("MEMORIA", 28, 12, 220, 28, juce::Justification::centredLeft);
    g.setColour (Pal::dim());
    g.setFont (F (11, false, .2f));
    g.drawText ("dream engine", 30, 38, 240, 14, juce::Justification::centredLeft);
    g.setColour (acc);
    g.fillEllipse (14, 22, 7, 7);

    g.setColour (Pal::line());
    g.drawHorizontalLine (408, 28, (float) getWidth() - 28);
}

void MemoriaEditor::resized()
{
    const int W = getWidth();
    presetBox.setBounds (W - 228, 16, 200, 28);
    const int segW = 420, sizeW = 186, gap = 16, sx = (W - segW - gap - sizeW) / 2;
    modeSeg.setBounds (sx, 62, segW, 32);
    sizeSeg.setBounds (sx + segW + gap, 62, sizeW, 32);
    ring.setBounds (0, 100, W, 300);

    layerPanel.setBounds (W - 268, 156, 214, 4 * 48);
    harmStep.setBounds (W - 268, 116, 104, 30);
    orgStep.setBounds (W - 158, 116, 104, 30);
    const int nb = 10, kw = 92, kx = (W - kw * nb) / 2;
    for (int i = 0; i < nb; ++i) knobs[i]->setBounds (kx + i * kw, 416, kw, 100);
    // слева от кольца: TONE · DEPTH / CHAOS · RUIN
    knobs[10]->setBounds (52, 196, 92, 96);
    knobs[11]->setBounds (148, 196, 92, 96);
    knobs[12]->setBounds (52, 300, 92, 96);
    knobs[13]->setBounds (148, 300, 92, 96);
    tailRange.setBounds (W - 268, 354, 214, 44);

    const int cw = 98, cg = 6, cx = (W - cw * chips.size() - cg * (chips.size() - 1)) / 2;
    for (int i = 0; i < chips.size(); ++i) chips[i]->setBounds (cx + i * (cw + cg), 540, cw, 32);
}

// ============================================================ LayerPanel
void LayerPanel::paint (juce::Graphics& g)
{
    auto& v = proc.getEngine().viz;
    const int mode = v.mode.load();
    const int n = juce::jmax (1, v.nLayers.load());
    const float la = v.layers.load();
    const float act = .35f + .65f * la;
    for (int j = 0; j < 4; ++j)
    {
        const auto r = juce::Rectangle<float> (0, (float) j * 48, (float) getWidth(), 42);
        const bool used = j < n;
        const auto col = Pal::layer (mode, j);
        g.setColour (Pal::panel().withAlpha (.85f));
        g.fillRoundedRectangle (r, 8);
        g.setColour ((used ? col : Pal::line()).withAlpha (used ? .45f * act : 1.0f));
        g.drawRoundedRectangle (r.reduced (.5f), 8, .7f);
        if (! used) { g.setColour (Pal::dim().withAlpha (.5f)); g.setFont (F (10.5f, false, .1f)); g.drawText (U ("— нет в этом режиме —"), r, juce::Justification::centred); continue; }

        const int semis = v.lSemis[(size_t) j].load();
        const int band = v.lBand[(size_t) j].load(), tex = v.lTex[(size_t) j].load();
        const float lvl = juce::jmin (1.0f, std::sqrt (v.lLvl[(size_t) j].load() * 6));
        const int dist = v.lDist[(size_t) j].load();
        // точка слоя: ближний — сплошная, дальний — полое кольцо (чем дальше, тем тоньше и прозрачнее)
        {
            const auto dot = juce::Rectangle<float> (8, 8).withCentre ({ r.getX() + 14, r.getY() + 14 });
            if (dist == 0) { g.setColour (col.withAlpha (act)); g.fillEllipse (dot); }
            else { g.setColour (col.withAlpha (act * (1.0f - .18f * (float) dist))); g.fillEllipse (dot.reduced (1.5f * (float) dist)); g.drawEllipse (dot, .8f); }
        }
        g.setColour (Pal::text().withAlpha (.4f + .6f * act));
        g.setFont (F (11, true, .14f));
        g.drawText ("LAYER " + juce::String (j + 1), r.withTrimmedLeft (26).withHeight (26), juce::Justification::centredLeft);
        g.setColour (Pal::dim().withAlpha (.8f)); g.setFont (F (9, true, .16f));
        g.drawText (mem::distName (dist), r.withTrimmedLeft (98).withHeight (26), juce::Justification::centredLeft);
        g.setColour (Pal::dim());
        g.setFont (F (10.5f, false, .1f));
        const float sw = v.lSwell[(size_t) j].load();
        g.drawText (juce::String (mem::bandName (band)) + U (" · ") + mem::texName (tex) + (sw > .05f ? U (" · SWELL ") + juce::String (sw, 1) + "s" : juce::String()),
                    r.withTrimmedLeft (26).withTrimmedRight (60).withTrimmedTop (20).withHeight (16), juce::Justification::centredLeft, true);
        g.setColour (col.withAlpha (act));
        g.setFont (F (17, true));
        const juce::String st = semis == 0 ? U ("±0") : (semis > 0 ? "+" + juce::String (semis) : U ("−") + juce::String (-semis));
        g.drawText (st, r.withTrimmedRight (12), juce::Justification::centredRight);
        // уровень слоя
        g.setColour (Pal::line());
        g.fillRoundedRectangle (r.getX() + 10, r.getBottom() - 5, r.getWidth() - 20, 2, 1);
        g.setColour (col.withAlpha (.8f * act));
        g.fillRoundedRectangle (r.getX() + 10, r.getBottom() - 5, (r.getWidth() - 20) * lvl, 2, 1);
    }
}

// ============================================================ TailRange
static float hzToX (float hz, float w) { return std::log (hz / 15.0f) / std::log (20000.0f / 15.0f) * w; }
static float xToHz (float x, float w) { return 15.0f * std::pow (20000.0f / 15.0f, juce::jlimit (0.0f, 1.0f, x / w)); }

TailRange::TailRange (MemoriaProcessor& p) : proc (p)
{
    lo = proc.apvts.getParameter ("tailLo");
    hi = proc.apvts.getParameter ("tailHi");
    attLo = std::make_unique<juce::ParameterAttachment> (*lo, [this] (float) { repaint(); }, nullptr);
    attHi = std::make_unique<juce::ParameterAttachment> (*hi, [this] (float) { repaint(); }, nullptr);
    setTooltip (U ("Частоты хвостов: левая точка срезает низ, правая — верх. Двойной клик — сброс. Светлые точки — полосы PRISM (их движение задаёт DRIFT)."));
    startTimerHz (20);
}

juce::Rectangle<float> TailRange::bar() const { return getLocalBounds().toFloat().withTrimmedTop (18).reduced (6, 6); }

void TailRange::mouseDown (const juce::MouseEvent& e)
{
    const auto b = bar();
    const float xl = b.getX() + hzToX (lo->convertFrom0to1 (lo->getValue()), b.getWidth());
    const float xh = b.getX() + hzToX (hi->convertFrom0to1 (hi->getValue()), b.getWidth());
    dragLo = std::fabs ((float) e.x - xl) < std::fabs ((float) e.x - xh);
    (dragLo ? attLo : attHi)->beginGesture();
    mouseDrag (e);
}
void TailRange::mouseDrag (const juce::MouseEvent& e)
{
    const auto b = bar();
    float hz = xToHz ((float) e.x - b.getX(), b.getWidth());
    if (dragLo) attLo->setValueAsPartOfGesture (juce::jlimit (15.0f, 4000.0f, hz));
    else attHi->setValueAsPartOfGesture (juce::jlimit (300.0f, 20000.0f, hz));
}
void TailRange::mouseUp (const juce::MouseEvent&) { (dragLo ? attLo : attHi)->endGesture(); }
void TailRange::mouseDoubleClick (const juce::MouseEvent&)
{
    attLo->setValueAsCompleteGesture (40.0f);
    attHi->setValueAsCompleteGesture (16000.0f);
}

void TailRange::paint (juce::Graphics& g)
{
    auto& v = proc.getEngine().viz;
    const auto acc = Pal::mode (v.mode.load());
    const auto b = bar();
    const float flo = lo->convertFrom0to1 (lo->getValue()), fhi = hi->convertFrom0to1 (hi->getValue());
    const float xl = b.getX() + hzToX (flo, b.getWidth()), xh = b.getX() + hzToX (fhi, b.getWidth());
    auto fmt = [] (float f) { return f >= 1000 ? juce::String (f / 1000.0f, 1) + "k" : juce::String (juce::roundToInt (f)); };

    g.setColour (Pal::dim()); g.setFont (F (11, true, .14f));
    g.drawText ("TAIL", getLocalBounds().removeFromTop (16).withTrimmedLeft (6), juce::Justification::centredLeft);
    g.setColour (Pal::text().withAlpha (.8f)); g.setFont (F (11));
    g.drawText ((flo <= 15.5f ? juce::String ("15") : fmt (flo)) + U (" Гц – ") + (fhi >= 19500 ? juce::String ("20k") : fmt (fhi)),
                getLocalBounds().removeFromTop (16).withTrimmedRight (6), juce::Justification::centredRight);

    g.setColour (Pal::panel()); g.fillRoundedRectangle (b, 6);
    // полоса пропускания
    juce::ColourGradient gr (acc.withAlpha (.05f), xl, 0, acc.withAlpha (.05f), xh, 0, false);
    gr.addColour (.5, acc.withAlpha (.22f));
    g.setGradientFill (gr);
    g.fillRoundedRectangle (juce::Rectangle<float> (xl, b.getY(), juce::jmax (2.0f, xh - xl), b.getHeight()), 6);
    // деления октав
    g.setColour (Pal::line());
    for (float f : { 100.0f, 1000.0f, 10000.0f }) g.drawVerticalLine ((int) (b.getX() + hzToX (f, b.getWidth())), b.getY() + 3, b.getBottom() - 3);
    // полосы PRISM
    const float pr = v.prism.load();
    for (int j = 0; j < 5; ++j)
    {
        const float f = v.prismF[(size_t) j].load();
        if (f <= 0) continue;
        const float x = b.getX() + hzToX (f, b.getWidth());
        const bool inside = x >= xl && x <= xh;
        g.setColour (juce::Colours::white.withAlpha ((.15f + .7f * pr) * (inside ? 1.0f : .3f)));
        g.fillEllipse (juce::Rectangle<float> (5, 5).withCentre ({ x, b.getCentreY() }));
    }
    // точки-ручки
    for (float x : { xl, xh })
    {
        g.setColour (acc); g.fillRoundedRectangle (juce::Rectangle<float> (4, b.getHeight() + 4).withCentre ({ x, b.getCentreY() }), 2);
    }
    g.setColour (Pal::line()); g.drawRoundedRectangle (b, 6, .7f);
}

// ============================================================ Stepper
Stepper::Stepper (MemoriaProcessor& p, const char* id, const juce::String& cap) : caption (cap)
{
    auto* par = p.apvts.getParameter (id);
    if (auto* ch = dynamic_cast<juce::AudioParameterChoice*> (par)) names = ch->choices;
    att = std::make_unique<juce::ParameterAttachment> (*par, [this] (float v) { cur = juce::roundToInt (v); repaint(); }, nullptr);
    att->sendInitialUpdate();
}
void Stepper::step (int d) { if (names.isEmpty()) return; att->setValueAsCompleteGesture ((float) ((cur + d + names.size()) % names.size())); }
void Stepper::mouseUp (const juce::MouseEvent& e) { if (getLocalBounds().contains (e.getPosition())) step (e.x < getWidth() / 3 ? -1 : 1); }
void Stepper::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) { step (w.deltaY > 0 ? 1 : -1); }
void Stepper::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (.5f);
    g.setColour (Pal::panel().brighter (hover ? .06f : 0)); g.fillRoundedRectangle (r, r.getHeight() * .5f);
    g.setColour (accent.withAlpha (.4f)); g.drawRoundedRectangle (r, r.getHeight() * .5f, .7f);
    g.setColour (Pal::dim()); g.setFont (F (8.5f, true, .14f));
    g.drawText (caption, r.withTrimmedTop (3).withHeight (10), juce::Justification::centred);
    g.setColour (accent); g.setFont (F (12.5f, true, .14f));
    g.drawText (names[cur], r.withTrimmedTop (10), juce::Justification::centred);
    g.setColour (Pal::dim().brighter (hover ? .4f : 0)); g.setFont (F (11, true));
    g.drawText (U ("◂"), r.withWidth (18).translated (4, 4), juce::Justification::centred);
    g.drawText (U ("▸"), r.withTrimmedLeft (r.getWidth() - 22).translated (0, 4), juce::Justification::centred);
}

// ============================================================ MemoryRing: CORE
bool MemoryRing::overCore (juce::Point<float> p) const
{
    const float cx = (float) getWidth() * .45f, cy = (float) getHeight() * .5f;
    return p.getDistanceFrom ({ cx, cy }) < 62.0f;
}
void MemoryRing::mouseMove (const juce::MouseEvent& e) { const bool h = overCore (e.position); if (h != coreHover) { coreHover = h; setMouseCursor (h ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::NormalCursor); } }
void MemoryRing::mouseDown (const juce::MouseEvent& e)
{
    coreDrag = overCore (e.position);
    if (! coreDrag) return;
    dragStartVal = corePar->getValue(); dragStartY = e.position.y;
    coreAtt->beginGesture();
}
void MemoryRing::mouseDrag (const juce::MouseEvent& e)
{
    if (! coreDrag) return;
    const float v = juce::jlimit (0.0f, 1.0f, dragStartVal + (dragStartY - e.position.y) / 180.0f);
    coreAtt->setValueAsPartOfGesture (v);
}
void MemoryRing::mouseUp (const juce::MouseEvent&) { if (coreDrag) coreAtt->endGesture(); coreDrag = false; }
void MemoryRing::mouseDoubleClick (const juce::MouseEvent& e) { if (overCore (e.position)) coreAtt->setValueAsCompleteGesture (.5f); }
