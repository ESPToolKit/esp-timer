#include <Arduino.h>
#include <ESPTimer.h>
#include <unity.h>

void test_api_compiles() {
    ESPTimer timer;
    ESPTimerConfig cfg;
    cfg.usePSRAMBuffers = true;
    timer.init(cfg);
    TEST_ASSERT_TRUE(timer.isInitialized());

    auto id1 = timer.setTimeout([]() {}, 1000);
    auto id2 = timer.setInterval([]() {}, 20);
    auto id3 = timer.setSecCounter([](int) {}, 1000);
    auto id4 = timer.setMsCounter([](uint32_t) {}, 100);
    auto id5 = timer.setMinCounter([](int) {}, 60000);
    auto id6 = timer.setTimeout([]() {}, 1000);

    TEST_ASSERT_TRUE(id1 > 0);
    TEST_ASSERT_TRUE(id2 > 0);
    TEST_ASSERT_TRUE(id3 > 0);
    TEST_ASSERT_TRUE(id4 > 0);
    TEST_ASSERT_TRUE(id5 > 0);
    TEST_ASSERT_TRUE(id6 > 0);

    // Pause then resume; both should return true if found and state changed
    TEST_ASSERT_TRUE(timer.pauseInterval(id2));
    TEST_ASSERT_TRUE(timer.resumeInterval(id2));

    // Toggle should return new running state
    bool running = timer.toggleRunStatusInterval(id2);
    TEST_ASSERT_FALSE(running);
    running = timer.toggleRunStatusInterval(id2);
    TEST_ASSERT_TRUE(running);

    TEST_ASSERT_TRUE(timer.clearTimeout(id1));
    TEST_ASSERT_TRUE(timer.clearTimeout(id6));

    // Clear should return true once
    TEST_ASSERT_TRUE(timer.clearInterval(id2));

    timer.deinit();
    TEST_ASSERT_FALSE(timer.isInitialized());
}

void test_deinit_pre_init_is_safe_and_idempotent() {
    ESPTimer timer;

    TEST_ASSERT_FALSE(timer.isInitialized());
    timer.deinit();
    TEST_ASSERT_FALSE(timer.isInitialized());
    timer.deinit();
    TEST_ASSERT_FALSE(timer.isInitialized());
}

void test_reinit_lifecycle() {
    ESPTimer timer;

    timer.init();
    TEST_ASSERT_TRUE(timer.isInitialized());
    auto firstId = timer.setTimeout([]() {}, 5);
    TEST_ASSERT_TRUE(firstId > 0);

    timer.deinit();
    TEST_ASSERT_FALSE(timer.isInitialized());
    timer.deinit();
    TEST_ASSERT_FALSE(timer.isInitialized());

    timer.init();
    TEST_ASSERT_TRUE(timer.isInitialized());
    auto secondId = timer.setInterval([]() {}, 5);
    TEST_ASSERT_TRUE(secondId > 0);
    TEST_ASSERT_TRUE(timer.clearInterval(secondId));

    timer.deinit();
    TEST_ASSERT_FALSE(timer.isInitialized());
}

void setup() {
    delay(2000);
    UNITY_BEGIN();
    RUN_TEST(test_api_compiles);
    RUN_TEST(test_deinit_pre_init_is_safe_and_idempotent);
    RUN_TEST(test_reinit_lifecycle);
    UNITY_END();
}

void loop() {}
