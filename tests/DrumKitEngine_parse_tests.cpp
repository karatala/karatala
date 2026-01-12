// Test removed per request: no further tests should be added.
#if 0
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>

#include "../source/Engine/DrumKitEngine.h"

using namespace juce;

TEST_CASE("parseInstrumentMapText parses valid lines and ignores comments/empty lines") {
    juce::String text = "# comment\n\n  kick1 : NakedDrums by WilkinsonAudio [CC-BY-4.0]/kick-22-YamahaRecordingCustom\nsnare1: MyKit/snare-1\ninvalidlinewithoutcolon\n  \n# another comment\n";

    auto pairs = DrumKitEngine::parseInstrumentMapText(text);
    REQUIRE(pairs.size() == 2);
    REQUIRE(pairs[0].first == "kick1");
    REQUIRE(pairs[0].second == "NakedDrums by WilkinsonAudio [CC-BY-4.0]/kick-22-YamahaRecordingCustom");
    REQUIRE(pairs[1].first == "snare1");
    REQUIRE(pairs[1].second == "MyKit/snare-1");
}
#endif
