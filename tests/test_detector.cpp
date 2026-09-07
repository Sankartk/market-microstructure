#include "detector.hpp"
#include <cassert>
#include <iostream>

using namespace mm;

static int failures = 0;

#define CHECK(cond) \
    do { if (!(cond)) { \
        std::cerr << "FAIL: " << __FILE__ << ":" << __LINE__ \
                  << "  " << #cond << "\n"; \
        ++failures; \
    } } while (0)

static bool has_alert(const std::vector<Alert>& alerts, AlertType t) {
    for (const auto& a : alerts) if (a.type == t) return true;
    return false;
}

static void test_spoofing_fires_on_large_fast_cancel() {
    PatternDetector::Config cfg;
    cfg.spoof_cancel_window_ns = 500'000'000;
    cfg.spoof_min_size_ratio   = 2.0;
    PatternDetector det(cfg);

    BookSnapshot snap{};
    snap.bid_depth = 100;
    det.on_book_update(snap, "TEST");

    det.on_add(1, 1000000, 1000, Side::Buy, 0, "TEST");
    det.on_cancel(1, 100'000'000, "TEST");  // cancel after 100ms

    auto alerts = det.drain_alerts();
    CHECK(has_alert(alerts, AlertType::Spoofing));
}

static void test_spoofing_no_fire_on_small_cancel() {
    PatternDetector::Config cfg;
    cfg.spoof_min_size_ratio = 100.0;  // impossibly large
    PatternDetector det(cfg);

    BookSnapshot snap{};
    snap.bid_depth = 10000;
    det.on_book_update(snap, "TEST");

    det.on_add(1, 1000000, 10, Side::Buy, 0, "TEST");
    det.on_cancel(1, 100'000'000, "TEST");

    auto alerts = det.drain_alerts();
    CHECK(!has_alert(alerts, AlertType::Spoofing));
}

static void test_spoofing_no_fire_on_slow_cancel() {
    PatternDetector::Config cfg;
    cfg.spoof_cancel_window_ns = 100'000'000;  // 100ms window
    cfg.spoof_min_size_ratio   = 2.0;
    PatternDetector det(cfg);

    BookSnapshot snap{};
    snap.bid_depth = 10;
    det.on_book_update(snap, "TEST");

    det.on_add(1, 1000000, 5000, Side::Buy, 0, "TEST");
    // Cancel 600ms later — outside window
    det.on_cancel(1, 600'000'000, "TEST");

    auto alerts = det.drain_alerts();
    CHECK(!has_alert(alerts, AlertType::Spoofing));
}

static void test_quote_stuffing_fires_above_threshold() {
    PatternDetector::Config cfg;
    cfg.quote_stuffing_threshold = 100;
    PatternDetector det(cfg);

    for (uint32_t i = 0; i < 150; ++i)
        det.on_add(i + 1, 1000000, 10, Side::Buy, i * 1'000'000, "TEST");

    auto alerts = det.drain_alerts();
    CHECK(has_alert(alerts, AlertType::QuoteStuffing));
}

static void test_no_alert_on_normal_traffic() {
    PatternDetector det;
    det.on_add(1, 1000000, 100, Side::Buy, 0, "TEST");
    det.on_add(2, 1001000, 100, Side::Sell, 1'000'000, "TEST");
    det.on_trade(1, 1000000, 50, Side::Buy, 2'000'000, "TEST");

    auto alerts = det.drain_alerts();
    CHECK(alerts.empty());
}

static void test_drain_alerts_clears_buffer() {
    PatternDetector::Config cfg;
    cfg.quote_stuffing_threshold = 5;
    PatternDetector det(cfg);

    for (uint32_t i = 0; i < 10; ++i)
        det.on_add(i + 1, 1000000, 10, Side::Buy, i * 1'000'000, "TEST");

    auto first = det.drain_alerts();
    auto second = det.drain_alerts();
    CHECK(!first.empty());
    CHECK(second.empty());
}

int main() {
    test_spoofing_fires_on_large_fast_cancel();
    test_spoofing_no_fire_on_small_cancel();
    test_spoofing_no_fire_on_slow_cancel();
    test_quote_stuffing_fires_above_threshold();
    test_no_alert_on_normal_traffic();
    test_drain_alerts_clears_buffer();

    if (failures == 0) {
        std::cout << "[test_detector] all tests passed\n";
        return 0;
    }
    std::cerr << "[test_detector] " << failures << " test(s) failed\n";
    return 1;
}
