# TODO06: Per-Instance Mic Layout via VST State

## Goal

Allow each plugin instance (each track in a DAW project) to persist and restore its own mic layout choice. On project reload, the correct mic layout — including correct output bus names — is applied per-instance, with fallback to `config.yaml` default.

## Background: VST3 Lifecycle

The VST3 host calls these in order:

```
1. JuceVST3Component()      → createPluginFilter() → PluginProcessor() → bus topology locked
2. initialize()              → preparePlugin() (no audio yet)
3. setState(IBStream*)       → setStateInformation(data, size)
4. setActive(true)           → prepareToPlay()
5. process()                 → processBlock()
```

The saved project state arrives at step 3, **after** the `AudioProcessor` constructor has already defined bus names at step 1.

### Key locations in JUCE source

- `JuceVST3Component` constructor: `juce_audio_plugin_client_VST3.cpp:2538–2573`
- `createPluginFilterOfType()`: `detail/juce_CreatePluginFilter.h:40–57`
- `setState()` (host→component): `juce_audio_plugin_client_VST3.cpp:2903–2930`
- `setStateInformation()` (component→AudioProcessor): `juce_audio_plugin_client_VST3.cpp:2770–2810`
- `preparePlugin()`: `juce_audio_plugin_client_VST3.cpp:3759–3773`

## Strategy

### Option B (primary): Reconstruct `AudioProcessor` during `setState()`

When the host delivers saved state via `setState()`, we **parse the state first**, extract the mic layout ID, and if it differs from the current one, **destroy and recreate** the `AudioProcessor` with the correct layout baked into its bus topology from construction.

This gives truly correct bus names from the host's perspective.

### Option A (fallback/safety net): `Bus::setName()` accessor

A 1-line JUCE mod adding `void setName(const String&)` to `AudioProcessor::Bus`. Used as a lightweight rename-in-place after `setStateInformation()`. Most hosts won't re-read bus names after `setState()`, so this is cosmetic insurance only.

### Part 1: Standard state persistence (no JUCE mods, prerequisite for everything)

Implement the currently-empty `getStateInformation()` / `setStateInformation()` stubs so that mic layout, kit layout, and instrument map are saved/restored per instance.

---

## Implementation Plan

### Phase 1: State Persistence (prerequisite)

**Files:** `source/PluginProcessor.cpp`, `source/Engine/DrumKitEngine.cpp`

#### 1.1 Implement `getStateInformation()`

Standard APVTS ValueTree serialization:

```cpp
void PluginProcessor::getStateInformation(juce::MemoryBlock& destData) {
    auto state = mAudioProcessorValueTreeStatePtr->copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    this->copyXmlToBinary(*xml, destData);
}
```

#### 1.2 Implement `setStateInformation()`

```cpp
void PluginProcessor::setStateInformation(const void* data, int sizeInBytes) {
    auto xml = this->getXmlFromBinary(data, sizeInBytes);
    if (xml != nullptr) {
        auto newState = juce::ValueTree::fromXml(*xml);
        if (newState.isValid())
            mAudioProcessorValueTreeStatePtr->replaceState(newState);
    }
    // Re-apply engine state from the restored tree
    this->drumKitEnginePtr->useState(*mAudioProcessorValueTreeStatePtr);
}
```

#### 1.3 Write layout IDs into ValueTree when changed

In `DrumKitEngine`, after `setMicLayout()` / `setKitLayout()` succeed, write the ID back to the ValueTree state so it persists. This requires passing or storing a reference to the APVTS.

**Option:** Add a method `DrumKitEngine::syncStateToTree(APVTS& treeState)` that writes current layout IDs and instrument map into `treeState.state` properties. Call it from `PluginProcessor` after any layout change.

**OR** make `useState()` bidirectional — after resolving layouts, write the resolved IDs back:

```cpp
void DrumKitEngine::useState(juce::AudioProcessorValueTreeState& treeState) {
    // ... existing resolution logic (reads mic_layout, kit_layout) ...

    // Write resolved IDs back so they persist in state
    if (this->activeMicLayoutPtr)
        treeState.state.setProperty("mic_layout", this->activeMicLayoutPtr->id, nullptr);
    if (this->activeKitLayoutPtr)
        treeState.state.setProperty("kit_layout", this->activeKitLayoutPtr->id, nullptr);
}
```

Note: `useState` signature changes from `const APVTS&` to `APVTS&`.

---

### Phase 2: Plugin Reconstruction in `setState()` (Option B)

**Files:**
- `JUCE/modules/juce_audio_plugin_client/juce_audio_plugin_client_VST3.cpp`
- `JUCE/modules/juce_audio_plugin_client/detail/juce_CreatePluginFilter.h`
- `source/PluginProcessor.h` / `source/PluginProcessor.cpp`

#### 2.1 Overview

Modify `JuceVST3Component::setState()` to:
1. Read the raw state bytes into a `MemoryBlock`
2. Peek at the state to extract the mic layout ID (without fully applying it)
3. Compare against the current `AudioProcessor`'s mic layout
4. If different: destroy old `pluginInstance`, stash the state data in a thread-local or static, create a new `pluginInstance` (which reads the stash in its constructor), rewire internal pointers
5. Call `setStateInformation()` on the (possibly new) instance

#### 2.2 State Peeking

Add a **static** method to `PluginProcessor`:

```cpp
// Peeks at serialized state data and extracts the mic_layout property without
// constructing a full engine. Returns empty string if not found.
static juce::String peekMicLayoutFromState(const void* data, int sizeInBytes);
```

Implementation: parse XML → ValueTree, read the `"mic_layout"` property. No side effects.

#### 2.3 Stash Mechanism for Constructor

Add a static thread-local to `PluginProcessor`:

```cpp
// In PluginProcessor.h:
static inline thread_local const void* sPendingStateData = nullptr;
static inline thread_local int sPendingStateSize = 0;
```

The `PluginProcessor` constructor checks this stash:

```cpp
PluginProcessor::PluginProcessor()
    : AudioProcessor([&]() {
        MicLayout layout;

        // If pending state contains a mic layout, use it for bus construction
        if (sPendingStateData != nullptr) {
            auto layoutId = peekMicLayoutFromState(sPendingStateData, sPendingStateSize);
            if (layoutId.isNotEmpty() && layoutId != "default") {
                auto resolved = DrumKitEngine::resolveMicLayoutById(layoutId);
                if (resolved.has_value()) {
                    layout = *resolved;
                } else {
                    layout = DrumKitEngine::resolveMicLayoutFromConfig();
                }
            } else {
                layout = DrumKitEngine::resolveMicLayoutFromConfig();
            }
        } else {
            layout = DrumKitEngine::resolveMicLayoutFromConfig();
        }

        return buildBusesFromMicLayout(layout);
    }())
    // ...
```

Extract the bus-building lambda into a static method `buildBusesFromMicLayout(const MicLayout&)` for reuse.

#### 2.4 New Static Helper on DrumKitEngine

```cpp
// Resolves a mic layout by ID from the known layout files (same scan as resolveMicLayoutFromConfig,
// but takes an explicit ID instead of reading config.yaml).
static std::optional<MicLayout> resolveMicLayoutById(const juce::String& layoutId);
```

This reuses the `MicLayoutManager` scan logic already in `resolveMicLayoutFromConfig()`.

#### 2.5 Modify `JuceVST3Component::setState()`

This is the core JUCE modification. In `juce_audio_plugin_client_VST3.cpp`:

```cpp
tresult PLUGIN_API setState (IBStream* state) override
{
    assertHostMessageThread();
    if (state == nullptr)
        return kInvalidArgument;

    FUnknownPtr<IBStream> stateRefHolder (state);

    // Read the entire state into a MemoryBlock first
    MemoryBlock rawState;
    {
        MemoryOutputStream out(rawState, false);
        const size_t chunkSize = 4096;
        HeapBlock<char> buffer(chunkSize);
        for (;;) {
            Steinberg::int32 bytesRead = 0;
            auto status = state->read(buffer, (Steinberg::int32)chunkSize, &bytesRead);
            if (bytesRead <= 0 || (status != kResultTrue && !detail::PluginUtilities::getHostType().isWavelab()))
                break;
            out.write(buffer, (size_t)bytesRead);
        }
    }

    if (rawState.isEmpty())
        return kResultFalse;

    // Check if we need to reconstruct the AudioProcessor for a different mic layout
    // (Plugin-specific: calls PluginProcessor::peekMicLayoutFromState)
    // This is the only plugin-specific coupling point.
    maybeReconstructForState(rawState);

    // Now apply state to the (possibly new) plugin instance
    setStateInformation(rawState.getData(), (int)rawState.getSize());
    return kResultTrue;
}
```

The `maybeReconstructForState()` method:

```cpp
void maybeReconstructForState(const MemoryBlock& rawState)
{
    // Extract mic layout ID from state to compare with current
    auto requestedLayout = PluginProcessor::peekMicLayoutFromState(
        rawState.getData(), (int)rawState.getSize());

    auto currentLayout = /* get current mic layout ID from pluginInstance */;

    if (requestedLayout == currentLayout || requestedLayout.isEmpty())
        return; // no reconstruction needed

    // Stash state data for the constructor to peek at
    PluginProcessor::sPendingStateData = rawState.getData();
    PluginProcessor::sPendingStateSize = (int)rawState.getSize();

    // Tear down existing instance
    if (pluginInstance->getPlayHead() == this)
        pluginInstance->setPlayHead(nullptr);

    // Create new AudioProcessor (constructor will read the stash)
    auto newInstance = createPluginFilterOfType(AudioProcessor::wrapperType_VST3);

    PluginProcessor::sPendingStateData = nullptr;
    PluginProcessor::sPendingStateSize = 0;

    // Swap in the new instance
    pluginInstance = newInstance.release();
    comPluginInstance = VSTComSmartPtr(new JuceAudioProcessor(pluginInstance), IncrementRef::yes);
    pluginInstance->setPlayHead(this);

    // Re-link edit controller if connected
    if (juceVST3EditController != nullptr)
        juceVST3EditController->setAudioProcessor(comPluginInstance.get());

    // Re-prepare (no audio yet, just internal routing)
    processContext.sampleRate = processSetup.sampleRate;
    preparePlugin(processSetup.sampleRate, (int)processSetup.maxSamplesPerBlock, CallPrepareToPlay::no);
}
```

#### 2.6 Concerns & Mitigations

| Concern | Mitigation |
|---------|-----------|
| **Memory leak of old `pluginInstance`** | The old raw pointer must be `delete`d before reassignment. Currently it's a raw `AudioProcessor*`. Wrap the delete explicitly. |
| **Edit controller holds stale `comPluginInstance`** | `setAudioProcessor()` re-binds it. Verify the edit controller re-reads parameter state after swap. |
| **Race with audio thread** | `setState()` is called from the message thread when `active == false` (VST3 spec). No audio processing during reconstruction. But add `jassert(!active)` as safety. |
| **Standalone wrapper** | Standalone doesn't call `setState()` the same way — it uses `getStateInformation`/`setStateInformation` directly. The stash mechanism is harmless (null by default). |
| **Thread-local correctness** | `thread_local` ensures multiple plugin instances being loaded simultaneously on different threads don't interfere. |
| **First-load (no saved state)** | `sPendingStateData` is null → constructor falls back to `resolveMicLayoutFromConfig()` as today. |

#### 2.7 Risk: Host Caches Bus Topology at Component Creation

Some hosts (notably Cubase, Studio One) cache bus topology from the VST3 `getAudioBusInfo()` response **at initialization time** and might not re-query after `setState()`. If so, the host's mixer view shows stale bus info even though the actual audio routing is correct.

**Mitigation:** After reconstruction, call `restartComponent(Vst::kIoChanged)` to hint the host to re-query bus info. Some hosts respect this, some don't.

---

### Phase 3: Bus::setName() (Option A — supplementary safety net)

**File:** `JUCE/modules/juce_audio_processors_headless/processors/juce_AudioProcessor.h`

One-line addition next to `getName()` in the `Bus` class:

```cpp
const String& getName() const noexcept                          { return name; }
void setName(const String& newName) noexcept                    { name = newName; }
```

This is used as a fallback in `setStateInformation()` if Option B reconstruction didn't fire (e.g., same layout ID, or running in standalone mode). After `useState()` resolves the mic layout, rename buses to match:

```cpp
// In PluginProcessor::setStateInformation(), after useState():
void PluginProcessor::renameBusesFromMicLayout() {
    const auto* mic = this->drumKitEnginePtr->getMicLayout();
    if (mic == nullptr) return;

    int busIdx = 0;
    for (const auto& m : mic->mics) {
        int ch = std::max(1, m.specs.channels);
        if (ch >= 2) {
            if (auto* bus = this->getBus(false, busIdx++)) bus->setName(m.name + " L");
            if (auto* bus = this->getBus(false, busIdx++)) bus->setName(m.name + " R");
        } else {
            if (auto* bus = this->getBus(false, busIdx++)) bus->setName(m.name);
        }
    }
    int auxIndex = 1;
    while (busIdx < DrumKitEngine::TOTAL_HOST_OUTPUT_CHANNELS) {
        if (auto* bus = this->getBus(false, busIdx++))
            bus->setName("AUX " + juce::String(auxIndex++));
    }
}
```

---

## File Change Summary

| File | Change | Phase |
|------|--------|-------|
| `source/PluginProcessor.cpp` | Implement `getStateInformation()`, `setStateInformation()`, extract `buildBusesFromMicLayout()`, add `peekMicLayoutFromState()`, constructor reads stash, add `renameBusesFromMicLayout()` | 1, 2, 3 |
| `source/PluginProcessor.h` | Add statics `sPendingStateData`/`sPendingStateSize`, declare new methods | 2 |
| `source/Engine/DrumKitEngine.h` | Add `static resolveMicLayoutById()`, change `useState()` signature to non-const APVTS ref | 1, 2 |
| `source/Engine/DrumKitEngine.cpp` | Implement `resolveMicLayoutById()`, write resolved IDs back in `useState()` | 1, 2 |
| `JUCE/.../juce_audio_plugin_client_VST3.cpp` | Modify `setState()` to read state first, call `maybeReconstructForState()`, add that method | 2 |
| `JUCE/.../juce_AudioProcessor.h` | Add `Bus::setName()` | 3 |

## Implementation Order

1. **Phase 1** — state persistence. Test: save/load project, verify `mic_layout` round-trips in state.
2. **Phase 3** — `Bus::setName()` (trivial, no risk). Test: rename buses after `setStateInformation()`, verify names in debug log.
3. **Phase 2** — reconstruction in `setState()`. Test: save project with layout X, close, reopen, verify bus names match layout X in host mixer.

Phase 3 before Phase 2 because it provides a fallback while Phase 2 is being debugged.

## Open Questions

1. **Should `maybeReconstructForState()` live in the JUCE wrapper or should we add a virtual hook?** — A virtual method like `AudioProcessor::shouldReconstructForState(const void* data, int size)` would be cleaner and less JUCE-fork-specific. The wrapper calls it, and if it returns true, triggers reconstruction. This way the JUCE mod is generic, not plugin-specific.

2. **Do we want `restartComponent(kIoChanged)` after reconstruction?** — Depends on target DAW testing. Worth trying.

3. **Old `pluginInstance` cleanup** — The raw pointer needs explicit `delete`. Should we switch it to `unique_ptr` while we're modifying the wrapper? That would be a larger but cleaner refactor.
