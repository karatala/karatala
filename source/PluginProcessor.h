#pragma once

#include "Engine/DrumKitEngine.h"
#include <juce_audio_processors/juce_audio_processors.h>

#if (MSVC)
    #include "ipps.h"
#endif

class PluginProcessor : public juce::AudioProcessor, juce::AudioProcessorValueTreeState::Listener {
public:
    PluginProcessor();
    ~PluginProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override {
        //TODO: probably we can remove all this checks and just return true
        // Reject any non-disabled input buses
        for (int i = 0; i < layouts.inputBuses.size(); ++i) {
            if (!layouts.inputBuses.getReference(i).isDisabled())
                return false;
        }
        // Must have the same number of output buses we declared
        if (layouts.outputBuses.size() != this->getBusCount(false)) return false;
        for (int i = 0; i < layouts.outputBuses.size(); ++i) {
            const auto& requested = layouts.outputBuses.getReference(i);
            if (requested.isDisabled()) continue;

            if (requested != juce::AudioChannelSet::mono()) return false;
        }

        return true;
    }

    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }

    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // juce::AudioProcessorValueTreeState::Listener
    void parameterChanged(const juce::String& parameterId, float newValue) override;

    std::optional<juce::String> getNameForMidiNoteNumber (int note, int midiChannel) override;
    DrumKitEngine& getEngine() { return *this->drumKitEnginePtr; }
    const DrumKitEngine& getEngine() const { return *this->drumKitEnginePtr; }
    juce::AudioProcessorValueTreeState& getValueTreeState() { return *this->mAudioProcessorValueTreeStatePtr; }
    const juce::AudioProcessorValueTreeState& getValueTreeState() const { return *this->mAudioProcessorValueTreeStatePtr; }
    bool setVelocityResponseDefinition(const juce::String& definition);
    void setSampleLoadingModeOverride(SampleLoadingModeOverride mode);

    /** Queue a MIDI message from the UI thread to be processed in the next audio block. */
    void injectMidiFromUI(const juce::MidiMessage& msg);

    /** Set a callback to receive MIDI note-on events for MIDI learn.
     *  The callback will be posted to the message thread. Pass nullptr to disable. */
    void setMidiLearnCallback(std::function<void(int)> callback);
    void setMidiCaptureEnabled(bool enabled);
    bool popCapturedMidiNote(int& noteNumber, int& velocity);

private:
    std::unique_ptr<juce::AudioFormatManager> mAudioFormatManagerPtr;
    std::unique_ptr<juce::AudioProcessorValueTreeState> mAudioProcessorValueTreeStatePtr;
    // std::unique_ptr<PluginPresetManager> mPresetManagerPtr;
    // std::unique_ptr<PluginParameters> mPluginParameters;

    std::unique_ptr<DrumKitEngine> drumKitEnginePtr;

    // MIDI messages injected from UI (pad clicks), consumed in processBlock
    juce::MidiBuffer pendingUiMidiMessages;
    juce::CriticalSection uiMidiLock;

    std::function<void(int)> midiLearnCallback;
    juce::CriticalSection midiLearnLock;
    static constexpr int midiCaptureQueueSize = 256;
    juce::AbstractFifo midiCaptureFifo { midiCaptureQueueSize };
    std::array<int, midiCaptureQueueSize> capturedMidiNotes {};
    std::array<int, midiCaptureQueueSize> capturedMidiVelocities {};
    std::atomic<bool> midiCaptureEnabled { false };

    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginProcessor)
};
