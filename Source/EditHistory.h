// holterdipolter Filter - undo / redo
//
// A plain history of parameter snapshots. A step is recorded whenever a user
// gesture on any parameter ends; everything that changes within one pass of
// the message loop (adding a band sets eight parameters) lands in one step.
// Used from the message thread only; reset() may be called from any thread.

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

class EditHistory final : private juce::AudioProcessorParameter::Listener,
                          private juce::AsyncUpdater
{
public:
    explicit EditHistory (juce::AudioProcessor& processorToWatch);
    ~EditHistory() override;

    bool canUndo() const;
    bool canRedo() const;

    void undo();
    void redo();

    /** Forgets everything and takes the next state it sees as the starting point.
        Call after loading a state. Safe from any thread. */
    void reset();

private:
    using Snapshot = std::vector<float>;

    Snapshot capture() const;
    void commit();
    void flush();
    void apply (const Snapshot&);

    void parameterValueChanged (int, float) override {}
    void parameterGestureChanged (int parameterIndex, bool gestureIsStarting) override;
    void handleAsyncUpdate() override;

    static constexpr size_t maxSteps = 200;
    static constexpr juce::uint32 mergeWindowMs = 600;

    juce::Array<juce::AudioProcessorParameter*> params;

    std::vector<Snapshot> steps;
    size_t position = 0;

    bool applying = false;
    bool commitPending = false;
    int lastStepParam = -1;             // set if the newest step changed exactly one parameter
    juce::uint32 lastStepTime = 0;
    std::atomic<bool> resetRequested { true };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EditHistory)
};
