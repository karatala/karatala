#pragma once

#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <utility>
#include <vector>

class CompactModalDialog final : public juce::Component {
public:
    struct Field {
        juce::String id;
        juce::String label;
        juce::String value;
        int maximumLength = 0;
        juce::String allowedCharacters;
    };

    struct Options {
        juce::String title;
        juce::String message;
        juce::String confirmText { "OK" };
        juce::String cancelText { "Cancel" };
        bool destructive = false;
        std::vector<Field> fields;
    };

    using ConfirmHandler = std::function<juce::String(const CompactModalDialog& dialog)>;

    static void show(juce::Component& source, Options options, ConfirmHandler onConfirm);

    juce::String getFieldText(const juce::String& fieldId) const;

    void paint(juce::Graphics& graphics) override;
    void resized() override;
    void parentSizeChanged() override;
    void mouseDown(const juce::MouseEvent& event) override;
    bool keyPressed(const juce::KeyPress& key) override;

private:
    static constexpr int preferredWidth = 340;
    static constexpr int panelPadding = 10;
    static constexpr int fieldRowHeight = 38;

    struct FieldControl {
        juce::String id;
        juce::Label label;
        juce::TextEditor editor;
    };

    CompactModalDialog(Options options, ConfirmHandler onConfirm);

    Options options;
    ConfirmHandler onConfirm;
    std::vector<std::unique_ptr<FieldControl>> fieldControls;
    juce::Rectangle<int> panelBounds;
    juce::Label titleLabel;
    juce::Label messageLabel;
    juce::Label statusLabel;
    juce::TextButton confirmButton;
    juce::TextButton cancelButton;
    juce::TextButton closeButton;
    bool closing = false;

    int getPreferredHeight() const;
    void confirm();
    void cancel();
    void focusInitialControl();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CompactModalDialog)
};
