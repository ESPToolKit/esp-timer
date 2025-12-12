#include <Arduino.h>
#include <ESPTimer.h>
#include <unity.h>

ESPTimer timer;

void test_api_compiles() {
    ESPTimerConfig cfg;
    timer.init(cfg);

    auto id1 = timer.setTimeout([]() {}, 10);
    auto id2 = timer.setInterval([]() {}, 20);
    auto id3 = timer.setSecCounter([](int) {}, 1000);
    auto id4 = timer.setMsCounter([](uint32_t) {}, 100);
    auto id5 = timer.setMinCounter([](int) {}, 60000);

    TEST_ASSERT_TRUE(id1 > 0);
    TEST_ASSERT_TRUE(id2 > 0);
    TEST_ASSERT_TRUE(id3 > 0);
    TEST_ASSERT_TRUE(id4 > 0);
    TEST_ASSERT_TRUE(id5 > 0);

    // Pause then resume; both should return true if found and state changed
    TEST_ASSERT_TRUE(timer.pauseInterval(id2));
    TEST_ASSERT_TRUE(timer.resumeInterval(id2));

    // Toggle should return new running state
    bool running = timer.toggleRunStatusInterval(id2);
    TEST_ASSERT_FALSE(running);
    running = timer.toggleRunStatusInterval(id2);
    TEST_ASSERT_TRUE(running);

    // Clear should return true once
    TEST_ASSERT_TRUE(timer.clearInterval(id2));
}

void setup() {
    delay(2000);
    UNITY_BEGIN();
    RUN_TEST(test_api_compiles);
    UNITY_END();
}

void loop() {}
