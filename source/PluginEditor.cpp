#include "PluginEditor.h"

PluginEditor::PluginEditor(PluginProcessor& p)
    : AudioProcessorEditor(&p)
    , processorRef(p) {
    addAndMakeVisible(inspectButton);
    addAndMakeVisible(referencePanel);
    addAndMakeVisible(referencePanelLabel);
    addAndMakeVisible(instrumentMapEditor);
    addAndMakeVisible(instrumentMapLabel);
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

    referencePanelLabel.setText("Reference", juce::dontSendNotification);
    referencePanelLabel.setFont(juce::Font(12.0f, juce::Font::bold));
    referencePanelLabel.setJustificationType(juce::Justification::centredLeft);

    referencePanel.setMultiLine(true);
    referencePanel.setReadOnly(true);
    referencePanel.setScrollbarsShown(true);
    referencePanel.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 11.0f, juce::Font::plain));
    referencePanel.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff1e1e1e));
    referencePanel.setColour(juce::TextEditor::textColourId, juce::Colour(0xffaaaaaa));
    referencePanel.setText(processorRef.getEngine().buildReferenceText(), false);

    instrumentMapLabel.setText("Instrument Map", juce::dontSendNotification);
    instrumentMapLabel.setFont(juce::Font(12.0f, juce::Font::bold));
    instrumentMapLabel.setJustificationType(juce::Justification::centredLeft);

    instrumentMapEditor.setMultiLine(true);
    instrumentMapEditor.setReturnKeyStartsNewLine(true);
    instrumentMapEditor.setScrollbarsShown(true);
    instrumentMapEditor.setTextToShowWhenEmpty("kick1:MyKit/kick-24\nsnare1:MyKit/snare-14:tuning=-20,attack=25,decay=250", juce::Colours::grey);
    auto instrumentMapText = processorRef.getValueTreeState().state["instrument_map"].toString();
    if (instrumentMapText.trim().isEmpty()) {
        instrumentMapText = processorRef.getEngine().getSuggestedInstrumentMapText();
        if (instrumentMapText.isNotEmpty())
            processorRef.getValueTreeState().state.setProperty("instrument_map", instrumentMapText, nullptr);
    }
    instrumentMapEditor.setText(instrumentMapText, false);

    statusLabel.setJustificationType(juce::Justification::topLeft);
    statusLabel.setColour(juce::Label::textColourId, juce::Colours::lightgreen);
    statusLabel.setText(processorRef.getEngine().getLastInstrumentMapStatus(), juce::dontSendNotification);

    applyButton.onClick = [this] {
        auto text = instrumentMapEditor.getText();
        processorRef.getValueTreeState().state.setProperty("instrument_map", text, nullptr);

        const bool applied = processorRef.getEngine().useInstrumentMap(text);
        if (!applied && text.trim().isEmpty()) {
            processorRef.getEngine().mapDefaultKit();
            const auto generatedMap = processorRef.getEngine().getSuggestedInstrumentMapText();
            instrumentMapEditor.setText(generatedMap, false);
            processorRef.getValueTreeState().state.setProperty("instrument_map", generatedMap, nullptr);
        }

        statusLabel.setText(processorRef.getEngine().getLastInstrumentMapStatus(), juce::dontSendNotification);
    };

    // Make sure that before the constructor has finished, you've set the
    // editor's size to whatever you need it to be.
    setSize(1000, 580);
}

PluginEditor::~PluginEditor() {
}

void PluginEditor::paint(juce::Graphics& g) {
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));

    auto area = getLocalBounds();
    g.setColour(juce::Colours::white);
    g.setFont(16.0f);
    auto helloWorld = juce::String("Hello from ") + JucePlugin_Name;
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

    // Split remaining area: 1/3 reference panel on left, 2/3 instrument map on right
    const int gap = 8;
    const int refWidth = (area.getWidth() - gap) / 3;
    // const int mapWidth = area.getWidth() - gap - refWidth;

    auto refArea = area.removeFromLeft(refWidth);
    area.removeFromLeft(gap);
    auto mapArea = area; // remaining 2/3

    const int labelHeight = 18;
    referencePanelLabel.setBounds(refArea.removeFromTop(labelHeight));
    referencePanel.setBounds(refArea);

    instrumentMapLabel.setBounds(mapArea.removeFromTop(labelHeight));
    instrumentMapEditor.setBounds(mapArea);
}
