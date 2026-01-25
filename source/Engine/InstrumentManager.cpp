#include "InstrumentManager.h"

using namespace juce;

InstrumentManager::InstrumentManager(const File& kitsDir)
    : mKitsDir(kitsDir)
    , mTotalSamples(0) {
    if (!validateKitsDir(kitsDir))
        return;
}

bool InstrumentManager::validateKitsDir(const File& dir) {
    if (!dir.isDirectory()) {
        DBG("Warning: kitsDir is not a directory: " + dir.getFullPathName());
        return false;
    }
    return true;
}

void InstrumentManager::setKitsDir(const File& dir) {
    if (!validateKitsDir(dir))
        return;
    mKitsDir = dir;
    mKits.clear();
    mTotalSamples = 0;
}

int InstrumentManager::scanAll() {
    mKits.clear();
    mTotalSamples = 0;
    mTotalInstruments = 0;

    if (!validateKitsDir(mKitsDir))
        return 0;

    DBG("INFO: InstrumentManager: Scanning for drum kits in " + mKitsDir.getFullPathName());

    int kitCount = 0;
    std::unordered_map<String, int> dimensionCodeCounts;
    std::unordered_map<String, int> micCodeCounts;
    std::unordered_map<String, int> sampleVariantCounts;
    std::unordered_map<int, int> velocityCounts;

    for (const juce::DirectoryEntry& entry : juce::RangedDirectoryIterator(mKitsDir, false, "*", juce::File::TypesOfFileToFind::findDirectories)) {
        const juce::File& kitDir = entry.getFile();
        if (!kitDir.isDirectory())
            continue;

        auto parsedInfo = DrumKit::parseInfo(kitDir);
        if (!parsedInfo.ok()) {
            DBG("WARN: Could not parse kitDir '" + kitDir.getFullPathName() + "': " + parsedInfo.error());
            continue;
        }

        auto kit = std::make_unique<DrumKit>(parsedInfo.value);
        int instrsInKit = kit->scanInstruments();
        int samplesInKit = 0;

        for (auto& pair : kit->instruments) {
            auto samplesInInstr = pair.second->scanInstrumentSamples();
            if (samplesInInstr > 0) {
                samplesInKit += samplesInInstr;
                mTotalSamples += samplesInInstr;
                mTotalInstruments++;

                // Collect statistics
                for (const auto& articulation : pair.second->articulations) {
                    for (const auto& velocityLayer : articulation->velocityLayers) {
                        velocityCounts[velocityLayer->velocity]++;
                        for (const auto& sampleVariant : velocityLayer->sampleVariants) {
                            micCodeCounts[sampleVariant->micDescriptor.raw]++;
                            sampleVariantCounts[String(sampleVariant->variantLetter)]++;
                        }
                    }
                }
            }
        }

        if (!kit->isEmpty()) {
            mKits.push_back(std::move(kit));
            kitCount++;
            DBG("INFO: Scanned kit: " + kitDir.getFileName() + ", " + String(instrsInKit) + " instruments, " + String(samplesInKit) + " samples");
        } else {
            DBG("NOTICE: Skipped empty kit: " + kitDir.getFileName());
        }
    }

    // Log detailed statistics
    DBG("INFO: Dimension Code Usage:");
    for (const auto& [key, count] : dimensionCodeCounts) {
        DBG("  " + key + ": " + String(count));
    }

    DBG("INFO: Mic Code Usage:");
    for (const auto& [key, count] : micCodeCounts) {
        DBG("  " + key + ": " + String(count));
    }

    DBG("INFO: Sample Variant Usage:");
    for (const auto& [key, count] : sampleVariantCounts) {
        DBG("  " + key + ": " + String(count));
    }

    DBG("INFO: Velocity Usage:");
    for (const auto& [key, count] : velocityCounts) {
        DBG("  " + String(key) + ": " + String(count));
    }

    DBG("INFO: Total: " + String(kitCount) + " kits, " + String(mTotalInstruments) + " instruments, " + String(mTotalSamples) + " samples");
    return kitCount;
}

int InstrumentManager::getKitCount() const {
    return static_cast<int>(mKits.size());
}

DrumKit* InstrumentManager::getKit(int index) {
    if (index < 0 || index >= static_cast<int>(mKits.size())) {
        return nullptr;
    }
    return mKits[index].get();
}

DrumKit* InstrumentManager::findKit(const String& name) {
    for (auto& kit : mKits) {
        if (kit->meta.name == name) {
            return kit.get();
        }
    }
    return nullptr;
}

const std::vector<std::unique_ptr<DrumKit>>& InstrumentManager::getAllKits() const {
    return mKits;
}

int InstrumentManager::getTotalSamples() const {
    return mTotalSamples;
}

bool InstrumentManager::hasKitsChanged() const {
    for (const auto& kit : mKits) {
        if (kit->hasChanged()) {
            return true;
        }
    }
    return false;
}

std::pair<DrumKitInstrument*, String> InstrumentManager::getMappedInstrumentForNote(const String& layoutId, int midiNote) const {
    auto it = mLayoutMappings.find(layoutId);
    if (it == mLayoutMappings.end())
        return { nullptr, String() };
    const auto& mapping = it->second;
    auto nit = mapping.noteToInstrument.find(midiNote);
    if (nit == mapping.noteToInstrument.end())
        return { nullptr, String() };
    return nit->second;
}




// bool InstrumentManager::mapKitToLayout(const KitLayout& layout, DrumKit* kit, String& outError) {

//     if (kit == nullptr) {
//         outError = "No kit provided";
//         return false;
//     }

//     LayoutInstrumentMapping mapping;
//     mapping.layoutId = layout.id.isEmpty() ? layout.name : layout.id;

//     // For each layout instrument, try to find a matching kit instrument
//     for (const auto& lin : layout.instruments) {
//         DrumKitInstrument* matched = nullptr;

//         // 1) exact match by code
//         if (!lin.id.isEmpty())
//             matched = kit->findInstrument(lin.id);

//         // 2) exact match by instrument name
//         // if (!matched && !lin.id.isEmpty()) {
//         //     auto found = kit->findInstruments(lin.id);
//         //     if (!found.empty())
//         //         matched = found.front();
//         // }

//         // 3) fuzzy match: check containment in instrument name
//         // if (!matched && !lin.id.isEmpty()) {
//         //     String needle = lin.id.toLowerCase();
//         //     for (const auto& instPtr : kit->instruments) {
//         //         if (instPtr->meta.name.toLowerCase().contains(needle) || instPtr->meta.code.toLowerCase().contains(needle)) {
//         //             matched = instPtr.get();
//         //             break;
//         //         }
//         //     }
//         // }

//         // // 4) fallback: try matching by type/display/name
//         // if (!matched) {
//         //     for (const auto& instPtr : kit->instruments) {
//         //         if (instPtr->meta.name.equalsIgnoreCase(lin.display) || instPtr->meta.code.equalsIgnoreCase(lin.id)) {
//         //             matched = instPtr.get();
//         //             break;
//         //         }
//         //     }
//         // }

//         if (!matched) {
//             // no instrument matched for this layout instrument; record as null and continue
//             mapping.instrumentByLayoutId[lin.id] = nullptr;
//         } else {
//             mapping.instrumentByLayoutId[lin.id] = matched;

//             // Helper to canonicalize articulation names (aliases) and choose an articulation on the matched instrument.
//             // Uses aliases such as head->center, top->center, side->edge, shell->rim, rimshot->rim.
//             auto canonicalizeArt = [&](const String& s) -> String {
//                 String key = s.trim().toLowerCase();
//                 if (key.isEmpty())
//                     return key;
//                 if (key == "head" || key == "top" || key == "mid" || key == "middle")
//                     return "center";
//                 if (key == "side" || key == "sidehit")
//                     return "edge";
//                 if (key == "shell" || key == "rimshot" || key == "rimshot")
//                     return "rim";
//                 if (key == "bell")
//                     return "bell";
//                 if (key == "bow")
//                     return "bow";
//                 // default: return lowercased original
//                 return key;
//             };

//             auto chooseArticulation = [&](DrumKitInstrument* inst, const String& requested) -> String {
//                 String reqCanon = canonicalizeArt(requested);

//                 // 1) if requested articulation exists (by exact name)
//                 if (requested.isNotEmpty()) {
//                     if (inst->findArticulation(requested) != nullptr)
//                         return requested;
//                 }

//                 // 1b) if requested canonical exists among instrument articulations, return the actual articulation name
//                 if (reqCanon.isNotEmpty()) {
//                     for (const auto& a : inst->articulations) {
//                         if (canonicalizeArt(a->name) == reqCanon)
//                             return a->name;
//                     }
//                 }

//                 // priority list (high -> low)
//                 const StringArray priority { "center", "bow" };
//                 for (auto& p : priority) {
//                     for (const auto& a : inst->articulations) {
//                         if (canonicalizeArt(a->name) == canonicalizeArt(p))
//                             return a->name;
//                     }
//                 }

//                 // fallback: first existing articulation on the instrument (return its actual name)
//                 if (!inst->articulations.empty())
//                     return inst->articulations.front()->name;

//                 return String();
//             };

//             // Map articulations: use requested articulation if present, otherwise fallback
//             for (const auto& art : lin.articulations) {
//                 if (art.code.isEmpty())
//                     continue;
//                 String targetArt = chooseArticulation(matched, art.code);
//                 if (targetArt.isEmpty())
//                     continue;

//                 // map notes for this articulation (map to chosen articulation name)
//                 for (int note : art.notes) {
//                     mapping.noteToInstrument[note] = { matched, targetArt };
//                 }
//             }

//             // Also map any top-level notes defined on the layout instrument
//             // for (int note : lin.notes) {
//             //     if (mapping.noteToInstrument.find(note) == mapping.noteToInstrument.end()) {
//             //         // choose default articulation for this instrument (no specific requested)
//             //         String defaultArt = chooseArticulation(matched, String());
//             //         mapping.noteToInstrument[note] = { matched, defaultArt };
//             //     }
//             // }
//         }
//     }

//     // store mapping keyed by layout id
//     String key = mapping.layoutId;
//     mLayoutMappings[key] = std::move(mapping);
//     return true;
// }
