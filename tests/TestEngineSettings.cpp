#include "../source/Engine/EngineSettings.h"
#include "catch2/catch_test_macros.hpp"

TEST_CASE("EngineSettings - realtime scalar settings load and store", "[EngineSettings]") {
    RealtimeSetting<float> floatSetting { 0.25f };
    RealtimeSetting<int> intSetting { 2 };

    REQUIRE(floatSetting.load() == 0.25f);
    REQUIRE(intSetting.load() == 2);

    floatSetting.store(0.75f);
    intSetting.store(4);

    REQUIRE(floatSetting.load() == 0.75f);
    REQUIRE(intSetting.load() == 4);
}

TEST_CASE("EngineSettings - realtime settings remain copyable snapshots", "[EngineSettings]") {
    EngineSettings sourceSettings;
    sourceSettings.hihatPedalCrossfadeSec.store(0.125f);
    sourceSettings.roundRobinLock.store(3);

    EngineSettings copiedSettings = sourceSettings;
    REQUIRE(copiedSettings.hihatPedalCrossfadeSec.load() == 0.125f);
    REQUIRE(copiedSettings.roundRobinLock.load() == 3);

    copiedSettings.hihatPedalCrossfadeSec.store(0.5f);
    copiedSettings.roundRobinLock.store(1);

    REQUIRE(sourceSettings.hihatPedalCrossfadeSec.load() == 0.125f);
    REQUIRE(sourceSettings.roundRobinLock.load() == 3);
}
