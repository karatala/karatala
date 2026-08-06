#include "LibraryAssignmentPicker.h"
#include "../Engine/ArticulationNames.h"
#include "UIComponentStyle.h"
#include "UITheme.h"

#include <algorithm>
#include <iterator>
#include <map>
#include <set>

static constexpr int assignmentBadgeHeight = 14;
static constexpr int assignmentBadgeGap = 2;
static constexpr int micBadgeHorizontalPadding = 3;
static constexpr int countBadgeHorizontalPadding = 4;

static juce::Colour micBadgeColour(const juce::String& badge) {
    if (badge == "OH") return UITheme::Colours::libraryBadgeOverhead;
    if (badge == "MS") return UITheme::Colours::libraryBadgeFormat;
    if (badge.startsWithChar('R')) return UITheme::Colours::libraryBadgeRoom;
    return UITheme::Colours::libraryBadgeDirect;
}

static int assignmentBadgeWidth(const juce::Font& font, const juce::String& text, int horizontalPadding) {
    return juce::GlyphArrangement::getStringWidthInt(font, text) + horizontalPadding * 2;
}

static juce::String roundRobinBadgeText(const LibrarySampleSummary& summary) {
    return summary.roundRobinCountText.isEmpty() ? juce::String() : UITheme::Symbols::dice + " " + summary.roundRobinCountText;
}

static juce::String velocityBadgeText(const LibrarySampleSummary& summary) {
    return summary.velocityLevelCountText.isEmpty() ? juce::String() : UITheme::Symbols::signalBars + " " + summary.velocityLevelCountText;
}

static int previewArticulationPriority(const AvailableLibraryArticulation& libraryArticulation, const ArticulationSlotFallbackRules& priorityRules) {
    const auto& preferences = priorityRules.forType(libraryArticulation.instType);
    const auto articulationCode = libraryArticulation.rawArtCode.isNotEmpty() ? libraryArticulation.rawArtCode : libraryArticulation.artCode;
    for (int priorityIdx = 0; priorityIdx < static_cast<int>(preferences.size()); ++priorityIdx)
        if (ArticulationNames::mapFilterMatchesArticulation(preferences[static_cast<size_t>(priorityIdx)], articulationCode)) return priorityIdx;
    return static_cast<int>(preferences.size()) + (ArticulationNames::normalizeArticulationName(articulationCode) == "choke" ? 1 : 0);
}

static int librarySummaryBadgeWidth(const LibrarySampleSummary& summary, const juce::Font& font) {
    int totalWidth = 0;
    auto includeWidth = [&totalWidth](int badgeWidth) {
        if (badgeWidth <= 0) return;
        if (totalWidth > 0) totalWidth += assignmentBadgeGap;
        totalWidth += badgeWidth;
    };
    for (const auto& badge : summary.micBadges) includeWidth(assignmentBadgeWidth(font, badge, micBadgeHorizontalPadding));
    const auto roundRobinText = roundRobinBadgeText(summary);
    const auto velocityText = velocityBadgeText(summary);
    if (roundRobinText.isNotEmpty()) includeWidth(assignmentBadgeWidth(font, roundRobinText, countBadgeHorizontalPadding));
    if (velocityText.isNotEmpty()) includeWidth(assignmentBadgeWidth(font, velocityText, countBadgeHorizontalPadding));
    return totalWidth;
}

static int paintAssignmentBadge(juce::Graphics& graphics, const juce::Font& font, const juce::String& text, int rightEdge, int rowHeight, int horizontalPadding, float cornerRadius, juce::Colour fillColour) {
    const int badgeWidth = assignmentBadgeWidth(font, text, horizontalPadding);
    const auto badgeBounds = juce::Rectangle<int>(rightEdge - badgeWidth, (rowHeight - assignmentBadgeHeight) / 2, badgeWidth, assignmentBadgeHeight);
    graphics.setColour(fillColour);
    graphics.fillRoundedRectangle(badgeBounds.toFloat(), cornerRadius);
    graphics.setColour(UITheme::Colours::textPrimary);
    graphics.setFont(font);
    graphics.drawText(text, badgeBounds, juce::Justification::centred);
    return badgeBounds.getX() - assignmentBadgeGap;
}

static void paintLibrarySummaryBadges(juce::Graphics& graphics, const LibrarySampleSummary& summary, juce::Rectangle<int> area, const juce::Font& font) {
    juce::Graphics::ScopedSaveState saveState(graphics);
    graphics.reduceClipRegion(area);
    int rightEdge = area.getRight();
    const auto velocityText = velocityBadgeText(summary);
    if (velocityText.isNotEmpty()) rightEdge = paintAssignmentBadge(graphics, font, velocityText, rightEdge, area.getHeight(), countBadgeHorizontalPadding, assignmentBadgeHeight * 0.5f, UITheme::Colours::libraryBadgeVelocity);
    const auto roundRobinText = roundRobinBadgeText(summary);
    if (roundRobinText.isNotEmpty()) rightEdge = paintAssignmentBadge(graphics, font, roundRobinText, rightEdge, area.getHeight(), countBadgeHorizontalPadding, assignmentBadgeHeight * 0.5f, UITheme::Colours::libraryBadgeRoundRobin);
    for (auto badgeIterator = summary.micBadges.rbegin(); badgeIterator != summary.micBadges.rend(); ++badgeIterator)
        rightEdge = paintAssignmentBadge(graphics, font, *badgeIterator, rightEdge, area.getHeight(), micBadgeHorizontalPadding, 2.0f, micBadgeColour(*badgeIterator));
}

LibraryAssignmentPicker::LibraryAssignmentPicker(const std::vector<AvailableLibraryArticulation>& availableArticulations, const InstrumentCatalog& instrumentCatalog, const ArticulationSlotFallbackRules& priorityRules, SelectionMode mode)
    : availableLibraryArticulations(availableArticulations)
    , catalog(instrumentCatalog)
    , articulationPriorityRules(priorityRules)
    , selectionMode(mode) {
    this->titleLabel.setFont(UITheme::Fonts::sectionTitle().withHeight(14.0f));
    this->titleLabel.setColour(juce::Label::textColourId, UITheme::Colours::textPrimary);
    this->titleLabel.setJustificationType(juce::Justification::centredLeft);
    this->titleLabel.setMinimumHorizontalScale(0.75f);
    this->addAndMakeVisible(this->titleLabel);

    UIComponentStyle::configureDialogCloseButton(this->closeButton);
    this->closeButton.onClick = [this] {
        if (this->onDismiss) this->onDismiss();
    };
    this->addAndMakeVisible(this->closeButton);

    UIComponentStyle::configureBorderlessTextEditor(this->filterField);
    this->filterField.setFont(UITheme::Fonts::normal().withHeight(static_cast<float>(UITheme::Sizes::standardControlTextHeight)));
    this->filterField.setTextToShowWhenEmpty("filter...", UITheme::Colours::textMuted);
    this->filterField.onTextChange = [this] { this->queueRebuildFiltered(); };
    this->addAndMakeVisible(this->filterField);

    for (auto* button : { &this->allCategoryButton, &this->drumsCategoryButton, &this->cymbalsCategoryButton, &this->otherCategoryButton }) {
        UIComponentStyle::configureCategoryButton(*button);
        this->addAndMakeVisible(*button);
    }
    this->allCategoryButton.onClick = [this] { this->setCategory(Category::All); };
    this->drumsCategoryButton.onClick = [this] { this->setCategory(Category::Drums); };
    this->cymbalsCategoryButton.onClick = [this] { this->setCategory(Category::Cymbals); };
    this->otherCategoryButton.onClick = [this] { this->setCategory(Category::Other); };

    this->kindListLabel.setText("Types", juce::dontSendNotification);
    UIComponentStyle::configureCaptionLabel(this->kindListLabel, juce::Justification::centredLeft);
    this->addAndMakeVisible(this->kindListLabel);

    this->kindListBox.setModel(&this->kindListModel);
    this->kindListBox.setRowHeight(22);
    this->kindListBox.setColour(juce::ListBox::backgroundColourId, UITheme::Colours::bgControlAlt);
    this->addAndMakeVisible(this->kindListBox);

    this->articulationListLabel.setText(this->selectionMode == SelectionMode::Instrument ? "Preview articulations" : "Articulations", juce::dontSendNotification);
    UIComponentStyle::configureCaptionLabel(this->articulationListLabel, juce::Justification::centredLeft);
    this->addAndMakeVisible(this->articulationListLabel);
    this->articulationListBox.setModel(&this->articulationListModel);
    this->articulationListBox.setRowHeight(22);
    this->articulationListBox.setMultipleSelectionEnabled(this->selectionMode == SelectionMode::Articulation);
    this->articulationListBox.setClickingTogglesRowSelection(this->selectionMode == SelectionMode::Articulation);
    this->articulationListBox.setColour(juce::ListBox::backgroundColourId, UITheme::Colours::bgControlAlt);
    this->addAndMakeVisible(this->articulationListBox);

    this->assignmentListBox.setModel(&this->assignmentListModel);
    this->assignmentListBox.setRowHeight(22);
    this->assignmentListBox.setColour(juce::ListBox::backgroundColourId, UITheme::Colours::bgControlAlt);
    this->addAndMakeVisible(this->assignmentListBox);

    this->cachedLowerDisplayLabels.reserve(this->availableLibraryArticulations.size());
    for (const auto& libArt : this->availableLibraryArticulations)
        this->cachedLowerDisplayLabels.push_back(libArt.displayLabel().toLowerCase());

    this->rebuildKindEntries();
    this->rebuildArticulationEntries();
    this->rebuildFiltered();
    this->updateCategoryButtons();
}

void LibraryAssignmentPicker::setFilterType(KitLib::InstrumentType type) {
    this->selectedCategory = this->categoryForType(type);
    this->selectedKind = type;
    this->rebuildKindEntries();
    this->rebuildArticulationEntries();
    this->rebuildFiltered();
    this->updateCategoryButtons();
}

void LibraryAssignmentPicker::setSelectedLibraryArticulation(int libArtIndex) {
    const bool libArtIndexValid = libArtIndex >= 0 && libArtIndex < static_cast<int>(this->availableLibraryArticulations.size());
    for (int idx = 0; idx < static_cast<int>(this->visibleLibraryArticulationIndexes.size()); ++idx) {
        const int visibleLibArtIndex = this->visibleLibraryArticulationIndexes[static_cast<size_t>(idx)];
        const bool exactMatch = visibleLibArtIndex == libArtIndex;
        const bool instrumentMatch = this->selectionMode == SelectionMode::Instrument && libArtIndexValid
            && LibraryAssignmentPicker::qualifiedLibraryInstrumentId(this->availableLibraryArticulations[static_cast<size_t>(visibleLibArtIndex)]) == LibraryAssignmentPicker::qualifiedLibraryInstrumentId(this->availableLibraryArticulations[static_cast<size_t>(libArtIndex)]);
        if (!exactMatch && !instrumentMatch) continue;
        this->suppressPreviewOnSelection = true;
        this->assignmentListBox.selectRow(idx);
        this->suppressPreviewOnSelection = false;
        this->assignmentListBox.scrollToEnsureRowIsOnscreen(idx);
        if (this->selectionMode == SelectionMode::Instrument && libArtIndexValid) {
            juce::SparseSet<int> selectedRows;
            const auto articulationIterator = std::find(this->previewLibraryArticulationIndexes.begin(), this->previewLibraryArticulationIndexes.end(), libArtIndex);
            if (articulationIterator != this->previewLibraryArticulationIndexes.end()) {
                const int articulationRow = static_cast<int>(std::distance(this->previewLibraryArticulationIndexes.begin(), articulationIterator));
                selectedRows.addRange({ articulationRow, articulationRow + 1 });
            }
            this->articulationListBox.setSelectedRows(selectedRows, juce::dontSendNotification);
        }
        return;
    }
}

void LibraryAssignmentPicker::setTitle(const juce::String& title) {
    this->titleLabel.setText(title, juce::dontSendNotification);
}

void LibraryAssignmentPicker::focusAssignmentList() {
    this->assignmentListBox.grabKeyboardFocus();
}

void LibraryAssignmentPicker::resized() {
    auto area = this->getLocalBounds().reduced(4);
    auto titleRow = area.removeFromTop(22);
    this->closeButton.setBounds(titleRow.removeFromRight(22).withSizeKeepingCentre(UITheme::Sizes::standardControlHeight, UITheme::Sizes::standardControlHeight));
    titleRow.removeFromRight(4);
    this->titleLabel.setBounds(titleRow);
    area.removeFromTop(2);

    auto topRow = area.removeFromTop(22);
    const int categoryAreaW = juce::jmin(232, juce::jmax(184, topRow.getWidth() / 2));
    auto categoryArea = topRow.removeFromLeft(categoryAreaW);
    topRow.removeFromLeft(4);
    this->filterField.setBounds(topRow.reduced(0, 1));

    const int buttonW = categoryArea.getWidth() / 4;
    this->allCategoryButton.setBounds(categoryArea.removeFromLeft(buttonW).reduced(1, 1));
    this->drumsCategoryButton.setBounds(categoryArea.removeFromLeft(buttonW).reduced(1, 1));
    this->cymbalsCategoryButton.setBounds(categoryArea.removeFromLeft(buttonW).reduced(1, 1));
    this->otherCategoryButton.setBounds(categoryArea.reduced(1, 1));

    area.removeFromTop(4);
    auto body = area;
    const int leftWidth = juce::jmin(sidebarWidth, juce::jmax(90, body.getWidth() / 4));
    auto sidebarArea = body.removeFromLeft(leftWidth);
    const int labelHeight = 16;
    const int listGap = 4;
    const int availableListHeight = sidebarArea.getHeight() - labelHeight * 2 - listGap;
    const int kindListHeight = juce::jmin(this->kindListBox.getRowHeight() * 10, availableListHeight);
    this->kindListLabel.setBounds(sidebarArea.removeFromTop(labelHeight));
    this->kindListBox.setBounds(sidebarArea.removeFromTop(kindListHeight));
    sidebarArea.removeFromTop(listGap);
    this->articulationListLabel.setBounds(sidebarArea.removeFromTop(labelHeight));
    this->articulationListBox.setBounds(sidebarArea);
    body.removeFromLeft(4);
    this->assignmentListBox.setBounds(body);
}

void LibraryAssignmentPicker::paint(juce::Graphics& g) {
    g.setColour(UITheme::Colours::bgPanelRaised);
    g.fillRoundedRectangle(this->getLocalBounds().toFloat(), 6.0f);
}

void LibraryAssignmentPicker::setCategory(Category category) {
    if (this->selectedCategory == category)
        return;

    this->selectedCategory = category;
    this->selectedKind = KitLib::InstrumentType::Unknown;
    this->rebuildKindEntries();
    this->rebuildArticulationEntries();
    this->rebuildFiltered();
    this->updateCategoryButtons();
}

std::optional<InstrumentFamily> LibraryAssignmentPicker::familyForCategory(Category category) {
    switch (category) {
        case Category::Drums: return InstrumentFamily::Drum;
        case Category::Cymbals: return InstrumentFamily::Cymbal;
        case Category::Other: return InstrumentFamily::Other;
        case Category::All: break;
    }
    return std::nullopt;
}

LibraryAssignmentPicker::Category LibraryAssignmentPicker::categoryForType(KitLib::InstrumentType type) const {
    switch (this->catalog.familyFor(type)) {
        case InstrumentFamily::Drum: return Category::Drums;
        case InstrumentFamily::Cymbal: return Category::Cymbals;
        case InstrumentFamily::Other: return Category::Other;
        case InstrumentFamily::Unknown: break;
    }
    return Category::All;
}

bool LibraryAssignmentPicker::matchesCategory(KitLib::InstrumentType type) const {
    const auto categoryFamily = familyForCategory(this->selectedCategory);
    if (!categoryFamily.has_value())
        return true;
    return this->catalog.familyFor(type) == *categoryFamily;
}

juce::String LibraryAssignmentPicker::kindLabel(KitLib::InstrumentType type) {
    return KitLib::instrumentTypeToString(type).replaceCharacter('_', ' ');
}

juce::String LibraryAssignmentPicker::qualifiedLibraryInstrumentId(const AvailableLibraryArticulation& libArt) {
    return libArt.kitCodeRaw.trim().toLowerCase() + "\n" + libArt.instCode.trim().toLowerCase();
}

juce::String LibraryAssignmentPicker::libraryArticulationDisplayLabel(const AvailableLibraryArticulation& libArt) {
    const auto kitName = libArt.kitDisplayName.isNotEmpty() ? libArt.kitDisplayName : libArt.kitCodeRaw;
    juce::StringArray parts { kitName };
    if (libArt.instCode.isNotEmpty()) parts.add(libArt.instCode);
    if (libArt.artCode.isNotEmpty()) parts.add(libArt.artCode);
    return parts.joinIntoString(" / ");
}

juce::String LibraryAssignmentPicker::libraryInstrumentDisplayLabel(const AvailableLibraryArticulation& libArt) {
    const auto kitName = libArt.kitDisplayName.isNotEmpty() ? libArt.kitDisplayName : libArt.kitCodeRaw;
    return libArt.instCode.isNotEmpty() ? kitName + " / " + libArt.instCode : kitName;
}

void LibraryAssignmentPicker::rebuildKindEntries() {
    std::set<KitLib::InstrumentType> seenKinds;
    for (const auto& libArt : this->availableLibraryArticulations) {
        if (libArt.instType == KitLib::InstrumentType::Unknown)
            continue;
        if (!this->matchesCategory(libArt.instType))
            continue;
        seenKinds.insert(libArt.instType);
    }

    this->kindEntries.assign(seenKinds.begin(), seenKinds.end());
    std::sort(this->kindEntries.begin(), this->kindEntries.end(), [](auto left, auto right) {
        return kindLabel(left).compareIgnoreCase(kindLabel(right)) < 0;
    });

    if (this->selectedKind != KitLib::InstrumentType::Unknown && !vectorContains(this->kindEntries, this->selectedKind))
        this->selectedKind = KitLib::InstrumentType::Unknown;

    this->kindListBox.updateContent();
    int selectedRow = 0;
    if (this->selectedKind != KitLib::InstrumentType::Unknown) {
        auto it = std::find(this->kindEntries.begin(), this->kindEntries.end(), this->selectedKind);
        if (it != this->kindEntries.end())
            selectedRow = static_cast<int>(std::distance(this->kindEntries.begin(), it)) + 1;
    }
    this->kindListBox.selectRow(selectedRow);
}

void LibraryAssignmentPicker::rebuildArticulationEntries() {
    if (this->selectionMode == SelectionMode::Instrument) {
        this->articulationEntries.clear();
        this->previewLibraryArticulationIndexes.clear();
        if (this->selectedLibraryInstrumentArticulationIndex >= 0 && this->selectedLibraryInstrumentArticulationIndex < static_cast<int>(this->availableLibraryArticulations.size())) {
            const auto selectedInstrumentKey = LibraryAssignmentPicker::qualifiedLibraryInstrumentId(this->availableLibraryArticulations[static_cast<size_t>(this->selectedLibraryInstrumentArticulationIndex)]);
            std::set<juce::String> seenArticulations;
            for (int libArtIndex = 0; libArtIndex < static_cast<int>(this->availableLibraryArticulations.size()); ++libArtIndex) {
                const auto& libArt = this->availableLibraryArticulations[static_cast<size_t>(libArtIndex)];
                if (LibraryAssignmentPicker::qualifiedLibraryInstrumentId(libArt) != selectedInstrumentKey) continue;
                const auto articulationName = libArt.artCode.isNotEmpty() ? libArt.artCode : juce::String("(default)");
                if (!seenArticulations.insert(articulationName.toLowerCase()).second) continue;
                this->articulationEntries.push_back(articulationName);
                this->previewLibraryArticulationIndexes.push_back(libArtIndex);
            }
        }
        this->articulationListBox.updateContent();
        juce::SparseSet<int> selectedRows;
        this->articulationListBox.setSelectedRows(selectedRows, juce::dontSendNotification);
        return;
    }

    std::set<juce::String> availableArticulations;
    for (const auto& libArt : this->availableLibraryArticulations) {
        if (!this->matchesCategory(libArt.instType)) continue;
        if (this->selectedKind != KitLib::InstrumentType::Unknown && libArt.instType != this->selectedKind) continue;
        const auto articulationName = ArticulationNames::normalizeArticulationName(libArt.rawArtCode.isNotEmpty() ? libArt.rawArtCode : libArt.artCode);
        if (articulationName.isNotEmpty()) availableArticulations.insert(articulationName);
    }

    this->articulationEntries.assign(availableArticulations.begin(), availableArticulations.end());
    for (auto selectedIterator = this->selectedArticulations.begin(); selectedIterator != this->selectedArticulations.end();) {
        if (availableArticulations.find(*selectedIterator) == availableArticulations.end())
            selectedIterator = this->selectedArticulations.erase(selectedIterator);
        else
            ++selectedIterator;
    }

    this->articulationListBox.updateContent();
    juce::SparseSet<int> selectedRows;
    for (int idx = 0; idx < static_cast<int>(this->articulationEntries.size()); ++idx) {
        if (this->selectedArticulations.find(this->articulationEntries[static_cast<size_t>(idx)]) != this->selectedArticulations.end())
            selectedRows.addRange({ idx, idx + 1 });
    }
    this->articulationListBox.setSelectedRows(selectedRows, juce::dontSendNotification);
}

bool LibraryAssignmentPicker::matchesCurrentFilters(const AvailableLibraryArticulation& libArt, const juce::String& lowerDisplayLabel, const juce::String& filterText) const {
    if (!this->matchesCategory(libArt.instType))
        return false;
    if (this->selectedKind != KitLib::InstrumentType::Unknown && libArt.instType != this->selectedKind)
        return false;
    if (this->selectionMode == SelectionMode::Articulation && !this->selectedArticulations.empty()) {
        const auto articulationName = ArticulationNames::normalizeArticulationName(libArt.rawArtCode.isNotEmpty() ? libArt.rawArtCode : libArt.artCode);
        if (this->selectedArticulations.find(articulationName) == this->selectedArticulations.end()) return false;
    }

    juce::StringArray filterTerms;
    filterTerms.addTokens(filterText, " \t\r\n", "");
    filterTerms.removeEmptyStrings();
    for (const auto& filterTerm : filterTerms) {
        if (filterTerm.startsWithChar('-') && filterTerm.length() > 1) {
            if (lowerDisplayLabel.contains(filterTerm.substring(1))) return false;
        } else if (filterTerm != "-" && !lowerDisplayLabel.contains(filterTerm)) {
            return false;
        }
    }
    return true;
}

void LibraryAssignmentPicker::rebuildFiltered() {
    this->stopTimer();
    this->assignmentListBox.deselectAllRows();
    this->visibleLibraryArticulationIndexes.clear();
    this->actionRow = -1;
    this->actionRowBeforeMouseSelection = -1;
    this->mouseSelectionChanged = false;
    auto filterText = this->filterField.getText().toLowerCase();
    std::map<juce::String, size_t> visibleRowByInstrument;

    for (int idx = 0; idx < static_cast<int>(this->availableLibraryArticulations.size()); ++idx) {
        const auto& libArt = this->availableLibraryArticulations[static_cast<size_t>(idx)];
        if (!this->matchesCurrentFilters(libArt, this->cachedLowerDisplayLabels[static_cast<size_t>(idx)], filterText))
            continue;
        if (this->selectionMode == SelectionMode::Instrument) {
            const auto instrumentKey = LibraryAssignmentPicker::qualifiedLibraryInstrumentId(libArt);
            const auto existingRow = visibleRowByInstrument.find(instrumentKey);
            if (existingRow == visibleRowByInstrument.end()) {
                visibleRowByInstrument.emplace(instrumentKey, this->visibleLibraryArticulationIndexes.size());
                this->visibleLibraryArticulationIndexes.push_back(idx);
                continue;
            }

            const auto visibleRow = existingRow->second;
            const int currentLibArtIndex = this->visibleLibraryArticulationIndexes[visibleRow];
            const auto& currentLibArt = this->availableLibraryArticulations[static_cast<size_t>(currentLibArtIndex)];
            if (previewArticulationPriority(libArt, this->articulationPriorityRules) < previewArticulationPriority(currentLibArt, this->articulationPriorityRules)) this->visibleLibraryArticulationIndexes[visibleRow] = idx;
            continue;
        }
        this->visibleLibraryArticulationIndexes.push_back(idx);
    }

    if (this->selectionMode == SelectionMode::Instrument) {
        this->selectedLibraryInstrumentArticulationIndex = -1;
        this->rebuildArticulationEntries();
    }
    this->assignmentListBox.updateContent();
}

void LibraryAssignmentPicker::updateCategoryButtons() {
    auto apply = [this](juce::TextButton& button, Category category) {
        const bool selected = this->selectedCategory == category;
        button.setColour(juce::TextButton::buttonColourId, selected ? UITheme::Colours::selectionStrong : UITheme::Colours::bgControl);
        button.setColour(juce::TextButton::textColourOffId, selected ? UITheme::Colours::textPrimary : UITheme::Colours::textSecondary);
        button.repaint();
    };

    apply(this->allCategoryButton, Category::All);
    apply(this->drumsCategoryButton, Category::Drums);
    apply(this->cymbalsCategoryButton, Category::Cymbals);
    apply(this->otherCategoryButton, Category::Other);
}

void LibraryAssignmentPicker::confirmAssignment(int libArtIndex) {
    if (libArtIndex < 0 || libArtIndex >= static_cast<int>(this->availableLibraryArticulations.size())) return;
    if (this->selectionMode == SelectionMode::Articulation) {
        if (this->onLibraryArticulationSelected) this->onLibraryArticulationSelected(libArtIndex);
        return;
    }

    const auto& libArt = this->availableLibraryArticulations[static_cast<size_t>(libArtIndex)];
    if (!this->onLibraryInstrumentSelected) return;
    LibraryInstrumentSelection selection;
    selection.kitId = libArt.kitCodeRaw;
    selection.instrumentId = libArt.instCode;
    this->onLibraryInstrumentSelected(selection);
}

void LibraryAssignmentPicker::timerCallback() {
    this->rebuildFiltered();
}

void LibraryAssignmentPicker::queueRebuildFiltered() {
    this->startTimer(filterDebounceMs);
}

int LibraryAssignmentPicker::KindListModel::getNumRows() {
    return static_cast<int>(this->owner.kindEntries.size()) + 1;
}

void LibraryAssignmentPicker::KindListModel::paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) {
    const bool selected = (rowNumber == 0 && this->owner.selectedKind == KitLib::InstrumentType::Unknown)
        || (rowNumber > 0
            && rowNumber - 1 < static_cast<int>(this->owner.kindEntries.size())
            && this->owner.kindEntries[static_cast<size_t>(rowNumber - 1)] == this->owner.selectedKind);

    g.fillAll(selected || rowIsSelected
        ? UITheme::Colours::listSelection
        : (rowNumber % 2 == 0 ? UITheme::Colours::bgControlAlt : UITheme::Colours::listRowOdd));

    juce::String text = "all";
    if (rowNumber > 0 && rowNumber - 1 < static_cast<int>(this->owner.kindEntries.size()))
        text = kindLabel(this->owner.kindEntries[static_cast<size_t>(rowNumber - 1)]);

    g.setColour(selected ? UITheme::Colours::textPrimary : UITheme::Colours::textSecondary);
    g.setFont(UITheme::Fonts::normal().withHeight(static_cast<float>(UITheme::Sizes::standardControlTextHeight)));
    g.drawText(text, 6, 0, width - 10, height, juce::Justification::centredLeft);
}

void LibraryAssignmentPicker::KindListModel::listBoxItemClicked(int row, const juce::MouseEvent& event) {
    juce::ignoreUnused(event);
    if (row <= 0) {
        this->owner.selectedKind = KitLib::InstrumentType::Unknown;
    } else if (row - 1 < static_cast<int>(this->owner.kindEntries.size())) {
        this->owner.selectedKind = this->owner.kindEntries[static_cast<size_t>(row - 1)];
    } else {
        return;
    }

    this->owner.kindListBox.selectRow(row);
    this->owner.kindListBox.repaint();
    this->owner.rebuildArticulationEntries();
    this->owner.rebuildFiltered();
}

int LibraryAssignmentPicker::ArticulationListModel::getNumRows() {
    return static_cast<int>(this->owner.articulationEntries.size());
}

void LibraryAssignmentPicker::ArticulationListModel::paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) {
    if (rowNumber < 0 || rowNumber >= static_cast<int>(this->owner.articulationEntries.size())) return;
    g.fillAll(rowIsSelected ? UITheme::Colours::listSelection : (rowNumber % 2 == 0 ? UITheme::Colours::bgControlAlt : UITheme::Colours::listRowOdd));
    g.setColour(rowIsSelected ? UITheme::Colours::textPrimary : UITheme::Colours::textSecondary);
    g.setFont(UITheme::Fonts::normal().withHeight(static_cast<float>(UITheme::Sizes::standardControlTextHeight)));
    g.drawText(this->owner.articulationEntries[static_cast<size_t>(rowNumber)], 6, 0, width - 10, height, juce::Justification::centredLeft);
}

void LibraryAssignmentPicker::ArticulationListModel::selectedRowsChanged(int lastRowSelected) {
    if (this->owner.selectionMode == SelectionMode::Instrument) {
        if (lastRowSelected < 0 || lastRowSelected >= static_cast<int>(this->owner.previewLibraryArticulationIndexes.size())) return;
        const int libArtIndex = this->owner.previewLibraryArticulationIndexes[static_cast<size_t>(lastRowSelected)];
        if (this->owner.onLibraryArticulationPreview) this->owner.onLibraryArticulationPreview(libArtIndex, static_cast<juce::uint8>(127));
        return;
    }

    juce::ignoreUnused(lastRowSelected);
    this->owner.selectedArticulations.clear();
    for (int idx = 0; idx < this->owner.articulationListBox.getNumSelectedRows(); ++idx) {
        const int selectedRow = this->owner.articulationListBox.getSelectedRow(idx);
        if (selectedRow >= 0 && selectedRow < static_cast<int>(this->owner.articulationEntries.size()))
            this->owner.selectedArticulations.insert(this->owner.articulationEntries[static_cast<size_t>(selectedRow)]);
    }
    this->owner.rebuildFiltered();
}

int LibraryAssignmentPicker::AssignmentListModel::getNumRows() {
    return this->owner.visibleLibraryArticulationIndexes.empty() ? 1 : static_cast<int>(this->owner.visibleLibraryArticulationIndexes.size());
}

void LibraryAssignmentPicker::AssignmentListModel::paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) {
    if (this->owner.visibleLibraryArticulationIndexes.empty()) {
        g.fillAll(UITheme::Colours::bgControlAlt);
        g.setColour(UITheme::Colours::textMuted);
        g.setFont(UITheme::Fonts::normal().withHeight(static_cast<float>(UITheme::Sizes::standardControlTextHeight)));

        const bool instrumentMode = this->owner.selectionMode == SelectionMode::Instrument;
        const auto emptyMessage = this->owner.availableLibraryArticulations.empty()
            ? juce::String(instrumentMode ? "No library instruments available" : "No library articulations available")
            : juce::String(instrumentMode ? "No matching library instruments" : "No matching library articulations");
        const auto emptyHint = this->owner.availableLibraryArticulations.empty()
            ? juce::String("Check your sample library path")
            : juce::String("Try a different search or category");

        g.drawText(emptyMessage, 8, 1, width - 16, height / 2, juce::Justification::centredLeft);
        g.setColour(UITheme::Colours::textMuted.withAlpha(0.75f));
        g.setFont(UITheme::Fonts::normal().withHeight(static_cast<float>(UITheme::Sizes::standardControlTextHeight)));
        g.drawText(emptyHint, 8, height / 2 - 1, width - 16, height / 2, juce::Justification::centredLeft);
        return;
    }

    if (rowNumber < 0 || rowNumber >= static_cast<int>(this->owner.visibleLibraryArticulationIndexes.size())) return;

    const int libArtIndex = this->owner.visibleLibraryArticulationIndexes[static_cast<size_t>(rowNumber)];
    const auto& libArt = this->owner.availableLibraryArticulations[static_cast<size_t>(libArtIndex)];
    const int btnW = LibraryAssignmentPicker::selectButtonWidth;
    const int prevW = LibraryAssignmentPicker::previewButtonWidth;
    const bool showActions = rowNumber == this->owner.actionRow;
    const int actionWidth = btnW + prevW;

    g.fillAll(rowIsSelected ? UITheme::Colours::listSelection : (rowNumber % 2 == 0 ? UITheme::Colours::bgControlAlt : UITheme::Colours::listRowOdd));

    const auto rowFont = UITheme::Fonts::normal().withHeight(static_cast<float>(UITheme::Sizes::standardControlTextHeight));
    const auto badgeFont = UITheme::Fonts::small();
    const bool instrumentMode = this->owner.selectionMode == SelectionMode::Instrument;
    const auto displayText = instrumentMode ? LibraryAssignmentPicker::libraryInstrumentDisplayLabel(libArt) : LibraryAssignmentPicker::libraryArticulationDisplayLabel(libArt);
    const auto& summary = instrumentMode ? libArt.instrumentSummary : libArt.articulationSummary;
    const int horizontalPadding = 4;
    const int summaryGap = 12;
    const int minimumLabelWidth = juce::jmin(160, width / 2);
    const int desiredSummaryWidth = librarySummaryBadgeWidth(summary, badgeFont);
    const int summaryWidth = juce::jmin(desiredSummaryWidth, juce::jmax(0, width - horizontalPadding * 2 - summaryGap - minimumLabelWidth));
    const auto summaryArea = juce::Rectangle<int>(width - horizontalPadding - summaryWidth, 0, summaryWidth, height);
    const auto labelArea = juce::Rectangle<int>(horizontalPadding, 0, juce::jmax(0, summaryArea.getX() - summaryGap - horizontalPadding), height);

    g.setColour(UITheme::Colours::textPrimary);
    g.setFont(rowFont);
    g.drawFittedText(displayText, labelArea, juce::Justification::centredLeft, 1, 0.8f);
    paintLibrarySummaryBadges(g, summary, summaryArea, badgeFont);

    if (!showActions) return;

    const int actionX = width - actionWidth;
    const int actionFadeWidth = actionWidth + 24;
    const int actionFadeX = width - actionFadeWidth;
    g.setGradientFill(juce::ColourGradient(UITheme::Colours::bgPanelRaised.withAlpha(0.0f), static_cast<float>(actionFadeX), 0.0f, UITheme::Colours::bgPanelRaised.withAlpha(0.8f), static_cast<float>(width), 0.0f, false));
    g.fillRect(actionFadeX, 0, actionFadeWidth, height);

    auto prevArea = juce::Rectangle<int>(actionX, 0, prevW, height).reduced(2, 3);
    juce::Path previewTriangle;
    previewTriangle.addTriangle(static_cast<float>(prevArea.getX()), static_cast<float>(prevArea.getBottom()),
                                static_cast<float>(prevArea.getRight()), static_cast<float>(prevArea.getBottom()),
                                static_cast<float>(prevArea.getRight()), static_cast<float>(prevArea.getY()));
    g.setColour(UITheme::Colours::accentSoft);
    g.fillPath(previewTriangle);

    auto btnArea = juce::Rectangle<int>(actionX + prevW, 0, btnW, height).withSizeKeepingCentre(btnW, btnW).reduced(1);
    g.setColour(UITheme::Colours::borderMid);
    g.fillRoundedRectangle(btnArea.toFloat(), 2.0f);
    g.setColour(UITheme::Colours::textPrimary);
    g.setFont(rowFont);
    g.drawText(UITheme::Symbols::checkedBox, btnArea, juce::Justification::centred);
}

void LibraryAssignmentPicker::AssignmentListModel::listBoxItemClicked(int row, const juce::MouseEvent& event) {
    if (this->owner.visibleLibraryArticulationIndexes.empty()) return;
    if (row < 0 || row >= static_cast<int>(this->owner.visibleLibraryArticulationIndexes.size())) return;

    const int libArtIndex = this->owner.visibleLibraryArticulationIndexes[static_cast<size_t>(row)];
    const bool wasActionRow = this->owner.mouseSelectionChanged ? this->owner.actionRowBeforeMouseSelection == row : this->owner.actionRow == row;
    this->owner.mouseSelectionChanged = false;
    this->owner.actionRowBeforeMouseSelection = -1;
    this->owner.actionRow = row;
    this->owner.assignmentListBox.selectRow(row);
    this->owner.assignmentListBox.repaint();

    if (!wasActionRow) {
        if (this->owner.onLibraryArticulationPreview) this->owner.onLibraryArticulationPreview(libArtIndex, static_cast<juce::uint8>(127));
        return;
    }

    const int btnW = LibraryAssignmentPicker::selectButtonWidth;
    const int prevW = LibraryAssignmentPicker::previewButtonWidth;
    const int totalWidth = this->owner.assignmentListBox.getWidth();
    const int textWidth = totalWidth - btnW - prevW;
    const int clickX = event.getPosition().getX();

    if (clickX >= textWidth + prevW) {
        this->owner.confirmAssignment(libArtIndex);
    } else if (clickX >= textWidth) {
        if (this->owner.onLibraryArticulationPreview) {
            const float xRatio = juce::jlimit(0.0f, 1.0f, static_cast<float>(clickX - textWidth) / static_cast<float>(juce::jmax(1, prevW)));
            const juce::uint8 velocity = static_cast<juce::uint8>(juce::jlimit(1, 127, static_cast<int>(xRatio * 127.0f)));
            this->owner.onLibraryArticulationPreview(libArtIndex, velocity);
        }
    } else {
        if (this->owner.onLibraryArticulationPreview) this->owner.onLibraryArticulationPreview(libArtIndex, static_cast<juce::uint8>(127));
    }
}

void LibraryAssignmentPicker::AssignmentListModel::selectedRowsChanged(int lastRowSelected) {
    if (lastRowSelected < 0 || lastRowSelected >= static_cast<int>(this->owner.visibleLibraryArticulationIndexes.size())) {
        this->owner.actionRow = -1;
        if (this->owner.selectionMode == SelectionMode::Instrument) {
            this->owner.selectedLibraryInstrumentArticulationIndex = -1;
            this->owner.rebuildArticulationEntries();
        }
        this->owner.assignmentListBox.repaint();
        return;
    }

    if (this->owner.assignmentListBox.isMouseButtonDown(true)) {
        this->owner.actionRowBeforeMouseSelection = this->owner.actionRow;
        this->owner.mouseSelectionChanged = true;
    }
    this->owner.actionRow = lastRowSelected;
    const int libArtIndex = this->owner.visibleLibraryArticulationIndexes[static_cast<size_t>(lastRowSelected)];
    if (this->owner.selectionMode == SelectionMode::Instrument) {
        this->owner.selectedLibraryInstrumentArticulationIndex = libArtIndex;
        this->owner.rebuildArticulationEntries();
    }
    this->owner.assignmentListBox.repaint();

    if (!this->owner.suppressPreviewOnSelection && !this->owner.assignmentListBox.isMouseButtonDown(true) && this->owner.onLibraryArticulationPreview) {
        this->owner.onLibraryArticulationPreview(libArtIndex, static_cast<juce::uint8>(127));
    }
}

void LibraryAssignmentPicker::AssignmentListModel::returnKeyPressed(int lastRowSelected) {
    if (lastRowSelected < 0 || lastRowSelected >= static_cast<int>(this->owner.visibleLibraryArticulationIndexes.size())) return;
    const int libArtIndex = this->owner.visibleLibraryArticulationIndexes[static_cast<size_t>(lastRowSelected)];
    this->owner.confirmAssignment(libArtIndex);
}
