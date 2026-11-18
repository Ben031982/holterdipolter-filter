#include "EditHistory.h"

EditHistory::EditHistory (juce::AudioProcessor& processorToWatch)
{
    params = processorToWatch.getParameters();

    for (auto* p : params)
        p->addListener (this);
}

EditHistory::~EditHistory()
{
    cancelPendingUpdate();

    for (auto* p : params)
        p->removeListener (this);
}

bool EditHistory::canUndo() const
{
    return ! resetRequested.load() && position > 0;
}

bool EditHistory::canRedo() const
{
    return ! resetRequested.load() && position + 1 < steps.size();
}

void EditHistory::reset()
{
    resetRequested.store (true);
}

EditHistory::Snapshot EditHistory::capture() const
{
    Snapshot s ((size_t) params.size());

    for (int i = 0; i < params.size(); ++i)
        s[(size_t) i] = params.getUnchecked (i)->getValue();

    return s;
}

void EditHistory::commit()
{
    auto now = capture();

    if (resetRequested.exchange (false) || steps.empty())
    {
        steps.clear();
        steps.push_back (std::move (now));
        position = 0;
        lastStepParam = -1;
        return;
    }

    const auto& current = steps[position];
    int numChanged = 0, changedParam = -1;

    for (size_t i = 0; i < now.size(); ++i)
    {
        if (! juce::exactlyEqual (now[i], current[i]))
        {
            ++numChanged;
            changedParam = (int) i;
        }
    }

    if (numChanged == 0)
        return;

    const auto time = juce::Time::getMillisecondCounter();

    // Mouse-wheel ticks and nudges on one control arrive as many tiny gestures;
    // keep them together as a single step.
    const bool merge = numChanged == 1
                    && changedParam == lastStepParam
                    && position > 0
                    && position + 1 == steps.size()
                    && time - lastStepTime < mergeWindowMs;

    if (merge)
    {
        steps[position] = std::move (now);
    }
    else
    {
        steps.resize (position + 1);   // a new edit discards what could have been redone
        steps.push_back (std::move (now));
        ++position;

        if (steps.size() > maxSteps)
        {
            steps.erase (steps.begin());
            --position;
        }
    }

    lastStepParam = numChanged == 1 ? changedParam : -1;
    lastStepTime = time;
}

void EditHistory::flush()
{
    cancelPendingUpdate();
    commitPending = false;
    commit();
}

void EditHistory::apply (const Snapshot& snapshot)
{
    applying = true;

    for (int i = 0; i < params.size() && (size_t) i < snapshot.size(); ++i)
    {
        auto* p = params.getUnchecked (i);
        const float value = snapshot[(size_t) i];

        if (! juce::exactlyEqual (p->getValue(), value))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (value);
            p->endChangeGesture();
        }
    }

    applying = false;
    lastStepParam = -1;
}

void EditHistory::undo()
{
    flush();

    if (position > 0)
        apply (steps[--position]);
}

void EditHistory::redo()
{
    flush();

    if (position + 1 < steps.size())
        apply (steps[++position]);
}

void EditHistory::parameterGestureChanged (int, bool gestureIsStarting)
{
    if (applying)
        return;

    if (! juce::MessageManager::existsAndIsCurrentThread())
    {
        triggerAsyncUpdate();
        return;
    }

    if (gestureIsStarting)
    {
        // Whatever changed since the last step without a gesture (host automation,
        // a preset) becomes its own step, so this edit can be undone on its own.
        // Not while a step is still being collected, or it would be cut in two.
        if (! commitPending)
            commit();
    }
    else
    {
        commitPending = true;
        triggerAsyncUpdate();
    }
}

void EditHistory::handleAsyncUpdate()
{
    commitPending = false;
    commit();
}
