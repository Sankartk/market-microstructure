#pragma once

#include "order.hpp"
#include <deque>
#include <unordered_map>
#include <vector>
#include <string>
#include <chrono>

namespace mm {

enum class AlertType : uint8_t {
    Spoofing,
    Layering,
    MomentumIgnition,
    QuoteStuffing
};

struct Alert {
    AlertType   type;
    std::string symbol;
    TsNs        timestamp;
    double      confidence;  // 0.0 – 1.0
    std::string description;
    OrderId     subject_order_id;
};

// Pattern detection engine. Sliding window over recent events per symbol.
class PatternDetector {
public:
    struct Config {
        // Spoofing: large order placed then cancelled within N ms, no fill
        uint64_t spoof_cancel_window_ns = 500'000'000;   // 500 ms
        double   spoof_min_size_ratio   = 5.0;           // vs avg book depth
        // Layering: N+ orders at same side within N price ticks
        uint32_t layering_min_orders    = 4;
        int64_t  layering_max_spread    = 5;             // ticks
        // Momentum ignition: rapid sequence of aggressive orders one direction
        uint64_t momentum_window_ns     = 2'000'000'000; // 2 s
        uint32_t momentum_min_trades    = 5;
        // Quote stuffing: messages/second threshold
        uint32_t quote_stuffing_threshold = 10'000;
    };

    explicit PatternDetector(Config cfg) : cfg_(cfg) {}
    PatternDetector() : PatternDetector(Config{}) {}

    void on_add(OrderId id, Price price, Qty qty, Side side,
                TsNs ts, const std::string& symbol);
    void on_cancel(OrderId id, TsNs ts, const std::string& symbol);
    void on_trade(OrderId aggressive_id, Price price, Qty qty, Side side,
                  TsNs ts, const std::string& symbol);
    void on_book_update(const BookSnapshot& snap, const std::string& symbol);

    [[nodiscard]] std::vector<Alert> drain_alerts();
    [[nodiscard]] size_t alert_count() const { return alert_buffer_.size(); }

private:
    Config cfg_;

    struct OrderEvent {
        OrderId id;
        Price   price;
        Qty     qty;
        Side    side;
        TsNs    ts;
        bool    filled;
        bool    cancelled;
    };

    struct SymbolState {
        std::deque<OrderEvent> recent_orders;
        std::deque<TsNs>       msg_timestamps;
        BookSnapshot           last_snap{};
        uint64_t               window_msgs = 0;
    };

    std::unordered_map<std::string, SymbolState> state_;
    std::vector<Alert>                           alert_buffer_;

    void detect_spoofing(SymbolState& s, const std::string& sym, TsNs now);
    void detect_layering(SymbolState& s, const std::string& sym, TsNs now);
    void detect_momentum(SymbolState& s, const std::string& sym, TsNs now);
    void detect_quote_stuffing(SymbolState& s, const std::string& sym, TsNs now);
    void prune_window(SymbolState& s, TsNs now, uint64_t window_ns);
    void emit(AlertType type, const std::string& sym, TsNs ts,
              double conf, const std::string& desc, OrderId subject = 0);
};

} // namespace mm
