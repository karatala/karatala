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
        juce::ignoreUnused(layouts);
        return true;
    }

    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return false; }

    const juce::String getName() const override { return JucePlugin_Name; }

    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return true; }
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
        DBG("CHECK IT OUT parameterChanged: " + parameterId + ": " + juce::String::formatted("%.3f", newValue));
    }

private:
    std::unique_ptr<juce::AudioFormatManager> mAudioFormatManagerPtr;
    std::unique_ptr<juce::AudioProcessorValueTreeState> mAudioProcessorValueTreeStatePtr;
    // std::unique_ptr<PluginPresetManager> mPresetManagerPtr;
    // std::unique_ptr<PluginParameters> mPluginParameters;

    std::unique_ptr<DrumKitEngine> mDrumKitEngine;

    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginProcessor)
};
