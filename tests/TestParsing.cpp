#include "../source/Config/Parsing.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

namespace ParsingTests {

TEST_CASE("Parsing accepts only complete finite float text", "[Parsing]") {
    const auto parsedValue = Parsing::parseFiniteFloat(" -12.5 ");

    REQUIRE(parsedValue.has_value());
    CHECK(*parsedValue == Catch::Approx(-12.5f));
    CHECK_FALSE(Parsing::parseFiniteFloat("").has_value());
    CHECK_FALSE(Parsing::parseFiniteFloat("12.5 ms").has_value());
    CHECK_FALSE(Parsing::parseFiniteFloat("nan").has_value());
    CHECK_FALSE(Parsing::parseFiniteFloat("inf").has_value());
    CHECK_FALSE(Parsing::parseFiniteFloat("1e100").has_value());
}

TEST_CASE("Parsing separates wrapped text", "[Parsing]") {
    const auto segment = Parsing::extractWrappedSegment("bow[open]", "[", "]");

    CHECK(segment.prefix == "bow");
    CHECK(segment.wrapped == "open");
    CHECK(segment.suffix.isEmpty());
}

} // namespace ParsingTests
