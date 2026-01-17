#pragma once
#include <array>
#include <vector>
#include <variant>
#include <algorithm>
#include <stdexcept>
#include <cstdint>



enum class ZoneSplitStrategy { Boundary, MidSplit };
template <size_t N>
class SampleSelector {
public:
    static_assert(N > 0 && N <= 4, "Supported dimensions: 1..4");

    static const uint8_t MIDI_MAX_VALUE = 127;


    struct Sample {
        std::array<uint8_t, N> dimensions;
    };

    struct ZoneNode {
        uint8_t min;
        uint8_t max;
        std::vector<ZoneNode> children;
        std::vector<const Sample*> samples;
    };

    struct Range {
        uint8_t min;
        uint8_t max;
    };

    explicit SampleSelector(const std::vector<Sample>& samples, ZoneSplitStrategy strategy = ZoneSplitStrategy::Boundary)
        : strategy(strategy)
    {
        this->validateSamples(samples);
        this->ownedSamples = samples;
        this->root = buildTree(this->ownedSamples, strategy);
    }

    const ZoneNode* findZone(const std::array<uint8_t, N>& values, size_t level = 0) const { return this->findZoneRecursive(this->root, values, level); }

    const ZoneNode& getRoot() const { return this->root; }

    static void validateSamples(const std::vector<Sample>& samples) {
        if (samples.empty())
            throw std::logic_error("Samples must not be empty");

        for (const auto& s : samples) {
            for (auto v : s.dimensions) {
                if (v > MIDI_MAX_VALUE)
                    throw std::logic_error("Dimension #" + std::to_string(int(v)) + " value out of MIDI range: " + std::to_string(int(v))); //FIXME: make clear error on what number of sample and number of dimension what value gave error
            }
        }
    }

    static std::vector<Range> makeMidSplitRanges(const std::vector<uint8_t>& points) {
        std::vector<Range> ranges;
        for (size_t i = 0; i < points.size(); ++i) {
            uint8_t lo = (i == 0) ? 0 : (points[i - 1] + points[i]) / 2 + 1;
            uint8_t hi = (i + 1 == points.size()) ? MIDI_MAX_VALUE : (points[i] + points[i + 1]) / 2;
            ranges.push_back({lo, hi});
        }
        return ranges;
    }

    static std::vector<Range> makeBoundaryRanges(const std::vector<uint8_t>& points) {
        std::vector<Range> ranges;
        if (points.empty()) return ranges;

        if (points.size() == 1) {
            ranges.push_back({0, MIDI_MAX_VALUE});
            return ranges;
        }
        ranges.push_back({0, points[0]});
        for (size_t i = 1; i < points.size() - 1; ++i)
            ranges.push_back({static_cast<uint8_t>(points[i - 1] + 1), points[i]});
        // last range: [lastPoint+1 .. MIDI_MAX_VALUE]
        ranges.push_back({static_cast<uint8_t>(points[points.size() - 2] + 1), MIDI_MAX_VALUE});
        return ranges;
    }

    static ZoneNode buildTree(const std::vector<Sample>& samples, ZoneSplitStrategy strategy) {
        std::vector<const Sample*> ptrs;
        for (const auto& s : samples)
            ptrs.push_back(&s);

        ZoneNode node{0, MIDI_MAX_VALUE};
        SampleSelector::buildLevel(node, ptrs, 0, strategy);
        return node;
    }

    static void buildLevel(ZoneNode& node, const std::vector<const Sample*>& samples, size_t level, ZoneSplitStrategy strategy) {
        if (level == N) {
            node.samples = samples;
            return;
        }

        auto values = SampleSelector::collectUnique(samples, level);
        auto ranges = (strategy == ZoneSplitStrategy::Boundary)
            ? SampleSelector::makeBoundaryRanges(values)
            : SampleSelector::makeMidSplitRanges(values);

        for (const auto& r : ranges) {
            std::vector<const Sample*> subset;
            for (auto* s : samples) {
                uint8_t v = s->dimensions[level];
                if (v >= r.min && v <= r.max)
                    subset.push_back(s);
            }

            ZoneNode child{r.min, r.max};
            SampleSelector::buildLevel(child, subset, level + 1, strategy);
            node.children.push_back(std::move(child));
        }
    }

    static std::vector<uint8_t> collectUnique(const std::vector<const Sample*>& samples, size_t level) {
        std::vector<uint8_t> v;
        for (auto* s : samples)
            v.push_back(s->dimensions[level]);

        std::sort(v.begin(), v.end());
        v.erase(std::unique(v.begin(), v.end()), v.end());
        return v;
    }

private:
    ZoneNode root;
    ZoneSplitStrategy strategy;
    std::vector<Sample> ownedSamples;

    const ZoneNode* findZoneRecursive(const ZoneNode& node, const std::array<uint8_t, N>& values, size_t level) const {
        if (node.children.empty())
            return node.samples.empty() ? nullptr : &node;

        uint8_t v = values[level];

        for (const auto& c : node.children) {
            if (v >= c.min && v <= c.max)
                return findZoneRecursive(c, values, level + 1);
        }
        return nullptr;
    }
};


struct RuntimeSample {
    std::vector<uint8_t> dimensions;
};
class SampleSelectorRuntime {
public:
    using Selector = std::variant<
        std::monostate,
        SampleSelector<1>,
        SampleSelector<2>,
        SampleSelector<3>,
        SampleSelector<4>>;

    template <typename SampleVec>
    explicit SampleSelectorRuntime(const SampleVec& samples) {
        if (samples.empty())
            throw std::logic_error("Samples must not be empty");

        switch (samples.front().dimensions.size()) {
            case 1: selector.emplace<1>(convert<1>(samples)); break;
            case 2: selector.emplace<2>(convert<2>(samples)); break;
            case 3: selector.emplace<3>(convert<3>(samples)); break;
            case 4: selector.emplace<4>(convert<4>(samples)); break;
            default:
                throw std::logic_error("Unsupported dimension count");
        }
    }

private:
    Selector selector;

    template <size_t N, typename SampleVec>
    static std::vector<typename SampleSelector<N>::Sample>
    convert(const SampleVec& samples) {
        std::vector<typename SampleSelector<N>::Sample> out;
        out.reserve(samples.size());

        for (const auto& s : samples) {
            if (s.dimensions.size() != N)
                throw std::logic_error("Dimension mismatch");

            std::array<uint8_t, N> arr{};
            std::copy_n(s.dimensions.begin(), N, arr.begin());
            out.push_back({arr});
        }
        return out;
    }
};
