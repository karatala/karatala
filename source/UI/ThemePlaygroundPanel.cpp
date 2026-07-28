#include "ThemePlaygroundPanel.h"

#include "CompactParameterControl.h"
#include "DrumPadGrid.h"
#include "HorizontalKnobBrick.h"
#include "MidiMapTable.h"
#include "MidiNoteAssignmentControl.h"
#include "MidiNotePicker.h"
#include "ParameterPot.h"
#include "ParameterSlider.h"
#include "StatusBar.h"
#include "UIHelpers.h"
#include "UITheme.h"

#include <vector>

class ThemePlaygroundPanel::Content : public juce::Component {
public:
    Content() {
        this->parameterSlider.setSliderStyle(juce::Slider::LinearHorizontal);
        this->parameterSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, UITheme::Sizes::sliderTextBoxW, UITheme::Sizes::sliderTextBoxH);
        this->parameterSlider.setRange(0.0, 100.0, 1.0);
        this->parameterSlider.setInteraction(50.0, 1.0, 0.1);
        this->parameterSlider.setValue(65.0, juce::dontSendNotification);

        this->compactParameterControl.setInteraction(50.0, 1.0, 0.1);
        this->compactParameterControl.setLabelVisible(true);

        this->midiNoteAssignmentControl.setNotes(36, 38);
        this->midiNoteAssignmentControl.setLayoutMetrics({ 60, UITheme::Sizes::standardControlHeight, 0, 6, 0 });
        this->midiNoteAssignmentControl.onLearnButtonClicked = [this](int slotIdx) {
            this->midiNoteAssignmentControl.setLearningSlot(this->midiNoteAssignmentControl.getLearningSlot() == slotIdx ? -1 : slotIdx);
        };
        this->midiNotePicker.setCurrentNote(36);

        this->drumPadGrid.setGridSize(3, 1);
        this->drumPadGrid.setPadGap(2);
        this->drumPadGrid.populate(nullptr);

        this->midiMapTable.populatePresets({ { "factory", "Factory layout", false }, { "user", "User layout", true } }, "factory");
        this->statusBar.setStatusText("StatusBar success / ready text");
        this->statusBar.setInfoText("StatusBar information text");

        this->standardLabel.setText("juce::Label using configureCaptionLabel", juce::dontSendNotification);
        UIHelpers::configureCaptionLabel(this->standardLabel, juce::Justification::centredLeft);

        UIHelpers::configureControlButton(this->controlButton);
        this->controlButton.setTooltip("TooltipWindow renders this tooltip after the normal delay");
        UIHelpers::configureCategoryButton(this->categoryButton);
        UIHelpers::configureToggleButton(this->toggleTextButton);
        this->toggleTextButton.setToggleState(true, juce::dontSendNotification);
        this->standardToggle.setColour(juce::ToggleButton::textColourId, UITheme::Colours::textSecondary);
        this->standardToggle.setToggleState(true, juce::dontSendNotification);

        UIHelpers::configureComboBox(this->standardCombo);
        this->standardCombo.addItemList({ "Standard item", "Second item", "Disabled-looking text is separate" }, 1);
        this->standardCombo.setSelectedId(1, juce::dontSendNotification);

        UIHelpers::configureComboBox(this->layoutCombo, UIHelpers::ComboBoxStyle::LayoutSelector);
        this->layoutCombo.addItemList({ "Long layout selector item", "Another layout" }, 1);
        this->layoutCombo.setSelectedId(1, juce::dontSendNotification);

        UIHelpers::configureComboBox(this->midiCombo, UIHelpers::ComboBoxStyle::MidiControllerList);
        this->midiCombo.addItemList({ "1 / Modulation Wheel", "7 / Channel Volume", "11 / Expression", "64 / Sustain" }, 1);
        this->midiCombo.setSelectedId(3, juce::dontSendNotification);

        this->filterEditor.setFont(UITheme::Fonts::normal().withHeight(static_cast<float>(UITheme::Sizes::standardControlTextHeight)));
        this->filterEditor.setText("Source filter TextEditor", false);
        this->filterEditor.setTextToShowWhenEmpty("filter...", UITheme::Colours::textMuted);

        UIHelpers::configureMonoTextEditor(this->monoEditor, UITheme::Colours::textSecondary, true);
        this->monoEditor.setText("kick1/center:DemoKit/kick-24/center\n# Monospaced editor style", false);

        this->listBox.setModel(&this->listModel);
        this->listBox.setColour(juce::ListBox::backgroundColourId, UITheme::Colours::bgControlAlt);

        UIHelpers::configureControlButton(this->popupMenuButton);
        this->popupMenuButton.onClick = [this] {
            juce::PopupMenu popupMenu;
            popupMenu.setLookAndFeel(&this->getLookAndFeel());
            popupMenu.addItem(1, "Normal popup item");
            popupMenu.addItem(2, "Selected popup item", true, true);
            popupMenu.addSeparator();
            popupMenu.addItem(3, "Disabled popup item", false, false);
            popupMenu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this->popupMenuButton), [](int selectedItemId) { juce::ignoreUnused(selectedItemId); });
        };

        const std::vector<juce::Component*> visibleComponents { &this->parameterSlider, &this->parameterPot, &this->horizontalKnobBrick, &this->compactParameterControl, &this->midiNoteAssignmentControl,
                                                                &this->midiNotePicker, &this->drumPadGrid, &this->midiMapTable, &this->statusBar, &this->standardLabel, &this->controlButton, &this->categoryButton,
                                                                &this->toggleTextButton, &this->standardToggle, &this->standardCombo, &this->layoutCombo, &this->midiCombo, &this->filterEditor, &this->monoEditor,
                                                                &this->listBox, &this->popupMenuButton };
        for (auto* component : visibleComponents) {
            this->addAndMakeVisible(component);
        }

        this->customDemos = {
            { "ParameterSlider", "Shared linear parameter slider with editable value box", &this->parameterSlider, 54, 420 },
            { "ParameterPot", "Shared vertical rotary control with caption and editable value", &this->parameterPot, 92, 110 },
            { "HorizontalKnobBrick", "Horizontal ParameterPot layout used in compact rows", &this->horizontalKnobBrick, 68, 230 },
            { "CompactParameterControl", "Compact value, label, track, and accent fill", &this->compactParameterControl, 56, 170 },
            { "MidiNoteAssignmentControl", "Two note buttons and two MIDI learn buttons", &this->midiNoteAssignmentControl, 50,
              this->midiNoteAssignmentControl.getPreferredWidth(), UITheme::Sizes::standardControlHeight },
            { "DrumPadGrid / DrumPad", "Empty pad state. Instrument colors are shown in the color section", &this->drumPadGrid, 112, 430 },
            { "MidiMapTable", "Map toolbar and layout-selector styling without engine data", &this->midiMapTable, 100, 650 },
            { "MidiNotePicker", "Full note picker used by MidiNoteAssignmentControl callouts", &this->midiNotePicker, 326, 380 },
            { "StatusBar", "Success status and secondary information text", &this->statusBar, 44, 650 },
        };

        this->standardDemos = {
            { "juce::Label", "Direct caption label styled by UIHelpers", &this->standardLabel, 40, 420, UITheme::Sizes::standardControlHeight },
            { "juce::TextButton / control", "Standard 20 px project button", &this->controlButton, 42, 220, UITheme::Sizes::standardControlHeight },
            { "juce::TextButton / category", "Standard 20 px connected category selector", &this->categoryButton, 42, 220, UITheme::Sizes::standardControlHeight },
            { "juce::TextButton / toggle", "Standard 20 px active toggle", &this->toggleTextButton, 42, 220, UITheme::Sizes::standardControlHeight },
            { "juce::ToggleButton", "Standard 20 px toggle with normal 14 px text", &this->standardToggle, 42, 260, UITheme::Sizes::standardControlHeight },
            { "juce::ComboBox / standard", "Standard 20 px list selector", &this->standardCombo, 44, 340, UITheme::Sizes::standardControlHeight },
            { "juce::ComboBox / layout", "Standard 20 px layout selector", &this->layoutCombo, 44, 340, UITheme::Sizes::standardControlHeight },
            { "juce::ComboBox / MIDI", "Standard 20 px MIDI controller selector", &this->midiCombo, 44, 340, UITheme::Sizes::standardControlHeight },
            { "juce::TextEditor / filter", "Source-picker filter with normal 14 px text", &this->filterEditor, 46, 420, UITheme::Sizes::standardControlHeight },
            { "juce::TextEditor / mono", "Monospaced calibration and compact value editor style", &this->monoEditor, 78, 520 },
            { "juce::ListBox", "List rows used by SourcePickerPopup", &this->listBox, 108, 420 },
            { "juce::PopupMenu", "Temporary menu opened by a standard 20 px button", &this->popupMenuButton, 42, 220, UITheme::Sizes::standardControlHeight },
        };

        this->fontUsages = {
            { "normal()", UITheme::Fonts::normal(), "Instrument and articulation names, layer source details, and normal drum-pad labels" },
            { "normal() at 14 px", UITheme::Fonts::normal().withHeight(static_cast<float>(UITheme::Sizes::standardControlTextHeight)),
              "Standard buttons, toggles, dropdowns, menus, source lists, Raw, and Fix" },
            { "normal().boldened()", UITheme::Fonts::normal().boldened(), "Drum-pad articulation cell labels" },
            { "label()", UITheme::Fonts::label(), "Editable values below ParameterPot controls" },
            { "label() at 13 px", UITheme::Fonts::label().withHeight(13.0f), "Editable values inside HorizontalKnobBrick controls" },
            { "labelBold()", UITheme::Fonts::labelBold(), "Responsive drum-pad instrument names" },
            { "labelBoldMedium()", UITheme::Fonts::labelBoldMedium(), "CompactParameterControl values and Kit intro" },
            { "labelBold() at 10-15 px", UITheme::Fonts::labelBold().withHeight(15.0f), "Responsive DrumPad instrument names" },
            { "labelBold() at 15 px", UITheme::Fonts::labelBold().withHeight(15.0f), "Crosstalk calibration status" },
            { "small()", UITheme::Fonts::small(), "Control captions, CompactParameterControl labels, warnings, and MIDI headers" },
            { "small().boldened()", UITheme::Fonts::small().boldened(), "Instrument and Map table headers, and vertical DrumPad instrument rail" },
            { "small() at 11 px", UITheme::Fonts::small().withHeight(11.0f), "Empty DrumPad label" },
            { "table()", UITheme::Fonts::table(), "Base of tableSmall(). No direct UI use found" },
            { "tableSmall()", UITheme::Fonts::tableSmall(), "Both StatusBar text fields" },
            { "mono()", UITheme::Fonts::mono(), "Balance fields, MIDI note names, and calibration profiles" },
            { "mono() at 10.5 px", UITheme::Fonts::mono().withHeight(10.5f), "MidiNotePicker note names and numbers" },
            { "sectionTitle()", UITheme::Fonts::sectionTitle(), "Kit assignment heading and shared section labels" },
            { "sectionTitle() at 16 px", UITheme::Fonts::sectionTitle().withHeight(16.0f), "Crosstalk calibration title" },
            { "sectionTitle() at 18 px", UITheme::Fonts::sectionTitle().withHeight(18.0f), "Kit page title" },
            { "Graphics default", juce::Font(juce::FontOptions(14.0f)), "Large rotary-control tick labels when enough space is available" },
            { "TooltipWindow default", juce::Font(juce::FontOptions(13.0f).withStyle("Bold")), "TooltipWindow text inherited from JUCE LookAndFeel_V4" },
            { "AlertWindow title", juce::Font(juce::FontOptions(18.0f).withStyle("Bold")), "AlertWindow titles inherited from JUCE LookAndFeel_V4" },
            { "AlertWindow message", juce::Font(juce::FontOptions(16.0f)), "AlertWindow messages inherited from JUCE LookAndFeel_V4" },
            { "AlertWindow field", juce::Font(juce::FontOptions(14.0f)), "Other AlertWindow text inherited from JUCE LookAndFeel_V4" },
        };

        this->themeColours = {
            { "bgApp", UITheme::Colours::bgApp, "Editor window background and DrumPad title rail" },
            { "bgEditor", UITheme::Colours::bgEditor, "Raw text editor and MIDI map background" },
            { "bgPanel", UITheme::Colours::bgPanel, "Sidebar, StatusBar, popup groups, and knob gradient" },
            { "bgPanelRaised", UITheme::Colours::bgPanelRaised, "Menus, popup surfaces, and default widget background" },
            { "bgControl", UITheme::Colours::bgControl, "Buttons, ComboBox fields, category controls, and source controls" },
            { "bgControlAlt", UITheme::Colours::bgControlAlt, "Source picker lists and pad-popup source buttons" },
            { "bgControlRaised", UITheme::Colours::bgControlRaised, "Knob gradient and inactive MIDI learn buttons" },
            { "borderSubtle", UITheme::Colours::borderSubtle, "Sidebar, StatusBar, table, ComboBox, and routing borders" },
            { "borderMid", UITheme::Colours::borderMid, "Rotary tracks, popup highlights, note-grid gaps, and tab background" },
            { "borderStrong", UITheme::Colours::borderStrong, "Knob edges and popup outer borders" },
            { "textPrimary", UITheme::Colours::textPrimary, "Main labels, editor text, menu text, selected controls, and pad text" },
            { "textSecondary", UITheme::Colours::textSecondary, "Captions, values, ComboBox arrows, table text, and log text" },
            { "textInfo", UITheme::Colours::textInfo, "Kit intro and StatusBar information" },
            { "textMuted", UITheme::Colours::textMuted, "Disabled values, empty states, placeholders, and table headers" },
            { "textSubtle", UITheme::Colours::textSubtle, "ParameterPot and CompactParameterControl captions" },
            { "textAccent", UITheme::Colours::textAccent, "Warnings and articulation or popup group labels" },
            { "textAccentSoft", UITheme::Colours::textAccentSoft, "MIDI map controller articulation text" },
            { "textSuccess", UITheme::Colours::textSuccess, "StatusBar success text" },
            { "accent", UITheme::Colours::accent, "Knob or slider fill, active marks, and destructive remove text" },
            { "accentSoft", UITheme::Colours::accentSoft, "Source picker action indicator" },
            { "accentWarning", UITheme::Colours::accentWarning, "Active toggles, invalid sources, and routing warnings" },
            { "broadEditDanger", UITheme::Colours::broadEditDanger, "Active LINK ARTS and LINK TOM BLEED controls" },
            { "midiLearnIdle", UITheme::Colours::midiLearnIdle, "Inactive MIDI learn symbol" },
            { "midiLearnActive", UITheme::Colours::midiLearnActive, "Active MIDI learn button background" },
            { "separator", UITheme::Colours::separator, "DrumPad cell and outer separators" },
            { "padEmpty", UITheme::Colours::padEmpty, "Empty DrumPad base" },
            { "listRowOdd", UITheme::Colours::listRowOdd, "Alternating SourcePickerPopup row" },
            { "listSelection", UITheme::Colours::listSelection, "Selected list row and TextEditor selection" },
            { "selectionStrong", UITheme::Colours::selectionStrong, "Selected source category and MIDI note" },
            { "keyWhite", UITheme::Colours::keyWhite, "MidiNotePicker white key cells" },
            { "keyWhiteText", UITheme::Colours::keyWhiteText, "Text on white MIDI cells and active learn button" },
            { "keyBlack", UITheme::Colours::keyBlack, "MidiNotePicker black key cells" },
            { "keyBlackText", UITheme::Colours::keyBlackText, "Text on black MIDI cells" },
            { "transparent", UITheme::Colours::transparent, "Hidden slider outlines and transparent remove-button background" },
        };

        this->instrumentColours = {
            { "instrumentType Kick", UITheme::Colours::instrumentType(KitLib::InstrumentSpec::Type::Kick), "Kick DrumPads" },
            { "instrumentType Snare", UITheme::Colours::instrumentType(KitLib::InstrumentSpec::Type::Snare), "Snare DrumPads" },
            { "instrumentType HiHat", UITheme::Colours::instrumentType(KitLib::InstrumentSpec::Type::HiHat), "Hi-hat DrumPads" },
            { "instrumentType Tom", UITheme::Colours::instrumentType(KitLib::InstrumentSpec::Type::Tom), "Tom DrumPads" },
            { "instrumentType Ride", UITheme::Colours::instrumentType(KitLib::InstrumentSpec::Type::Ride), "Ride DrumPads" },
            { "instrumentType Crash", UITheme::Colours::instrumentType(KitLib::InstrumentSpec::Type::Crash), "Crash DrumPads" },
            { "instrumentType Splash", UITheme::Colours::instrumentType(KitLib::InstrumentSpec::Type::Splash), "Splash DrumPads" },
            { "instrumentType China", UITheme::Colours::instrumentType(KitLib::InstrumentSpec::Type::China), "China DrumPads" },
            { "instrumentType cymbal family", UITheme::Colours::instrumentType(KitLib::InstrumentSpec::Type::Cymbal), "Bell, stack, chopper, gong, karatala, and generic cymbal DrumPads" },
            { "instrumentType drum family", UITheme::Colours::instrumentType(KitLib::InstrumentSpec::Type::Drum), "Octoban, hand-drum, world-drum, and generic drum DrumPads" },
            { "instrumentType percussion family", UITheme::Colours::instrumentType(KitLib::InstrumentSpec::Type::Percussion), "Cowbell, shaker, blocks, and other percussion DrumPads" },
            { "instrumentType other", UITheme::Colours::instrumentType(KitLib::InstrumentSpec::Type::Other), "Unknown and other DrumPads" },
        };

        this->derivedColours = {
            { "white at 5%", juce::Colours::white.withAlpha(0.05f), "TextButton upper outline" },
            { "black at 5%", juce::Colours::black.withAlpha(0.05f), "TextButton lower shadow" },
            { "white at 40%", juce::Colours::white.withAlpha(0.4f), "Idle ScrollBar thumb" },
            { "white at 60%", juce::Colours::white.withAlpha(0.6f), "Hovered or dragged ScrollBar thumb" },
            { "textPrimary at 20%", UITheme::Colours::textPrimary.withAlpha(0.2f), "PopupMenu separators" },
            { "textPrimary at 25%", UITheme::Colours::textPrimary.withAlpha(0.25f), "Inactive PopupMenu text" },
            { "textPrimary at 80%", UITheme::Colours::textPrimary.withAlpha(0.8f), "SourcePickerPopup secondary path text" },
            { "textPrimary at 85%", UITheme::Colours::textPrimary.withAlpha(0.85f), "Active PopupMenu and DrumPad articulation text" },
            { "textPrimary at 90%", UITheme::Colours::textPrimary.withAlpha(0.9f), "DrumPad instrument name" },
            { "textMuted at 75%", UITheme::Colours::textMuted.withAlpha(0.75f), "SourcePickerPopup empty or filter message" },
            { "bgControl at 75%", UITheme::Colours::bgControl.withMultipliedAlpha(0.75f), "Normal enabled TextButton base before its darker gradient" },
            { "bgControl at 30%", UITheme::Colours::bgControl.withMultipliedAlpha(0.3f), "Normal disabled TextButton base before its darker gradient" },
            { "bgControl 75%, darker 10%", UITheme::Colours::bgControl.withMultipliedAlpha(0.75f).darker(0.1f), "TextButton upper gradient color" },
            { "bgControl 75%, darker 20%", UITheme::Colours::bgControl.withMultipliedAlpha(0.75f).darker(0.2f), "TextButton lower gradient color" },
            { "textSecondary at 80%", UITheme::Colours::textSecondary.withAlpha(0.8f), "Enabled ComboBox arrow" },
            { "textSecondary at 20%", UITheme::Colours::textSecondary.withAlpha(0.2f), "Disabled ComboBox arrow" },
            { "bgApp darker 20%", UITheme::Colours::bgApp.darker(0.2f), "Resize-corner dark line" },
            { "bgApp brighter 4%", UITheme::Colours::bgApp.brighter(0.04f), "Resize-corner light line" },
            { "borderMid brighter 10%", UITheme::Colours::borderMid.brighter(0.1f), "Tab upper gradient color when a tab is present" },
            { "borderMid darker 10%", UITheme::Colours::borderMid.darker(0.1f), "Tab lower gradient color when a tab is present" },
            { "bgPanelRaised darker 30%", UITheme::Colours::bgPanelRaised.darker(0.3f), "Tab outline when a tab is present" },
            { "pad color at 25% brightness", UITheme::Colours::instrumentType(KitLib::InstrumentSpec::Type::Kick).withMultipliedBrightness(0.25f), "Representative empty articulation cell derived from its instrument color" },
            { "pad color at 30% brightness", UITheme::Colours::instrumentType(KitLib::InstrumentSpec::Type::Kick).withMultipliedBrightness(0.3f), "Representative simple DrumPad body derived from its instrument color" },
            { "pad color at 31% brightness", UITheme::Colours::instrumentType(KitLib::InstrumentSpec::Type::Kick).withMultipliedBrightness(0.31f), "Representative active articulation cell derived from its instrument color" },
        };

        this->projectInventory = {
            { "PluginEditor", "Top-level editor that switches all sidebar modes" },
            { "ThemePlaygroundPanel", "This scrollable visual audit page" },
            { "Sidebar", "Main navigation and performance knobs" },
            { "Sidebar::ModeButton", "TextButton subtype used for each sidebar mode" },
            { "StatusBar", "Live example above and persistent bottom status" },
            { "KitHomePanel", "Kit setup mode. Requires live engine and library data" },
            { "DrumPadGrid", "Live empty-state example above and Zone mode grid" },
            { "DrumPad", "Rendered inside DrumPadGrid" },
            { "MidiMapTable", "Live toolbar example above and Map mode table" },
            { "InstrumentConfigPanel", "Instrument mode. Requires live engine and map data" },
            { "EngineSettingsPanel", "Settings mode. Requires live engine settings" },
            { "TroubleshootingPanel", "Fix mode. Requires logger and engine state" },
            { "PadContextPopup", "Zone editing popup. Requires a selected instrument slot" },
            { "PadContextPopup::LayerContentComponent", "Internal layer container inside PadContextPopup" },
            { "CrosstalkCalibrationPopup", "Calibration popup. Requires MIDI capture and engine state" },
            { "SourcePickerPopup", "Source list popup shared by instrument and pad editors" },
            { "DismissOverlay", "Transparent click-capture layer behind popups" },
            { "ScrollablePanel", "Shared Viewport container used by long panels" },
            { "ParameterSlider", "Live shared slider example above" },
            { "ParameterPot", "Live shared rotary control example above" },
            { "HorizontalKnobBrick", "Live horizontal ParameterPot example above" },
            { "CompactParameterControl", "Live compact control example above" },
            { "MidiNoteAssignmentControl", "Live note and MIDI learn example above" },
            { "MidiNotePicker", "Live note grid example above and CallOutBox content" },
            { "LookAndFeel", "Non-component renderer for buttons, sliders, menus, scrollbars, ComboBoxes, and tabs" },
            { "UIHelpers", "Non-component functions that apply shared control styles" },
            { "UITheme", "Non-component source of named fonts, colors, spacing, and sizes" },
        };

        this->juceInventory = {
            { "juce::AudioProcessorEditor", "Base of PluginEditor" },
            { "juce::Component", "Base container for panels, rows, pads, popups, and overlays" },
            { "juce::Button", "Base class used by TextButton controls and LookAndFeel button rendering" },
            { "juce::Label", "Direct captions, values, headers, status text, and row text" },
            { "juce::TextButton", "Direct actions, mode buttons, source buttons, note buttons, and compact switches" },
            { "juce::ToggleButton", "Default-layout and log-file switches" },
            { "juce::ComboBox", "Layout, kit, source, preset, routing, and controller selectors" },
            { "juce::Slider", "Base of ParameterSlider and internal rotary or linear values" },
            { "juce::TextEditor", "Raw maps, logs, filters, balance values, and calibration profiles" },
            { "juce::ListBox / ListBoxModel", "SourcePickerPopup category and source lists" },
            { "juce::Viewport", "ScrollablePanel, popup layers, tables, and this audit page" },
            { "juce::ScrollBar", "Created by Viewport and styled by LookAndFeel" },
            { "juce::PopupMenu", "Preset actions and ComboBox menus" },
            { "juce::CallOutBox", "MidiNotePicker and expression-curve popups" },
            { "juce::AlertWindow", "Preset save and delete dialogs" },
            { "juce::TooltipWindow", "Editor-wide delayed tooltips" },
            { "juce::TabbedComponent / TabBarButton", "LookAndFeel style hook exists, but no direct instance was found" },
            { "juce::GroupComponent", "LookAndFeel color hook exists, but no direct instance was found" },
            { "juce::ResizableWindow", "Background color source used by PluginEditor and LookAndFeel" },
        };
    }

    ~Content() override {
        this->listBox.setModel(nullptr);
    }

    int getPreferredHeight(int contentWidth) const {
        juce::ignoreUnused(contentWidth);
        int height = this->pagePadding + this->titleHeight + this->introHeight;
        height += this->sectionBlockHeight(this->customDemos);
        height += this->sectionBlockHeight(this->standardDemos);
        height += this->sectionGap + this->sectionTitleHeight + static_cast<int>(this->fontUsages.size()) * this->fontRowHeight;
        height += this->sectionGap + this->sectionTitleHeight + static_cast<int>(this->themeColours.size()) * this->colourRowHeight;
        height += this->sectionGap + this->sectionTitleHeight + static_cast<int>(this->instrumentColours.size()) * this->colourRowHeight;
        height += this->sectionGap + this->sectionTitleHeight + static_cast<int>(this->derivedColours.size()) * this->colourRowHeight;
        height += this->sectionGap + this->sectionTitleHeight + static_cast<int>(this->projectInventory.size()) * this->inventoryRowHeight;
        height += this->sectionGap + this->sectionTitleHeight + static_cast<int>(this->juceInventory.size()) * this->inventoryRowHeight;
        return height + this->pagePadding;
    }

    void paint(juce::Graphics& graphics) override {
        graphics.fillAll(UITheme::Colours::bgEditor);

        graphics.setColour(UITheme::Colours::textPrimary);
        graphics.setFont(UITheme::Fonts::sectionTitle().withHeight(18.0f));
        graphics.drawText("UI components and theme audit", this->titleBounds, juce::Justification::centredLeft);

        graphics.setColour(UITheme::Colours::textInfo);
        graphics.setFont(UITheme::Fonts::normal());
        graphics.drawText("Live controls use the current project styles. Font rows use their real font. Color rows show exact values and current usage.", this->introBounds, juce::Justification::centredLeft);

        this->paintSectionTitle(graphics, this->customSectionBounds, "Project custom components - live examples");
        this->paintDemoRows(graphics, this->customDemos, this->customDemoBounds);

        this->paintSectionTitle(graphics, this->standardSectionBounds, "Standard JUCE components used directly - live examples");
        this->paintDemoRows(graphics, this->standardDemos, this->standardDemoBounds);

        this->paintSectionTitle(graphics, this->fontSectionBounds, "Font styles and usage places");
        for (int idx = 0; idx < static_cast<int>(this->fontUsages.size()); ++idx) this->paintFontRow(graphics, this->fontUsages[static_cast<size_t>(idx)], this->fontUsageBounds[static_cast<size_t>(idx)], idx);

        this->paintSectionTitle(graphics, this->themeColourSectionBounds, "Named UITheme colors");
        this->paintColourRows(graphics, this->themeColours, this->themeColourBounds);

        this->paintSectionTitle(graphics, this->instrumentColourSectionBounds, "Instrument colors - pads render these at 25-31% brightness");
        this->paintColourRows(graphics, this->instrumentColours, this->instrumentColourBounds);

        this->paintSectionTitle(graphics, this->derivedColourSectionBounds, "Hard-coded and derived colors outside named theme tokens");
        this->paintColourRows(graphics, this->derivedColours, this->derivedColourBounds);

        this->paintSectionTitle(graphics, this->projectInventorySectionBounds, "Project component inventory");
        this->paintInventoryRows(graphics, this->projectInventory, this->projectInventoryBounds);

        this->paintSectionTitle(graphics, this->juceInventorySectionBounds, "Standard JUCE component inventory");
        this->paintInventoryRows(graphics, this->juceInventory, this->juceInventoryBounds);
    }

    void resized() override {
        const int contentWidth = juce::jmax(1, this->getWidth() - this->pagePadding * 2);
        int currentY = this->pagePadding;
        this->titleBounds = { this->pagePadding, currentY, contentWidth, this->titleHeight };
        currentY += this->titleHeight;
        this->introBounds = { this->pagePadding, currentY, contentWidth, this->introHeight };
        currentY += this->introHeight;

        this->layoutDemoSection(this->customDemos, this->customDemoBounds, this->customSectionBounds, currentY, contentWidth);
        this->layoutDemoSection(this->standardDemos, this->standardDemoBounds, this->standardSectionBounds, currentY, contentWidth);
        this->layoutFixedSection(this->fontSectionBounds, this->fontUsageBounds, currentY, contentWidth, static_cast<int>(this->fontUsages.size()), this->fontRowHeight);
        this->layoutFixedSection(this->themeColourSectionBounds, this->themeColourBounds, currentY, contentWidth, static_cast<int>(this->themeColours.size()), this->colourRowHeight);
        this->layoutFixedSection(this->instrumentColourSectionBounds, this->instrumentColourBounds, currentY, contentWidth, static_cast<int>(this->instrumentColours.size()), this->colourRowHeight);
        this->layoutFixedSection(this->derivedColourSectionBounds, this->derivedColourBounds, currentY, contentWidth, static_cast<int>(this->derivedColours.size()), this->colourRowHeight);
        this->layoutFixedSection(this->projectInventorySectionBounds, this->projectInventoryBounds, currentY, contentWidth, static_cast<int>(this->projectInventory.size()), this->inventoryRowHeight);
        this->layoutFixedSection(this->juceInventorySectionBounds, this->juceInventoryBounds, currentY, contentWidth, static_cast<int>(this->juceInventory.size()), this->inventoryRowHeight);
    }

private:
    struct DemoEntry {
        juce::String name;
        juce::String usage;
        juce::Component* component = nullptr;
        int rowHeight = 0;
        int preferredWidth = 0;
        int preferredHeight = 0;
    };

    struct FontUsage {
        juce::String name;
        juce::Font font;
        juce::String usage;
    };

    struct ColourUsage {
        juce::String name;
        juce::Colour colour;
        juce::String usage;
    };

    struct InventoryEntry {
        juce::String name;
        juce::String usage;
    };

    class DemoListModel : public juce::ListBoxModel {
    public:
        int getNumRows() override { return 4; }

        void paintListBoxItem(int rowNumber, juce::Graphics& graphics, int width, int height, bool rowIsSelected) override {
            const auto background = rowIsSelected ? UITheme::Colours::listSelection : (rowNumber % 2 == 0 ? UITheme::Colours::bgControlAlt : UITheme::Colours::listRowOdd);
            graphics.fillAll(background);
            graphics.setColour(UITheme::Colours::textPrimary);
            graphics.setFont(UITheme::Fonts::normal().withHeight(static_cast<float>(UITheme::Sizes::standardControlTextHeight)));
            graphics.drawText("SourcePickerPopup list row " + juce::String(rowNumber + 1), 8, 0, width - 16, height, juce::Justification::centredLeft);
        }
    };

    static constexpr int pagePadding = 16;
    static constexpr int titleHeight = 30;
    static constexpr int introHeight = 36;
    static constexpr int sectionGap = 18;
    static constexpr int sectionTitleHeight = 30;
    static constexpr int fontRowHeight = 50;
    static constexpr int colourRowHeight = 42;
    static constexpr int inventoryRowHeight = 36;

    ParameterSlider parameterSlider;
    ParameterPot parameterPot { "ParameterPot", 0.0, 100.0, 1.0, 65.0 };
    HorizontalKnobBrick horizontalKnobBrick { "HorizontalKnobBrick", -50.0, 50.0, 1.0, 12.0 };
    CompactParameterControl compactParameterControl { "CompactParameterControl", 0.0, 100.0, 1.0, 65.0 };
    MidiNoteAssignmentControl midiNoteAssignmentControl;
    MidiNotePicker midiNotePicker;
    DrumPadGrid drumPadGrid;
    MidiMapTable midiMapTable;
    StatusBar statusBar;

    juce::Label standardLabel;
    juce::TextButton controlButton { "Control TextButton" };
    juce::TextButton categoryButton { "Category TextButton" };
    juce::TextButton toggleTextButton { "Toggle TextButton" };
    juce::ToggleButton standardToggle { "ToggleButton using JUCE font" };
    juce::ComboBox standardCombo;
    juce::ComboBox layoutCombo;
    juce::ComboBox midiCombo;
    juce::TextEditor filterEditor;
    juce::TextEditor monoEditor;
    DemoListModel listModel;
    juce::ListBox listBox;
    juce::TextButton popupMenuButton { "Open PopupMenu" };

    std::vector<DemoEntry> customDemos;
    std::vector<DemoEntry> standardDemos;
    std::vector<FontUsage> fontUsages;
    std::vector<ColourUsage> themeColours;
    std::vector<ColourUsage> instrumentColours;
    std::vector<ColourUsage> derivedColours;
    std::vector<InventoryEntry> projectInventory;
    std::vector<InventoryEntry> juceInventory;

    juce::Rectangle<int> titleBounds;
    juce::Rectangle<int> introBounds;
    juce::Rectangle<int> customSectionBounds;
    juce::Rectangle<int> standardSectionBounds;
    juce::Rectangle<int> fontSectionBounds;
    juce::Rectangle<int> themeColourSectionBounds;
    juce::Rectangle<int> instrumentColourSectionBounds;
    juce::Rectangle<int> derivedColourSectionBounds;
    juce::Rectangle<int> projectInventorySectionBounds;
    juce::Rectangle<int> juceInventorySectionBounds;
    std::vector<juce::Rectangle<int>> customDemoBounds;
    std::vector<juce::Rectangle<int>> standardDemoBounds;
    std::vector<juce::Rectangle<int>> fontUsageBounds;
    std::vector<juce::Rectangle<int>> themeColourBounds;
    std::vector<juce::Rectangle<int>> instrumentColourBounds;
    std::vector<juce::Rectangle<int>> derivedColourBounds;
    std::vector<juce::Rectangle<int>> projectInventoryBounds;
    std::vector<juce::Rectangle<int>> juceInventoryBounds;

    int sectionBlockHeight(const std::vector<DemoEntry>& demos) const {
        int height = this->sectionGap + this->sectionTitleHeight;
        for (const auto& demo : demos) height += demo.rowHeight;
        return height;
    }

    void layoutDemoSection(const std::vector<DemoEntry>& demos, std::vector<juce::Rectangle<int>>& rowBounds, juce::Rectangle<int>& headingBounds, int& currentY, int contentWidth) {
        currentY += this->sectionGap;
        headingBounds = { this->pagePadding, currentY, contentWidth, this->sectionTitleHeight };
        currentY += this->sectionTitleHeight;
        rowBounds.clear();
        rowBounds.reserve(demos.size());

        for (const auto& demo : demos) {
            const juce::Rectangle<int> row(this->pagePadding, currentY, contentWidth, demo.rowHeight);
            rowBounds.push_back(row);
            currentY += demo.rowHeight;

            auto componentArea = row.reduced(8, 5);
            const int descriptionWidth = juce::jlimit(190, 310, componentArea.getWidth() * 2 / 5);
            componentArea.removeFromLeft(descriptionWidth);
            const int componentWidth = juce::jmin(demo.preferredWidth, componentArea.getWidth());
            const int componentHeight = demo.preferredHeight > 0 ? juce::jmin(demo.preferredHeight, componentArea.getHeight()) : componentArea.getHeight();
            demo.component->setBounds(componentArea.withSizeKeepingCentre(componentWidth, componentHeight));
        }
    }

    void layoutFixedSection(juce::Rectangle<int>& headingBounds, std::vector<juce::Rectangle<int>>& rowBounds, int& currentY, int contentWidth, int rowCount, int rowHeight) {
        currentY += this->sectionGap;
        headingBounds = { this->pagePadding, currentY, contentWidth, this->sectionTitleHeight };
        currentY += this->sectionTitleHeight;
        rowBounds.clear();
        rowBounds.reserve(static_cast<size_t>(rowCount));
        for (int idx = 0; idx < rowCount; ++idx) {
            rowBounds.emplace_back(this->pagePadding, currentY, contentWidth, rowHeight);
            currentY += rowHeight;
        }
    }

    void paintSectionTitle(juce::Graphics& graphics, const juce::Rectangle<int>& bounds, const juce::String& title) const {
        graphics.setColour(UITheme::Colours::textPrimary);
        graphics.setFont(UITheme::Fonts::sectionTitle());
        graphics.drawText(title, bounds.withTrimmedRight(8), juce::Justification::centredLeft);
        graphics.setColour(UITheme::Colours::borderMid);
        graphics.drawHorizontalLine(bounds.getBottom() - 1, static_cast<float>(bounds.getX()), static_cast<float>(bounds.getRight()));
    }

    void paintDemoRows(juce::Graphics& graphics, const std::vector<DemoEntry>& demos, const std::vector<juce::Rectangle<int>>& boundsList) const {
        for (int idx = 0; idx < static_cast<int>(demos.size()); ++idx) {
            const auto& demo = demos[static_cast<size_t>(idx)];
            const auto bounds = boundsList[static_cast<size_t>(idx)];
            graphics.setColour(idx % 2 == 0 ? UITheme::Colours::bgPanel : UITheme::Colours::bgPanelRaised);
            graphics.fillRect(bounds.reduced(0, 1));

            auto descriptionBounds = bounds.reduced(8, 5);
            descriptionBounds.setWidth(juce::jlimit(190, 310, descriptionBounds.getWidth() * 2 / 5) - 8);
            auto nameBounds = descriptionBounds.removeFromTop(20);
            graphics.setColour(UITheme::Colours::textPrimary);
            graphics.setFont(UITheme::Fonts::labelBoldMedium());
            graphics.drawText(demo.name, nameBounds, juce::Justification::centredLeft);
            graphics.setColour(UITheme::Colours::textInfo);
            graphics.setFont(UITheme::Fonts::small());
            graphics.drawFittedText(demo.usage, descriptionBounds, juce::Justification::centredLeft, 2);
        }
    }

    void paintFontRow(juce::Graphics& graphics, const FontUsage& fontUsage, const juce::Rectangle<int>& bounds, int rowIdx) const {
        graphics.setColour(rowIdx % 2 == 0 ? UITheme::Colours::bgPanel : UITheme::Colours::bgPanelRaised);
        graphics.fillRect(bounds.reduced(0, 1));
        auto row = bounds.reduced(8, 3);
        auto nameArea = row.removeFromLeft(250);

        graphics.setColour(UITheme::Colours::textPrimary);
        graphics.setFont(UITheme::Fonts::labelBold());
        graphics.drawText(fontUsage.name, nameArea.removeFromTop(20), juce::Justification::centredLeft);
        graphics.setColour(UITheme::Colours::textMuted);
        graphics.setFont(UITheme::Fonts::small());
        graphics.drawText(this->fontDescription(fontUsage.font), nameArea, juce::Justification::centredLeft);

        graphics.setColour(UITheme::Colours::textPrimary);
        graphics.setFont(fontUsage.font);
        graphics.drawText(fontUsage.usage, row, juce::Justification::centredLeft, true);
    }

    void paintColourRows(juce::Graphics& graphics, const std::vector<ColourUsage>& colours, const std::vector<juce::Rectangle<int>>& boundsList) const {
        for (int idx = 0; idx < static_cast<int>(colours.size()); ++idx) {
            const auto& colourUsage = colours[static_cast<size_t>(idx)];
            const auto bounds = boundsList[static_cast<size_t>(idx)];
            graphics.setColour(idx % 2 == 0 ? UITheme::Colours::bgPanel : UITheme::Colours::bgPanelRaised);
            graphics.fillRect(bounds.reduced(0, 1));

            auto row = bounds.reduced(8, 6);
            auto swatch = row.removeFromLeft(30).withSizeKeepingCentre(26, 26);
            this->paintSwatchBackground(graphics, swatch);
            graphics.setColour(colourUsage.colour);
            graphics.fillRect(swatch);
            graphics.setColour(UITheme::Colours::borderStrong);
            graphics.drawRect(swatch);

            row.removeFromLeft(8);
            auto nameArea = row.removeFromLeft(240);
            graphics.setColour(UITheme::Colours::textPrimary);
            graphics.setFont(UITheme::Fonts::labelBold());
            graphics.drawText(colourUsage.name, nameArea.removeFromTop(16), juce::Justification::centredLeft);
            graphics.setColour(UITheme::Colours::textMuted);
            graphics.setFont(UITheme::Fonts::small());
            graphics.drawText("#" + colourUsage.colour.toDisplayString(true), nameArea, juce::Justification::centredLeft);

            graphics.setColour(UITheme::Colours::textSecondary);
            graphics.setFont(UITheme::Fonts::normal());
            graphics.drawText(colourUsage.usage, row, juce::Justification::centredLeft, true);
        }
    }

    void paintInventoryRows(juce::Graphics& graphics, const std::vector<InventoryEntry>& entries, const std::vector<juce::Rectangle<int>>& boundsList) const {
        for (int idx = 0; idx < static_cast<int>(entries.size()); ++idx) {
            const auto& entry = entries[static_cast<size_t>(idx)];
            const auto bounds = boundsList[static_cast<size_t>(idx)];
            graphics.setColour(idx % 2 == 0 ? UITheme::Colours::bgPanel : UITheme::Colours::bgPanelRaised);
            graphics.fillRect(bounds.reduced(0, 1));
            auto row = bounds.reduced(8, 3);
            auto nameArea = row.removeFromLeft(250);
            graphics.setColour(UITheme::Colours::textPrimary);
            graphics.setFont(UITheme::Fonts::labelBoldMedium());
            graphics.drawText(entry.name, nameArea, juce::Justification::centredLeft);
            graphics.setColour(UITheme::Colours::textSecondary);
            graphics.setFont(UITheme::Fonts::normal());
            graphics.drawText(entry.usage, row, juce::Justification::centredLeft, true);
        }
    }

    juce::String fontDescription(const juce::Font& font) const {
        juce::String description = juce::String(font.getHeight(), 1) + " px";
        if (font.isBold()) description += " / bold";
        else description += " / regular";
        if (font.isItalic()) description += " / italic";
        if (font.getTypefaceName() == juce::Font::getDefaultMonospacedFontName()) description += " / monospace";
        return description;
    }

    void paintSwatchBackground(juce::Graphics& graphics, const juce::Rectangle<int>& bounds) const {
        constexpr int squareSize = 6;
        for (int rowIdx = 0; rowIdx < bounds.getHeight(); rowIdx += squareSize) {
            for (int columnIdx = 0; columnIdx < bounds.getWidth(); columnIdx += squareSize) {
                graphics.setColour(((rowIdx / squareSize) + (columnIdx / squareSize)) % 2 == 0 ? UITheme::Colours::bgControl : UITheme::Colours::bgPanelRaised);
                graphics.fillRect(bounds.getX() + columnIdx, bounds.getY() + rowIdx, juce::jmin(squareSize, bounds.getWidth() - columnIdx), juce::jmin(squareSize, bounds.getHeight() - rowIdx));
            }
        }
    }
};

ThemePlaygroundPanel::ThemePlaygroundPanel()
    : content(std::make_unique<Content>()) {
    this->viewport.setViewedComponent(this->content.get(), false);
    this->viewport.setScrollBarsShown(true, false);
    this->addAndMakeVisible(this->viewport);
}

ThemePlaygroundPanel::~ThemePlaygroundPanel() {
    this->viewport.setViewedComponent(nullptr, false);
}

void ThemePlaygroundPanel::resized() {
    this->viewport.setBounds(this->getLocalBounds());
    const int contentWidth = juce::jmax(1, this->viewport.getWidth() - this->viewport.getScrollBarThickness());
    this->content->setSize(contentWidth, this->content->getPreferredHeight(contentWidth));
}
