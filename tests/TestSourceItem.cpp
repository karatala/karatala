#include "../source/UI/SourceItem.h"
#include <catch2/catch_test_macros.hpp>

namespace SourceItemTests {

static SourceItem makeSourceItem(const juce::String& articulation) {
    SourceItem item;
    item.kitCodeRaw = "KitA";
    item.kitDisplayName = "Kit A";
    item.instCode = "hihat-14";
    item.artCode = articulation;
    return item;
}

TEST_CASE("SourceItem anchors a legacy hihat source to the destination expression", "[UI][InstrumentMap][HiHat]") {
    const std::vector<SourceItem> sourceItems {
        makeSourceItem("edge[close]"),
        makeSourceItem("edge[open]"),
        makeSourceItem("edge[tight]")
    };

    CHECK(SourceItem::resolveLegacyHihatSourcePath(sourceItems, "KitA/hihat-14/edge[close]", "bow[open]") == "KitA/hihat-14/edge[open]");
    CHECK(SourceItem::resolveLegacyHihatSourcePath(sourceItems, "KitA/hihat-14/edge[close]", "bow[tight]") == "KitA/hihat-14/edge[tight]");
}

TEST_CASE("SourceItem keeps the old hihat source when the matching expression is unavailable", "[UI][InstrumentMap][HiHat]") {
    const std::vector<SourceItem> sourceItems { makeSourceItem("edge[close]") };

    CHECK(SourceItem::resolveLegacyHihatSourcePath(sourceItems, "KitA/hihat-14/edge[close]", "bow[open]") == "KitA/hihat-14/edge[close]");
}

TEST_CASE("SourceItem migrates only legacy destination expression filters", "[UI][InstrumentMap][HiHat]") {
    CHECK(SourceItem::usesLegacyArticulationSlotExpressionFilter({ "bow" }, "bow[open]"));
    CHECK_FALSE(SourceItem::usesLegacyArticulationSlotExpressionFilter({}, "bow[tight]"));
    CHECK_FALSE(SourceItem::usesLegacyArticulationSlotExpressionFilter({ "bow[open]" }, "bow[open]"));
    CHECK_FALSE(SourceItem::usesLegacyArticulationSlotExpressionFilter({ "edge" }, "bow[open]"));
    CHECK_FALSE(SourceItem::usesLegacyArticulationSlotExpressionFilter({ "bow" }, "bow"));
}

} // namespace SourceItemTests
