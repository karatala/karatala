#include "PluginEditor.h"

PluginEditor::PluginEditor(PluginProcessor& p)
    : AudioProcessorEditor(&p)
    , processorRef(p) {
    addAndMakeVisible(inspectButton);
    addAndMakeVisible(instrumentMapEditor);
    addAndMakeVisible(applyButton);
    addAndMakeVisible(statusLabel);

    // this chunk of code instantiates and opens the melatonin inspector
    inspectButton.onClick = [&] {
        if (!inspector) {
            inspector = std::make_unique<melatonin::Inspector>(*this);
            inspector->onClose = [this]() { inspector.reset(); };
        }

        inspector->setVisible(true);
    };

    instrumentMapEditor.setMultiLine(true);
    instrumentMapEditor.setReturnKeyStartsNewLine(true);
    instrumentMapEditor.setScrollbarsShown(true);
    instrumentMapEditor.setTextToShowWhenEmpty("kick1:MyKit/kick-24\nsnare1:MyKit/snare-14:tuning=-20,attack=25,decay=250", juce::Colours::grey);
    instrumentMapEditor.setText(processorRef.getValueTreeState().state["instrument_map"].toString(), false);

    statusLabel.setJustificationType(juce::Justification::topLeft);
    statusLabel.setColour(juce::Label::textColourId, juce::Colours::lightgreen);
    statusLabel.setText(processorRef.getEngine().getLastInstrumentMapStatus(), juce::dontSendNotification);

    applyButton.onClick = [this] {
        auto text = instrumentMapEditor.getText();
        processorRef.getValueTreeState().state.setProperty("instrument_map", text, nullptr);

        const bool applied = processorRef.getEngine().useInstrumentMap(text);
        if (!applied && text.trim().isEmpty()) {
            processorRef.getEngine().mapDefaultKit();
        }

        statusLabel.setText(processorRef.getEngine().getLastInstrumentMapStatus(), juce::dontSendNotification);
    };

    // Make sure that before the constructor has finished, you've set the
    // editor's size to whatever you need it to be.
    setSize(760, 520);
}

PluginEditor::~PluginEditor() {
}

void PluginEditor::paint(juce::Graphics& g) {
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));

    auto area = getLocalBounds();
    g.setColour(juce::Colours::white);
    g.setFont(16.0f);
    auto helloWorld = juce::String("Hello from ") + PRODUCT_NAME_WITHOUT_VERSION + " v" VERSION + " running in " + CMAKE_BUILD_TYPE;
    g.drawText(helloWorld, area.removeFromTop(150), juce::Justification::centred, false);
}

void PluginEditor::resized() {
    auto area = getLocalBounds().reduced(12);
    auto topBar = area.removeFromTop(36);
    inspectButton.setBounds(topBar.removeFromRight(120));

    area.removeFromTop(8);
    auto statusArea = area.removeFromBottom(88);
    auto buttonArea = statusArea.removeFromTop(32);
    applyButton.setBounds(buttonArea.removeFromLeft(180));
    statusArea.removeFromTop(8);
    statusLabel.setBounds(statusArea);

    instrumentMapEditor.setBounds(area);
}
