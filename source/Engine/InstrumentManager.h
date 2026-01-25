#pragma once

#include "DrumKitModel.h"
#include "LayoutManager.h"
#include <juce_core/juce_core.h>
#include <memory>
#include <vector>

using namespace juce;

/**
 * InstrumentManager - loading, caching, and hot-reloading of drum kits from a directory structure.
 *
 */
class InstrumentManager {
public:
    struct LayoutInstrumentMapping {
        String layoutId;
        // midi note -> (instrument pointer, articulation name)
        std::map<int, std::pair<DrumKitInstrument*, String>> noteToInstrument;
        // layout instrument id -> kit instrument pointer
        std::map<String, DrumKitInstrument*> instrumentByLayoutId;
    };

    /**
     * [UNUSED?] Map a KitLayout to a specific DrumKit. This will attempt to match
     * layout instrument ids/names to instruments in the kit and populate
     * the kit with any missing articulations referenced by the layout.
     * The resulting mapping is stored internally and can be queried via
     * getMappedInstrumentForNote().
     */
    // bool mapKitToLayout(const KitLayout& layout, DrumKit* kit, String& outError);
    /**
     * Query mapped instrument + articulation for a given layout id and midi note.
     * Returns pair<DrumKitInstrument*, articulationName>. If not found, returns {nullptr, ""}.
     */
    std::pair<DrumKitInstrument*, String> getMappedInstrumentForNote(const String& layoutId, int midiNote) const;

    InstrumentManager();
    explicit InstrumentManager(const File& kitsDir);

    bool validateKitsDir(const File& dir);
    void setKitsDir(const File& dir);

    int scanAll();

    int getKitCount() const;
    DrumKit* getKit(int index);
    DrumKit* findKit(const String& name);
    const std::vector<std::unique_ptr<DrumKit>>& getAllKits() const;

    int getTotalSamples() const;
    bool hasKitsChanged() const;

private:
    File mKitsDir;
    std::vector<std::unique_ptr<DrumKit>> mKits;
    
    int mTotalInstruments;
    int mTotalSamples;
    String logs;

    // Cached mappings per layout id
    std::map<String, LayoutInstrumentMapping> mLayoutMappings;
};
