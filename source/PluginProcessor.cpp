#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
PluginProcessor::PluginProcessor()
    : AudioProcessor(
          BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
              //
              .withOutput("Out 01", juce::AudioChannelSet::stereo(), true)
              .withOutput("Out 02", juce::AudioChannelSet::stereo(), true)
              .withOutput("Out 03", juce::AudioChannelSet::stereo(), true)
              .withOutput("Out 04", juce::AudioChannelSet::stereo(), true)
              .withOutput("Out 05", juce::AudioChannelSet::stereo(), true)
              .withOutput("Out 06", juce::AudioChannelSet::stereo(), true)
              .withOutput("Out 07", juce::AudioChannelSet::stereo(), true)
              .withOutput("Out 08", juce::AudioChannelSet::stereo(), true)
              .withOutput("Out 09", juce::AudioChannelSet::stereo(), true)
              .withOutput("Out 10", juce::AudioChannelSet::stereo(), true)
              .withOutput("Out 11", juce::AudioChannelSet::stereo(), true)
              .withOutput("Out 12", juce::AudioChannelSet::stereo(), true)
              .withOutput("Out 13", juce::AudioChannelSet::stereo(), true)
              .withOutput("Out 14", juce::AudioChannelSet::stereo(), true)
              .withOutput("Out 15", juce::AudioChannelSet::stereo(), true)
              .withOutput("Out 16", juce::AudioChannelSet::stereo(), true)

              )

    , mAudioFormatManagerPtr(std::make_unique<juce::AudioFormatManager>())
    , mAudioProcessorValueTreeStatePtr(std::make_unique<juce::AudioProcessorValueTreeState>(*this, nullptr, juce::Identifier("plugin_params"), createParameterLayout())
          //
      ) {
    mDrumKitEngine = std::make_unique<DrumKitEngine>();
    mDrumKitEngine->init(*mAudioProcessorValueTreeStatePtr);
}

PluginProcessor::~PluginProcessor() {
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
    this->mDrumKitEngine->prepareToPlay(sampleRate, samplesPerBlock);
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

    if (mDrumKitEngine) {
        mDrumKitEngine->processBlock(buffer, midiMessages);
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout PluginProcessor::createParameterLayout() {
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID { "kitLayoutChoice", 1 }, "Kit Layout", juce::StringArray { "default", "FUK U JUCE" }, 0)); //FIXME: is it even changable later?

    return layout;
}

//==============================================================================
void PluginProcessor::getStateInformation(juce::MemoryBlock& destData) {
    // You should use this method to store your parameters in the memory block.
    // You could do that either as raw data, or use the XML or ValueTree classes
    // as intermediaries to make it easy to save and load complex data.
    juce::ignoreUnused(destData);
}

void PluginProcessor::setStateInformation(const void* data, int sizeInBytes) {
    // You should use this method to restore your parameters from this memory block,
    // whose contents will have been created by the getStateInformation() call.
    juce::ignoreUnused(data, sizeInBytes);
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new PluginProcessor();
}

juce::AudioProcessorEditor* PluginProcessor::createEditor() {
    return new PluginEditor(*this);
}
