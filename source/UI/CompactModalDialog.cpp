#include "CompactModalDialog.h"

#include "UIComponentStyle.h"
#include "UITheme.h"

#include <juce_audio_processors/juce_audio_processors.h>

CompactModalDialog::CompactModalDialog(Options dialogOptions, ConfirmHandler confirmHandler)
    : options(std::move(dialogOptions))
    , onConfirm(std::move(confirmHandler)) {
    this->setWantsKeyboardFocus(true);

    this->titleLabel.setText(this->options.title, juce::dontSendNotification);
    this->titleLabel.setFont(UITheme::Fonts::sectionTitle());
    this->titleLabel.setColour(juce::Label::textColourId, UITheme::Colours::textPrimary);
    this->titleLabel.setJustificationType(juce::Justification::centredLeft);
    this->addAndMakeVisible(this->titleLabel);

    this->messageLabel.setText(this->options.message, juce::dontSendNotification);
    this->messageLabel.setFont(UITheme::Fonts::normal());
    this->messageLabel.setColour(juce::Label::textColourId, UITheme::Colours::textSecondary);
    this->messageLabel.setJustificationType(juce::Justification::centredLeft);
    this->messageLabel.setVisible(this->options.message.isNotEmpty());
    this->addAndMakeVisible(this->messageLabel);

    this->statusLabel.setFont(UITheme::Fonts::small());
    this->statusLabel.setColour(juce::Label::textColourId, UITheme::Colours::accent);
    this->statusLabel.setJustificationType(juce::Justification::centredLeft);
    this->addAndMakeVisible(this->statusLabel);

    for (const auto& field : this->options.fields) {
        auto control = std::make_unique<FieldControl>();
        control->id = field.id;
        control->label.setText(field.label, juce::dontSendNotification);
        UIComponentStyle::configureCaptionLabel(control->label, juce::Justification::centredLeft);
        this->addAndMakeVisible(control->label);

        control->editor.setMultiLine(false);
        control->editor.setFont(UITheme::Fonts::normal());
        control->editor.setText(field.value, false);
        control->editor.setColour(juce::TextEditor::backgroundColourId, UITheme::Colours::bgControl);
        control->editor.setColour(juce::TextEditor::textColourId, UITheme::Colours::textPrimary);
        UIComponentStyle::configureBorderlessTextEditor(control->editor);
        if (field.maximumLength > 0 || field.allowedCharacters.isNotEmpty()) control->editor.setInputRestrictions(field.maximumLength, field.allowedCharacters);
        control->editor.onReturnKey = [this] { this->confirm(); };
        control->editor.onEscapeKey = [this] { this->cancel(); };
        this->addAndMakeVisible(control->editor);
        this->fieldControls.push_back(std::move(control));
    }

    this->confirmButton.setButtonText(this->options.confirmText);
    UIComponentStyle::configureControlButton(this->confirmButton);
    if (this->options.destructive) this->confirmButton.setColour(juce::TextButton::buttonColourId, UITheme::Colours::accentWarning);
    this->confirmButton.onClick = [this] { this->confirm(); };
    this->addAndMakeVisible(this->confirmButton);

    this->cancelButton.setButtonText(this->options.cancelText);
    UIComponentStyle::configureControlButton(this->cancelButton);
    this->cancelButton.onClick = [this] { this->cancel(); };
    this->addAndMakeVisible(this->cancelButton);

    UIComponentStyle::configureDialogCloseButton(this->closeButton);
    this->closeButton.onClick = [this] { this->cancel(); };
    this->addAndMakeVisible(this->closeButton);
}

void CompactModalDialog::show(juce::Component& source, Options options, ConfirmHandler onConfirm) {
    juce::Component* dialogHost = &source;
    for (auto* candidate = &source; candidate != nullptr; candidate = candidate->getParentComponent()) {
        dialogHost = candidate;
        if (dynamic_cast<juce::AudioProcessorEditor*>(candidate) != nullptr) break;
    }

    auto* dialog = new CompactModalDialog(std::move(options), std::move(onConfirm));
    dialogHost->addAndMakeVisible(dialog);
    dialog->setBounds(dialogHost->getLocalBounds());
    dialog->toFront(true);
    dialog->enterModalState(true, nullptr, true);

    juce::Component::SafePointer<CompactModalDialog> safeDialog(dialog);
    juce::MessageManager::callAsync([safeDialog] {
        if (safeDialog != nullptr) safeDialog->focusInitialControl();
    });
}

juce::String CompactModalDialog::getFieldText(const juce::String& fieldId) const {
    for (const auto& control : this->fieldControls)
        if (control->id == fieldId) return control->editor.getText();
    return {};
}

void CompactModalDialog::paint(juce::Graphics& graphics) {
    graphics.fillAll(juce::Colours::black.withAlpha(0.72f));
    graphics.setColour(UITheme::Colours::bgPanelRaised);
    graphics.fillRoundedRectangle(this->panelBounds.toFloat(), 6.0f);
}

void CompactModalDialog::resized() {
    const int availableWidth = juce::jmax(1, this->getWidth() - 16);
    const int availableHeight = juce::jmax(1, this->getHeight() - 16);
    const int panelWidth = juce::jmin(this->preferredWidth, availableWidth);
    const int panelHeight = juce::jmin(this->getPreferredHeight(), availableHeight);
    this->panelBounds = this->getLocalBounds().withSizeKeepingCentre(panelWidth, panelHeight);

    auto area = this->panelBounds.reduced(this->panelPadding);
    auto header = area.removeFromTop(UITheme::Sizes::standardControlHeight);
    this->closeButton.setBounds(header.removeFromRight(UITheme::Sizes::standardControlHeight));
    header.removeFromRight(UITheme::Spacing::sm);
    this->titleLabel.setBounds(header);
    area.removeFromTop(UITheme::Spacing::sm);

    if (this->options.message.isNotEmpty()) {
        this->messageLabel.setBounds(area.removeFromTop(24));
        area.removeFromTop(UITheme::Spacing::sm);
    }

    for (auto& control : this->fieldControls) {
        auto fieldArea = area.removeFromTop(this->fieldRowHeight);
        control->label.setBounds(fieldArea.removeFromTop(12));
        fieldArea.removeFromTop(UITheme::Spacing::xs);
        control->editor.setBounds(fieldArea.removeFromTop(UITheme::Sizes::standardControlHeight));
    }

    this->statusLabel.setBounds(area.removeFromTop(16));
    area.removeFromTop(UITheme::Spacing::sm);
    auto buttons = area.removeFromTop(UITheme::Sizes::standardControlHeight);
    this->cancelButton.setBounds(buttons.removeFromRight(74));
    buttons.removeFromRight(UITheme::Spacing::sm);
    this->confirmButton.setBounds(buttons.removeFromRight(88));
}

void CompactModalDialog::parentSizeChanged() {
    if (auto* parent = this->getParentComponent()) this->setBounds(parent->getLocalBounds());
}

void CompactModalDialog::mouseDown(const juce::MouseEvent& event) {
    if (!this->panelBounds.contains(event.getPosition())) this->cancel();
}

bool CompactModalDialog::keyPressed(const juce::KeyPress& key) {
    if (key == juce::KeyPress::escapeKey) {
        this->cancel();
        return true;
    }
    if (key == juce::KeyPress::returnKey) {
        this->confirm();
        return true;
    }
    return false;
}

int CompactModalDialog::getPreferredHeight() const {
    int height = this->panelPadding * 2 + UITheme::Sizes::standardControlHeight + UITheme::Spacing::sm;
    if (this->options.message.isNotEmpty()) height += 24 + UITheme::Spacing::sm;
    height += static_cast<int>(this->fieldControls.size()) * this->fieldRowHeight;
    height += 16 + UITheme::Spacing::sm + UITheme::Sizes::standardControlHeight;
    return height;
}

void CompactModalDialog::confirm() {
    if (this->closing) return;
    const auto error = this->onConfirm ? this->onConfirm(*this) : juce::String();
    if (error.isNotEmpty()) {
        this->statusLabel.setText(error, juce::dontSendNotification);
        return;
    }
    this->closing = true;
    this->exitModalState(1);
}

void CompactModalDialog::cancel() {
    if (this->closing) return;
    this->closing = true;
    this->exitModalState(0);
}

void CompactModalDialog::focusInitialControl() {
    if (!this->fieldControls.empty()) {
        this->fieldControls.front()->editor.selectAll();
        this->fieldControls.front()->editor.grabKeyboardFocus();
        return;
    }
    this->confirmButton.grabKeyboardFocus();
}
