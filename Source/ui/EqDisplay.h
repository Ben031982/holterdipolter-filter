// holterdipolter Filter - interactive EQ display with spectrum analyzer

#pragma once

#include <juce_dsp/juce_dsp.h>

#include "../PluginProcessor.h"
#include "Controls.h"
#include "Theme.h"
#include "../dsp/SketchFit.h"

namespace hdp::ui
{

class EqDisplay final : public juce::Component,
                        private juce::Timer
{
public:
    enum AnalyzerMode { analyzerOff = 0, analyzerPre, analyzerPost, analyzerBoth };

    explicit EqDisplay (HolterdipolterFilterProcessor&);
    ~EqDisplay() override;

    //==============================================================================
    // Selection. Any number of bands can be selected; one of them is the primary
    // band, the one the band strip edits.

    /** Called with the primary band (or -1) and the number of selected bands. */
    std::function<void (int, int)> onSelectionChanged;

    int getSelectedBand() const   { return primaryBand; }
    int getNumSelected() const;
    bool isSelected (int band) const;

    /** Selects exactly this band, or nothing for -1. */
    void setSelectedBand (int band);
    void selectAll();

    void removeBand (int band);
    void removeSelected();

    //==============================================================================
    // Copy and paste. Bands travel as text, so they also move between instances.

    juce::String copySelection() const;                 // empty if nothing is selected
    int pasteBands (const juce::String& text);          // number of bands added
    void copyToClipboard() const;
    void pasteFromClipboard();

    //==============================================================================
    // View settings. Stored with the project, but not automatable.

    void setDisplayRange (float rangeDb);
    void setAnalyzerMode (int mode);
    void setAnalyzerSpeed (int index);                  // 0 slow, 1 medium, 2 fast
    void setAnalyzerResolution (int index);             // 0 low ... 3 maximum
    void setAnalyzerTilt (int index);                   // 0, 3, 4.5, 6 dB per octave
    void setAnalyzerRange (int index);                  // 60, 90, 120 dB
    void setAnalyzerFrozen (bool shouldBeFrozen);
    void setPianoVisible (bool shouldBeVisible);
    void setSidechainVisible (bool shouldBeVisible);    // the spectrum at the sidechain input
    void setCollisionsVisible (bool shouldBeVisible);   // where that and this track are both loud
    void setGrabEnabled (bool shouldBeEnabled);

    /** How strongly a place counts as a collision, 0 to 1. Both tracks have to be loud
        there (measured against the loudest peak on screen), and this track must not lie
        clearly below the other one: level with it is a full collision, 12 dB below it none. */
    static float collisionStrength (float ownDb, float sideDb, float loudestDb, float floorDb);

    /** A range in which this track is in the other one's way, as marked in the display. */
    struct CollisionRegion
    {
        float x0 = 0.0f, x1 = 0.0f;         // in the display
        double lowHz = 0.0, highHz = 0.0;
        juce::Rectangle<float> button;      // the "FIX" tag to click
    };

    const std::vector<CollisionRegion>& getCollisionRegions() const   { return collisionRegions; }

    /** Puts a band on a collision that gives way while the other track sounds:
        a bell as wide as the region, 0 dB at rest, -6 dB of dynamic range, listening
        to the sidechain. Returns the band, or -1. */
    int fixCollision (int regionIndex);

    //==============================================================================
    // Reference curve: a spectrum that was recorded once and stays on screen as a
    // line to work against. Kept with the project, on a grid of its own, without tilt.

    /** Averages what the analyzer shows for a while (30 ticks are a second) and keeps it. */
    void captureReference (bool fromSidechain, int numTicks = 120);
    void clearReference();
    bool hasReference() const                               { return ! reference.empty(); }
    bool isCapturingReference() const                       { return captureTicksLeft > 0; }
    const std::vector<float>& getReference() const          { return reference; }   // dB on the sketch grid

    /** EQ Match: listens to what leaves the EQ for a while, compares it with the
        reference curve and adds the bands that bring the track's tone to that of the
        reference. Needs a reference; the bands arrive when the listening is over. */
    void matchToReference (int numTicks = 120);

    /** How many bands the last match added; -1 while none has finished. */
    int getLastMatchCount() const   { return lastMatchCount; }

    /** Makes the way the display is set up right now the default for new instances. */
    bool saveViewAsDefault();

    //==============================================================================
    // EQ Sketch. While it is on, dragging across the plot draws a curve; on release
    // the plugin adds the bands that follow it.

    void setSketching (bool shouldBeOn);
    bool isSketching() const   { return sketching; }

    /** Adds bands for a curve given as points in the display. Returns how many. */
    int applySketch (const std::vector<juce::Point<float>>& points);

    //==============================================================================
    // Spectrum Grab. When the mouse rests on the analyzer, the spectrum stands still
    // and its peaks can be picked up: dragging one makes a band right there.

    struct GrabPeak
    {
        juce::Point<float> position;    // where its marker sits
        double freq = 0.0, q = 4.0;
    };

    /** Freezes the spectrum and marks its peaks. False if there is nothing to grab. */
    bool startGrab();
    void endGrab();
    bool isGrabbing() const                         { return grabActive; }
    const std::vector<GrabPeak>& getGrabPeaks() const   { return grabPeaks; }

    bool isPianoVisible() const   { return pianoVisible; }

    //==============================================================================
    void paint (juce::Graphics&) override;
    void resized() override;

    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    struct Snapshot
    {
        std::array<BandParams, kMaxBands> bands;
        std::array<bool, kMaxBands> used {};
        float gainScale = 1.0f;
        double sampleRate = 0.0;
        int numPoints = 0;

        bool operator== (const Snapshot&) const;
    };

    struct Trace
    {
        std::vector<float> db;
        bool visible = false;
    };

    /** One band taking part in a drag, with what it was when the drag began. */
    struct DragItem
    {
        int band = -1;
        double freq = 0.0, gainDb = 0.0, q = 0.0;
        bool gainGesture = false, qGesture = false;
    };

    void timerCallback() override;

    // Geometry
    float freqToX (double freq) const;
    double xToFreq (float x) const;
    float dbToY (double db) const;
    double yToDb (float y) const;
    float pointX (int index) const;
    juce::Point<float> nodePosition (int band) const;
    int bandAt (juce::Point<float> p) const;

    // Model
    Snapshot takeSnapshot() const;
    void rebuildCurves();
    void updateTrace (const SpectrumTap&, Trace&);
    void updateMeter();
    void setUpFft();

    // Selection
    void setSelection (const std::array<bool, kMaxBands>& newSelection, int newPrimary);
    void toggleSelected (int band);
    void makePrimary (int band);
    void notifySelection();

    // Interaction
    void addBandAt (juce::Point<float> p);
    void beginDrag (int band, juce::Point<float> p);
    void endDrag();
    void updateLasso (juce::Point<float> p);
    void showBandMenu (int band, juce::Point<int> screenPos);
    void showBackgroundMenu (juce::Point<int> screenPos);
    void showAnalyzerMenu();
    int createBand (FilterType type, double freq, double gainDb, double q, int slopeIndex);
    int grabPeakAt (juce::Point<float> p) const;
    const Trace* ownTrace() const;
    float traceY (float db) const;
    void paintCollisions (juce::Graphics&);
    void paintReference (juce::Graphics&);
    void updateCollisions();
    void updateReferenceCapture();
    void storeReference();
    int addFittedBands (const std::vector<SketchBand>& fitted);
    void showNotice (const juce::String& text);
    void paintGrab (juce::Graphics&);
    void adjustQ (int band, float octaves);
    void setBandFrequency (int band, double freq);
    void refreshHeader();
    void storeView (const juce::Identifier& property, const juce::var& value);

    template <typename Function>
    void forEachSelected (Function&& f) const
    {
        for (int band = 0; band < kMaxBands; ++band)
            if (selected[(size_t) band] && processor.isBandUsed (band))
                f (band);
    }

    // Painting
    void paintGrid (juce::Graphics&);
    void paintPiano (juce::Graphics&);
    void paintTrace (juce::Graphics&, const Trace&, juce::Colour fill, juce::Colour line);
    void paintCurves (juce::Graphics&);
    void paintNodes (juce::Graphics&);
    void paintReadout (juce::Graphics&, int band);
    void paintMeter (juce::Graphics&);
    void paintSolo (juce::Graphics&);
    int soloedBand() const;
    juce::Path curvePath (const std::vector<float>& db, bool closedToZeroLine) const;

    HolterdipolterFilterProcessor& processor;

    // Header strip above the plot.
    LabelButton analyzerButton, pianoButton, sketchButton;
    SegmentedSelector rangeSelector;
    juce::Rectangle<int> rangeCaption;

    juce::Rectangle<float> plot, meterArea, pianoArea;
    float rangeDb = 12.0f;
    bool pianoVisible = false;

    // Analyzer
    int analyzerMode = analyzerBoth;
    int analyzerSpeed = 1, analyzerResolution = 2, analyzerTilt = 2, analyzerRange = 1;
    bool analyzerFrozen = false;
    bool sidechainVisible = true, collisionsVisible = true, grabEnabled = true;

    // Collisions: how strongly each column of the display counts, held for a moment
    // so that a kick does not make the marking flicker.
    std::vector<float> collisionHold;
    std::vector<CollisionRegion> collisionRegions;

    // Reference curve
    std::vector<float> reference;           // empty = none
    std::vector<double> captureSum;
    int captureTicksLeft = 0, captureTicks = 0;
    bool captureFromSidechain = false, captureForMatch = false, referenceVisible = true;
    int lastMatchCount = -1;

    // A short message at the top of the plot.
    juce::String notice;
    juce::uint32 noticeUntil = 0;

    // EQ Sketch
    bool sketching = false, sketchDrawing = false;
    std::vector<juce::Point<float>> sketchPoints;

    // Spectrum Grab
    bool grabActive = false, grabDragging = false;
    std::vector<GrabPeak> grabPeaks;
    int grabHover = -1;
    juce::Point<float> mousePos { -1.0f, -1.0f };
    juce::uint32 mouseRestingSince = 0;

    // Selection
    std::array<bool, kMaxBands> selected {};
    int primaryBand = -1;
    int notifiedPrimary = -1, notifiedCount = 0;

    // Dragging nodes
    int hoverBand = -1, dragBand = -1;
    std::vector<DragItem> dragItems;
    bool dragChangesGain = false, dragMoved = false, collapseOnRelease = false;
    juce::Point<float> dragVirtualPos, lastMousePos;
    float dragStartY = 0.0f;
    float mouseX = -1.0f;               // where the mouse is over the plot or the piano, or -1

    // Dragging a selection rectangle
    bool lassoActive = false;
    juce::Point<float> lassoStart, lassoEnd;
    std::array<bool, kMaxBands> lassoBase {};

    Snapshot snapshot;
    std::array<std::vector<float>, kMaxBands> bandCurves;
    std::vector<float> stereoTotal;
    std::array<std::vector<float>, kNumPlacements> groupTotals;
    std::array<bool, kNumPlacements> groupPresent {};

    int fftOrder = 13, fftSize = 1 << 13;
    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> window, fftBuffer;
    Trace preTrace, postTrace, sideTrace;

    std::array<float, 2> meterDb { { -100.0f, -100.0f } };
    std::array<float, 2> meterHoldDb { { -100.0f, -100.0f } };
    std::array<int, 2> meterHoldFrames { { 0, 0 } };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EqDisplay)
};

} // namespace hdp::ui
