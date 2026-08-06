#include "AvailableLibraryArticulation.h"

#include "../Engine/ArticulationNames.h"
#include "../Engine/KitLibModel.h"

#include <algorithm>
#include <unordered_set>
#include <utility>

static juce::String micBadgeFor(const KitLib::MicSpec& micSpec) {
    if (micSpec.type == KitLib::MicSpec::Type::Overhead) return "OH";
    if (micSpec.type == KitLib::MicSpec::Type::Room) {
        if (micSpec.position == KitLib::MicSpec::Position::Close) return "RC";
        if (micSpec.position == KitLib::MicSpec::Position::Far) return "RF";
    }
    if (micSpec.type == KitLib::MicSpec::Type::Direct) {
        switch (micSpec.position) {
            case KitLib::MicSpec::Position::Sub: return "DU";
            case KitLib::MicSpec::Position::Side: return "DS";
            case KitLib::MicSpec::Position::Top: return "DT";
            case KitLib::MicSpec::Position::Bottom: return "DB";
            case KitLib::MicSpec::Position::In: return "DI";
            case KitLib::MicSpec::Position::Out: return "DO";
            case KitLib::MicSpec::Position::Unknown:
            case KitLib::MicSpec::Position::Any:
            case KitLib::MicSpec::Position::Normal:
            case KitLib::MicSpec::Position::Wide:
            case KitLib::MicSpec::Position::Close:
            case KitLib::MicSpec::Position::Far: break;
        }
    }

    const auto rawCode = micSpec.raw.trim().toUpperCase();
    return rawCode.length() >= 2 ? rawCode.substring(0, 2) : rawCode;
}

static void addMicBadge(std::vector<juce::String>& badges, const juce::String& badge) {
    if (badge.isEmpty()) return;
    const bool alreadyPresent = std::any_of(badges.begin(), badges.end(), [&badge](const auto& existingBadge) { return existingBadge.compareIgnoreCase(badge) == 0; });
    if (!alreadyPresent) badges.push_back(badge);
}

static int micBadgePriority(const juce::String& badge) {
    static const std::vector<juce::String> preferredOrder { "DU", "DS", "DT", "DB", "DI", "DO", "OH", "RC", "RF", "MS" };
    const auto badgeIterator = std::find(preferredOrder.begin(), preferredOrder.end(), badge);
    return badgeIterator == preferredOrder.end() ? static_cast<int>(preferredOrder.size()) : static_cast<int>(std::distance(preferredOrder.begin(), badgeIterator));
}

static void sortMicBadges(std::vector<juce::String>& badges) {
    std::sort(badges.begin(), badges.end(), [](const auto& leftBadge, const auto& rightBadge) {
        const int leftPriority = micBadgePriority(leftBadge);
        const int rightPriority = micBadgePriority(rightBadge);
        if (leftPriority != rightPriority) return leftPriority < rightPriority;
        return leftBadge.compareIgnoreCase(rightBadge) < 0;
    });
}

static void updateSummaryLabels(LibrarySampleSummary& summary) {
    if (summary.maxRoundRobinCount > 0) {
        summary.roundRobinCountText = summary.minRoundRobinCount == summary.maxRoundRobinCount
            ? juce::String(summary.maxRoundRobinCount)
            : juce::String(summary.minRoundRobinCount) + "-" + juce::String(summary.maxRoundRobinCount);
    }
    if (summary.maxVelocityLevelCount > 0) {
        summary.velocityLevelCountText = summary.minVelocityLevelCount == summary.maxVelocityLevelCount
            ? juce::String(summary.maxVelocityLevelCount)
            : juce::String(summary.minVelocityLevelCount) + "-" + juce::String(summary.maxVelocityLevelCount);
    }
}

static LibrarySampleSummary summarizeArticulation(const KitLib::ArticulationSpec& articulation) {
    LibrarySampleSummary summary;
    int velocityLevelCount = 0;
    for (const auto& velocityLevel : articulation.velocityLevels) {
        if (velocityLevel == nullptr || velocityLevel->isEmpty()) continue;
        ++velocityLevelCount;

        int roundRobinCount = 0;
        for (const auto& logicalHit : velocityLevel->logicalHits) {
            if (logicalHit == nullptr || logicalHit->isEmpty()) continue;
            ++roundRobinCount;
            for (const auto& sample : logicalHit->samples) {
                if (sample == nullptr) continue;
                addMicBadge(summary.micBadges, micBadgeFor(sample->micSpec));
                if (sample->micSpec.format == KitLib::MicSpec::Format::MidSide) addMicBadge(summary.micBadges, "MS");
            }
        }

        if (roundRobinCount <= 0) continue;
        if (summary.minRoundRobinCount == 0 || roundRobinCount < summary.minRoundRobinCount) summary.minRoundRobinCount = roundRobinCount;
        summary.maxRoundRobinCount = juce::jmax(summary.maxRoundRobinCount, roundRobinCount);
    }

    summary.minVelocityLevelCount = velocityLevelCount;
    summary.maxVelocityLevelCount = velocityLevelCount;
    sortMicBadges(summary.micBadges);
    updateSummaryLabels(summary);
    return summary;
}

static void includeArticulationSummary(LibrarySampleSummary& instrumentSummary, const LibrarySampleSummary& articulationSummary) {
    if (articulationSummary.maxVelocityLevelCount <= 0) return;
    for (const auto& badge : articulationSummary.micBadges) addMicBadge(instrumentSummary.micBadges, badge);
    if (articulationSummary.minRoundRobinCount > 0 && (instrumentSummary.minRoundRobinCount == 0 || articulationSummary.minRoundRobinCount < instrumentSummary.minRoundRobinCount))
        instrumentSummary.minRoundRobinCount = articulationSummary.minRoundRobinCount;
    instrumentSummary.maxRoundRobinCount = juce::jmax(instrumentSummary.maxRoundRobinCount, articulationSummary.maxRoundRobinCount);
    if (instrumentSummary.minVelocityLevelCount == 0 || articulationSummary.minVelocityLevelCount < instrumentSummary.minVelocityLevelCount)
        instrumentSummary.minVelocityLevelCount = articulationSummary.minVelocityLevelCount;
    instrumentSummary.maxVelocityLevelCount = juce::jmax(instrumentSummary.maxVelocityLevelCount, articulationSummary.maxVelocityLevelCount);
}

juce::String AvailableLibraryArticulation::qualifiedId() const {
    juce::String result = this->kitCodeRaw;
    if (this->instCode.isNotEmpty()) result += "/" + this->instCode;
    if (this->artCode.isNotEmpty()) result += "/" + this->artCode;
    return result;
}

juce::String AvailableLibraryArticulation::rawQualifiedId() const {
    juce::String result = this->rawKitCode.isNotEmpty() ? this->rawKitCode : this->kitCodeRaw;
    const auto instrumentCode = this->rawInstCode.isNotEmpty() ? this->rawInstCode : this->instCode;
    const auto articulationCode = this->rawArtCode.isNotEmpty() ? this->rawArtCode : this->artCode;
    if (instrumentCode.isNotEmpty()) result += "/" + instrumentCode;
    if (articulationCode.isNotEmpty()) result += "/" + articulationCode;
    return result;
}

juce::String AvailableLibraryArticulation::displayLabel() const {
    juce::String result = this->kitDisplayName;
    if (this->instCode.isNotEmpty()) result += "/" + this->instCode;
    if (this->artCode.isNotEmpty()) result += "/" + this->artCode;
    return result;
}

std::vector<AvailableLibraryArticulation> AvailableLibraryArticulation::collectFromLibrary(const KitLib::LibraryManager* libraryManager) {
    std::vector<AvailableLibraryArticulation> availableLibraryArticulations;
    if (libraryManager == nullptr) return availableLibraryArticulations;

    std::unordered_set<std::string> seenQualifiedIds;
    auto addUniqueLibArt = [&availableLibraryArticulations, &seenQualifiedIds](AvailableLibraryArticulation libArt) {
        const auto qualifiedIdKey = libArt.qualifiedId().trim().toLowerCase().toStdString();
        if (qualifiedIdKey.empty()) return;
        if (seenQualifiedIds.insert(qualifiedIdKey).second) availableLibraryArticulations.push_back(std::move(libArt));
    };

    for (const auto& kitPtr : libraryManager->getKits()) {
        if (kitPtr == nullptr) continue;
        const auto kitId = libraryManager->getPreferredKitId(*kitPtr);
        juce::String kitDisplayName = kitPtr->meta.name;
        if (kitDisplayName.isEmpty()) kitDisplayName = kitPtr->meta.codeRaw;

        std::vector<const KitLib::InstrumentSpec*> libInstruments;
        for (const auto& [code, instrumentPtr] : kitPtr->getInstrumentSpecsMap()) {
            juce::ignoreUnused(code);
            libInstruments.push_back(instrumentPtr.get());
        }
        std::sort(libInstruments.begin(), libInstruments.end(), [](const auto* leftInstrument, const auto* rightInstrument) { return leftInstrument->meta.codeRaw < rightInstrument->meta.codeRaw; });

        for (const auto* libInst : libInstruments) {
            if (libInst == nullptr || libInst->articulationSpecs.empty()) continue;
            std::vector<std::pair<const KitLib::ArticulationSpec*, LibrarySampleSummary>> articulationSummaries;
            LibrarySampleSummary instrumentSummary;
            for (const auto& libArtPtr : libInst->articulationSpecs) {
                if (libArtPtr == nullptr) continue;
                auto articulationSummary = summarizeArticulation(*libArtPtr);
                includeArticulationSummary(instrumentSummary, articulationSummary);
                articulationSummaries.emplace_back(libArtPtr.get(), std::move(articulationSummary));
            }
            sortMicBadges(instrumentSummary.micBadges);
            updateSummaryLabels(instrumentSummary);
            for (const auto& [libArtSpec, articulationSummary] : articulationSummaries) {
                AvailableLibraryArticulation availableArticulation;
                availableArticulation.kitCodeRaw = kitId;
                availableArticulation.kitDisplayName = kitDisplayName;
                availableArticulation.instCode = kitPtr->getPreferredInstrumentId(*libInst);
                availableArticulation.artCode = libInst->getPreferredArticulationId(*libArtSpec);
                availableArticulation.rawKitCode = kitPtr->meta.codeRaw;
                availableArticulation.rawInstCode = libInst->meta.codeRaw;
                availableArticulation.rawArtCode = libArtSpec->codeRaw;
                availableArticulation.instType = libInst->meta.type;
                availableArticulation.articulationSummary = articulationSummary;
                availableArticulation.instrumentSummary = instrumentSummary;
                addUniqueLibArt(std::move(availableArticulation));
            }
        }
    }
    return availableLibraryArticulations;
}

int AvailableLibraryArticulation::findIndexByQualifiedId(const std::vector<AvailableLibraryArticulation>& availableLibraryArticulations, const juce::String& qualifiedLibraryArticulationId) {
    const auto needle = qualifiedLibraryArticulationId.trim().toLowerCase();
    if (needle.isEmpty()) return -1;
    for (int idx = 0; idx < static_cast<int>(availableLibraryArticulations.size()); ++idx) {
        const auto& libArt = availableLibraryArticulations[static_cast<size_t>(idx)];
        if (libArt.qualifiedId().toLowerCase() == needle || libArt.rawQualifiedId().toLowerCase() == needle) return idx;
    }

    juce::StringArray needleParts;
    needleParts.addTokens(needle, "/", "");
    needleParts.removeEmptyStrings();
    if (needleParts.size() < 3) return -1;

    const auto needleBaseArticulation = ArticulationNames::normalizeArticulationName(needleParts[2]);
    for (int idx = 0; idx < static_cast<int>(availableLibraryArticulations.size()); ++idx) {
        const auto& libArt = availableLibraryArticulations[static_cast<size_t>(idx)];
        if (libArt.kitCodeRaw.toLowerCase() != needleParts[0] && libArt.rawKitCode.toLowerCase() != needleParts[0]) continue;
        if (libArt.instCode.toLowerCase() != needleParts[1] && libArt.rawInstCode.toLowerCase() != needleParts[1]) continue;
        if (ArticulationNames::normalizeArticulationName(libArt.artCode) == needleBaseArticulation || ArticulationNames::normalizeArticulationName(libArt.rawArtCode) == needleBaseArticulation) return idx;
    }
    return -1;
}
