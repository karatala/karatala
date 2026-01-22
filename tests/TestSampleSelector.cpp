#include "helpers/test_helpers.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "../source/Engine/SampleSelector.h"

TEST_CASE("SampleSelector - Boundary strategy ranges and selection", "[SampleSelector]") {
    using Selector = SampleSelector<1>;
    using Sample = Selector::Sample;

    std::vector<Sample> samples{{{24}}, {{48}}};
    Selector selector(samples, ZoneSplitStrategy::Boundary);

    const auto& root = selector.getRoot();
    REQUIRE(root.children.size() == 2);
    REQUIRE(root.children[0].min == 0);
    REQUIRE(root.children[0].max == 24);
    REQUIRE(root.children[1].min == 25);
    REQUIRE(root.children[1].max == 127);

    const auto* z1 = selector.findZone({10});
    REQUIRE(z1 != nullptr);
    REQUIRE(z1->samples.size() == 1);
    REQUIRE(z1->samples[0]->dimensions[0] == 24);

    const auto* z2 = selector.findZone({25});
    REQUIRE(z2 != nullptr);
    REQUIRE(z2->samples.size() == 1);
    REQUIRE(z2->samples[0]->dimensions[0] == 48);

    const auto* z3 = selector.findZone({60});
    REQUIRE(z3 != nullptr);
    REQUIRE(z3->samples.size() == 1);
    REQUIRE(z3->samples[0]->dimensions[0] == 48);
}

TEST_CASE("SampleSelector - Boundary vs MidSplit selection strategy difference", "[SampleSelector]") {
    using Selector = SampleSelector<1>;
    using Sample = Selector::Sample;

    std::vector<Sample> samples{{{24}}, {{48}}};
    Selector selectorMidSplit(samples, ZoneSplitStrategy::MidSplit);
    Selector selectorBoundary(samples, ZoneSplitStrategy::Boundary);

    const auto* b1 = selectorBoundary.findZone({25});
    REQUIRE(b1 != nullptr);
    REQUIRE(b1->samples.size() == 1);
    REQUIRE(b1->samples[0]->dimensions[0] == 48);

    const auto* m1 = selectorMidSplit.findZone({25});
    REQUIRE(m1 != nullptr);
    REQUIRE(m1->samples.size() == 1);
    REQUIRE(m1->samples[0]->dimensions[0] == 24);

    const auto* m2 = selectorMidSplit.findZone({36});
    REQUIRE(m2 != nullptr);
    REQUIRE(m2->samples.size() == 1);
    REQUIRE(m2->samples[0]->dimensions[0] == 24);

    const auto* m3 = selectorMidSplit.findZone({37});
    REQUIRE(m3 != nullptr);
    REQUIRE(m3->samples.size() == 1);
    REQUIRE(m3->samples[0]->dimensions[0] == 48);
}

TEST_CASE("SampleSelector - MidSplit strategy selection", "[SampleSelector]") {
    using Selector = SampleSelector<1>;
    using Sample = Selector::Sample;

    std::vector<Sample> samples{{{10}}, {{100}}};
    Selector selector(samples, ZoneSplitStrategy::MidSplit);

    const auto* z1 = selector.findZone({55});
    REQUIRE(z1 != nullptr);
    REQUIRE(z1->samples.size() == 1);
    REQUIRE(z1->samples[0]->dimensions[0] == 10);

    const auto* z2 = selector.findZone({55});
    REQUIRE(z2 != nullptr);
    REQUIRE(z2->samples.size() == 1);
    REQUIRE(z2->samples[0]->dimensions[0] == 10);

    const auto* z3 = selector.findZone({56});
    REQUIRE(z3 != nullptr);
    REQUIRE(z3->samples.size() == 1);
    REQUIRE(z3->samples[0]->dimensions[0] == 100);

    const auto* z4 = selector.findZone({127});
    REQUIRE(z4 != nullptr);
    REQUIRE(z4->samples.size() == 1);
    REQUIRE(z4->samples[0]->dimensions[0] == 100);
}

TEST_CASE("SampleSelector - MidSplit with duplicate sample values", "[SampleSelector]") {
    using Selector = SampleSelector<1>;
    using Sample = Selector::Sample;

    std::vector<Sample> samples{
        {{10}},
        {{10}},
        {{100}}
    };

    Selector selector(samples, ZoneSplitStrategy::MidSplit);

    const auto& root = selector.getRoot();
    REQUIRE(root.children.size() == 2);


    const auto* z1 = selector.findZone({54});
    REQUIRE(z1 != nullptr);
    REQUIRE(z1->samples.size() == 2);
}

TEST_CASE("SampleSelector - return nullptr on lookup outside 0-127 ranges", "[SampleSelector]") {
    const auto dims = 2;
    using Selector = SampleSelector<dims>;
    using Sample = Selector::Sample;

    std::vector<Sample> samples{
        {{10, 20}}
    };
    Selector selector(samples);

    const auto* zone = selector.findZone({222, 222});
    REQUIRE(zone == nullptr);
}

TEST_CASE("SampleSelector - throw on constructor with empty samples", "[SampleSelector]") {
    using Selector = SampleSelector<1>;
    std::vector<Selector::Sample> samples;

    REQUIRE_THROWS_AS(Selector(samples), std::logic_error);
}

TEST_CASE("SampleSelector - throw on constructor with out-of-range MIDI value", "[SampleSelector]") {
    using Selector = SampleSelector<1>;
    using Sample = Selector::Sample;

    std::vector<Sample> samples{
        {{200}}
    };

    REQUIRE_THROWS_AS(Selector(samples), std::logic_error);
}

TEST_CASE("SampleSelector::makeMidSplitRanges - single point covers full MIDI range", "[SampleSelector]") {
    std::vector<uint8_t> points{64};
    auto ranges = SampleSelector<1>::makeMidSplitRanges(points);
    REQUIRE(ranges.size() == 1);
    REQUIRE(ranges[0].min == 0);
    REQUIRE(ranges[0].max == 127);
}

TEST_CASE("SampleSelector::makeMidSplitRanges - multiple points split into midpoint ranges", "[SampleSelector]") {
    std::vector<uint8_t> points{8, 64, 127};
    auto ranges = SampleSelector<1>::makeMidSplitRanges(points);
    REQUIRE(ranges.size() == 3);
    REQUIRE(ranges[0].min == 0);
    REQUIRE(ranges[0].max == 36);
    REQUIRE(ranges[1].min == 37);
    REQUIRE(ranges[1].max == 95);
    REQUIRE(ranges[2].min == 96);
    REQUIRE(ranges[2].max == 127);
}

TEST_CASE("SampleSelector::makeBoundaryRanges - single point covers full MIDI range", "[SampleSelector]") {
    std::vector<uint8_t> points{64};
    auto ranges = SampleSelector<1>::makeBoundaryRanges(points);
    REQUIRE(ranges.size() == 1);
    REQUIRE(ranges[0].min == 0);
    REQUIRE(ranges[0].max == 127);
}

TEST_CASE("SampleSelector::makeBoundaryRanges - multiple points create boundary-aligned ranges", "[SampleSelector]") {
    std::vector<uint8_t> points{8, 64, 110};
    auto ranges = SampleSelector<1>::makeBoundaryRanges(points);
    REQUIRE(ranges.size() == 3);
    REQUIRE(ranges[0].min == 0);
    REQUIRE(ranges[0].max == 8);
    REQUIRE(ranges[1].min == 9);
    REQUIRE(ranges[1].max == 64);
    REQUIRE(ranges[2].min == 65);
    REQUIRE(ranges[2].max == 127);
}

TEST_CASE("SampleSelector<1> - exact match in 1 dimension", "[SampleSelector]") {
    using Selector = SampleSelector<1>;
    using Sample = Selector::Sample;

    std::vector<Sample> samples{
        {{10}},
        {{50}},
        {{90}},
    };
    Selector selector(samples);

    const auto* zone = selector.findZone({50});
    REQUIRE(zone);
    REQUIRE(zone->samples.size() == 1);
    REQUIRE(zone->samples[0]->dimensions[0] == 50);
}

TEST_CASE("SampleSelector<2> - exact match in 2 dimensions", "[SampleSelector]") {
    using Selector = SampleSelector<2>;
    using Sample = Selector::Sample;

    std::vector<Sample> samples{
        {{10, 20}},
        {{10, 80}},
        {{50, 40}},
    };
    Selector selector(samples);

    const auto* zone = selector.findZone({10, 80});
    REQUIRE(zone);
    REQUIRE(zone->samples.size() == 1);
    REQUIRE(zone->samples[0]->dimensions == std::array<uint8_t, 2>{10, 80});
}

TEST_CASE("SampleSelector<3> - exact match in 3 dimensions", "[SampleSelector]") {
    using Selector = SampleSelector<3>;
    using Sample = Selector::Sample;

    std::vector<Sample> samples{
        {{10, 20, 30}},
        {{80, 40, 60}}
    };
    Selector selector(samples);

    const auto* z1 = selector.findZone({10, 20, 30});
    REQUIRE(z1);
    REQUIRE(z1->samples[0]->dimensions[0] == 10);

    const auto* z2 = selector.findZone({80, 40, 60});
    REQUIRE(z2);
    REQUIRE(z2->samples[0]->dimensions[0] == 80);
}


TEST_CASE("SampleSelector<3> - full lookup Boundary strategy", "[SampleSelector]") {
    const uint8_t dimensions = 3;
    using Selector = SampleSelector<dimensions>;
    using Sample = Selector::Sample;

    constexpr std::array<uint8_t, 4> pdl = {24, 48, 72, 104};
    constexpr std::array<uint8_t, 2> zon = {44, 88};
    constexpr std::array<uint8_t, 5> dyn = {16, 46, 80, 110, 127};

    std::vector<Sample> samples;
    for (auto p : pdl)
        for (auto z : zon)
            for (auto d : dyn)
                samples.push_back({{p, z, d}});

    Selector selector(samples, ZoneSplitStrategy::Boundary);

    struct Case {
        std::array<uint8_t, dimensions> query;
        std::array<uint8_t, dimensions> expected;
    };

    const std::vector<Case> cases = {
        { {  48,  44,  80 }, {  48,  44,  80 }, },
        { {  73,  45, 111 }, { 104,  88, 127 }, },
        { {  89,  67,  64 }, { 104,  88,  80 }, },
        { {   0,   0,   0 }, {  24,  44,  16 }, },
        { { 127, 127, 127 }, { 104,  88, 127 }, },
    };

    for (const auto& c : cases) {
        DYNAMIC_SECTION(
            "query=("
            << int(c.query[0]) << "," << int(c.query[1]) << "," << int(c.query[2]) << ") "
            << "expected=("
            << int(c.expected[0]) << "," << int(c.expected[1]) << "," << int(c.expected[2]) << ")"
        ) {
            const auto* leaf = selector.findZone(c.query);
            REQUIRE(leaf != nullptr);
            REQUIRE(leaf->samples.size() == 1);

            const auto& dims = leaf->samples[0]->dimensions;
            REQUIRE(dims == c.expected);
        }
    }
}

TEST_CASE("SampleSelector<3> - full lookup MidSplit strategy", "[SampleSelector]") {
    const uint8_t dimensions = 3;
    using Selector = SampleSelector<dimensions>;
    using Sample = Selector::Sample;

    constexpr std::array<uint8_t, 4> pdl = {24, 48, 72, 104};
    constexpr std::array<uint8_t, 2> zon = {44, 88};
    constexpr std::array<uint8_t, 5> dyn = {16, 46, 80, 110, 127};

    std::vector<Sample> samples;
    for (auto p : pdl)
        for (auto z : zon)
            for (auto d : dyn)
                samples.push_back({{p, z, d}});

    Selector selector(samples, ZoneSplitStrategy::MidSplit);

    struct Case {
        std::array<uint8_t, dimensions> query;
        std::array<uint8_t, dimensions> expected;
    };

    const std::vector<Case> cases = {
        { {  36,  60,  60 }, {  24,  44,  46 }, },
        { {  60,  60,  60 }, {  48,  44,  46 }, },
        { {  89,  67,  64 }, { 104,  88,  80 }, },
        { {   0,   0,   0 }, {  24,  44,  16 }, },
        { { 127, 127, 127 }, { 104,  88, 127 }, },
    };

    for (const auto& c : cases) {
        DYNAMIC_SECTION(
            "query=("
            << int(c.query[0]) << "," << int(c.query[1]) << "," << int(c.query[2]) << ") "
            << "expected=("
            << int(c.expected[0]) << "," << int(c.expected[1]) << "," << int(c.expected[2]) << ")"
        ) {
            const auto* leaf = selector.findZone(c.query);
            REQUIRE(leaf != nullptr);
            REQUIRE(leaf->samples.size() == 1);

            const auto& dims = leaf->samples[0]->dimensions;
            REQUIRE(dims == c.expected);
        }
    }
}

// TEST_CASE("SampleSelectorRuntime - selects based on dimension count", "[SampleSelectorRuntime]") {
//     std::vector<RuntimeSample> samples{
//         {{10, 20, 30}},
//         {{40, 50, 60}}
//     };

//     REQUIRE_NOTHROW(SampleSelectorRuntime(samples));
// }

// TEST_CASE("SampleSelectorRuntime - unsupported dimension count throws", "[SampleSelectorRuntime]") {
//     std::vector<RuntimeSample> samples{
//         {{1, 2, 3, 4, 5}}
//     };

//     REQUIRE_THROWS_AS(SampleSelectorRuntime(samples), std::logic_error);
// }
