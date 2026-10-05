#include "BackScratchProcessor.h"
#include "PatternEditing.h"

namespace backScratch {
namespace {
constexpr size_t undoDepth = 64;
bool sameGeometry(const ScratchPattern& a, const ScratchPattern& b) noexcept {
    if (a.count != b.count || a.beats != b.beats || a.editingRange != b.editingRange) return false;
    for (int i = 0; i < a.count; ++i) {
        const auto& x = a.segments[static_cast<size_t>(i)];
        const auto& y = b.segments[static_cast<size_t>(i)];
        if (x.start != y.start || x.end != y.end || x.delayStart != y.delayStart || x.delayEnd != y.delayEnd
            || x.gateStart != y.gateStart || x.gateEnd != y.gateEnd || x.wet != y.wet || x.curved != y.curved
            || x.curveStart != y.curveStart || x.curveEnd != y.curveEnd) return false;
    }
    return true;
}
}

bool BackScratchProcessor::hasCustomPattern() const noexcept { return rawControls[edited]->load() > 0.5f; }

BackScratchProcessor::EditSnapshot BackScratchProcessor::captureEditSnapshot() const
{
    EditSnapshot result;
    result.enabled = hasCustomPattern();
    result.pattern = fitPatternToPhrase(currentPattern(), 1.0);
    result.pattern.gridOffset = 0;
    return result;
}

bool BackScratchProcessor::applyEditSnapshot(const EditSnapshot& snapshot)
{
    if (editedPatterns.publish(snapshot.pattern) == EditedPatternStore::None) return false;
    if (snapshot.enabled != hasCustomPattern()) writeParameterGesture("edited", snapshot.enabled ? 1.0f : 0.0f);
    return true;
}

bool BackScratchProcessor::beginPatternEdit()
{
    const juce::ScopedLock lock(editLock);
    if (editStart) return false;
    editStart = captureEditSnapshot();
    editStartSlot = editedPatterns.acquireLatest();
    if (editStartSlot < 0) { editStart.reset(); return false; }
    editChanged = false;
    return true;
}

bool BackScratchProcessor::previewPatternEdit(const ScratchPattern& input)
{
    const juce::ScopedLock lock(editLock);
    if (!editStart || !validateEditedPattern(input)) return false;
    EditSnapshot next {fitPatternToPhrase(input, 1.0), true};
    next.pattern.gridOffset = 0;
    const auto current = captureEditSnapshot();
    if (sameGeometry(current.pattern, next.pattern)) return true;
    if (!applyEditSnapshot(next)) return false;
    editChanged = !sameGeometry(editStart->pattern, next.pattern);
    return true;
}

bool BackScratchProcessor::commitPatternEdit()
{
    const juce::ScopedLock lock(editLock);
    if (!editStart) return false;
    const bool changed = editChanged;
    if (changed) {
        if (editUndo.size() == undoDepth) editUndo.erase(editUndo.begin());
        editUndo.push_back(*editStart);
        editRedo.clear();
        clearSavedName();
        updateHostDisplay(juce::AudioProcessor::ChangeDetails().withNonParameterStateChanged(true));
    } else if (hasCustomPattern() != editStart->enabled) {
        writeParameterGesture("edited", editStart->enabled ? 1.0f : 0.0f);
    }
    editStart.reset();
    editedPatterns.release(editStartSlot);
    editStartSlot = EditedPatternStore::None;
    editChanged = false;
    return changed;
}

void BackScratchProcessor::cancelPatternEdit()
{
    const juce::ScopedLock lock(editLock);
    if (!editStart) return;
    // The pre-drag snapshot is pinned throughout the gesture. Cancellation
    // can therefore always restore it, even with every pool slot in flight.
    editedPatterns.publishExisting(editStartSlot);
    if (editStart->enabled != hasCustomPattern()) writeParameterGesture("edited", editStart->enabled ? 1.0f : 0.0f);
    editStart.reset();
    editedPatterns.release(editStartSlot);
    editStartSlot = EditedPatternStore::None;
    editChanged = false;
}

bool BackScratchProcessor::canUndoPatternEdit() const {
    const juce::ScopedLock lock(editLock);
    return !editStart && !editUndo.empty();
}
bool BackScratchProcessor::canRedoPatternEdit() const {
    const juce::ScopedLock lock(editLock);
    return !editStart && !editRedo.empty();
}

void BackScratchProcessor::undoPatternEdit()
{
    const juce::ScopedLock lock(editLock);
    if (editStart || editUndo.empty()) return;
    const auto current = captureEditSnapshot();
    if (!applyEditSnapshot(editUndo.back())) return;
    editRedo.push_back(current);
    editUndo.pop_back();
    clearSavedName();
    updateHostDisplay(juce::AudioProcessor::ChangeDetails().withNonParameterStateChanged(true));
}

void BackScratchProcessor::redoPatternEdit()
{
    const juce::ScopedLock lock(editLock);
    if (editStart || editRedo.empty()) return;
    const auto current = captureEditSnapshot();
    if (!applyEditSnapshot(editRedo.back())) return;
    editUndo.push_back(current);
    editRedo.pop_back();
    clearSavedName();
    updateHostDisplay(juce::AudioProcessor::ChangeDetails().withNonParameterStateChanged(true));
}

void BackScratchProcessor::clearEditHistory()
{
    const juce::ScopedLock lock(editLock);
    editStart.reset();
    editedPatterns.release(editStartSlot);
    editStartSlot = EditedPatternStore::None;
    editChanged = false;
    editUndo.clear(); editRedo.clear();
}

void BackScratchProcessor::clearControlHistory() noexcept
{
    for (auto& frame : controlHistory) {
        editedPatterns.release(frame.editSlot);
        frame.editSlot = EditedPatternStore::None;
    }
    editedPatterns.release(renderedEditSlot);
    renderedEditSlot = EditedPatternStore::None;
    controlWrite = 0; controlFrames = 0;
}

BackScratchProcessor::~BackScratchProcessor()
{
    editedPatterns.release(editStartSlot);
    clearControlHistory();
    editedPatterns.release(inputEditSlot);
    editedPatterns.release(audibleEditSlot.exchange(EditedPatternStore::None));
}
} // namespace backScratch
