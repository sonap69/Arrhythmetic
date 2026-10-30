#include "MainComponent.h"

namespace
{
    // Time signatures. The grid is always 16th notes; groups say how the steps
    // are counted (in 16ths), so odd meters are coloured the way they are felt.
    struct TimeSig
    {
        const char* name;
        int numerator, denominator;
        std::vector<int> groups;
        int steps() const { int n = 0; for (int g : groups) n += g; return n; }
    };

    const TimeSig timeSigs[] =
    {
        { "2/4", 2, 4, { 4, 4 } },
        { "3/4", 3, 4, { 4, 4, 4 } },
        { "4/4", 4, 4, { 4, 4, 4, 4 } },
        { "5/4", 5, 4, { 4, 4, 4, 4, 4 } },
        { "6/4", 6, 4, { 4, 4, 4, 4, 4, 4 } },
        { "7/4", 7, 4, { 4, 4, 4, 4, 4, 4, 4 } },
        { "5/8  (3+2)", 5, 8, { 6, 4 } },
        { "5/8  (2+3)", 5, 8, { 4, 6 } },
        { "6/8", 6, 8, { 6, 6 } },
        { "7/8  (2+2+3)", 7, 8, { 4, 4, 6 } },
        { "7/8  (3+2+2)", 7, 8, { 6, 4, 4 } },
        { "9/8", 9, 8, { 6, 6, 6 } },
        { "9/8  (2+2+2+3)", 9, 8, { 4, 4, 4, 6 } },
        { "11/8 (3+3+3+2)", 11, 8, { 6, 6, 6, 4 } },
        { "12/8", 12, 8, { 6, 6, 6, 6 } },
        { "13/8 (3+3+3+2+2)", 13, 8, { 6, 6, 6, 4, 4 } },
        { "15/8", 15, 8, { 6, 6, 6, 6, 6 } },
    };
    constexpr int fourFour = 2;

    struct Preset
    {
        const char* name;
        float tempo, swing;               // swing in percent (0..60)
        std::array<const char*, DrumEngine::numVoices> rows;   // 'x' = hit; spaces ignored
    };

    // Voice order: kick, snare, clap, rim, low tom, hi tom, closed hat, open hat, crash, ride
    const Preset presets[] =
    {
        { "Boom bap", 92, 22, { "x... ...x x... ....", ".... x... .... x...", "", ".... .... ..x. ....",
                                "", "", "x.x. x.x. x.x. x.xx", ".... ...x .... ....", "", "" } },
        { "Four on the floor", 124, 0, { "x... x... x... x...", "", ".... x... .... x...", "",
                                         "", "", "x.x. x.x. x.x. x.x.", "..x. ..x. ..x. ..x.", "", "" } },
        { "Electro", 118, 8, { "x... ..x. ..x. ....", ".... x... .... x...", ".... x... .... x..x", "",
                               ".... .... .... .x..", ".... .... .... ..x.", "xxxx xxxx xxxx xxxx", "", "", "" } },
        { "Rock", 110, 0, { "x... ..x. x.x. ....", ".... x... .... x...", "", "",
                            ".... .... .... ..xx", ".... .... .... xx..", ".... .... .... ....", "", "x... .... .... ....",
                            "..x. x.x. x.x. ...." } },
        { "Jazz ride", 140, 50, { "x... .... .... ....", ".... .... .... ..x.", "", "",
                                  "", "", ".... x... .... x...", "", "", "x... x.xx x... x.xx" } },
        { "Empty", 104, 0, { "", "", "", "", "", "", "", "", "", "" } },
    };

    juce::Font mono (float size, bool bold = false)
    {
        return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), size,
                                              bold ? juce::Font::bold : juce::Font::plain));
    }

    juce::Colour groupColour (int group)
    {
        switch (group % 4)
        {
            case 0:  return Colours::g1;
            case 1:  return Colours::g2;
            case 2:  return Colours::g3;
            default: return Colours::g4;
        }
    }

    constexpr int firstUserPatternId = 1001;

    juce::String barsText (int bars) { return juce::String (bars) + (bars == 1 ? " bar" : " bars"); }

    bool isAudioFile (const juce::String& path)
    {
        return juce::File (path).hasFileExtension ("wav;aif;aiff;flac;ogg;mp3");
    }
}

//==============================================================================
DrumLookAndFeel::DrumLookAndFeel()
{
    setColour (juce::BubbleComponent::backgroundColourId, Colours::bg);
    setColour (juce::BubbleComponent::outlineColourId, Colours::edge);
    setColour (juce::TooltipWindow::backgroundColourId, Colours::bg);
    setColour (juce::TooltipWindow::textColourId, Colours::ink);
    setColour (juce::TooltipWindow::outlineColourId, Colours::edge);
}

juce::Font DrumLookAndFeel::getSliderPopupFont (juce::Slider&)
{
    return mono (12.0f, true);
}

void DrumLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float pos,
                                        float startAngle, float endAngle, juce::Slider& s)
{
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat();
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f - 1.5f;
    if (radius < 4.0f) return;

    const auto c = bounds.getCentre();
    const float lineW = juce::jmax (2.0f, radius * 0.17f);
    const float arcR = radius - lineW * 0.5f;
    const float angle = startAngle + pos * (endAngle - startAngle);
    const bool bipolar = (bool) s.getProperties().getWithDefault ("bipolar", false);
    const float from = bipolar ? (startAngle + endAngle) * 0.5f : startAngle;
    const auto accent = s.findColour (juce::Slider::rotarySliderFillColourId);
    const juce::PathStrokeType stroke (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

    juce::Path track;
    track.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
    g.setColour (Colours::stepOff);
    g.strokePath (track, stroke);

    if (std::abs (angle - from) > 0.01f)
    {
        juce::Path value;
        value.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, juce::jmin (from, angle), juce::jmax (from, angle), true);
        g.setColour (s.isEnabled() ? accent : Colours::muted);
        g.strokePath (value, stroke);
    }

    const float capR = arcR - lineW - 1.5f;
    g.setColour (Colours::control);
    g.fillEllipse (c.x - capR, c.y - capR, capR * 2.0f, capR * 2.0f);
    g.setColour (Colours::edge);
    g.drawEllipse (c.x - capR, c.y - capR, capR * 2.0f, capR * 2.0f, 1.0f);

    const juce::Point<float> dir (std::sin (angle), -std::cos (angle));
    g.setColour (Colours::ink);
    g.drawLine (juce::Line<float> (c + dir * (capR * 0.25f), c + dir * (capR * 0.85f)), juce::jmax (1.5f, lineW * 0.6f));
}

//==============================================================================
FxBlock::FxBlock (const juce::String& t, juce::Colour a, const juce::String& n)
    : title (t), note (n), accent (a)
{
    power.setWantsKeyboardFocus (false);
    power.setMouseClickGrabsKeyboardFocus (false);
    addAndMakeVisible (power);
    setOn (false);
}

juce::Slider& FxBlock::addKnob (const juce::String& label, double min, double max, double defaultValue, double interval,
                                std::function<juce::String (double)> textFromValue)
{
    auto k = std::make_unique<juce::Slider> (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox);
    k->setRange (min, max, interval);
    k->setValue (defaultValue, juce::dontSendNotification);
    k->setDoubleClickReturnValue (true, defaultValue);
    k->textFromValueFunction = std::move (textFromValue);
    k->setPopupDisplayEnabled (true, true, nullptr);
    k->setColour (juce::Slider::rotarySliderFillColourId, accent);
    k->setTooltip (title.substring (0, 1) + title.substring (1).toLowerCase() + " " + label.toLowerCase() + " (double-click to reset)");
    k->setWantsKeyboardFocus (false);
    k->setMouseClickGrabsKeyboardFocus (false);
    addAndMakeVisible (*k);
    labels.add (label);
    knobs.push_back (std::move (k));
    return *knobs.back();
}

void FxBlock::setOn (bool on)
{
    isOn = on;
    power.setColour (juce::TextButton::buttonColourId, on ? accent : Colours::control);
    power.setColour (juce::TextButton::textColourOffId, on ? Colours::bg : Colours::muted);
    repaint();
}

void FxBlock::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (Colours::well);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (isOn ? accent.withAlpha (0.6f) : Colours::edge);
    g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);

    auto top = getLocalBounds().reduced (10, 8).removeFromTop (20);
    g.setFont (mono (11.0f, true));
    g.setColour (isOn ? accent : Colours::ink);
    g.drawText (title, top, juce::Justification::centredLeft);
    if (note.isNotEmpty())
    {
        g.setFont (mono (9.5f));
        g.setColour (Colours::muted);
        g.drawText (note, top.withTrimmedLeft (title.length() * 8 + 6).withTrimmedRight (48), juce::Justification::centredLeft, true);
    }

    g.setFont (mono (9.0f));
    g.setColour (Colours::muted);
    for (size_t i = 0; i < knobs.size(); ++i)
    {
        auto kb = knobs[i]->getBounds();
        g.drawText (labels[(int) i], juce::Rectangle<int> (kb.getX() - 12, kb.getBottom(), kb.getWidth() + 24, 12),
                    juce::Justification::centredTop);
    }
}

void FxBlock::resized()
{
    auto area = getLocalBounds().reduced (10, 8);
    auto top = area.removeFromTop (20);
    power.setBounds (top.removeFromRight (40).reduced (0, 1));
    area.removeFromTop (2);
    area.removeFromBottom (12);   // knob labels

    if (knobs.empty()) return;
    const int w = area.getWidth() / (int) knobs.size();
    const int size = juce::jmin (w - 6, area.getHeight());
    for (auto& k : knobs)
        k->setBounds (area.removeFromLeft (w).withSizeKeepingCentre (size, size));
}

//==============================================================================
namespace
{
    void setupRowKnob (juce::Slider& k, double lo, double hi, double def, double step, bool bipolar,
                       juce::Colour colour, const juce::String& tip)
    {
        k.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        k.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        k.setRange (lo, hi, step);
        k.setValue (def, juce::dontSendNotification);
        k.setDoubleClickReturnValue (true, def);
        k.setPopupDisplayEnabled (true, true, nullptr);
        k.setColour (juce::Slider::rotarySliderFillColourId, colour);
        k.getProperties().set ("bipolar", bipolar);
        k.setTooltip (tip);
        k.setWantsKeyboardFocus (false);
        k.setMouseClickGrabsKeyboardFocus (false);
    }
}

StepGrid::StepGrid (DrumEngine& e) : engine (e)
{
    setMouseClickGrabsKeyboardFocus (false);
    setGroups ({ 4, 4, 4, 4 });

    for (int v = 0; v < DrumEngine::numVoices; ++v)
    {
        const juce::String name (DrumEngine::voiceName (v));
        auto& vk = volumeKnobs[(size_t) v];
        auto& pk = pitchKnobs[(size_t) v];

        setupRowKnob (vk, 0.0, 1.5, 1.0, 0.01, false, Colours::g2, name + " volume (double-click: 100%)");
        vk.textFromValueFunction = [] (double x) { return juce::String (juce::roundToInt (x * 100.0)) + "%"; };
        vk.onValueChange = [this, v] { engine.voiceVolume[(size_t) v].store ((float) volumeKnobs[(size_t) v].getValue()); };

        setupRowKnob (pk, -12.0, 12.0, 0.0, 0.1, true, Colours::g3, name + " pitch in semitones (double-click: 0)");
        pk.textFromValueFunction = [] (double x)
        {
            if (std::abs (x) < 0.05) return juce::String ("0 st");
            return (x > 0 ? "+" : "") + juce::String (x, 1) + " st";
        };
        pk.onValueChange = [this, v] { engine.voicePitch[(size_t) v].store ((float) pitchKnobs[(size_t) v].getValue()); };

        // hear the result when you let go of a knob (unless the beat is playing anyway)
        vk.onDragEnd = [this, v] { if (! engine.playing.load()) engine.previewRequest[(size_t) v].store (true); };
        pk.onDragEnd = [this, v] { if (! engine.playing.load()) engine.previewRequest[(size_t) v].store (true); };

        addAndMakeVisible (vk);
        addAndMakeVisible (pk);
    }
}

void StepGrid::syncFromEngine()
{
    for (size_t v = 0; v < (size_t) DrumEngine::numVoices; ++v)
    {
        volumeKnobs[v].setValue (engine.voiceVolume[v].load(), juce::dontSendNotification);
        pitchKnobs[v].setValue (engine.voicePitch[v].load(), juce::dontSendNotification);
    }
    repaint();
}

void StepGrid::resized()
{
    for (int v = 0; v < DrumEngine::numVoices; ++v)
    {
        volumeKnobs[(size_t) v].setBounds (volumeRect (v).toNearestInt());
        pitchKnobs[(size_t) v].setBounds (pitchRect (v).toNearestInt());
    }
}

void StepGrid::setGroups (const std::vector<int>& groupSizes)
{
    groupOf.clear();
    for (size_t g = 0; g < groupSizes.size(); ++g)
        for (int i = 0; i < groupSizes[g]; ++i)
            groupOf.push_back ((int) g);
    repaint();
}

float StepGrid::rowHeight() const
{
    return ((float) getHeight() - headerH - gap * (DrumEngine::numVoices - 1)) / DrumEngine::numVoices;
}

juce::Rectangle<float> StepGrid::nameRect (int row) const   { return { 0.0f, rowY (row), (float) nameW, rowHeight() }; }
juce::Rectangle<float> StepGrid::volumeRect (int row) const { return { (float) (nameW + gap), rowY (row), (float) knobW, rowHeight() }; }
juce::Rectangle<float> StepGrid::pitchRect (int row) const  { return { (float) (nameW + gap + knobW), rowY (row), (float) knobW, rowHeight() }; }
juce::Rectangle<float> StepGrid::muteRect (int row) const   { return { (float) (nameW + gap * 2 + knobW * 2), rowY (row), (float) muteW, rowHeight() }; }

juce::Rectangle<float> StepGrid::fxRect (int row, int fx) const
{
    return { (float) (nameW + gap * 3 + knobW * 2 + muteW + fx * fxW), rowY (row), (float) fxW, rowHeight() };
}

juce::Rectangle<float> StepGrid::fxHeaderRect (int fx) const
{
    return fxRect (0, fx).withY (0.0f).withHeight ((float) headerH - 2.0f);
}

float StepGrid::stepsX() const
{
    return (float) (nameW + gap * 5 + knobW * 2 + muteW + DrumEngine::numFx * fxW);
}

juce::Rectangle<float> StepGrid::stepRect (int row, int step) const
{
    const int n = steps();
    const int numGroups = groupOf.empty() ? 1 : groupOf.back() + 1;
    const float x0 = stepsX();
    const float w  = ((float) getWidth() - x0 - gap * (n - 1) - groupGap * (numGroups - 1)) / (float) n;
    const int group = step < (int) groupOf.size() ? groupOf[(size_t) step] : 0;
    return { x0 + step * (w + gap) + group * groupGap, rowY (row), w, rowHeight() };
}

int StepGrid::rowAt (float y) const
{
    for (int v = 0; v < DrumEngine::numVoices; ++v)
        if (y >= nameRect (v).getY() - gap * 0.5f && y < nameRect (v).getBottom() + gap * 0.5f)
            return v;
    return -1;
}

void StepGrid::paint (juce::Graphics& g)
{
    const int n = steps();
    const auto now = juce::Time::getMillisecondCounter();

    // column headings
    g.setFont (mono (9.0f));
    g.setColour (Colours::muted);
    g.drawText ("VOL",   volumeRect (0).withY (0.0f).withHeight ((float) headerH - 4.0f), juce::Justification::centred);
    g.drawText ("PITCH", pitchRect (0).withY (0.0f).withHeight ((float) headerH - 4.0f).expanded (4.0f, 0.0f), juce::Justification::centred);
    for (int f = 0; f < DrumEngine::numFx; ++f)
    {
        const bool on = engine.fxOn[(size_t) f].load();
        g.setColour (on ? Colours::fx[f] : Colours::muted);
        g.drawText (DrumEngine::fxShort (f), fxHeaderRect (f).expanded (1.0f, 0.0f), juce::Justification::centred);
    }

    // step numbers: the first step of each counted group is highlighted
    for (int s = 0; s < n; ++s)
    {
        auto r = stepRect (0, s);
        const bool groupStart = s == 0 || groupOf[(size_t) s] != groupOf[(size_t) s - 1];
        g.setColour (groupStart ? Colours::ink : Colours::muted);
        g.setFont (mono (n > 24 ? 9.5f : 11.0f));
        g.drawText (juce::String (s + 1), juce::Rectangle<float> (r.getX() - 4.0f, 0.0f, r.getWidth() + 8.0f, (float) headerH - 4.0f),
                    juce::Justification::centred);
    }

    for (int v = 0; v < DrumEngine::numVoices; ++v)
    {
        const bool isMuted = engine.muted[(size_t) v].load();
        const Sample* sample = engine.samples[(size_t) v].load();
        const bool flashing = now < flashUntil[(size_t) v];

        // voice name, key badge, and loaded sample name
        auto nr = nameRect (v);
        g.setColour (flashing ? Colours::edge.brighter (0.4f) : Colours::control);
        g.fillRoundedRectangle (nr, 4.0f);
        g.setColour (v == dropRow ? Colours::g3 : Colours::edge);
        g.drawRoundedRectangle (nr.reduced (0.5f), 4.0f, v == dropRow ? 2.0f : 1.0f);

        auto inner = nr.reduced (8.0f, 0.0f);
        auto badge = inner.removeFromRight (16.0f).withSizeKeepingCentre (16.0f, 16.0f);
        g.setColour (Colours::bg);
        g.fillRoundedRectangle (badge, 3.0f);
        g.setColour (Colours::muted);
        g.setFont (mono (10.5f, true));
        g.drawText (juce::String::charToString ((juce::juce_wchar) DrumEngine::voiceKey (v)).toUpperCase(), badge, juce::Justification::centred);

        g.setColour (isMuted ? Colours::muted : Colours::ink);
        if (sample != nullptr && nr.getHeight() >= 26.0f)
        {
            auto top = inner.withHeight (inner.getHeight() * 0.5f).translated (0.0f, 2.0f);
            g.setFont (mono (10.5f));
            g.drawText (DrumEngine::voiceName (v), top, juce::Justification::bottomLeft);
            g.setColour (Colours::g2);
            g.setFont (mono (9.5f));
            g.drawText (sample->name, inner.withTrimmedTop (inner.getHeight() * 0.5f).translated (0.0f, 1.0f),
                        juce::Justification::topLeft, true);
        }
        else
        {
            g.setFont (mono (11.0f));
            g.drawText (DrumEngine::voiceName (v), inner, juce::Justification::centredLeft);
            if (sample != nullptr)
            {
                g.setColour (Colours::g2);
                g.fillEllipse (inner.getRight() - 26.0f, inner.getCentreY() - 3.0f, 6.0f, 6.0f);
            }
        }

        // mute
        auto mr = muteRect (v);
        g.setColour (isMuted ? Colours::led.withAlpha (0.85f) : Colours::control);
        g.fillRoundedRectangle (mr, 4.0f);
        g.setColour (Colours::edge);
        g.drawRoundedRectangle (mr.reduced (0.5f), 4.0f, 1.0f);
        g.setColour (isMuted ? juce::Colours::white : Colours::muted);
        g.setFont (mono (11.0f, true));
        g.drawText ("M", mr, juce::Justification::centred);

        // which effects this sound goes through (dimmed while that effect is off)
        for (int f = 0; f < DrumEngine::numFx; ++f)
        {
            const bool send = engine.fxSend[(size_t) f][(size_t) v].load();
            const bool effectOn = engine.fxOn[(size_t) f].load();
            const float size = juce::jmin (11.0f, rowHeight() - 6.0f);
            auto box = fxRect (v, f).withSizeKeepingCentre (size, size);
            if (send)
            {
                g.setColour (Colours::fx[f].withAlpha (effectOn ? 1.0f : 0.35f));
                g.fillRoundedRectangle (box, 2.5f);
            }
            else
            {
                g.setColour (Colours::edge.brighter (0.15f));
                g.drawRoundedRectangle (box.reduced (0.5f), 2.5f, 1.0f);
            }
        }

        // steps
        for (int s = 0; s < n; ++s)
        {
            auto r = stepRect (v, s);
            const bool on = engine.pattern[(size_t) v][(size_t) s].load();
            const bool isNow = (s == playhead);

            juce::Colour c = on ? groupColour (groupOf[(size_t) s]) : Colours::stepOff;
            if (on && isMuted) c = c.withAlpha (0.35f);
            if (on && isNow)   c = c.brighter (0.3f);

            g.setColour (juce::Colours::black.withAlpha (0.4f));
            g.fillRoundedRectangle (r.translated (0.0f, 2.0f), 3.0f);
            g.setColour (c);
            g.fillRoundedRectangle (r, 3.0f);

            const float ledSize = juce::jmin (6.0f, r.getWidth() * 0.3f);
            auto led = juce::Rectangle<float> (ledSize, ledSize).withCentre ({ r.getCentreX(), r.getY() + r.getHeight() * 0.25f });
            g.setColour (on ? juce::Colours::white : juce::Colours::black.withAlpha (0.35f));
            g.fillEllipse (led);

            if (isNow)
            {
                g.setColour (Colours::ink);
                g.drawRoundedRectangle (r.expanded (1.5f), 4.0f, 2.0f);
            }
        }
    }
}

bool StepGrid::hitStep (juce::Point<float> p, int& row, int& step) const
{
    for (int v = 0; v < DrumEngine::numVoices; ++v)
        for (int s = 0; s < steps(); ++s)
            if (stepRect (v, s).expanded (gap * 0.5f).contains (p))
            {
                row = v; step = s;
                return true;
            }
    return false;
}

void StepGrid::setStep (int row, int step, bool on)
{
    engine.pattern[(size_t) row][(size_t) step].store (on);
    repaint();
}

void StepGrid::flashVoice (int v)
{
    flashUntil[(size_t) v] = juce::Time::getMillisecondCounter() + 90;
    repaint (nameRect (v).toNearestInt().expanded (2));
}

void StepGrid::tick()
{
    const auto now = juce::Time::getMillisecondCounter();
    for (int v = 0; v < DrumEngine::numVoices; ++v)
        if (flashUntil[(size_t) v] != 0 && now >= flashUntil[(size_t) v])
        {
            flashUntil[(size_t) v] = 0;
            repaint (nameRect (v).toNearestInt().expanded (2));
        }
}

void StepGrid::mouseDown (const juce::MouseEvent& e)
{
    const auto p = e.position;

    // click an effect's column heading: every sound in, or (if all are in already) every sound out
    for (int f = 0; f < DrumEngine::numFx; ++f)
    {
        if (fxHeaderRect (f).contains (p))
        {
            bool allIn = true;
            for (auto& s : engine.fxSend[(size_t) f]) allIn = allIn && s.load();
            for (auto& s : engine.fxSend[(size_t) f]) s.store (! allIn);
            repaint();
            return;
        }
    }

    for (int v = 0; v < DrumEngine::numVoices; ++v)
    {
        if (nameRect (v).contains (p))
        {
            if (e.mods.isPopupMenu())
            {
                if (onSoundMenu) onSoundMenu (v);
            }
            else
            {
                engine.previewRequest[(size_t) v].store (true);
                flashVoice (v);
            }
            return;
        }
        if (muteRect (v).contains (p))
        {
            engine.muted[(size_t) v].store (! engine.muted[(size_t) v].load());
            repaint();
            return;
        }
        for (int f = 0; f < DrumEngine::numFx; ++f)
        {
            if (fxRect (v, f).contains (p))
            {
                auto& send = engine.fxSend[(size_t) f][(size_t) v];
                send.store (! send.load());
                repaint();
                return;
            }
        }
    }

    int row, step;
    if (hitStep (p, row, step))
    {
        dragValue = ! engine.pattern[(size_t) row][(size_t) step].load();
        dragRow = row;
        dragging = true;
        setStep (row, step, dragValue);

        if (dragValue && ! engine.playing.load())
            engine.previewRequest[(size_t) row].store (true);
    }
}

void StepGrid::mouseDrag (const juce::MouseEvent& e)
{
    // drag along a row to paint or erase several steps in one go
    int row, step;
    if (dragging && hitStep (e.position, row, step) && row == dragRow
        && engine.pattern[(size_t) row][(size_t) step].load() != dragValue)
        setStep (row, step, dragValue);
}

void StepGrid::mouseUp (const juce::MouseEvent&)
{
    dragging = false;
    dragRow = -1;
}

bool StepGrid::isInterestedInFileDrag (const juce::StringArray& files)
{
    return files.size() == 1 && isAudioFile (files[0]);
}

void StepGrid::fileDragEnter (const juce::StringArray&, int, int y) { dropRow = rowAt ((float) y); repaint(); }
void StepGrid::fileDragMove (const juce::StringArray&, int, int y)
{
    const int r = rowAt ((float) y);
    if (r != dropRow) { dropRow = r; repaint(); }
}
void StepGrid::fileDragExit (const juce::StringArray&) { dropRow = -1; repaint(); }

void StepGrid::filesDropped (const juce::StringArray& files, int, int y)
{
    const int row = rowAt ((float) y);
    dropRow = -1;
    repaint();
    if (row >= 0 && onFileDropped)
        onFileDropped (row, juce::File (files[0]));
}

void StepGrid::setPlayhead (int step)
{
    if (step != playhead)
    {
        playhead = step;
        repaint();
    }
}

//==============================================================================
MainComponent::MainComponent()
{
    setLookAndFeel (&lookAndFeel);
    setWantsKeyboardFocus (true);
    formats.registerBasicFormats();

    juce::PropertiesFile::Options opts;
    opts.applicationName     = "DR-16";
    opts.folderName          = "DR-16";
    opts.filenameSuffix      = ".settings";
    opts.osxLibrarySubFolder = "Application Support";
    settings = std::make_unique<juce::PropertiesFile> (opts);

    auto styleLabel = [this] (juce::Label& l, const juce::String& text)
    {
        l.setText (text, juce::dontSendNotification);
        l.setFont (mono (10.5f));
        l.setColour (juce::Label::textColourId, Colours::muted);
        addAndMakeVisible (l);
    };
    styleLabel (tempoLabel, "TEMPO");
    styleLabel (swingLabel, "SWING");
    styleLabel (volumeLabel, "VOLUME");
    styleLabel (meterLabel, "TIME SIGNATURE");
    styleLabel (presetLabel, "PATTERN");
    styleLabel (hintLabel, "Click a voice to hear it, right-click it (or drop a file) for your own sound.  Keys or MIDI play "
                           "the voices, REC records.  Click RV/DL/CP/DS/ST to route all.  Space = start/stop,  Hold Q = stutter");
    hintLabel.setFont (mono (10.0f));

    // Tempo: an LED-style box you click and type into. Up/Down arrows nudge it too.
    bpmDisplay.setFont (mono (24.0f, true));
    bpmDisplay.setJustificationType (juce::Justification::centred);
    bpmDisplay.setColour (juce::Label::textColourId, Colours::led);
    bpmDisplay.setColour (juce::Label::backgroundColourId, juce::Colour (0xff0e0e10));
    bpmDisplay.setColour (juce::Label::outlineColourId, juce::Colours::black);
    bpmDisplay.setColour (juce::Label::textWhenEditingColourId, Colours::led);
    bpmDisplay.setColour (juce::Label::backgroundWhenEditingColourId, juce::Colour (0xff0e0e10));
    bpmDisplay.setColour (juce::Label::outlineWhenEditingColourId, Colours::led);
    bpmDisplay.setColour (juce::TextEditor::highlightColourId, Colours::led.withAlpha (0.35f));
    bpmDisplay.setColour (juce::CaretComponent::caretColourId, Colours::led);
    bpmDisplay.setEditable (true, false, true);
    bpmDisplay.setTooltip ("Click and type a tempo (20-400), then press Enter");
    bpmDisplay.onEditorShow = [this]
    {
        if (auto* ed = bpmDisplay.getCurrentTextEditor())
        {
            ed->setInputRestrictions (5, "0123456789.");
            ed->setJustification (juce::Justification::centred);
            ed->selectAll();
        }
    };
    bpmDisplay.onTextChange = [this]
    {
        const double v = bpmDisplay.getText().getDoubleValue();
        setTempo (v > 0.0 ? v : (double) engine.tempo.load());
        juce::Component::SafePointer<MainComponent> sp (this);
        juce::MessageManager::callAsync ([sp] { if (sp != nullptr) sp->grabKeyboardFocus(); });
    };
    bpmDisplay.onEditorHide = [this] { grabKeyboardFocus(); };
    addAndMakeVisible (bpmDisplay);

    auto styleSlider = [this] (juce::Slider& s, double lo, double hi, double val)
    {
        s.setSliderStyle (juce::Slider::LinearHorizontal);
        s.setRange (lo, hi, 1.0);
        s.setValue (val, juce::dontSendNotification);
        s.setColour (juce::Slider::trackColourId, Colours::g2);
        s.setColour (juce::Slider::backgroundColourId, Colours::stepOff);
        s.setColour (juce::Slider::thumbColourId, Colours::ink);
        s.setColour (juce::Slider::textBoxTextColourId, Colours::muted);
        s.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        s.setWantsKeyboardFocus (false);
        s.setMouseClickGrabsKeyboardFocus (false);
        addAndMakeVisible (s);
    };

    styleSlider (swingSlider, 0, 60, 12);
    swingSlider.setTextBoxStyle (juce::Slider::TextBoxRight, true, 42, 20);
    swingSlider.setTextValueSuffix ("%");
    swingSlider.onValueChange = [this] { engine.swing.store ((float) swingSlider.getValue() / 100.0f); };

    styleSlider (volumeSlider, 0, 100, 80);
    volumeSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    volumeSlider.onValueChange = [this] { engine.volume.store ((float) volumeSlider.getValue() / 100.0f); };

    auto styleButton = [this] (juce::TextButton& b, juce::Colour fill, juce::Colour text)
    {
        b.setColour (juce::TextButton::buttonColourId, fill);
        b.setColour (juce::TextButton::textColourOffId, text);
        b.setWantsKeyboardFocus (false);
        b.setMouseClickGrabsKeyboardFocus (false);
        addAndMakeVisible (b);
    };
    styleButton (playButton, Colours::g1, juce::Colours::white);
    styleButton (recButton, Colours::control, Colours::muted);
    styleButton (clearButton, Colours::control, Colours::ink);
    styleButton (saveButton, Colours::control, Colours::ink);
    styleButton (exportButton, Colours::control, Colours::ink);
    saveButton.onClick   = [this] { showSaveDialog(); };
    exportButton.onClick = [this] { showExportMenu(); };
    playButton.onClick  = [this] { togglePlay(); };
    recButton.onClick   = [this] { toggleRecord(); };
    clearButton.onClick = [this] { clearPattern(); };

    auto styleCombo = [this] (juce::ComboBox& c)
    {
        c.setColour (juce::ComboBox::backgroundColourId, Colours::control);
        c.setColour (juce::ComboBox::textColourId, Colours::ink);
        c.setColour (juce::ComboBox::outlineColourId, Colours::edge);
        c.setColour (juce::ComboBox::arrowColourId, Colours::muted);
        c.setWantsKeyboardFocus (false);
        c.setMouseClickGrabsKeyboardFocus (false);
        addAndMakeVisible (c);
    };

    styleCombo (presetBox);
    presetBox.onChange = [this] { onPatternChosen(); };
    rebuildPatternMenu (0);

    int id = 1;
    for (auto& t : timeSigs)
        meterBox.addItem (t.name, id++);
    styleCombo (meterBox);
    meterBox.onChange = [this] { setTimeSignature (meterBox.getSelectedItemIndex()); };

    grid.onSoundMenu   = [this] (int v) { showSoundMenu (v); };
    grid.onFileDropped = [this] (int v, const juce::File& f) { loadSample (v, f, true); };
    addAndMakeVisible (grid);

    // bring back sounds loaded in earlier sessions
    for (int v = 0; v < DrumEngine::numVoices; ++v)
    {
        const juce::File f (settings->getValue ("sample_" + juce::String (v)));
        if (f.existsAsFile())
            loadSample (v, f, false);
    }

    buildFxPanel();

    meterBox.setSelectedItemIndex (fourFour);
    presetBox.setSelectedId (1, juce::sendNotificationSync);   // loads "Boom bap"

    setSize (1240, 740);
    setAudioChannels (0, 2);

    for (auto& f : midiFlash) f.store (false);
    deviceManager.addMidiInputDeviceCallback ({}, this);   // empty id = every enabled MIDI input
    refreshMidiInputs();

    startTimerHz (60);
}

MainComponent::~MainComponent()
{
    setLookAndFeel (nullptr);
    stopTimer();
    deviceManager.removeMidiInputDeviceCallback ({}, this);
    shutdownAudio();   // audio thread is stopped before the samples it reads are freed
    settings->saveIfNeeded();
}

void MainComponent::prepareToPlay (int, double sampleRate)
{
    engine.prepare (sampleRate);
}

void MainComponent::getNextAudioBlock (const juce::AudioSourceChannelInfo& info)
{
    auto* buffer = info.buffer;
    const int numChannels = buffer->getNumChannels();
    if (numChannels == 0) return;

    float* left  = buffer->getWritePointer (0, info.startSample);
    float* right = numChannels > 1 ? buffer->getWritePointer (1, info.startSample) : nullptr;
    engine.render (left, right, info.numSamples);
}

//==============================================================================
void MainComponent::togglePlay()
{
    const bool nowPlaying = ! engine.playing.load();
    engine.playing.store (nowPlaying);
    playButton.setButtonText (nowPlaying ? "STOP" : "START");
    playButton.setColour (juce::TextButton::buttonColourId, nowPlaying ? Colours::control : Colours::g1);
    playButton.setColour (juce::TextButton::textColourOffId, nowPlaying ? Colours::g1 : juce::Colours::white);
}

void MainComponent::toggleRecord()
{
    const bool on = ! engine.recording.load();
    engine.recording.store (on);
    recButton.setColour (juce::TextButton::buttonColourId, on ? Colours::led : Colours::control);
    recButton.setColour (juce::TextButton::textColourOffId, on ? juce::Colours::white : Colours::muted);
}

void MainComponent::setTempo (double bpm)
{
    bpm = juce::jlimit (20.0, 400.0, bpm);
    bpm = std::round (bpm * 10.0) / 10.0;
    engine.tempo.store ((float) bpm);
    bpmDisplay.setText (std::abs (bpm - std::round (bpm)) < 0.05 ? juce::String ((int) std::round (bpm))
                                                                  : juce::String (bpm, 1),
                        juce::dontSendNotification);
}

void MainComponent::setTimeSignature (int index)
{
    if (index < 0) return;
    const auto& t = timeSigs[index];
    engine.numSteps.store (t.steps());
    grid.setGroups (t.groups);
}

void MainComponent::loadPreset (int index)
{
    if (index < 0) return;
    const auto& p = presets[index];
    currentPatternName = p.name;

    for (int v = 0; v < DrumEngine::numVoices; ++v)
    {
        juce::String row (p.rows[(size_t) v]);
        row = row.removeCharacters (" ");
        for (int s = 0; s < DrumEngine::maxSteps; ++s)
            engine.pattern[(size_t) v][(size_t) s].store (s < row.length() && row[s] == 'x');
    }

    meterBox.setSelectedItemIndex (fourFour, juce::sendNotificationSync);   // presets are all 4/4
    setTempo (p.tempo);
    swingSlider.setValue (p.swing, juce::sendNotificationSync);
    grid.repaint();
}

void MainComponent::clearPattern()
{
    for (auto& row : engine.pattern)
        for (auto& s : row) s.store (false);
    grid.repaint();
}

//==============================================================================
void MainComponent::showSoundMenu (int voice)
{
    const bool hasSample = engine.samples[(size_t) voice].load() != nullptr;

    juce::PopupMenu m;
    m.addSectionHeader (DrumEngine::voiceName (voice));
    m.addItem (1, "Load sound...");
    m.addItem (2, "Use built-in sound", hasSample);

    juce::Component::SafePointer<MainComponent> sp (this);
    m.showMenuAsync (juce::PopupMenu::Options(),
                     [sp, voice] (int result)
                     {
                         if (sp == nullptr) return;
                         if (result == 1) sp->chooseSample (voice);
                         if (result == 2) sp->useBuiltInSound (voice);
                         sp->grabKeyboardFocus();
                     });
}

void MainComponent::chooseSample (int voice)
{
    const juce::File start (settings->getValue ("lastFolder",
                            juce::File::getSpecialLocation (juce::File::userMusicDirectory).getFullPathName()));

    chooser = std::make_unique<juce::FileChooser> (juce::String ("Load sound for ") + DrumEngine::voiceName (voice),
                                                   start, formats.getWildcardForAllFormats());

    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this, voice] (const juce::FileChooser& fc)
                          {
                              const auto f = fc.getResult();
                              if (f.existsAsFile())
                              {
                                  settings->setValue ("lastFolder", f.getParentDirectory().getFullPathName());
                                  loadSample (voice, f, true);
                              }
                              grabKeyboardFocus();
                          });
}

bool MainComponent::loadSample (int voice, const juce::File& file, bool remember, bool preview)
{
    if (remember)
        settings->setValue ("sample_" + juce::String (voice), file.getFullPathName());

    // the same file loaded before (e.g. by another pattern) is reused, not read again
    for (auto& existing : loadedSamples)
    {
        if (existing->path == file.getFullPathName())
        {
            engine.samples[(size_t) voice].store (existing.get());
            if (preview) engine.previewRequest[(size_t) voice].store (true);
            grid.repaint();
            return true;
        }
    }

    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr)
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Can't load sound",
                                                file.getFileName() + " isn't an audio file DR-16 can read "
                                                "(use WAV, AIFF, FLAC, OGG or MP3).");
        return false;
    }

    // drum sounds are short; anything past 10 seconds is cut off
    const int len = (int) juce::jmin<juce::int64> (reader->lengthInSamples, (juce::int64) (reader->sampleRate * 10.0));
    const int chans = (int) reader->numChannels;

    juce::AudioBuffer<float> raw (chans, len);
    reader->read (&raw, 0, len, 0, true, true);

    auto s = std::make_unique<Sample>();
    s->data.setSize (1, len + 1);
    s->data.clear();
    for (int c = 0; c < chans; ++c)                                   // mix down to mono
        s->data.addFrom (0, 0, raw, c, 0, len, 1.0f / (float) chans);
    s->sampleRate = reader->sampleRate;
    s->name = file.getFileNameWithoutExtension();
    s->path = file.getFullPathName();

    engine.samples[(size_t) voice].store (s.get());
    loadedSamples.push_back (std::move (s));   // old sounds stay in memory until exit, the audio thread may still be playing them

    if (preview)
        engine.previewRequest[(size_t) voice].store (true);
    grid.repaint();
    return true;
}

void MainComponent::useBuiltInSound (int voice, bool preview)
{
    engine.samples[(size_t) voice].store (nullptr);
    settings->removeValue ("sample_" + juce::String (voice));
    if (preview)
        engine.previewRequest[(size_t) voice].store (true);
    grid.repaint();
}

//==============================================================================
bool MainComponent::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::spaceKey)
    {
        togglePlay();
        return true;
    }

    if (key.getKeyCode() == juce::KeyPress::upKey || key.getKeyCode() == juce::KeyPress::downKey)
    {
        const double delta = (key.getModifiers().isShiftDown() ? 10.0 : 1.0)
                           * (key.getKeyCode() == juce::KeyPress::upKey ? 1.0 : -1.0);
        setTempo (std::round (engine.tempo.load()) + delta);
        return true;
    }

    if (key.getModifiers().isCommandDown() || key.getModifiers().isAltDown())
        return false;

    const auto ch = juce::CharacterFunctions::toLowerCase (key.getTextCharacter());

    if (ch == 'q')   // stutter for as long as Q is held
    {
        if (heldKeys.insert (key.getKeyCode()).second)
        {
            stutterKeyHeld = true;
            stutterKeyCode = key.getKeyCode();
            updateStutter();
        }
        return true;
    }

    for (int v = 0; v < DrumEngine::numVoices; ++v)
    {
        if (ch == (juce::juce_wchar) DrumEngine::voiceKey (v))
        {
            // holding a key down sends repeats; only the first press is a hit
            if (heldKeys.insert (key.getKeyCode()).second)
            {
                engine.liveHit[(size_t) v].store (1.0f);
                grid.flashVoice (v);
            }
            return true;
        }
    }
    return false;
}

bool MainComponent::keyStateChanged (bool)
{
    for (auto it = heldKeys.begin(); it != heldKeys.end();)
        it = juce::KeyPress::isKeyCurrentlyDown (*it) ? std::next (it) : heldKeys.erase (it);

    if (stutterKeyHeld && ! juce::KeyPress::isKeyCurrentlyDown (stutterKeyCode))
    {
        stutterKeyHeld = false;
        updateStutter();
    }
    return false;
}

void MainComponent::timerCallback()
{
    grid.setPlayhead (engine.currentStep.load());

    for (int v = 0; v < DrumEngine::numVoices; ++v)
        if (midiFlash[(size_t) v].exchange (false))
            grid.flashVoice (v);
    grid.tick();

    // pick up MIDI keyboards plugged in while the app is running
    if (--midiScanCountdown <= 0)
    {
        midiScanCountdown = 120;   // every 2 seconds
        refreshMidiInputs();
    }

    // keep the letter keys working after clicking around (menus, sliders...),
    // but never steal focus from the tempo box, a menu or a file dialog
    if (! hasKeyboardFocus (true)
        && juce::ModalComponentManager::getInstance()->getNumModalComponents() == 0
        && juce::TopLevelWindow::getActiveTopLevelWindow() == getTopLevelComponent())
        grabKeyboardFocus();

    const auto ver = engine.patternVersion.load();
    if (ver != lastPatternVersion)
    {
        lastPatternVersion = ver;
        grid.repaint();
    }
}

//==============================================================================
void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (Colours::bg);

    auto panel = getLocalBounds().reduced (14).toFloat();
    g.setColour (Colours::panel);
    g.fillRoundedRectangle (panel, 10.0f);
    g.setColour (Colours::edge);
    g.drawRoundedRectangle (panel.reduced (0.5f), 10.0f, 1.0f);

    auto header = getLocalBounds().reduced (36, 0).withTrimmedTop (28).withHeight (30);
    g.setFont (mono (22.0f, true));
    g.setColour (Colours::ink);
    g.drawText ("PANO", header, juce::Justification::centredLeft);
    g.setColour (Colours::g1);
    g.drawText ("DR-16", header.withTrimmedLeft (74), juce::Justification::centredLeft);

    g.setFont (mono (11.0f));
    g.setColour (Colours::muted);
    g.drawText (midiStatus, midiStatusArea, juce::Justification::centredRight, true);
}

void MainComponent::resized()
{
    auto area = getLocalBounds().reduced (36, 0);

    auto headerRow = area.withTrimmedTop (27).withHeight (32);
    exportButton.setBounds (headerRow.removeFromRight (96));
    headerRow.removeFromRight (8);
    saveButton.setBounds (headerRow.removeFromRight (70));
    headerRow.removeFromRight (16);
    midiStatusArea = headerRow.withTrimmedLeft (200);

    area.removeFromTop (78);

    auto transport = area.removeFromTop (58);
    auto labelRow  = transport.removeFromTop (18);
    auto ctrlRow   = transport.removeFromTop (36);

    auto place = [&] (juce::Label* label, int width, int spacing = 20) -> juce::Rectangle<int>
    {
        if (label != nullptr) label->setBounds (labelRow.removeFromLeft (width + spacing));
        else labelRow.removeFromLeft (width + spacing);
        auto r = ctrlRow.removeFromLeft (width);
        ctrlRow.removeFromLeft (spacing);
        return r;
    };

    playButton.setBounds (place (nullptr, 86, 8));
    recButton.setBounds (place (nullptr, 56));
    bpmDisplay.setBounds (place (&tempoLabel, 86));
    swingSlider.setBounds (place (&swingLabel, 150));
    volumeSlider.setBounds (place (&volumeLabel, 110));
    meterBox.setBounds (place (&meterLabel, 160).reduced (0, 2));
    presetBox.setBounds (place (&presetLabel, 170).reduced (0, 2));
    clearButton.setBounds (ctrlRow.removeFromRight (76));

    // effects panel: five blocks side by side, the delay one wider for its third knob
    area.removeFromTop (10);
    auto fxRow = area.removeFromTop (86);
    const int fxGap = 10;
    const float unit = (float) (fxRow.getWidth() - fxGap * (DrumEngine::numFx - 1)) / 5.6f;
    const float widths[DrumEngine::numFx] = { 1.0f, 1.4f, 1.0f, 1.0f, 1.2f };
    for (int f = 0; f < DrumEngine::numFx; ++f)
    {
        fxBlocks[(size_t) f]->setBounds (f == DrumEngine::numFx - 1 ? fxRow : fxRow.removeFromLeft ((int) (unit * widths[f])));
        fxRow.removeFromLeft (fxGap);
    }

    hintLabel.setBounds (area.removeFromBottom (40).withTrimmedBottom (12));
    area.removeFromTop (14);
    grid.setBounds (area.withTrimmedBottom (4));
}

//==============================================================================
// Saved patterns: one small JSON file per pattern in the user's app-data folder
// (on Windows: %APPDATA%\DR-16\Patterns).

juce::File MainComponent::patternFolder()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("DR-16").getChildFile ("Patterns");
}

void MainComponent::rebuildPatternMenu (int idToSelect)
{
    presetBox.clear (juce::dontSendNotification);

    int id = 1;
    for (auto& p : presets)
        presetBox.addItem (p.name, id++);

    userPatterns.clear();
    for (const auto& f : patternFolder().findChildFiles (juce::File::findFiles, false, "*.dr16"))
        userPatterns.push_back (f);
    std::sort (userPatterns.begin(), userPatterns.end(),
               [] (const juce::File& a, const juce::File& b)
               { return a.getFileNameWithoutExtension().compareIgnoreCase (b.getFileNameWithoutExtension()) < 0; });

    if (! userPatterns.empty())
    {
        presetBox.addSeparator();
        presetBox.addSectionHeading ("SAVED");
        for (size_t i = 0; i < userPatterns.size(); ++i)
            presetBox.addItem (userPatterns[i].getFileNameWithoutExtension(), firstUserPatternId + (int) i);
    }

    if (idToSelect > 0)
        presetBox.setSelectedId (idToSelect, juce::dontSendNotification);
}

void MainComponent::onPatternChosen()
{
    const int id = presetBox.getSelectedId();
    if (id >= firstUserPatternId)
    {
        const size_t i = (size_t) (id - firstUserPatternId);
        if (i < userPatterns.size())
            loadPatternFile (userPatterns[i]);
    }
    else if (id > 0)
    {
        loadPreset (id - 1);
    }
    grabKeyboardFocus();
}

void MainComponent::showSaveDialog()
{
    saveDialog = std::make_unique<juce::AlertWindow> ("Save pattern",
        "It will appear in the Pattern menu. Saving with an existing name replaces that pattern.",
        juce::MessageBoxIconType::NoIcon, this);
    saveDialog->addTextEditor ("name", currentPatternName, "Name:");
    saveDialog->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    saveDialog->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    juce::Component::SafePointer<MainComponent> sp (this);
    saveDialog->enterModalState (true, juce::ModalCallbackFunction::create ([sp] (int result)
    {
        if (sp == nullptr) return;
        if (result == 1)
            sp->savePattern (sp->saveDialog->getTextEditorContents ("name").trim());

        // the dialog is still running its own code here, so delete it a moment later
        juce::MessageManager::callAsync ([sp]
        {
            if (sp == nullptr) return;
            sp->saveDialog.reset();
            sp->grabKeyboardFocus();
        });
    }), false);

    if (auto* ed = saveDialog->getTextEditor ("name"))
    {
        ed->grabKeyboardFocus();
        ed->selectAll();
    }
}

void MainComponent::savePattern (const juce::String& name)
{
    if (name.isEmpty()) return;

    auto* obj = new juce::DynamicObject();
    obj->setProperty ("format", "DR-16 pattern");
    obj->setProperty ("version", 2);
    obj->setProperty ("name", name);
    obj->setProperty ("timeSignature", timeSigs[juce::jmax (0, meterBox.getSelectedItemIndex())].name);
    obj->setProperty ("tempo", (double) engine.tempo.load());
    obj->setProperty ("swing", swingSlider.getValue());

    auto* steps  = new juce::DynamicObject();
    auto* sounds = new juce::DynamicObject();
    juce::Array<juce::var> mutedVoices;

    for (int v = 0; v < DrumEngine::numVoices; ++v)
    {
        juce::String row;
        for (int st = 0; st < DrumEngine::maxSteps; ++st)
            row << (engine.pattern[(size_t) v][(size_t) st].load() ? "x" : ".");
        steps->setProperty (DrumEngine::voiceId (v), row);

        if (engine.muted[(size_t) v].load())
            mutedVoices.add (DrumEngine::voiceId (v));

        if (auto* smp = engine.samples[(size_t) v].load())
            sounds->setProperty (DrumEngine::voiceId (v), smp->path);
    }
    obj->setProperty ("steps", juce::var (steps));
    obj->setProperty ("muted", mutedVoices);
    obj->setProperty ("sounds", juce::var (sounds));

    auto* voiceSettings = new juce::DynamicObject();
    for (int v = 0; v < DrumEngine::numVoices; ++v)
    {
        auto* vo = new juce::DynamicObject();
        vo->setProperty ("volume", (double) engine.voiceVolume[(size_t) v].load());
        vo->setProperty ("pitch", (double) engine.voicePitch[(size_t) v].load());
        voiceSettings->setProperty (DrumEngine::voiceId (v), juce::var (vo));
    }
    obj->setProperty ("voices", juce::var (voiceSettings));

    auto* effects = new juce::DynamicObject();
    for (int f = 0; f < DrumEngine::numFx; ++f)
    {
        auto* fo = new juce::DynamicObject();
        if (f != DrumEngine::fxStutter)   // stutter is played live, so it's never saved as "on"
            fo->setProperty ("on", engine.fxOn[(size_t) f].load());
        juce::Array<juce::var> routed;
        for (int v = 0; v < DrumEngine::numVoices; ++v)
            if (engine.fxSend[(size_t) f][(size_t) v].load())
                routed.add (DrumEngine::voiceId (v));
        fo->setProperty ("sounds", routed);
        effects->setProperty (DrumEngine::fxId (f), juce::var (fo));
    }
    auto fxProp = [effects] (int f, const char* key, const juce::var& value)
    {
        if (auto* o = effects->getProperty (DrumEngine::fxId (f)).getDynamicObject())
            o->setProperty (key, value);
    };
    fxProp (DrumEngine::fxReverb,  "amount",   (double) engine.reverbAmount.load());
    fxProp (DrumEngine::fxReverb,  "size",     (double) engine.reverbSize.load());
    fxProp (DrumEngine::fxDelay,   "amount",   (double) engine.delayAmount.load());
    fxProp (DrumEngine::fxDelay,   "time",     DrumEngine::delayTimeName (engine.delayTime.load()));
    fxProp (DrumEngine::fxDelay,   "feedback", (double) engine.delayFeedback.load());
    fxProp (DrumEngine::fxComp,    "amount",   (double) engine.compAmount.load());
    fxProp (DrumEngine::fxComp,    "mix",      (double) engine.compMix.load());
    fxProp (DrumEngine::fxDist,    "drive",    (double) engine.distDrive.load());
    fxProp (DrumEngine::fxDist,    "tone",     (double) engine.distTone.load());
    fxProp (DrumEngine::fxStutter, "rate",     DrumEngine::stutterRateName (engine.stutterRate.load()));
    obj->setProperty ("effects", juce::var (effects));

    const auto folder = patternFolder();
    folder.createDirectory();
    const auto file = folder.getChildFile (juce::File::createLegalFileName (name) + ".dr16");

    if (! file.replaceWithText (juce::JSON::toString (juce::var (obj))))
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Couldn't save",
                                                "DR-16 couldn't write to " + file.getFullPathName());
        return;
    }

    currentPatternName = name;
    rebuildPatternMenu (0);
    for (size_t i = 0; i < userPatterns.size(); ++i)
        if (userPatterns[i] == file)
            presetBox.setSelectedId (firstUserPatternId + (int) i, juce::dontSendNotification);
}

void MainComponent::loadPatternFile (const juce::File& file)
{
    const auto json = juce::JSON::parse (file.loadFileAsString());
    if (! json.isObject())
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Can't open pattern",
                                                file.getFileName() + " isn't a valid DR-16 pattern.");
        return;
    }

    // time signature first, since it decides how many steps there are
    const juce::String sigName = json["timeSignature"].toString();
    for (int i = 0; i < juce::numElementsInArray (timeSigs); ++i)
        if (sigName == timeSigs[i].name)
            meterBox.setSelectedItemIndex (i, juce::sendNotificationSync);

    setTempo ((double) json["tempo"]);
    swingSlider.setValue ((double) json["swing"], juce::sendNotificationSync);

    const auto steps  = json["steps"];
    const auto sounds = json["sounds"];
    const auto* mutedVoices = json["muted"].getArray();
    juce::StringArray missing;

    for (int v = 0; v < DrumEngine::numVoices; ++v)
    {
        const juce::Identifier id (DrumEngine::voiceId (v));

        const juce::String row = steps[id].toString();
        for (int st = 0; st < DrumEngine::maxSteps; ++st)
            engine.pattern[(size_t) v][(size_t) st].store (st < row.length() && row[st] == 'x');

        engine.muted[(size_t) v].store (mutedVoices != nullptr && mutedVoices->contains (juce::var (id.toString())));

        const juce::String path = sounds[id].toString();
        if (path.isEmpty())
            useBuiltInSound (v, false);
        else if (juce::File (path).existsAsFile())
            loadSample (v, juce::File (path), true, false);
        else
        {
            useBuiltInSound (v, false);
            missing.add (juce::File (path).getFileName());
        }
    }

    // per-sound volume and pitch (patterns saved before these existed get the defaults)
    const auto voiceSettings = json["voices"];
    for (int v = 0; v < DrumEngine::numVoices; ++v)
    {
        const auto vo = voiceSettings[juce::Identifier (DrumEngine::voiceId (v))];
        engine.voiceVolume[(size_t) v].store (vo.isObject() ? (float) (double) vo.getProperty ("volume", 1.0) : 1.0f);
        engine.voicePitch[(size_t) v].store (vo.isObject() ? (float) (double) vo.getProperty ("pitch", 0.0) : 0.0f);
    }

    // effects (older patterns keep whatever effects are set up now)
    const auto effects = json["effects"];
    if (effects.isObject())
    {
        auto indexOf = [] (const juce::String& name, int count, const char* (*nameOf) (int), int fallback)
        {
            for (int i = 0; i < count; ++i)
                if (name == nameOf (i)) return i;
            return fallback;
        };

        for (int f = 0; f < DrumEngine::numFx; ++f)
        {
            const auto fo = effects[juce::Identifier (DrumEngine::fxId (f))];
            if (! fo.isObject()) continue;

            if (f != DrumEngine::fxStutter)
                engine.fxOn[(size_t) f].store ((bool) fo.getProperty ("on", false));
            if (const auto* routed = fo["sounds"].getArray())
                for (int v = 0; v < DrumEngine::numVoices; ++v)
                    engine.fxSend[(size_t) f][(size_t) v].store (routed->contains (juce::var (DrumEngine::voiceId (v))));
        }

        auto num = [&effects] (int f, const char* key, std::atomic<float>& target)
        {
            const auto fo = effects[juce::Identifier (DrumEngine::fxId (f))];
            if (fo.isObject() && fo.hasProperty (key))
                target.store ((float) (double) fo.getProperty (key, 0.0));
        };
        num (DrumEngine::fxReverb, "amount",   engine.reverbAmount);
        num (DrumEngine::fxReverb, "size",     engine.reverbSize);
        num (DrumEngine::fxDelay,  "amount",   engine.delayAmount);
        num (DrumEngine::fxDelay,  "feedback", engine.delayFeedback);
        num (DrumEngine::fxComp,   "amount",   engine.compAmount);
        num (DrumEngine::fxComp,   "mix",      engine.compMix);
        num (DrumEngine::fxDist,   "drive",    engine.distDrive);
        num (DrumEngine::fxDist,   "tone",     engine.distTone);

        const auto delayName   = effects[juce::Identifier (DrumEngine::fxId (DrumEngine::fxDelay))]["time"].toString();
        const auto stutterName = effects[juce::Identifier (DrumEngine::fxId (DrumEngine::fxStutter))]["rate"].toString();
        engine.delayTime.store (indexOf (delayName, DrumEngine::numDelayTimes, &DrumEngine::delayTimeName, engine.delayTime.load()));
        engine.stutterRate.store (indexOf (stutterName, DrumEngine::numStutterRates, &DrumEngine::stutterRateName, engine.stutterRate.load()));
    }

    currentPatternName = json["name"].toString().isNotEmpty() ? json["name"].toString()
                                                               : file.getFileNameWithoutExtension();
    syncFxPanelFromEngine();
    grid.syncFromEngine();

    if (! missing.isEmpty())
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Some sounds are missing",
            "These files were moved or deleted, so the built-in sounds are used instead:\n\n" + missing.joinIntoString ("\n"));
}

//==============================================================================
// Export

void MainComponent::showExportMenu()
{
    juce::PopupMenu wav, midi;
    for (int bars : { 1, 2, 4, 8 })
    {
        wav.addItem (100 + bars, barsText (bars));
        midi.addItem (200 + bars, barsText (bars));
    }

    juce::PopupMenu m;
    m.addSubMenu ("Audio (WAV)", wav);
    m.addSubMenu ("MIDI file", midi);

    juce::Component::SafePointer<MainComponent> sp (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&exportButton),
                     [sp] (int result)
                     {
                         if (sp == nullptr || result == 0) return;
                         sp->chooseExportFile (result >= 200, result % 100);
                     });
}

void MainComponent::chooseExportFile (bool midi, int bars)
{
    const juce::String ext = midi ? ".mid" : ".wav";
    const juce::File folder (settings->getValue ("exportFolder",
                             juce::File::getSpecialLocation (juce::File::userMusicDirectory).getFullPathName()));
    const auto suggested = folder.getChildFile (juce::File::createLegalFileName (currentPatternName) + ext);

    chooser = std::make_unique<juce::FileChooser> (midi ? "Export MIDI" : "Export audio", suggested, "*" + ext);

    juce::Component::SafePointer<MainComponent> sp (this);
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [sp, midi, bars, ext] (const juce::FileChooser& fc)
                          {
                              if (sp == nullptr) return;
                              auto f = fc.getResult();
                              if (f == juce::File()) return;
                              f = f.withFileExtension (ext);
                              sp->settings->setValue ("exportFolder", f.getParentDirectory().getFullPathName());

                              const bool ok = midi ? sp->writeMidi (f, bars) : sp->writeWav (f, bars);
                              if (! ok)
                                  juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Export failed",
                                                                          "DR-16 couldn't write " + f.getFullPathName());
                              sp->grabKeyboardFocus();
                          });
}

bool MainComponent::writeWav (const juce::File& file, int bars)
{
    // Rendered offline by a second engine with the same pattern and sounds, so it is
    // faster than real time and doesn't interrupt what's playing.
    constexpr double rate = 48000.0;
    DrumEngine ex;
    ex.copySettingsFrom (engine);
    ex.fxOn[DrumEngine::fxStutter].store (false);   // stutter is a live effect, never baked into exports
    ex.prepare (rate);
    ex.playing.store (true);

    const double barLength = rate * 60.0 / (double) ex.tempo.load() / 4.0 * ex.numSteps.load();
    const int preRoll = (int) std::round (barLength);
    const int total   = (int) std::round (barLength * bars);

    juce::AudioBuffer<float> buffer (2, juce::jmax (preRoll, total));
    constexpr int block = 512;

    auto renderInto = [&] (int numSamples)
    {
        for (int pos = 0; pos < numSamples; pos += block)
        {
            const int n = juce::jmin (block, numSamples - pos);
            ex.render (buffer.getWritePointer (0, pos), buffer.getWritePointer (1, pos), n);
        }
    };

    // Play one bar first and throw it away: cymbal tails from the end of that bar
    // ring into the start of the export, so the file loops seamlessly in a DAW.
    renderInto (preRoll);
    buffer.clear();
    renderInto (total);

    file.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);
    auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}.withSampleRate (rate)
                                                                              .withNumChannels (2)
                                                                              .withBitsPerSample (24));
    return writer != nullptr && writer->writeFromAudioSampleBuffer (buffer, 0, total);
}

bool MainComponent::writeMidi (const juce::File& file, int bars)
{
    constexpr int ppq = 960;
    constexpr int ticksPerStep = ppq / 4;   // 16th notes
    constexpr int noteLength = ticksPerStep / 2;

    const auto& sig = timeSigs[juce::jmax (0, meterBox.getSelectedItemIndex())];
    const int steps = engine.numSteps.load();
    const int swingTicks = (int) std::round (DrumEngine::swingDelay (engine.swing.load()) * ticksPerStep);

    juce::MidiMessageSequence seq;
    seq.addEvent (juce::MidiMessage::textMetaEvent (3, currentPatternName), 0);   // track name
    seq.addEvent (juce::MidiMessage::tempoMetaEvent ((int) std::round (60000000.0 / engine.tempo.load())), 0);
    seq.addEvent (juce::MidiMessage::timeSignatureMetaEvent (sig.numerator, sig.denominator), 0);

    for (int bar = 0; bar < bars; ++bar)
        for (int st = 0; st < steps; ++st)
            for (int v = 0; v < DrumEngine::numVoices; ++v)
            {
                if (! engine.pattern[(size_t) v][(size_t) st].load() || engine.muted[(size_t) v].load())
                    continue;

                const int tick = (bar * steps + st) * ticksPerStep + (st % 2 == 1 ? swingTicks : 0);
                const int note = DrumEngine::voiceMidiNote (v);
                const int velocity = juce::jlimit (1, 127, juce::roundToInt (100.0f * engine.voiceVolume[(size_t) v].load()));
                seq.addEvent (juce::MidiMessage::noteOn (10, note, (juce::uint8) velocity), tick);   // channel 10 = drums
                seq.addEvent (juce::MidiMessage::noteOff (10, note), tick + noteLength);
            }

    seq.addEvent (juce::MidiMessage::endOfTrack(), bars * steps * ticksPerStep);   // clip length = whole bars
    seq.updateMatchedPairs();

    juce::MidiFile mf;
    mf.setTicksPerQuarterNote (ppq);
    mf.addTrack (seq);

    file.deleteFile();
    juce::FileOutputStream out (file);
    return out.openedOk() && mf.writeTo (out, 1);
}

//==============================================================================
// External MIDI: every connected input is enabled; notes follow the General MIDI
// drum map (36 kick, 38 snare, 42 closed hat...), velocity sets the loudness.

void MainComponent::refreshMidiInputs()
{
    juce::StringArray names;
    for (const auto& d : juce::MidiInput::getAvailableDevices())
    {
        if (! deviceManager.isMidiInputDeviceEnabled (d.identifier))
            deviceManager.setMidiInputDeviceEnabled (d.identifier, true);
        names.add (d.name);
    }

    const juce::String status = names.isEmpty() ? "MIDI IN: none" : "MIDI IN: " + names.joinIntoString (", ");
    if (status != midiStatus)
    {
        midiStatus = status;
        repaint (midiStatusArea);
    }
}

void MainComponent::handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& m)
{
    // runs on the MIDI thread: only touch atomics here
    if (! m.isNoteOn()) return;

    const int v = DrumEngine::voiceForMidiNote (m.getNoteNumber());
    if (v < 0) return;

    engine.liveHit[(size_t) v].store (0.15f + 0.85f * m.getFloatVelocity());
    midiFlash[(size_t) v].store (true);
}

//==============================================================================
// Effects panel

void MainComponent::buildFxPanel()
{
    using E = DrumEngine;
    auto percent = [] (double x) { return juce::String (juce::roundToInt (x * 100.0)) + "%"; };

    for (int f = 0; f < E::numFx; ++f)
    {
        fxBlocks[(size_t) f] = std::make_unique<FxBlock> (E::fxName (f), Colours::fx[f],
                                                          f == E::fxStutter ? "or hold Q" : juce::String());
        addAndMakeVisible (*fxBlocks[(size_t) f]);
        if (f == E::fxStutter)
            fxBlocks[(size_t) f]->power.onClick = [this] { stutterLatched = ! stutterLatched; updateStutter(); };
        else
            fxBlocks[(size_t) f]->power.onClick = [this, f] { setFxOn (f, ! engine.fxOn[(size_t) f].load()); };
    }

    auto knob = [this] (int f, const char* label, double lo, double hi, double step, std::atomic<float>& target,
                        std::function<juce::String (double)> text)
    {
        auto* k = &fxBlocks[(size_t) f]->addKnob (label, lo, hi, target.load(), step, std::move (text));
        k->onValueChange = [k, &target] { target.store ((float) k->getValue()); };
    };
    auto choice = [this] (int f, const char* label, int count, const char* (*nameOf) (int), std::atomic<int>& target)
    {
        auto* k = &fxBlocks[(size_t) f]->addKnob (label, 0, count - 1, target.load(), 1,
                                                  [nameOf] (double x) { return juce::String (nameOf (juce::roundToInt (x))); });
        k->onValueChange = [k, &target] { target.store (juce::roundToInt (k->getValue())); };
    };

    knob   (E::fxReverb,  "AMOUNT",   0, 1,   0.01, engine.reverbAmount,  percent);
    knob   (E::fxReverb,  "SIZE",     0, 1,   0.01, engine.reverbSize,    percent);
    knob   (E::fxDelay,   "AMOUNT",   0, 1,   0.01, engine.delayAmount,   percent);
    choice (E::fxDelay,   "TIME",     E::numDelayTimes, &E::delayTimeName, engine.delayTime);
    knob   (E::fxDelay,   "FEEDBACK", 0, 0.9, 0.01, engine.delayFeedback, percent);
    knob   (E::fxComp,    "AMOUNT",   0, 1,   0.01, engine.compAmount,    percent);
    knob   (E::fxComp,    "MIX",      0, 1,   0.01, engine.compMix,       percent);
    knob   (E::fxDist,    "DRIVE",    0, 1,   0.01, engine.distDrive,     percent);
    knob   (E::fxDist,    "TONE",     0, 1,   0.01, engine.distTone,      percent);
    choice (E::fxStutter, "RATE",     E::numStutterRates, &E::stutterRateName, engine.stutterRate);

    fxBlocks[E::fxStutter]->power.setButtonText ("ON");
}

void MainComponent::setFxOn (int fx, bool on)
{
    engine.fxOn[(size_t) fx].store (on);
    fxBlocks[(size_t) fx]->setOn (on);
    grid.repaint();   // the routing column for this effect lights up / dims
}

void MainComponent::updateStutter()
{
    const bool on = stutterLatched || stutterKeyHeld;
    engine.fxOn[DrumEngine::fxStutter].store (on);
    fxBlocks[DrumEngine::fxStutter]->setOn (on);
    grid.repaint();
}

void MainComponent::syncFxPanelFromEngine()
{
    using E = DrumEngine;
    auto set = [this] (int f, size_t knobIndex, double value)
    {
        fxBlocks[(size_t) f]->knobs[knobIndex]->setValue (value, juce::dontSendNotification);
    };
    set (E::fxReverb, 0, engine.reverbAmount.load());   set (E::fxReverb, 1, engine.reverbSize.load());
    set (E::fxDelay, 0, engine.delayAmount.load());     set (E::fxDelay, 1, engine.delayTime.load());
    set (E::fxDelay, 2, engine.delayFeedback.load());
    set (E::fxComp, 0, engine.compAmount.load());       set (E::fxComp, 1, engine.compMix.load());
    set (E::fxDist, 0, engine.distDrive.load());        set (E::fxDist, 1, engine.distTone.load());
    set (E::fxStutter, 0, engine.stutterRate.load());

    for (int f = 0; f < E::numFx; ++f)
        if (f != E::fxStutter)
            fxBlocks[(size_t) f]->setOn (engine.fxOn[(size_t) f].load());
    grid.repaint();
}
