#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <algorithm>

PluginProcessor::PluginProcessor()
    : AudioProcessor([&]() {
        const auto layout = DrumKitEngine::resolveMicLayoutFromConfig();
        BusesProperties props;
        int usedChannels = 0;

        // All buses are mono. Stereo mics get two mono buses with L/R suffixes.
        for (const auto& mic : layout.mics) {
            int channelCount = std::max(1, mic.specs.channels);
            if (usedChannels + channelCount > DrumKitEngine::TOTAL_HOST_OUTPUT_CHANNELS) {
                LOG(Warning, "buildBusesFromMicLayout: MicLayout exceeds " + juce::String(DrumKitEngine::TOTAL_HOST_OUTPUT_CHANNELS) + " channels; truncating at '" + mic.name + "'.");
                break;
            }

            const juce::String baseName = mic.name.isNotEmpty()
                ? mic.name
                : ("Mic " + juce::String(usedChannels + 1));

            if (channelCount >= 2) {
                props = props.withOutput(baseName + " L", juce::AudioChannelSet::mono(), true);
                props = props.withOutput(baseName + " R", juce::AudioChannelSet::mono(), true);
            } else {
                props = props.withOutput(baseName, juce::AudioChannelSet::mono(), true);
            }
            usedChannels += channelCount;
        }

        // Pad remaining channels with mono AUX buses
        int auxIndex = 1;
        while (usedChannels < DrumKitEngine::TOTAL_HOST_OUTPUT_CHANNELS) {
            props = props.withOutput("AUX " + juce::String(auxIndex++), juce::AudioChannelSet::mono(), true);
            ++usedChannels;
        }

        return props;
    }())

    , mAudioFormatManagerPtr(std::make_unique<juce::AudioFormatManager>())
    , mAudioProcessorValueTreeStatePtr(std::make_unique<juce::AudioProcessorValueTreeState>(*this, nullptr, juce::Identifier("plugin_params"), createParameterLayout())
) {
    if (LOG_ENABLED(Info)) {
        juce::String summary = "Output buses:";
        const int outputBusCount = this->getBusCount(false);
        for (int busIndex = 0; busIndex < outputBusCount; ++busIndex) {
            const auto* bus = this->getBus(false, busIndex);
            if (bus == nullptr) continue;
            summary += "\n  [" + juce::String(busIndex) + ": '" + bus->getName() + "'" + (bus->isEnabled() ? "" : ", disabled") + "]";
        }
        LOG(Info, summary);
    }
    this->drumKitEnginePtr = std::make_unique<DrumKitEngine>();

    if (!this->mAudioProcessorValueTreeStatePtr->state.hasProperty("kit_layout"))
        this->mAudioProcessorValueTreeStatePtr->state.setProperty("kit_layout", juce::String(), nullptr);
    if (!this->mAudioProcessorValueTreeStatePtr->state.hasProperty("mic_layout"))
        this->mAudioProcessorValueTreeStatePtr->state.setProperty("mic_layout", juce::String(), nullptr);
    if (!this->mAudioProcessorValueTreeStatePtr->state.hasProperty("instrument_map"))
        this->mAudioProcessorValueTreeStatePtr->state.setProperty("instrument_map", juce::String(), nullptr);

    this->drumKitEnginePtr->init(*mAudioProcessorValueTreeStatePtr);
}

PluginProcessor::~PluginProcessor() {
}

std::optional<juce::String> PluginProcessor::getNameForMidiNoteNumber(int note, int midiChannel) {
    return this->drumKitEnginePtr->getNameForMidiNoteNumber(note, midiChannel);
}

//==============================================================================
int PluginProcessor::getNumPrograms() {
    return 1; // NB: some hosts don't cope very well if you tell them there are 0 programs,
    // so this should be at least 1, even if you're not really implementing programs.
}

int PluginProcessor::getCurrentProgram() {
    return 0;
}

void PluginProcessor::setCurrentProgram(int index) {
    juce::ignoreUnused(index);
}

const juce::String PluginProcessor::getProgramName(int index) {
    juce::ignoreUnused(index);
    return {};
}

void PluginProcessor::changeProgramName(int index, const juce::String& newName) {
    juce::ignoreUnused(index, newName);
}

//==============================================================================
void PluginProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    this->drumKitEnginePtr->prepareToPlay(sampleRate, samplesPerBlock);
}

double PluginProcessor::getTailLengthSeconds() const {
    return 0.0;
}

void PluginProcessor::releaseResources() {}

void PluginProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) {

    juce::ScopedNoDenormals noDenormals;
    auto totalNumInputChannels = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();
    auto numBufferChannels = buffer.getNumChannels();

    // In case we have more outputs than inputs, this code clears any output
    // channels that didn't contain input data, (because these aren't
    // guaranteed to be empty - they may contain garbage).
    // This is here to avoid people getting screaming feedback
    // when they first compile a plugin, but obviously you don't need to keep
    // this code if your algorithm always overwrites all the output channels.
    // Guard against hosts passing an empty buffer (0 channels). Only clear indices
    // that actually exist in the supplied buffer.
    for (auto i = totalNumInputChannels; i < totalNumOutputChannels && i < numBufferChannels; ++i)
        buffer.clear(i, 0, buffer.getNumSamples());

    // This is the place where you'd normally do the guts of your plugin's
    // audio processing...
    // Make sure to reset the state if your inner loop is processing
    // the samples and the outer loop is handling the channels.
    // Alternatively, you can process the samples with the channels
    // interleaved by keeping the same state.
    // Only iterate channels that both the processor and the buffer provide.
    auto channelsToProcess = std::min<int> (totalNumInputChannels, numBufferChannels);
    for (int channel = 0; channel < channelsToProcess; ++channel) {
        auto* channelData = buffer.getWritePointer(channel);
        juce::ignoreUnused(channelData);
        // ..do something to the data...
    }

    if (this->drumKitEnginePtr) {
        this->drumKitEnginePtr->processBlock(buffer, midiMessages);
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout PluginProcessor::createParameterLayout() {
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID { "kitLayoutChoice", 1 }, "Kit Layout", juce::StringArray { "default", "FUK U JUCE" }, 0)); //FIXME: is it even changable later?

    return layout;
}

//==============================================================================
void PluginProcessor::getStateInformation(juce::MemoryBlock& destData) {
    if (auto stateXml = this->mAudioProcessorValueTreeStatePtr->copyState().createXml())
        copyXmlToBinary(*stateXml, destData);
}

void PluginProcessor::setStateInformation(const void* data, int sizeInBytes) {
    auto stateXml = getXmlFromBinary(data, sizeInBytes);
    if (stateXml == nullptr)
        return;

    const auto restoredState = juce::ValueTree::fromXml(*stateXml);
    if (!restoredState.isValid())
        return;

    this->mAudioProcessorValueTreeStatePtr->replaceState(restoredState);
    if (this->drumKitEnginePtr != nullptr)
        this->drumKitEnginePtr->useState(*this->mAudioProcessorValueTreeStatePtr);
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new PluginProcessor();
}

juce::AudioProcessorEditor* PluginProcessor::createEditor() {
    return new PluginEditor(*this);
}
