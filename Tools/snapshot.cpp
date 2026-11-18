// Renders the plugin editor to a PNG without a host.
//
//   hdp_snapshot out.png [demo|solo|empty|piano|multi|dynamic|grab|sidechain|collision|sketch|match|interact|history|features|selftest] [selected band, 1-based] [width height]
//
// Runs some audio through the processor first so the analyzer has something to show.

#include "../Source/PluginEditor.h"
#include "../Source/PluginProcessor.h"
#include "../Source/dsp/SketchFit.h"

using namespace hdp;

static void setBand (HolterdipolterFilterProcessor& proc, int band, FilterType type, float freq, float gain,
                     float q, int slopeIndex, Placement placement, bool enabled = true)
{
    const auto set = [&] (params::BandField field, float value)
    {
        HolterdipolterFilterProcessor::setParam (proc.bandParam (band, field), value);
    };

    set (params::Type, (float) (int) type);
    set (params::Freq, freq);
    set (params::Gain, gain);
    set (params::Q, q);
    set (params::Slope, (float) slopeIndex);
    set (params::Place, (float) (int) placement);
    set (params::On, enabled ? 1.0f : 0.0f);
    set (params::Used, 1.0f);
}

/** Checks the processor glue that the plain DSP tests cannot see. */
static int runProcessorCheck()
{
    int failures = 0;
    const auto check = [&failures] (bool ok, const char* what)
    {
        std::printf ("  %s %s\n", ok ? "ok  " : "FAIL", what);
        if (! ok) ++failures;
    };

    juce::MidiBuffer midi;
    juce::Random random (3);

    // No bands, unity output: the plugin must pass audio through bit for bit.
    {
        HolterdipolterFilterProcessor proc;
        proc.setRateAndBufferSizeDetails (44100.0, 256);
        proc.prepareToPlay (44100.0, 256);

        juce::AudioBuffer<float> in (2, 256), out;
        bool identical = true;

        for (int block = 0; block < 20; ++block)
        {
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 256; ++i)
                    in.setSample (ch, i, random.nextFloat() * 2.0f - 1.0f);

            out.makeCopyOf (in);
            proc.processBlock (out, midi);

            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 256; ++i)
                    identical = identical && juce::exactlyEqual (in.getSample (ch, i), out.getSample (ch, i));
        }

        check (identical, "empty EQ is bit-transparent");
    }

    // State round trip, including the view settings.
    {
        HolterdipolterFilterProcessor a, b;
        setBand (a, 0, FilterType::LowCut, 41.0f, 0.0f, 1.3f, 5, Placement::Stereo);
        setBand (a, 7, FilterType::Bell, 2345.0f, -7.5f, 3.3f, 3, Placement::Side, false);
        setBand (a, 23, FilterType::TiltShelf, 900.0f, 2.5f, 1.0f, 0, Placement::Left);
        HolterdipolterFilterProcessor::setParam (a.outGainParam(), -3.5f);
        HolterdipolterFilterProcessor::setParam (a.gainScaleParam(), 150.0f);
        a.apvts.state.setProperty ("uiRange", 2, nullptr);

        juce::MemoryBlock block;
        a.getStateInformation (block);
        b.setStateInformation (block.getData(), (int) block.getSize());

        bool same = true;

        for (int band = 0; band < kMaxBands; ++band)
        {
            const auto pa = a.getBandParams (band), pb = b.getBandParams (band);
            same = same && a.isBandUsed (band) == b.isBandUsed (band) && pa.active == pb.active
                        && pa.type == pb.type && pa.placement == pb.placement && pa.slopeIndex == pb.slopeIndex
                        && std::abs (pa.freq - pb.freq) < 0.01 && std::abs (pa.gainDb - pb.gainDb) < 0.001
                        && std::abs (pa.q - pb.q) < 0.001;
        }

        check (same, "all 24 bands survive save and restore");
        check (std::abs (b.getGainScale() - 1.5f) < 0.001f
               && std::abs (b.outGainParam()->convertFrom0to1 (b.outGainParam()->getValue()) + 3.5f) < 0.001f,
               "gain scale and output gain survive save and restore");
        check ((int) b.apvts.state.getProperty ("uiRange", -1) == 2, "view settings survive save and restore");

        const char garbage[] = "this is not a saved state";
        b.setStateInformation (garbage, (int) sizeof (garbage));
        check (b.isBandUsed (7), "garbage state data is ignored");
    }

    // Mono, odd block sizes, several sample rates: finite output, silence stays silent.
    {
        bool finite = true, silent = true, layoutsOk = true;

        for (double rate : { 22050.0, 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 })
        {
            for (int channels : { 1, 2 })
            {
                HolterdipolterFilterProcessor proc;
                const auto set = channels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();
                juce::AudioProcessor::BusesLayout layout;
                layout.inputBuses.add (set);
                layout.inputBuses.add (juce::AudioChannelSet::disabled());   // no sidechain
                layout.outputBuses.add (set);
                layoutsOk = layoutsOk && proc.setBusesLayout (layout);

                setBand (proc, 0, FilterType::LowCut, 25.0f, 0.0f, 1.0f, 7, Placement::Stereo);
                setBand (proc, 1, FilterType::Bell, 18000.0f, 12.0f, 6.0f, 7, Placement::Side);
                setBand (proc, 2, FilterType::HighCut, 29000.0f, 0.0f, 4.0f, 7, Placement::Mid);
                setBand (proc, 3, FilterType::Notch, 60.0f, 0.0f, 30.0f, 3, Placement::Right);

                proc.setRateAndBufferSizeDetails (rate, 512);
                proc.prepareToPlay (rate, 512);

                for (int size : { 1, 7, 33, 64, 511, 512, 2048 })
                {
                    juce::AudioBuffer<float> buffer (channels, size);

                    for (int ch = 0; ch < channels; ++ch)
                        for (int i = 0; i < size; ++i)
                            buffer.setSample (ch, i, random.nextFloat() - 0.5f);

                    proc.processBlock (buffer, midi);

                    for (int ch = 0; ch < channels; ++ch)
                        for (int i = 0; i < size; ++i)
                            finite = finite && std::isfinite (buffer.getSample (ch, i));
                }

                // Let it ring out on silence, then the output must have died away.
                juce::AudioBuffer<float> quiet (channels, 512);

                for (int block = 0; block < (int) (rate * 4.0 / 512.0); ++block)
                {
                    quiet.clear();
                    proc.processBlock (quiet, midi);
                }

                silent = silent && quiet.getMagnitude (0, 512) < 1.0e-6f;
            }
        }

        check (layoutsOk, "mono and stereo layouts are accepted");
        check (finite, "finite output at 22.05 to 192 kHz, mono and stereo, block sizes 1 to 2048");
        check (silent, "filters ring out to silence");

        HolterdipolterFilterProcessor proc;
        juce::AudioProcessor::BusesLayout bad;
        bad.inputBuses.add (juce::AudioChannelSet::mono());
        bad.inputBuses.add (juce::AudioChannelSet::disabled());
        bad.outputBuses.add (juce::AudioChannelSet::stereo());
        check (! proc.checkBusesLayoutSupported (bad), "mono-in / stereo-out is refused");
    }

    // Solo and auto gain reach the engine.
    {
        HolterdipolterFilterProcessor proc;
        setBand (proc, 2, FilterType::Bell, 1000.0f, 12.0f, 2.0f, 1, Placement::Stereo);
        proc.setRateAndBufferSizeDetails (48000.0, 512);
        proc.prepareToPlay (48000.0, 512);

        const auto levelOf100Hz = [&proc, &midi]
        {
            juce::AudioBuffer<float> buffer (2, 512);
            double phase = 0.0;
            float peak = 0.0f;

            for (int block = 0; block < 60; ++block)
            {
                for (int i = 0; i < 512; ++i)
                {
                    const float v = 0.5f * (float) std::sin (phase);
                    phase += juce::MathConstants<double>::twoPi * 100.0 / 48000.0;
                    buffer.setSample (0, i, v);
                    buffer.setSample (1, i, v);
                }

                proc.processBlock (buffer, midi);
                peak = buffer.getMagnitude (0, 512);
            }

            return peak;
        };

        check (std::abs (levelOf100Hz() - 0.5f) < 0.02f, "a 1 kHz bell leaves a 100 Hz tone alone");

        proc.soloBand.store (2);
        check (levelOf100Hz() < 0.01f, "with that band soloed the 100 Hz tone is gone");

        proc.soloBand.store (9);   // not in use
        check (std::abs (levelOf100Hz() - 0.5f) < 0.02f, "solo on a band that does not exist is ignored");

        proc.soloBand.store (-1);
        setBand (proc, 3, FilterType::HighShelf, 2000.0f, 12.0f, 1.0f, 1, Placement::Stereo);
        HolterdipolterFilterProcessor::setParam (proc.autoGainParam(), 1.0f);
        levelOf100Hz();
        check (proc.autoGainDb.load() < -3.0f, "auto gain reports a clear reduction for a big boost");

        HolterdipolterFilterProcessor::setParam (proc.autoGainParam(), 0.0f);
        levelOf100Hz();
        check (std::abs (proc.autoGainDb.load()) < 1.0e-6f, "auto gain off: no compensation");
    }

    // Sidechain: looked at, never heard, and optional.
    {
        HolterdipolterFilterProcessor proc;
        proc.setRateAndBufferSizeDetails (48000.0, 256);
        proc.prepareToPlay (48000.0, 256);
        proc.analyzerEnabled.store (true);

        juce::AudioBuffer<float> buffer (4, 256), before;
        bool mainUntouched = true, sideUntouched = true;

        for (int block = 0; block < 10; ++block)
        {
            for (int ch = 0; ch < 4; ++ch)
                for (int i = 0; i < 256; ++i)
                    buffer.setSample (ch, i, random.nextFloat() * 2.0f - 1.0f);

            before.makeCopyOf (buffer);
            proc.processBlock (buffer, midi);

            for (int i = 0; i < 256; ++i)
            {
                mainUntouched = mainUntouched && juce::exactlyEqual (buffer.getSample (0, i), before.getSample (0, i))
                                              && juce::exactlyEqual (buffer.getSample (1, i), before.getSample (1, i));
                sideUntouched = sideUntouched && juce::exactlyEqual (buffer.getSample (2, i), before.getSample (2, i))
                                              && juce::exactlyEqual (buffer.getSample (3, i), before.getSample (3, i));
            }
        }

        float seen[256];
        proc.sideTap.copyLatest (seen, 256);
        bool tapMatches = true;

        for (int i = 0; i < 256; ++i)
            tapMatches = tapMatches && std::abs (seen[i] - 0.5f * (before.getSample (2, i) + before.getSample (3, i))) < 1.0e-6f;

        check (mainUntouched && sideUntouched, "with a sidechain connected, an empty EQ still passes everything bit for bit");
        check (proc.sidechainConnected.load() && tapMatches, "the analyzer gets to see the sidechain");

        juce::AudioBuffer<float> plain (2, 256);
        plain.clear();
        proc.processBlock (plain, midi);
        check (! proc.sidechainConnected.load(), "a host that sends no sidechain channels is recognised");

        // The sidechain may be stereo, mono or absent; anything else is refused.
        const auto layoutWith = [] (juce::AudioChannelSet side)
        {
            juce::AudioProcessor::BusesLayout layout;
            layout.inputBuses.add (juce::AudioChannelSet::stereo());
            layout.inputBuses.add (side);
            layout.outputBuses.add (juce::AudioChannelSet::stereo());
            return layout;
        };

        check (proc.checkBusesLayoutSupported (layoutWith (juce::AudioChannelSet::stereo()))
               && proc.checkBusesLayoutSupported (layoutWith (juce::AudioChannelSet::mono()))
               && proc.checkBusesLayoutSupported (layoutWith (juce::AudioChannelSet::disabled()))
               && ! proc.checkBusesLayoutSupported (layoutWith (juce::AudioChannelSet::create5point1())),
               "sidechain layouts: stereo, mono or none");

        // Mono track with a mono sidechain.
        HolterdipolterFilterProcessor mono;
        juce::AudioProcessor::BusesLayout layout;
        layout.inputBuses.add (juce::AudioChannelSet::mono());
        layout.inputBuses.add (juce::AudioChannelSet::mono());
        layout.outputBuses.add (juce::AudioChannelSet::mono());
        const bool accepted = mono.setBusesLayout (layout);
        mono.setRateAndBufferSizeDetails (48000.0, 256);
        mono.prepareToPlay (48000.0, 256);
        mono.analyzerEnabled.store (true);

        juce::AudioBuffer<float> two (2, 256);
        for (int i = 0; i < 256; ++i) { two.setSample (0, i, 0.25f); two.setSample (1, i, -0.5f); }
        mono.processBlock (two, midi);
        mono.sideTap.copyLatest (seen, 4);
        check (accepted && mono.sidechainConnected.load() && std::abs (seen[3] + 0.5f) < 1.0e-6f
               && std::abs (two.getSample (0, 255) - 0.25f) < 1.0e-6f, "a mono track with a mono sidechain works too");
    }

    // Phase invert reaches the engine; a 0.2 project still loads.
    {
        HolterdipolterFilterProcessor proc;
        proc.setRateAndBufferSizeDetails (48000.0, 256);
        proc.prepareToPlay (48000.0, 256);
        HolterdipolterFilterProcessor::setParam (proc.phaseInvertParam(), 1.0f);

        juce::AudioBuffer<float> in (2, 256), out;
        float worst = 0.0f;

        for (int block = 0; block < 100; ++block)
        {
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 256; ++i)
                    in.setSample (ch, i, random.nextFloat() * 2.0f - 1.0f);

            out.makeCopyOf (in);
            proc.processBlock (out, midi);

            if (block > 90)
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < 256; ++i)
                        worst = juce::jmax (worst, std::abs (in.getSample (ch, i) + out.getSample (ch, i)));
        }

        check (worst < 1.0e-5f, "phase invert turns the signal upside down, nothing else");

        // A state as 0.2 wrote it: nine shapes, no phase invert, the old range index.
        const char* old = "<HolterdipolterFilter uiRange=\"2\"><PARAM id=\"b1_used\" value=\"1.0\"/><PARAM id=\"b1_type\" value=\"8.0\"/>"
                          "<PARAM id=\"b1_freq\" value=\"321.0\"/><PARAM id=\"b2_used\" value=\"1.0\"/><PARAM id=\"b2_type\" value=\"7.0\"/></HolterdipolterFilter>";
        juce::MemoryBlock block;
        juce::AudioProcessor::copyXmlToBinary (*juce::parseXML (old), block);

        HolterdipolterFilterProcessor fresh;
        fresh.setStateInformation (block.getData(), (int) block.getSize());
        check (fresh.getBandParams (0).type == FilterType::AllPass && fresh.getBandParams (1).type == FilterType::TiltShelf
               && std::abs (fresh.getBandParams (0).freq - 321.0) < 0.01 && fresh.phaseInvertParam()->getValue() < 0.5f
               && fresh.presets.getCurrentName().isEmpty(), "a project saved with 0.2 loads with the same shapes");
    }

    std::printf ("%d processor failures\n", failures);
    return failures == 0 ? 0 : 1;
}

/** Undo and redo, driven through the display like a user would. */
static int runHistoryCheck (HolterdipolterFilterProcessor& proc, juce::AudioProcessorEditor& editor)
{
    ui::EqDisplay* display = nullptr;

    for (auto* child : editor.getChildren())
        if (auto* d = dynamic_cast<ui::EqDisplay*> (child))
            display = d;

    if (display == nullptr)
        return 1;

    int failures = 0;
    const auto check = [&failures] (bool ok, const char* what)
    {
        std::printf ("  %s %s\n", ok ? "ok  " : "FAIL", what);
        if (! ok) ++failures;
    };

    // Steps are recorded when the message loop comes round, as in a real host.
    const auto pump = [] { juce::MessageManager::getInstance()->runDispatchLoopUntil (40); };

    const auto source = juce::Desktop::getInstance().getMainMouseSource();
    const auto event = [&] (juce::Point<float> pos, juce::Point<float> downPos, int clicks, bool dragged)
    {
        return juce::MouseEvent (source, pos, juce::ModifierKeys::leftButtonModifier, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                 display, display, juce::Time::getCurrentTime(), downPos, juce::Time::getCurrentTime(), clicks, dragged);
    };

    auto& history = proc.history;
    const auto bounds = display->getLocalBounds().toFloat();
    const juce::Point<float> a (bounds.getWidth() * 0.4f, bounds.getHeight() * 0.3f);
    const juce::Point<float> b (bounds.getWidth() * 0.6f, bounds.getHeight() * 0.7f);

    pump();
    check (! history.canUndo() && ! history.canRedo(), "a fresh instance has nothing to undo");

    // Add a band: eight parameters change, one step.
    display->mouseDown (event (a, a, 2, false));
    display->mouseDoubleClick (event (a, a, 2, false));
    display->mouseUp (event (a, a, 2, false));
    pump();

    const auto added = proc.getBandParams (0);
    check (proc.isBandUsed (0) && history.canUndo(), "adding a band is recorded");

    history.undo();
    check (! proc.isBandUsed (0), "one undo removes the whole band again");
    check (! history.canUndo() && history.canRedo(), "adding a band was a single step");

    history.redo();
    const auto redone = proc.getBandParams (0);
    check (proc.isBandUsed (0) && std::abs (redone.freq - added.freq) < 0.01 && std::abs (redone.gainDb - added.gainDb) < 0.001,
           "redo brings it back exactly as it was");

    // Drag it: many value changes, one step.
    display->mouseDown (event (a, a, 1, false));
    for (int i = 1; i <= 10; ++i)
        display->mouseDrag (event (a + (b - a) * ((float) i / 10.0f), a, 1, true));
    display->mouseUp (event (b, a, 1, true));
    pump();

    const auto dragged = proc.getBandParams (0);
    check (dragged.freq > added.freq * 1.5, "the drag moved the band");

    history.undo();
    const auto undragged = proc.getBandParams (0);
    check (proc.isBandUsed (0) && std::abs (undragged.freq - added.freq) < 0.01 && std::abs (undragged.gainDb - added.gainDb) < 0.001,
           "one undo takes back the whole drag, and only the drag");

    history.redo();
    check (std::abs (proc.getBandParams (0).freq - dragged.freq) < 0.01, "redo repeats the drag");

    // Three wheel ticks in quick succession count as one step.
    juce::MouseWheelDetails wheel;
    wheel.deltaX = 0.0f; wheel.deltaY = 0.2f; wheel.isReversed = false; wheel.isSmooth = false; wheel.isInertial = false;

    const double qBefore = proc.getBandParams (0).q;

    for (int i = 0; i < 3; ++i)
    {
        display->mouseWheelMove (event (b, b, 0, false), wheel);
        pump();
    }

    check (proc.getBandParams (0).q > qBefore * 2.0, "the wheel raised Q three times");
    history.undo();
    check (std::abs (proc.getBandParams (0).q - qBefore) < 1.0e-4, "one undo takes back all three wheel ticks");

    // A new edit after an undo discards the redo branch.
    HolterdipolterFilterProcessor::setParam (proc.outGainParam(), -5.0f);
    pump();
    check (! history.canRedo() && history.canUndo(), "a new edit discards what could have been redone");

    // Changes that arrive without a gesture (automation) become their own step.
    proc.gainScaleParam()->setValueNotifyingHost (proc.gainScaleParam()->convertTo0to1 (50.0f));
    HolterdipolterFilterProcessor::setParam (proc.outGainParam(), 3.0f);
    pump();
    history.undo();
    check (std::abs (proc.getGainScale() - 0.5f) < 0.001f
           && std::abs (proc.outGainParam()->convertFrom0to1 (proc.outGainParam()->getValue()) + 5.0f) < 0.01f,
           "undo takes back the edit but keeps what automation changed before it");

    // Loading a state starts a new history.
    juce::MemoryBlock block;
    proc.getStateInformation (block);
    proc.setStateInformation (block.getData(), (int) block.getSize());
    pump();
    check (! history.canUndo() && ! history.canRedo(), "loading a state clears the history");

    HolterdipolterFilterProcessor::setParam (proc.outGainParam(), 1.0f);
    pump();
    history.undo();
    check (proc.isBandUsed (0) && std::abs (proc.outGainParam()->convertFrom0to1 (proc.outGainParam()->getValue()) + 5.0f) < 0.01f,
           "after loading, undo goes back to the loaded state and no further");

    std::printf ("%d history failures\n", failures);
    return failures == 0 ? 0 : 1;
}

/** Drives the display with synthetic mouse events and checks what the parameters do. */
static int runInteractionCheck (HolterdipolterFilterProcessor& proc, juce::AudioProcessorEditor& editor)
{
    ui::EqDisplay* display = nullptr;

    for (auto* child : editor.getChildren())
        if (auto* d = dynamic_cast<ui::EqDisplay*> (child))
            display = d;

    if (display == nullptr)
        return 1;

    int failures = 0;
    const auto check = [&failures] (bool ok, const char* what)
    {
        std::printf ("  %s %s\n", ok ? "ok  " : "FAIL", what);
        if (! ok) ++failures;
    };

    const auto source = juce::Desktop::getInstance().getMainMouseSource();
    const auto event = [&] (juce::Point<float> pos, juce::Point<float> downPos, int clicks, bool dragged,
                            juce::ModifierKeys mods = juce::ModifierKeys::leftButtonModifier)
    {
        return juce::MouseEvent (source, pos, mods, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, display, display,
                                 juce::Time::getCurrentTime(), downPos, juce::Time::getCurrentTime(), clicks, dragged);
    };

    const auto bounds = display->getLocalBounds().toFloat();
    const juce::Point<float> a (bounds.getWidth() * 0.5f, bounds.getHeight() * 0.3f);
    const juce::Point<float> b (bounds.getWidth() * 0.7f, bounds.getHeight() * 0.65f);

    // Double-click on empty space: mouseDown, mouseUp, mouseDown, mouseDoubleClick, (drag), mouseUp.
    display->mouseDown (event (a, a, 1, false));
    display->mouseUp (event (a, a, 1, false));
    display->mouseDown (event (a, a, 2, false));
    display->mouseDoubleClick (event (a, a, 2, false));

    check (proc.isBandUsed (0), "double-click adds band 1");
    check (display->getSelectedBand() == 0, "new band is selected");

    const auto added = proc.getBandParams (0);
    check (added.type == FilterType::Bell && added.gainDb > 1.0, "added in the upper half: bell with positive gain");
    std::printf ("       band 1 at %.0f Hz, %+.1f dB\n", added.freq, added.gainDb);

    // Still holding: drag down and to the right.
    display->mouseDrag (event (b, a, 2, true));
    display->mouseUp (event (b, a, 2, true));

    const auto moved = proc.getBandParams (0);
    check (moved.freq > added.freq * 2.0, "dragging right raises the frequency");
    check (moved.gainDb < -1.0, "dragging below the centre makes it a cut");
    std::printf ("       band 1 at %.0f Hz, %+.1f dB\n", moved.freq, moved.gainDb);

    // Shift-drag is five times finer.
    display->mouseDown (event (b, b, 1, false));
    display->mouseDrag (event (b + juce::Point<float> (50.0f, 0.0f), b, 1, true,
                               juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::shiftModifier)));
    display->mouseUp (event (b, b, 1, true));

    const double fineRatio = proc.getBandParams (0).freq / moved.freq;
    display->mouseDown (event (b, b, 1, false));
    display->mouseUp (event (b, b, 1, false));
    check (fineRatio > 1.0 && fineRatio < 1.15, "shift-drag moves in fine steps");

    // Alt-click bypasses; the wheel over the node changes Q.
    const auto current = proc.getBandParams (0);
    juce::MouseWheelDetails wheel;
    wheel.deltaX = 0.0f; wheel.deltaY = 0.25f; wheel.isReversed = false; wheel.isSmooth = false; wheel.isInertial = false;

    // Find the node by scanning: the display exposes no geometry, so probe with alt-clicks along the drag row.
    bool bypassed = false;
    juce::Point<float> hit;

    for (float dx = 0.0f; dx <= 20.0f && ! bypassed; dx += 2.0f)
    {
        hit = b + juce::Point<float> (dx, 0.0f);
        display->mouseDown (event (hit, hit, 1, false,
                                   juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::altModifier)));
        display->mouseUp (event (hit, hit, 1, false));
        bypassed = ! proc.getBandParams (0).active;
    }

    check (bypassed && proc.isBandUsed (0), "alt-click bypasses the band without removing it");

    display->mouseWheelMove (event (hit, hit, 0, false, juce::ModifierKeys()), wheel);
    check (proc.getBandParams (0).q > current.q * 1.2, "mouse wheel over a node raises Q");

    // Far left edge gives a low cut whose vertical drag sets Q.
    const juce::Point<float> edge (bounds.getWidth() * 0.06f, bounds.getHeight() * 0.5f);
    display->mouseDown (event (edge, edge, 2, false));
    display->mouseDoubleClick (event (edge, edge, 2, false));
    const auto cut = proc.getBandParams (1);
    display->mouseDrag (event (edge + juce::Point<float> (0.0f, -60.0f), edge, 2, true));
    display->mouseUp (event (edge, edge, 2, true));

    check (proc.isBandUsed (1) && cut.type == FilterType::LowCut, "double-click at the left edge adds a low cut");
    check (proc.getBandParams (1).q > cut.q * 1.5, "dragging a cut upwards adds resonance");

    // Delete key removes the selected band; double-click on a node removes that one.
    display->keyPressed (juce::KeyPress (juce::KeyPress::deleteKey));
    check (! proc.isBandUsed (1) && display->getSelectedBand() == -1, "delete key removes the selected band");

    display->mouseDown (event (hit, hit, 2, false));
    display->mouseDoubleClick (event (hit, hit, 2, false));
    display->mouseUp (event (hit, hit, 2, false));
    check (! proc.isBandUsed (0), "double-click on a node removes it");

    // Fill all 24 bands, the 25th double-click must do nothing.
    for (int i = 0; i < kMaxBands + 1; ++i)
    {
        const juce::Point<float> p (bounds.getWidth() * (0.15f + 0.028f * (float) i), bounds.getHeight() * 0.2f);
        display->mouseDown (event (p, p, 2, false));
        display->mouseDoubleClick (event (p, p, 2, false));
        display->mouseUp (event (p, p, 2, false));
    }

    check (proc.findFreeBand() == -1, "24 bands can be created, a 25th is ignored");

    std::printf ("%d interaction failures\n", failures);
    return failures == 0 ? 0 : 1;
}

/** What 0.3 added: presets, A/B, selecting several bands, copy and paste, the piano. */
static int runFeatureCheck (HolterdipolterFilterProcessor& proc, HolterdipolterFilterEditor& editor)
{
    auto* display = &editor.getDisplay();

    int failures = 0;
    const auto check = [&failures] (bool ok, const char* what)
    {
        std::printf ("  %s %s\n", ok ? "ok  " : "FAIL", what);
        if (! ok) ++failures;
    };

    const auto pump = [] { juce::MessageManager::getInstance()->runDispatchLoopUntil (40); };
    const auto outGain = [&proc] { return proc.outGainParam()->convertFrom0to1 (proc.outGainParam()->getValue()); };

    const auto source = juce::Desktop::getInstance().getMainMouseSource();
    const auto event = [&] (juce::Point<float> pos, juce::Point<float> downPos, int clicks, bool dragged,
                            juce::ModifierKeys mods = juce::ModifierKeys::leftButtonModifier)
    {
        return juce::MouseEvent (source, pos, mods, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, display, display,
                                 juce::Time::getCurrentTime(), downPos, juce::Time::getCurrentTime(), clicks, dragged);
    };

    auto& presets = proc.presets;
    const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("hdp-preset-test");
    folder.deleteRecursively();
    presets.setDirectory (folder);

    //==========================================================================
    std::printf (" presets\n");
    pump();
    check (presets.getCurrentName().isEmpty() && ! presets.isModified(), "a fresh instance is the unmodified default");

    setBand (proc, 0, FilterType::Bell, 200.0f, 3.0f, 2.0f, 1, Placement::Stereo);
    setBand (proc, 1, FilterType::FlatTilt, 1000.0f, -4.0f, 1.0f, 1, Placement::Mid);
    setBand (proc, 2, FilterType::HighShelf, 8000.0f, 2.0f, 1.0f, 1, Placement::Stereo);
    HolterdipolterFilterProcessor::setParam (proc.phaseInvertParam(), 1.0f);
    pump();
    check (presets.isModified(), "adding bands marks the setting as modified");

    check (presets.save ("  Bass: Fix?  ") && presets.getCurrentName() == PresetManager::cleanName ("Bass: Fix?")
           && ! presets.getCurrentName().containsAnyOf (":?/"), "saving cleans the name into a legal file name");
    check (presets.save ("Erster") && presets.getCurrentName() == "Erster" && ! presets.isModified(), "save: named and unmodified");
    check (folder.getChildFile ("Erster.hdpfilter").existsAsFile(), "the preset is a file in the preset folder");

    HolterdipolterFilterProcessor::setParam (proc.bandParam (0, params::Gain), -9.0f);
    HolterdipolterFilterProcessor::setParam (proc.outGainParam(), -2.0f);
    check (presets.isModified(), "an edit after saving shows as modified");
    check (presets.save ("Zweiter") && presets.getPresetNames().size() == 3, "a second preset; three in the list");

    display->removeBand (1);
    pump();
    check (presets.load ("Erster"), "loading the first preset");
    const auto tilt = proc.getBandParams (1);
    check (proc.isBandUsed (1) && tilt.type == FilterType::FlatTilt && std::abs (tilt.gainDb + 4.0) < 0.001
           && std::abs (proc.getBandParams (0).gainDb - 3.0) < 0.001 && std::abs (outGain()) < 0.001f
           && proc.phaseInvertParam()->getValue() > 0.5f && ! presets.isModified(),
           "it restores every band, the output gain and the polarity");

    pump();
    proc.history.undo();
    check (! proc.isBandUsed (1) && std::abs (outGain() + 2.0f) < 0.001f, "one undo takes back loading a preset");
    check (presets.isModified(), "and the preset then shows as modified");
    proc.history.redo();
    check (! presets.isModified(), "redo makes it the unmodified preset again");

    check (presets.step (+1) && presets.getCurrentName() == "Zweiter" && std::abs (outGain() + 2.0f) < 0.001f, "next preset");
    check (presets.step (+1) && presets.isCurrentFactory() && presets.getCurrentName() == "Low Cut 30 Hz",
           "after the last of your own presets, stepping wraps around to the first factory preset");
    check (presets.step (-1) && ! presets.isCurrentFactory() && presets.getCurrentName() == "Zweiter", "previous preset");

    // Saving over another preset has to be confirmed.
    presets.load ("Erster");
    auto& bar = editor.getPresetBar();
    check (! bar.commitName ("zweiter"), "typing the name of another preset asks first");
    check (bar.commitName ("zweiter") && presets.getPresetNames().size() == 3, "the second Return replaces it");
    check (bar.commitName ("Dritter") && presets.getPresetNames().size() == 4 && presets.getCurrentName() == "Dritter",
           "a new name is saved at once");

    check (presets.remove ("Dritter") && presets.getCurrentName().isEmpty() && proc.isBandUsed (1),
           "deleting the current preset keeps the setting");
    check (! presets.load ("gibt es nicht") && ! presets.save ("  "), "missing presets and empty names are refused");

    folder.getChildFile ("Kaputt.hdpfilter").replaceWithText ("<nonsense>");
    check (! presets.load ("Kaputt") && proc.isBandUsed (1), "a broken preset file changes nothing");

    //==========================================================================
    std::printf (" factory presets\n");
    {
        const auto groups = PresetManager::getFactoryGroups();
        const auto all = PresetManager::getFactoryNames ({});
        check (groups.size() == 5 && all.size() == 18, "18 factory presets in 5 groups");
        check (all.contains (juce::String::fromUTF8 ("Sch\xc3\xa4rfe z\xc3\xa4hmen")) && groups.contains (juce::String::fromUTF8 ("H\xc3\xb6hen")),
               "umlauts in the names arrive intact");

        bool allLoad = true, allSane = true;

        for (const auto& name : all)
        {
            allLoad = allLoad && presets.loadFactory (name) && presets.getCurrentName() == name
                              && presets.isCurrentFactory() && ! presets.isModified();

            int used = 0;

            for (int band = 0; band < kMaxBands; ++band)
            {
                if (! proc.isBandUsed (band))
                    continue;

                ++used;
                const auto p = proc.getBandParams (band);
                allSane = allSane && p.active && p.freq >= 20.0 && p.freq <= 20000.0 && std::abs (p.gainDb) <= 6.0
                                  && std::abs (p.dynRangeDb) <= 9.0 && (isExactlyZero (p.dynRangeDb) || typeHasDynamics (p.type));
            }

            allSane = allSane && used >= 1 && used <= 5 && std::abs (outGain()) < 0.001f;
        }

        check (allLoad, "every factory preset loads, named and unmodified");
        check (allSane, "every one of them has one to five bands with moderate values");

        presets.loadFactory ("Bass mono");
        const auto mono = proc.getBandParams (0);
        check (mono.type == FilterType::LowCut && mono.placement == Placement::Side && std::abs (mono.freq - 120.0) < 0.01
               && ! proc.isBandUsed (1), "'Bass mono' is a low cut at 120 Hz on the side signal, and nothing else");

        presets.loadFactory ("Schranz Kick");
        check (proc.isBandUsed (3) && ! proc.isBandUsed (4) && std::abs (proc.getBandParams (2).dynRangeDb + 5.0) < 0.001
               && proc.getBandParams (2).autoThreshold, "'Schranz Kick' has four bands, one of them dynamic");

        // Part of the plugin: not to be saved over or deleted; stepping walks through them first.
        HolterdipolterFilterProcessor::setParam (proc.bandParam (0, params::Freq), 50.0f);
        check (presets.isModified() && presets.isCurrentFactory(), "an edited factory preset shows as modified");
        check (bar.commitName ("Meine Kick") && ! presets.isCurrentFactory() && presets.getCurrentName() == "Meine Kick",
               "'Save as' turns it into a preset of your own");
        check (! presets.remove ("Schranz Kick"), "a factory preset cannot be deleted");

        presets.loadFactory (all[all.size() - 1]);
        check (presets.step (+1) && ! presets.isCurrentFactory(), "after the last factory preset comes the first of your own");
        check (presets.step (-1) && presets.isCurrentFactory() && presets.getCurrentName() == all[all.size() - 1], "and back");

        juce::MemoryBlock block;
        proc.getStateInformation (block);
        HolterdipolterFilterProcessor other;
        other.setStateInformation (block.getData(), (int) block.getSize());
        check (other.presets.isCurrentFactory() && other.presets.getCurrentName() == all[all.size() - 1] && ! other.presets.isModified(),
               "a project remembers which factory preset it holds");

        presets.remove ("Meine Kick");
        presets.load ("Erster");
    }

    //==========================================================================
    std::printf (" A/B\n");
    presets.load ("Erster");
    check (presets.getActiveSlot() == 0, "starts on A");
    presets.switchSlot();
    check (presets.getActiveSlot() == 1 && std::abs (proc.getBandParams (0).gainDb - 3.0) < 0.001 && presets.getCurrentName() == "Erster",
           "B starts as a copy of A");

    HolterdipolterFilterProcessor::setParam (proc.bandParam (0, params::Gain), 11.0f);
    display->removeBand (2);
    presets.switchSlot();
    check (presets.getActiveSlot() == 0 && std::abs (proc.getBandParams (0).gainDb - 3.0) < 0.001 && proc.isBandUsed (2)
           && ! presets.isModified(), "back on A: A is untouched");
    presets.switchSlot();
    check (std::abs (proc.getBandParams (0).gainDb - 11.0) < 0.001 && ! proc.isBandUsed (2) && presets.isModified(),
           "and B kept its edit");

    // Both sides and the preset name survive the project.
    {
        juce::MemoryBlock block;
        proc.getStateInformation (block);

        HolterdipolterFilterProcessor other;
        other.presets.setDirectory (folder);
        other.setStateInformation (block.getData(), (int) block.getSize());

        check (other.presets.getActiveSlot() == 1 && other.presets.getCurrentName() == "Erster" && other.presets.isModified()
               && std::abs (other.getBandParams (0).gainDb - 11.0) < 0.001, "the live side survives save and restore");
        other.presets.switchSlot();
        check (other.presets.getActiveSlot() == 0 && std::abs (other.getBandParams (0).gainDb - 3.0) < 0.001
               && other.isBandUsed (2) && ! other.presets.isModified(), "and so does the waiting side");
        check (other.apvts.state.getNumChildren() == proc.apvts.state.getNumChildren(), "nothing of it is left behind in the parameter tree");
    }

    presets.copyToOtherSlot();
    presets.switchSlot();
    check (presets.getActiveSlot() == 0 && std::abs (proc.getBandParams (0).gainDb - 11.0) < 0.001, "copy puts the live setting on the other side");

    //==========================================================================
    std::printf (" selecting several bands\n");
    presets.loadDefault();
    pump();
    check (proc.findFreeBand() == 0 && ! presets.isModified(), "default setting: no bands");

    const auto bounds = display->getLocalBounds().toFloat();
    const auto at = [&bounds] (float x, float y) { return juce::Point<float> (bounds.getWidth() * x, bounds.getHeight() * y); };
    const auto addAt = [&] (juce::Point<float> p)
    {
        display->mouseDown (event (p, p, 2, false));
        display->mouseDoubleClick (event (p, p, 2, false));
        display->mouseUp (event (p, p, 2, false));
    };

    const auto p0 = at (0.3f, 0.35f), p1 = at (0.5f, 0.4f), p2 = at (0.7f, 0.7f);
    addAt (p0); addAt (p1); addAt (p2);
    pump();
    check (proc.isBandUsed (2) && display->getNumSelected() == 1 && display->getSelectedBand() == 2, "three bands, the last one selected");

    // A rectangle around the first two.
    const auto from = at (0.2f, 0.25f), to = at (0.6f, 0.5f);
    display->mouseDown (event (from, from, 1, false));
    display->mouseDrag (event (to, from, 1, true));
    display->mouseUp (event (to, from, 1, true));
    check (display->getNumSelected() == 2 && display->isSelected (0) && display->isSelected (1) && ! display->isSelected (2),
           "dragging a rectangle selects the bands inside");

    const auto ctrl = juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::ctrlModifier);
    display->mouseDown (event (p2, p2, 1, false, ctrl));
    display->mouseUp (event (p2, p2, 1, false, ctrl));
    check (display->getNumSelected() == 3, "Ctrl-click adds a band to the selection");
    display->mouseDown (event (p2, p2, 1, false, ctrl));
    display->mouseUp (event (p2, p2, 1, false, ctrl));
    check (display->getNumSelected() == 2 && ! display->isSelected (2), "and takes it out again");

    // Drag one of the two: both move by the same interval and the same gain.
    const auto b0 = proc.getBandParams (0), b1 = proc.getBandParams (1), b2 = proc.getBandParams (2);
    const auto target = p0 + juce::Point<float> (60.0f, 30.0f);
    display->mouseDown (event (p0, p0, 1, false));
    display->mouseDrag (event (target, p0, 1, true));
    display->mouseUp (event (target, p0, 1, true));
    pump();

    const auto m0 = proc.getBandParams (0), m1 = proc.getBandParams (1);
    check (m0.freq > b0.freq * 1.2 && std::abs (m0.freq / b0.freq - m1.freq / b1.freq) < 0.002, "dragging one moves both by the same interval");
    check (m0.gainDb < b0.gainDb - 0.5 && std::abs ((m0.gainDb - b0.gainDb) - (m1.gainDb - b1.gainDb)) < 0.002, "and by the same number of dB");
    check (std::abs (proc.getBandParams (2).freq - b2.freq) < 0.001 && display->getNumSelected() == 2, "the band outside stays; the selection too");

    proc.history.undo();
    check (std::abs (proc.getBandParams (0).freq - b0.freq) < 0.01 && std::abs (proc.getBandParams (1).gainDb - b1.gainDb) < 0.001,
           "one undo takes back the move of both bands");
    proc.history.redo();
    pump();

    // A plain click on one of them picks it alone.
    display->mouseDown (event (target, target, 1, false));
    display->mouseUp (event (target, target, 1, false));
    check (display->getNumSelected() == 1 && display->getSelectedBand() == 0, "a click without moving selects that band alone");

    //==========================================================================
    std::printf (" copy and paste\n");
    display->selectAll();
    check (display->getNumSelected() == 3, "select all");

    const auto text = display->copySelection();
    check (text.isNotEmpty() && display->pasteBands (text) == 3 && proc.isBandUsed (5), "three copied bands paste into three free bands");
    check (display->getNumSelected() == 3 && display->isSelected (3) && ! display->isSelected (0), "the pasted bands are the selection");

    const auto original = proc.getBandParams (1), copy = proc.getBandParams (4);
    check (copy.type == original.type && std::abs (copy.freq - original.freq) < 0.01 && std::abs (copy.gainDb - original.gainDb) < 0.001
           && std::abs (copy.q - original.q) < 0.001, "a copy is like its original");

    pump();
    proc.history.undo();
    check (! proc.isBandUsed (3) && ! proc.isBandUsed (5) && proc.isBandUsed (2), "one undo takes back the whole paste");
    pump();
    check (display->getNumSelected() == 0, "the selection lets go of bands that are gone");

    check (display->pasteBands ("some other text") == 0 && display->pasteBands ("<HolterdipolterFilterBands><Band") == 0,
           "text that is no band is not pasted");

    // Another instance takes them too; only as many as there is room for.
    {
        HolterdipolterFilterProcessor other;
        for (int band = 0; band < kMaxBands - 2; ++band)
            setBand (other, band, FilterType::Bell, 100.0f + 50.0f * (float) band, 1.0f, 1.0f, 1, Placement::Stereo);

        std::unique_ptr<juce::AudioProcessorEditor> otherEditor (other.createEditor());
        auto& otherDisplay = dynamic_cast<HolterdipolterFilterEditor&> (*otherEditor).getDisplay();
        check (otherDisplay.pasteBands (text) == 2 && other.findFreeBand() == -1, "pasting into a nearly full instance fills what is free");
    }

    juce::SystemClipboard::copyTextToClipboard ("hdp clipboard probe");

    if (juce::SystemClipboard::getTextFromClipboard() == "hdp clipboard probe")
    {
        display->selectAll();
        display->keyPressed (juce::KeyPress ('c', juce::ModifierKeys::commandModifier, 0));
        display->keyPressed (juce::KeyPress ('v', juce::ModifierKeys::commandModifier, 0));
        check (proc.isBandUsed (5), "Ctrl+C and Ctrl+V go through the system clipboard");
        display->keyPressed (juce::KeyPress ('x', juce::ModifierKeys::commandModifier, 0));
        check (! proc.isBandUsed (5) && proc.isBandUsed (2), "Ctrl+X removes what it copied");
    }
    else
    {
        std::printf ("  skip no system clipboard here\n");
    }

    display->selectAll();
    display->keyPressed (juce::KeyPress (juce::KeyPress::deleteKey));
    check (proc.findFreeBand() == 0 && display->getNumSelected() == 0, "Delete removes every selected band");

    //==========================================================================
    std::printf (" piano\n");
    const auto offNote = [] (double hz) { return std::abs (1200.0 * std::log2 (hz / params::nearestNoteFrequency (hz))); };

    display->setPianoVisible (true);
    pump();
    const auto q0 = at (0.42f, 0.4f), q1 = at (0.47f, 0.45f);
    addAt (q0);
    check (offNote (proc.getBandParams (0).freq) < 0.5, "with the piano showing, a new band lands on a note");

    display->mouseDown (event (q0, q0, 1, false));
    display->mouseDrag (event (q1, q0, 1, true));
    display->mouseUp (event (q1, q0, 1, true));
    check (offNote (proc.getBandParams (0).freq) < 0.5, "and dragging moves from note to note");

    const auto shift = juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::shiftModifier);
    display->mouseDown (event (q1, q1, 1, false));
    display->mouseDrag (event (q1 + juce::Point<float> (13.0f, 0.0f), q1, 1, true, shift));
    display->mouseUp (event (q1, q1, 1, true));
    check (offNote (proc.getBandParams (0).freq) > 2.0, "Shift moves freely between the notes");

    auto* freqParam = proc.bandParam (0, params::Freq);
    const auto typed = [freqParam] (const char* entry) { return freqParam->convertFrom0to1 (freqParam->getValueForText (entry)); };
    check (std::abs (typed ("A4") - 440.0f) < 0.01f && std::abs (typed ("c#3") - 138.591f) < 0.01f && std::abs (typed ("Bb1") - 58.270f) < 0.01f,
           "the frequency field takes note names");
    check (std::abs (typed ("2.5k") - 2500.0f) < 0.01f && std::abs (typed ("80 Hz") - 80.0f) < 0.01f, "and still takes numbers");
    check (params::formatNote (440.0) == "A4" && params::formatNote (261.626) == "C4" && params::formatNote (446.4).startsWith ("A4 +25"),
           "notes are named with their distance in cents");

    display->setPianoVisible (false);
    display->setSelectedBand (0);
    display->removeSelected();

    //==========================================================================
    std::printf (" dynamic EQ\n");
    {
        setBand (proc, 0, FilterType::Bell, 1000.0f, 0.0f, 2.0f, 1, Placement::Stereo);
        setBand (proc, 1, FilterType::HighCut, 9000.0f, 0.0f, 1.0f, 3, Placement::Stereo);
        HolterdipolterFilterProcessor::setParam (proc.bandParam (0, params::Dyn), -10.0f);
        HolterdipolterFilterProcessor::setParam (proc.bandParam (0, params::Thresh), -40.0f);
        HolterdipolterFilterProcessor::setParam (proc.bandParam (0, params::AutoThresh), 0.0f);
        HolterdipolterFilterProcessor::setParam (proc.bandParam (1, params::Dyn), -10.0f);

        const auto playTone = [&proc] (float amplitude)
        {
            juce::AudioBuffer<float> buffer (2, 512);
            juce::MidiBuffer midi;
            double phase = 0.0;
            float peak = 0.0f;

            for (int block = 0; block < 80; ++block)
            {
                for (int i = 0; i < 512; ++i)
                {
                    const float v = amplitude * (float) std::sin (phase);
                    phase += juce::MathConstants<double>::twoPi * 1000.0 / 48000.0;
                    buffer.setSample (0, i, v);
                    buffer.setSample (1, i, v);
                }

                proc.processBlock (buffer, midi);
                peak = buffer.getMagnitude (0, 512);
            }

            return juce::Decibels::gainToDecibels (peak / amplitude);
        };

        check (std::abs (playTone (0.002f)) < 0.1f && std::abs (proc.dynamicGainDb[0].load()) < 0.05f, "a quiet tone passes a dynamic band unchanged");
        check (std::abs (playTone (0.5f) + 10.0f) < 0.3f && std::abs (proc.dynamicGainDb[0].load() + 10.0f) < 0.1f,
               "a loud one is turned down by the dynamic range, and the processor reports it");
        check (std::abs (proc.dynamicGainDb[1].load()) < 1.0e-6f, "a high cut stays static whatever its range says");

        // Alt + wheel sets the range; the menu's values travel with copy, paste, presets and the project.
        display->setSelectedBand (0);
        const auto copied = display->copySelection();
        check (display->pasteBands (copied) == 1 && std::abs (proc.getBandParams (2).dynRangeDb + 10.0) < 0.001
               && std::abs (proc.getBandParams (2).thresholdDb + 40.0) < 0.001 && ! proc.getBandParams (2).autoThreshold,
               "copy and paste keep the dynamics");

        check (presets.save ("Dynamisch"), "saved as a preset");
        presets.loadDefault();
        check (presets.load ("Dynamisch") && std::abs (proc.getBandParams (0).dynRangeDb + 10.0) < 0.001
               && ! proc.getBandParams (0).autoThreshold, "a preset keeps the dynamics");

        juce::MemoryBlock block;
        proc.getStateInformation (block);
        HolterdipolterFilterProcessor other;
        other.setStateInformation (block.getData(), (int) block.getSize());
        check (std::abs (other.getBandParams (0).dynRangeDb + 10.0) < 0.001 && std::abs (other.getBandParams (0).thresholdDb + 40.0) < 0.001,
               "and so does the project");

        HolterdipolterFilterProcessor fresh;
        setBand (fresh, 0, FilterType::Bell, 500.0f, 3.0f, 1.0f, 1, Placement::Stereo);
        check (isExactlyZero (fresh.getBandParams (0).dynRangeDb) && fresh.getBandParams (0).autoThreshold, "a new band is static");

        presets.loadDefault();
    }

    //==========================================================================
    std::printf (" spectrum grab\n");
    {
        // A tone at 1 kHz and one at 4.2 kHz over quiet noise, until the analyzer has settled.
        juce::AudioBuffer<float> buffer (2, 512);
        juce::MidiBuffer midi;
        juce::Random random (11);
        double phase1 = 0.0, phase2 = 0.0;

        for (int tick = 0; tick < 25; ++tick)
        {
            for (int block = 0; block < 4; ++block)
            {
                for (int i = 0; i < 512; ++i)
                {
                    phase1 += juce::MathConstants<double>::twoPi * 1000.0 / 48000.0;
                    phase2 += juce::MathConstants<double>::twoPi * 4200.0 / 48000.0;
                    const float v = 0.25f * (float) std::sin (phase1) + 0.08f * (float) std::sin (phase2)
                                  + 0.002f * (random.nextFloat() - 0.5f);
                    buffer.setSample (0, i, v);
                    buffer.setSample (1, i, v);
                }

                proc.processBlock (buffer, midi);
            }

            pump();
        }

        check (display->startGrab() && display->isGrabbing(), "the spectrum can be grabbed");

        const auto peaks = display->getGrabPeaks();
        int at1k = -1, at4k = -1;

        for (size_t i = 0; i < peaks.size(); ++i)
        {
            if (std::abs (peaks[i].freq / 1000.0 - 1.0) < 0.03) at1k = (int) i;
            if (std::abs (peaks[i].freq / 4200.0 - 1.0) < 0.03) at4k = (int) i;
        }

        std::printf ("       %d peaks marked\n", (int) peaks.size());
        check (at1k >= 0 && at4k >= 0, "both tones are marked as peaks, at their frequencies");

        if (at1k >= 0)
        {
            const int free = proc.findFreeBand();
            const auto grabAt = peaks[(size_t) at1k].position;
            const auto pullTo = grabAt + juce::Point<float> (0.0f, 45.0f);

            display->mouseDown (event (grabAt, grabAt, 1, false));
            check (proc.isBandUsed (free) && display->getSelectedBand() == free, "picking up a peak makes a band");

            display->mouseDrag (event (pullTo, grabAt, 1, true));
            display->mouseUp (event (pullTo, grabAt, 1, true));

            const auto made = proc.getBandParams (free);
            check (made.type == FilterType::Bell && std::abs (made.freq / 1000.0 - 1.0) < 0.03 && made.gainDb < -2.0 && made.q >= 1.5,
                   "pulled down, it is a narrow cut right on the tone");
            check (! display->isGrabbing(), "letting go releases the spectrum");

            display->removeBand (free);
        }

        display->setAnalyzerMode (ui::EqDisplay::analyzerOff);
        pump();
        check (! display->startGrab(), "with the analyzer off there is nothing to grab");
        display->setAnalyzerMode (ui::EqDisplay::analyzerBoth);
    }

    //==========================================================================
    std::printf (" 0.6: collisions, sidechain dynamics, sketch\n");
    {
        using D = ui::EqDisplay;
        check (std::abs (D::collisionStrength (-20.0f, -20.0f, -18.0f, -90.0f) - 1.0f) < 1.0e-6f, "level with the other track and loud: a full collision");
        check (std::abs (D::collisionStrength (-26.0f, -20.0f, -18.0f, -90.0f) - 0.5f) < 1.0e-6f, "6 dB below it: half");
        check (D::collisionStrength (-32.0f, -20.0f, -18.0f, -90.0f) <= 0.0f, "12 dB below it: none, the cut has done its work");
        check (std::abs (D::collisionStrength (-12.0f, -20.0f, -12.0f, -90.0f) - 1.0f) < 1.0e-6f, "louder than the other track: still a full collision");
        check (D::collisionStrength (-60.0f, -60.0f, -18.0f, -90.0f) <= 0.0f && D::collisionStrength (-85.0f, -85.0f, -84.0f, -90.0f) <= 0.0f,
               "where both are quiet there is none");

        // A dynamic band that listens to the sidechain, through the processor.
        setBand (proc, 0, FilterType::Bell, 1000.0f, 0.0f, 2.0f, 1, Placement::Stereo);
        HolterdipolterFilterProcessor::setParam (proc.bandParam (0, params::Dyn), -10.0f);
        HolterdipolterFilterProcessor::setParam (proc.bandParam (0, params::Thresh), -40.0f);
        HolterdipolterFilterProcessor::setParam (proc.bandParam (0, params::AutoThresh), 0.0f);
        HolterdipolterFilterProcessor::setParam (proc.bandParam (0, params::DynSide), 1.0f);

        const auto play = [&proc] (float own, float side, bool connected)
        {
            juce::AudioBuffer<float> buffer (connected ? 4 : 2, 512);
            juce::MidiBuffer midi;
            double phase = 0.0;
            float peak = 0.0f;

            for (int block = 0; block < 80; ++block)
            {
                for (int i = 0; i < 512; ++i)
                {
                    phase += juce::MathConstants<double>::twoPi * 1000.0 / 48000.0;

                    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                        buffer.setSample (ch, i, (ch < 2 ? own : side) * (float) std::sin (phase));
                }

                proc.processBlock (buffer, midi);
                peak = buffer.getMagnitude (0, 0, 512);   // the track itself, not the sidechain
            }

            return juce::Decibels::gainToDecibels (peak / own);
        };

        const float ducked = play (0.01f, 0.5f, true);
        std::printf ("       ducked by %.2f dB, engine reports %.2f\n", ducked, proc.dynamicGainDb[0].load());
        check (std::abs (ducked + 10.0f) < 0.3f, "a loud sidechain turns the track down by the dynamic range");
        check (std::abs (play (0.5f, 0.0005f, true)) < 0.1f, "a loud track with a quiet sidechain is left alone");
        check (std::abs (play (0.5f, 0.5f, false)) < 0.1f, "without a sidechain connected the band rests");

        display->setSelectedBand (0);
        const auto copied = display->copySelection();
        check (display->pasteBands (copied) == 1 && proc.getBandParams (1).dynFromSidechain, "copy and paste keep the sidechain switch");
        display->selectAll();
        display->removeSelected();

        // Sketch: a hump drawn by hand around 1 kHz becomes a bell there.
        const auto plotPoint = [&] (float xNorm, float yNorm)
        {
            return juce::Point<float> ((float) display->getWidth() * xNorm, (float) display->getHeight() * yNorm);
        };

        const auto middle = (float) display->getHeight() * 0.5f;
        juce::ignoreUnused (middle);

        display->setSketching (true);
        check (display->isSketching(), "sketch mode switches on");

        std::vector<juce::Point<float>> stroke;
        for (int i = 0; i <= 40; ++i)
        {
            const float t = (float) i / 40.0f;
            stroke.push_back (plotPoint (0.42f + 0.2f * t, 0.5f - 0.18f * std::sin (t * juce::MathConstants<float>::pi)));
        }

        display->mouseDown (event (stroke.front(), stroke.front(), 1, false));
        for (size_t i = 1; i < stroke.size(); ++i)
            display->mouseDrag (event (stroke[i], stroke.front(), 1, true));
        display->mouseUp (event (stroke.back(), stroke.front(), 1, true));

        int used = 0, bells = 0;
        double strongest = 0.0, strongestFreq = 0.0;

        for (int band = 0; band < kMaxBands; ++band)
        {
            if (! proc.isBandUsed (band))
                continue;

            ++used;
            const auto p = proc.getBandParams (band);
            bells += p.type == FilterType::Bell ? 1 : 0;

            if (p.gainDb > strongest)
            {
                strongest = p.gainDb;
                strongestFreq = p.freq;
            }
        }

        std::printf ("       %d band(s), strongest %+.1f dB at %.0f Hz\n", used, strongest, strongestFreq);
        check (used >= 1 && used <= 3 && bells >= 1 && strongest > 2.0, "a drawn hump becomes one to three bands, a boosting bell among them");
        check (! display->isSketching() && display->getNumSelected() == used, "sketch mode ends itself and the new bands are selected");

        pump();
        proc.history.undo();
        check (proc.findFreeBand() == 0, "one undo takes the whole sketch back");

        display->setSketching (true);
        display->keyPressed (juce::KeyPress (juce::KeyPress::escapeKey));
        check (! display->isSketching(), "Esc leaves sketch mode");
    }

    //==========================================================================
    std::printf (" 0.9: preset folders, view defaults, reference, fixing a collision\n");
    {
        // Presets in folders.
        check (PresetManager::cleanPath ("  Kick // Tief? ") == "Kick/Tief" && PresetManager::cleanPath ("..\\x/../y") == "x/y"
               && PresetManager::cleanPath ("a/b/c/d/e") == "c/d/e" && PresetManager::cleanPath (" / ").isEmpty(),
               "folder names are cleaned, and cannot climb out of the preset folder");

        setBand (proc, 0, FilterType::Bell, 80.0f, 2.0f, 1.0f, 1, Placement::Stereo);
        check (presets.save ("Kick/Tief") && presets.getCurrentName() == "Kick/Tief"
               && folder.getChildFile ("Kick").getChildFile ("Tief.hdpfilter").existsAsFile(), "\"Kick/Tief\" is saved into the folder Kick");
        HolterdipolterFilterProcessor::setParam (proc.bandParam (0, params::Freq), 120.0f);
        check (bar.commitName ("Kick/Hoch") && presets.getPresetNames().contains ("Kick/Hoch") && presets.getPresetNames().contains ("Kick/Tief"),
               "a second one beside it; both are listed with their folder");
        check (presets.load ("Kick/Tief") && std::abs (proc.getBandParams (0).freq - 80.0) < 0.01 && presets.getCurrentName() == "Kick/Tief"
               && ! presets.isModified(), "loading from a folder");
        check (presets.step (+1) && presets.getCurrentName() != "Kick/Tief", "stepping walks through folders too");
        check (presets.remove ("Kick/Tief") && presets.remove ("Kick/Hoch") && ! folder.getChildFile ("Kick").exists(),
               "removing the last preset of a folder removes the folder");
        presets.loadDefault();

        // View defaults: a new instance looks like the one that was saved as default.
        display->setPianoVisible (true);
        display->setDisplayRange (6.0f);
        display->setAnalyzerSpeed (0);
        check (display->saveViewAsDefault() && ViewDefaults::getFile().existsAsFile(), "the view is saved as default");

        {
            HolterdipolterFilterProcessor fresh;
            const auto& st = fresh.apvts.state;
            check ((bool) st.getProperty ("uiPiano", false) && std::abs ((float) st.getProperty ("uiRangeDb", 0.0f) - 6.0f) < 0.001f
                   && (int) st.getProperty ("uiAnSpeed", -1) == 0, "a new instance starts with that view");

            std::unique_ptr<juce::AudioProcessorEditor> freshEditor (fresh.createEditor());
            check (dynamic_cast<HolterdipolterFilterEditor&> (*freshEditor).getDisplay().isPianoVisible(), "and its display shows it");
            check (fresh.findFreeBand() == 0, "the EQ itself still starts empty");
        }

        check (ViewDefaults::clear(), "the default can be taken back");
        {
            HolterdipolterFilterProcessor fresh;
            check (! fresh.apvts.state.hasProperty ("uiPiano"), "after that, new instances start with the built-in view");
        }

        display->setPianoVisible (false);
        display->setDisplayRange (12.0f);
        display->setAnalyzerSpeed (1);

        // Reference curve and collisions need sound: a bass note on the track, a kick-like tone at the sidechain.
        const auto play = [&] (float own, float side, int ticks)
        {
            juce::AudioBuffer<float> buffer (4, 512);
            juce::MidiBuffer midi;
            static double phase = 0.0;

            for (int tick = 0; tick < ticks; ++tick)
            {
                for (int block = 0; block < 4; ++block)
                {
                    for (int i = 0; i < 512; ++i)
                    {
                        phase += juce::MathConstants<double>::twoPi * 60.0 / 48000.0;
                        const float low = (float) std::sin (phase), high = 0.1f * (float) std::sin (phase * 40.0);

                        for (int ch = 0; ch < 4; ++ch)
                            buffer.setSample (ch, i, ch < 2 ? own * (low + high) : side * low);
                    }

                    proc.processBlock (buffer, midi);
                }

                pump();
            }
        };

        display->setAnalyzerMode (ui::EqDisplay::analyzerBoth);
        play (0.3f, 0.3f, 20);

        check (! display->hasReference(), "no reference to begin with");
        display->captureReference (false, 8);
        check (display->isCapturingReference(), "recording a reference");
        play (0.3f, 0.3f, 12);
        check (display->hasReference() && ! display->isCapturingReference(), "after its time the reference is kept");

        if (display->hasReference())
        {
            const auto& ref = display->getReference();
            int loudest = 0;
            for (int i = 1; i < sketch::kGridPoints; ++i)
                if (ref[(size_t) i] > ref[(size_t) loudest])
                    loudest = i;

            std::printf ("       reference peaks at %.0f Hz\n", sketch::gridFreq (loudest));
            check (std::abs (sketch::gridFreq (loudest) / 60.0 - 1.0) < 0.12, "it peaks where the note is");
        }

        check (proc.apvts.state.hasProperty ("uiReference") && ! ViewDefaults::isProjectOnly ("uiPiano") && ViewDefaults::isProjectOnly ("uiReference"),
               "the reference belongs to the project, not to the defaults");

        {
            juce::MemoryBlock block;
            proc.getStateInformation (block);
            HolterdipolterFilterProcessor other;
            other.setStateInformation (block.getData(), (int) block.getSize());
            std::unique_ptr<juce::AudioProcessorEditor> otherEditor (other.createEditor());
            auto& otherDisplay = dynamic_cast<HolterdipolterFilterEditor&> (*otherEditor).getDisplay();
            check (otherDisplay.hasReference() && std::abs (otherDisplay.getReference()[40] - display->getReference()[40]) < 0.06f,
                   "a reopened project shows the same reference");
        }

        // Both loud at 60 Hz: a collision, and one click fixes it.
        const auto regions = display->getCollisionRegions();
        check (! regions.empty() && regions.front().lowHz < 60.0 && regions.front().highHz > 60.0, "the shared note is marked as a collision");

        const int fixed = display->fixCollision (0);
        const auto made = fixed >= 0 ? proc.getBandParams (fixed) : BandParams();
        std::printf ("       fix: band at %.0f Hz, Q %.2f, range %+.1f dB\n", made.freq, made.q, made.dynRangeDb);
        check (fixed >= 0 && made.type == FilterType::Bell && std::abs (made.freq / 60.0 - 1.0) < 0.15 && isExactlyZero (made.gainDb)
               && std::abs (made.dynRangeDb + 6.0) < 0.001 && made.dynFromSidechain && display->getSelectedBand() == fixed,
               "fixing it adds a dynamic bell there that listens to the sidechain");

        play (0.3f, 0.3f, 25);
        check (proc.dynamicGainDb[(size_t) juce::jmax (0, fixed)].load() < -5.0f, "and that band gives way while the sidechain sounds");

        pump();
        proc.history.undo();
        check (fixed >= 0 && ! proc.isBandUsed (fixed), "one undo takes the fix back");

        display->clearReference();
        check (! display->hasReference() && ! proc.apvts.state.hasProperty ("uiReference"), "the reference can be forgotten");
        play (0.0f, 0.0f, 2);
    }

    //==========================================================================
    std::printf (" 1.0: EQ match\n");
    {
        // The track is plain noise; the sidechain is the same noise through a known EQ, and louder.
        EqEngine colour;
        colour.prepare (48000.0);
        {
            BandParams b;
            b.active = true;
            b.type = FilterType::Bell;      b.freq = 1000.0; b.gainDb = 8.0;  b.q = 1.2;
            colour.setBand (0, b);
            b.type = FilterType::HighShelf; b.freq = 6000.0; b.gainDb = -7.0; b.q = 0.8;
            colour.setBand (1, b);
        }

        juce::Random noise (21);

        const auto playNoise = [&] (int ticks)
        {
            juce::AudioBuffer<float> buffer (4, 512);
            juce::MidiBuffer midi;

            for (int tick = 0; tick < ticks; ++tick)
            {
                for (int block = 0; block < 4; ++block)
                {
                    for (int i = 0; i < 512; ++i)
                    {
                        const float v = 0.2f * (noise.nextFloat() - 0.5f);
                        buffer.setSample (0, i, v);
                        buffer.setSample (1, i, v);
                        buffer.setSample (2, i, 2.0f * v);
                        buffer.setSample (3, i, 2.0f * v);
                    }

                    colour.process (buffer.getWritePointer (2), buffer.getWritePointer (3), 512);
                    proc.processBlock (buffer, midi);
                }

                pump();
            }
        };

        display->matchToReference (5);
        check (! display->isCapturingReference(), "without a reference there is nothing to match to");

        playNoise (25);
        display->captureReference (true, 30);
        playNoise (34);
        check (display->hasReference(), "the coloured sidechain is recorded as the reference");

        display->matchToReference (30);
        check (display->isCapturingReference() && display->getLastMatchCount() == -1, "the match listens first");
        playNoise (34);

        const int matched = display->getLastMatchCount();
        std::vector<SketchBand> made;

        for (int band = 0; band < kMaxBands; ++band)
            if (proc.isBandUsed (band))
            {
                const auto p = proc.getBandParams (band);
                made.push_back ({ p.type, p.freq, p.gainDb, p.q });
                std::printf ("       %-10s %7.0f Hz  %+5.1f dB  Q %.2f\n", p.type == FilterType::Bell ? "bell" : "shelf", p.freq, p.gainDb, p.q);
            }

        const auto got = sketch::responseOf (made);
        const auto wanted = sketch::responseOf (std::vector<SketchBand> { { FilterType::Bell, 1000.0, 8.0, 1.2 }, { FilterType::HighShelf, 6000.0, -7.0, 0.8 } });

        // Judged on tone alone: the level the two curves share is left out, as the match does.
        double offset = 0.0, worst = 0.0;
        int counted = 0;

        for (int i = 0; i < sketch::kGridPoints; ++i)
            if (sketch::gridFreq (i) >= 100.0 && sketch::gridFreq (i) <= 10000.0)
            {
                offset += got[(size_t) i] - wanted[(size_t) i];
                ++counted;
            }

        offset /= counted;

        for (int i = 0; i < sketch::kGridPoints; ++i)
            if (sketch::gridFreq (i) >= 100.0 && sketch::gridFreq (i) <= 10000.0)
                worst = std::max (worst, std::abs (got[(size_t) i] - wanted[(size_t) i] - offset));

        std::printf ("       %d bands, within %.1f dB of the EQ that coloured the reference\n", matched, worst);
        check (matched >= 1 && matched <= 8 && (int) made.size() == matched && display->getNumSelected() == matched,
               "the match adds a handful of bands and selects them");
        check (worst < 3.0, "together they follow the difference between track and reference");

        pump();
        proc.history.undo();
        check (proc.findFreeBand() == 0, "one undo takes the whole match back");
        proc.history.redo();
        pump();

        // Matched once, the track is close to the reference: a second match has little left to do.
        display->matchToReference (30);
        playNoise (34);
        std::printf ("       second match: %d band(s)\n", display->getLastMatchCount());
        check (display->getLastMatchCount() >= 0 && display->getLastMatchCount() <= 2, "a second match finds next to nothing left");

        display->selectAll();
        display->removeSelected();
        display->clearReference();
    }

    //==========================================================================
    std::printf (" view settings\n");
    display->setDisplayRange (3.0f);
    display->setAnalyzerMode (ui::EqDisplay::analyzerPost);
    display->setAnalyzerSpeed (2);
    display->setAnalyzerResolution (3);
    display->setAnalyzerTilt (0);
    display->setAnalyzerRange (2);
    pump();

    const auto& state = proc.apvts.state;
    check (std::abs ((float) state["uiRangeDb"] - 3.0f) < 0.001f && (int) state["uiAnalyzer"] == 2 && (int) state["uiAnSpeed"] == 2
           && (int) state["uiAnRes"] == 3 && (int) state["uiAnTilt"] == 0 && (int) state["uiAnRange"] == 2,
           "range and analyzer settings are kept with the project");

    for (int resolution = 0; resolution < 4; ++resolution)
    {
        display->setAnalyzerResolution (resolution);
        pump();
    }

    display->setAnalyzerFrozen (true);
    pump();
    display->setAnalyzerFrozen (false);
    check (true, "every analyzer resolution runs");

    folder.deleteRecursively();
    std::printf ("%d feature failures\n", failures);
    return failures == 0 ? 0 : 1;
}

int main (int argc, char** argv)
{
    if (argc < 2)
    {
        std::printf ("usage: hdp_snapshot out.png [demo|solo|empty|piano|multi|dynamic|grab|sidechain|collision|sketch|match|interact|history|features|selftest] [selected band] [width height]\n");
        return 1;
    }

    const juce::String scenario = argc > 2 ? argv[2] : "demo";
    const int selected = argc > 3 ? juce::String (argv[3]).getIntValue() - 1 : -1;

    juce::ScopedJuceInitialiser_GUI gui;

    // Never touch (or depend on) the view defaults of whoever runs this.
    const auto viewFile = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("hdp-view-defaults-test.xml");
    viewFile.deleteFile();
    ViewDefaults::setFileForTests (viewFile);

    if (scenario == "selftest")
        return runProcessorCheck();

    HolterdipolterFilterProcessor proc;
    proc.setRateAndBufferSizeDetails (48000.0, 512);
    proc.prepareToPlay (48000.0, 512);

    if (scenario == "demo" || scenario == "solo")
    {
        setBand (proc, 0, FilterType::LowCut,    32.0f,    0.0f, 1.0f,  3, Placement::Stereo);
        setBand (proc, 1, FilterType::Bell,      58.0f,    4.5f, 1.6f,  1, Placement::Mid);
        setBand (proc, 2, FilterType::Bell,      240.0f,  -5.0f, 2.2f,  1, Placement::Stereo);
        setBand (proc, 3, FilterType::Notch,     1150.0f,  0.0f, 9.0f,  1, Placement::Stereo, false);
        setBand (proc, 4, FilterType::Bell,      3200.0f,  3.0f, 0.9f,  3, Placement::Side);
        setBand (proc, 5, FilterType::HighShelf, 9000.0f,  4.0f, 1.0f,  1, Placement::Stereo);
    }

    std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());

    if (scenario == "interact")
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
        return runInteractionCheck (proc, *editor);
    }

    if (scenario == "history")
        return runHistoryCheck (proc, *editor);

    if (scenario == "features")
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
        return runFeatureCheck (proc, dynamic_cast<HolterdipolterFilterEditor&> (*editor));
    }

    const bool withSidechain = scenario == "sidechain" || scenario == "collision";
    const bool withTones = scenario == "grab";

    if (scenario == "sidechain")
    {
        // The bass makes room for the kick only while the kick sounds.
        setBand (proc, 0, FilterType::Bell, 62.0f, 0.0f, 1.6f, 1, Placement::Stereo);
        HolterdipolterFilterProcessor::setParam (proc.bandParam (0, params::Dyn), -9.0f);
        HolterdipolterFilterProcessor::setParam (proc.bandParam (0, params::DynSide), 1.0f);
    }

    if (scenario == "dynamic")
    {
        setBand (proc, 0, FilterType::LowCut,    30.0f,   0.0f, 1.0f, 3, Placement::Stereo);
        setBand (proc, 1, FilterType::Bell,      55.0f,   2.0f, 2.5f, 1, Placement::Stereo);
        setBand (proc, 2, FilterType::Bell,      900.0f, -2.0f, 1.4f, 1, Placement::Mid);
        setBand (proc, 3, FilterType::HighShelf, 6000.0f, 1.0f, 1.0f, 1, Placement::Stereo);
        HolterdipolterFilterProcessor::setParam (proc.bandParam (1, params::Dyn), -9.0f);
        HolterdipolterFilterProcessor::setParam (proc.bandParam (2, params::Dyn), -5.0f);
        HolterdipolterFilterProcessor::setParam (proc.bandParam (3, params::Dyn), 5.0f);
        HolterdipolterFilterProcessor::setParam (proc.bandParam (3, params::AutoThresh), 0.0f);
        HolterdipolterFilterProcessor::setParam (proc.bandParam (3, params::Thresh), -44.0f);
    }

    if (scenario == "piano" || scenario == "multi")
    {
        setBand (proc, 0, FilterType::LowCut,   36.71f,   0.0f, 1.0f, 3, Placement::Stereo);
        setBand (proc, 1, FilterType::Bell,     110.0f,   4.0f, 2.0f, 1, Placement::Stereo);
        setBand (proc, 2, FilterType::Bell,     440.0f,  -3.5f, 3.0f, 1, Placement::Mid);
        setBand (proc, 3, FilterType::FlatTilt, 1000.0f,  5.0f, 1.0f, 1, Placement::Stereo);
        setBand (proc, 4, FilterType::Bell,     3520.0f,  2.5f, 1.2f, 1, Placement::Side);

        auto& e = dynamic_cast<HolterdipolterFilterEditor&> (*editor);
        e.getDisplay().setPianoVisible (scenario == "piano");

        if (scenario == "piano")
        {
            // Park the mouse over the plot, so its note tag shows.
            auto& d = e.getDisplay();
            const juce::Point<float> pos ((float) d.getWidth() * 0.68f, (float) d.getHeight() * 0.5f);
            d.mouseMove (juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), pos, juce::ModifierKeys(), 0.0f, 0.0f, 0.0f,
                                           0.0f, 0.0f, &d, &d, juce::Time::getCurrentTime(), pos, juce::Time::getCurrentTime(), 0, false));
        }

        if (scenario == "multi")
        {
            e.getDisplay().selectAll();
            proc.presets.setDirectory (juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("hdp-shot"));
            proc.presets.save ("Kick Bus");
            proc.presets.switchSlot();
            HolterdipolterFilterProcessor::setParam (proc.phaseInvertParam(), 1.0f);
        }
    }

    if (selected >= 0)
        if (auto* e = dynamic_cast<HolterdipolterFilterEditor*> (editor.get()))
            e->selectBand (selected);

    if (scenario == "solo" && selected >= 0)
    {
        proc.soloBand.store (selected);
        HolterdipolterFilterProcessor::setParam (proc.autoGainParam(), 1.0f);
    }

    if (argc > 5)
        editor->setSize (juce::String (argv[4]).getIntValue(), juce::String (argv[5]).getIntValue());

    // Kick-like low sine plus noise that falls off towards the top.
    juce::AudioBuffer<float> buffer (withSidechain ? 4 : 2, 512);
    juce::MidiBuffer midi;
    juce::Random random (7);
    double phase = 0.0, tone1 = 0.0, tone2 = 0.0, tone3 = 0.0, kick = 0.0;
    float lpL = 0.0f, lpR = 0.0f;

    for (int tick = 0; tick < 30; ++tick)
    {
        for (int block = 0; block < 4; ++block)
        {
            for (int i = 0; i < buffer.getNumSamples(); ++i)
            {
                phase += juce::MathConstants<double>::twoPi * 55.0 / 48000.0;
                float low = 0.35f * (float) std::sin (phase);

                lpL += 0.12f * ((random.nextFloat() - 0.5f) - lpL);
                lpR += 0.12f * ((random.nextFloat() - 0.5f) - lpR);

                if (withTones)
                {
                    // Resonances to grab: a boom, a honk and a whistle.
                    tone1 += juce::MathConstants<double>::twoPi * 183.0 / 48000.0;
                    tone2 += juce::MathConstants<double>::twoPi * 820.0 / 48000.0;
                    tone3 += juce::MathConstants<double>::twoPi * 3100.0 / 48000.0;
                    low = 0.1f * low + 0.12f * (float) std::sin (tone1) + 0.02f * (float) std::sin (tone2)
                        + 0.012f * (float) std::sin (tone3);
                }

                if (withSidechain)
                {
                    // This track is a bass: a low note with overtones. The sidechain is a kick.
                    low = 0.22f * (float) (std::sin (phase) + 0.5 * std::sin (2.0 * phase) + 0.3 * std::sin (3.0 * phase)
                                           + 0.15 * std::sin (5.0 * phase));
                    kick += juce::MathConstants<double>::twoPi * 62.0 / 48000.0;
                    const float side = 0.5f * (float) std::sin (kick) + 0.25f * (float) std::sin (1.5 * kick)
                                     + 0.3f * (random.nextFloat() - 0.5f) * (float) std::pow (0.5 + 0.5 * std::sin (kick * 0.02), 8.0);
                    buffer.setSample (2, i, side);
                    buffer.setSample (3, i, side);
                }

                buffer.setSample (0, i, low + lpL * (withSidechain ? 0.15f : 0.9f));
                buffer.setSample (1, i, low + lpR * (withSidechain ? 0.15f : 0.9f));
            }

            proc.processBlock (buffer, midi);
        }

        juce::MessageManager::getInstance()->runDispatchLoopUntil (40);
    }

    if (withTones)
        dynamic_cast<HolterdipolterFilterEditor&> (*editor).getDisplay().startGrab();

    if (scenario == "match")
    {
        // Record the track as it is, darken and thin it out, and let the match bring it back.
        auto& d = dynamic_cast<HolterdipolterFilterEditor&> (*editor).getDisplay();
        EqEngine dull;
        dull.prepare (48000.0);
        {
            BandParams b;
            b.active = true;
            b.type = FilterType::HighShelf; b.freq = 3000.0; b.gainDb = -8.0; b.q = 0.8;
            dull.setBand (0, b);
            b.type = FilterType::Bell;      b.freq = 300.0;  b.gainDb = 6.0;  b.q = 1.0;
            dull.setBand (1, b);
        }

        const auto run = [&] (int ticks, bool dulled)
        {
            for (int tick = 0; tick < ticks; ++tick)
            {
                for (int block = 0; block < 4; ++block)
                {
                    for (int i = 0; i < 512; ++i)
                    {
                        phase += juce::MathConstants<double>::twoPi * 55.0 / 48000.0;
                        lpL += 0.12f * ((random.nextFloat() - 0.5f) - lpL);
                        lpR += 0.12f * ((random.nextFloat() - 0.5f) - lpR);
                        buffer.setSample (0, i, 0.35f * (float) std::sin (phase) + lpL * 0.9f);
                        buffer.setSample (1, i, 0.35f * (float) std::sin (phase) + lpR * 0.9f);
                    }

                    if (dulled)
                        dull.process (buffer.getWritePointer (0), buffer.getWritePointer (1), 512);

                    proc.processBlock (buffer, midi);
                }

                juce::MessageManager::getInstance()->runDispatchLoopUntil (40);
            }
        };

        d.captureReference (false, 40);
        run (45, false);
        d.matchToReference (40);
        run (60, true);
    }

    if (scenario == "sketch")
    {
        // A drawing in progress: more bass, a dip in the low mids, a little more air.
        auto& d = dynamic_cast<HolterdipolterFilterEditor&> (*editor).getDisplay();
        const auto at = [&d] (float x, float y) { return juce::Point<float> ((float) d.getWidth() * x, (float) d.getHeight() * y); };
        const auto mouse = [&d] (juce::Point<float> pos, juce::Point<float> down, bool dragged)
        {
            return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), pos, juce::ModifierKeys::leftButtonModifier,
                                     0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &d, &d, juce::Time::getCurrentTime(), down,
                                     juce::Time::getCurrentTime(), 1, dragged);
        };

        d.setSketching (true);
        const auto start = at (0.08f, 0.38f);
        d.mouseDown (mouse (start, start, false));

        for (int i = 1; i <= 120; ++i)
        {
            const float t = (float) i / 120.0f;
            const float y = 0.5f - 0.12f * std::exp (-t * 9.0f) + 0.11f * std::exp (-std::pow ((t - 0.38f) / 0.09f, 2.0f))
                          - 0.07f / (1.0f + std::exp (-(t - 0.8f) * 22.0f));
            d.mouseDrag (mouse (at (0.08f + 0.84f * t, y), start, true));
        }

        if (argc > 6)
            d.mouseUp (mouse (at (0.92f, 0.43f), start, true));   // any seventh argument: show the result instead
    }

    if (scenario == "sketch")
        juce::MessageManager::getInstance()->runDispatchLoopUntil (150);   // let the display catch up with the new bands

    const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.0f);

    juce::File out = juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]);
    out.deleteFile();

    juce::FileOutputStream stream (out);
    juce::PNGImageFormat png;

    if (! stream.openedOk() || ! png.writeImageToStream (image, stream))
    {
        std::printf ("could not write %s\n", out.getFullPathName().toRawUTF8());
        return 1;
    }

    std::printf ("wrote %s (%d x %d)\n", out.getFullPathName().toRawUTF8(), image.getWidth(), image.getHeight());
    return 0;
}
