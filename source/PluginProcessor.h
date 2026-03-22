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
        if (!layouts.inputBuses.isEmpty())
            return false;

        const auto current = this->getBusesLayout();

        if (layouts.outputBuses.size() != current.outputBuses.size())
            return false;

        //FIXME: with this for block uncommented it do endless WARNING: Instrument '...' mapping for mic channel N -> host index M is out of range (host channels=N). Falling back to wrap index. but we use all 32 output channels according micLayout, but since this fails output falls back to first 2 channels
        // but when its commented, it creates 32 channels for each output mic, like 32 Overheads, 32 Kick In, etc.
        for (int i = 0; i < layouts.outputBuses.size(); ++i) {
            const auto& requested = layouts.outputBuses.getReference(i);
            const auto& fixed = current.outputBuses.getReference(i);

            if (requested == juce::AudioChannelSet::disabled())
                continue;

            if (requested != fixed)
                return false;
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
    void parameterChanged(const juce::String& parameterId, float newValue) {
        // mPluginParameters->handleParameterChanged(*mAudioProcessorValueTreeStatePtr, parameterId, newValue, *mDSPManager);
        LOG(Debug, "CHECK IT OUT parameterChanged: " + parameterId + ": " + juce::String::formatted("%.3f", newValue));
    }

    std::optional<juce::String> getNameForMidiNoteNumber (int note, int midiChannel) override;

private:
    std::unique_ptr<juce::AudioFormatManager> mAudioFormatManagerPtr;
    std::unique_ptr<juce::AudioProcessorValueTreeState> mAudioProcessorValueTreeStatePtr;
    // std::unique_ptr<PluginPresetManager> mPresetManagerPtr;
    // std::unique_ptr<PluginParameters> mPluginParameters;

    std::unique_ptr<DrumKitEngine> drumKitEnginePtr;

    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginProcessor)
};
