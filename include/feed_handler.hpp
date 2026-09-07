#pragma once

#include "itch_parser.hpp"
#include "order_book.hpp"
#include "detector.hpp"
#include <string>
#include <unordered_map>
#include <memory>
#include <functional>
#include <cstdint>

namespace mm {

// Routes ITCH messages to the correct per-symbol order book,
// feeds the pattern detector, and exposes snapshots.
class FeedHandler {
public:
    struct Stats {
        uint64_t messages_processed = 0;
        uint64_t orders_added       = 0;
        uint64_t orders_cancelled   = 0;
        uint64_t trades             = 0;
        uint64_t alerts_fired       = 0;
        uint64_t parse_errors       = 0;
    };

    using AlertCallback = std::function<void(const Alert&)>;

    FeedHandler();

    // Process a single raw ITCH message buffer
    void process(const uint8_t* buf, size_t len);

    // Convenience: process a full file
    uint64_t process_file(const std::string& path);

    [[nodiscard]] const Stats& stats() const { return stats_; }
    [[nodiscard]] const OrderBook* book(const std::string& symbol) const;
    [[nodiscard]] std::vector<std::string> symbols() const;

    void set_alert_callback(AlertCallback cb) { on_alert_ = std::move(cb); }
    void set_detector_config(PatternDetector::Config cfg);

private:
    ItchParser                         parser_;
    PatternDetector                    detector_;
    AlertCallback                      on_alert_;
    Stats                              stats_;

    std::unordered_map<std::string, std::unique_ptr<OrderBook>> books_;

    OrderBook& get_or_create_book(const std::string& symbol);
    void handle_message(const ItchMessage& msg);
    void flush_alerts();
};

} // namespace mm
