#include "detector.hpp"
#include <algorithm>
#include <numeric>
#include <sstream>

namespace mm {

PatternDetector::PatternDetector(Config cfg) : cfg_(cfg) {}

void PatternDetector::on_add(OrderId id, Price price, Qty qty, Side side,
                              TsNs ts, const std::string& symbol) {
    auto& s = state_[symbol];
    s.msg_timestamps.push_back(ts);
    prune_window(s, ts, 60'000'000'000ULL);  // keep 60s window

    OrderEvent ev;
    ev.id = id; ev.price = price; ev.qty = qty;
    ev.side = side; ev.ts = ts;
    ev.filled = false; ev.cancelled = false;
    s.recent_orders.push_back(ev);

    detect_layering(s, symbol, ts);
    detect_quote_stuffing(s, symbol, ts);
}

void PatternDetector::on_cancel(OrderId id, TsNs ts, const std::string& symbol) {
    auto& s = state_[symbol];

    // Mark cancelled and check for spoofing
    for (auto& ev : s.recent_orders) {
        if (ev.id == id && !ev.filled && !ev.cancelled) {
            ev.cancelled = true;

            // Spoofing check: cancelled within window, large size vs book depth
            uint64_t age = ts - ev.ts;
            if (age <= cfg_.spoof_cancel_window_ns) {
                Qty depth = (ev.side == Side::Buy)
                    ? s.last_snap.bid_depth : s.last_snap.ask_depth;
                if (depth > 0 &&
                    static_cast<double>(ev.qty) >= cfg_.spoof_min_size_ratio * depth) {
                    double conf = std::min(1.0, static_cast<double>(ev.qty) /
                                          (cfg_.spoof_min_size_ratio * depth) * 0.8);
                    std::ostringstream desc;
                    desc << "Large " << (ev.side == Side::Buy ? "bid" : "ask")
                         << " of " << ev.qty << " cancelled after "
                         << age / 1'000'000 << "ms with no fill";
                    emit(AlertType::Spoofing, symbol, ts, conf, desc.str(), id);
                }
            }
            break;
        }
    }
    detect_spoofing(s, symbol, ts);
}

void PatternDetector::on_trade(OrderId /* id */, Price /* price */, Qty /* qty */,
                                Side side, TsNs ts, const std::string& symbol) {
    auto& s = state_[symbol];

    // Mark matching orders as filled
    for (auto& ev : s.recent_orders) {
        if (!ev.filled && !ev.cancelled) {
            ev.filled = true;
            break;
        }
    }
    (void)side;
    detect_momentum(s, symbol, ts);
}

void PatternDetector::on_book_update(const BookSnapshot& snap, const std::string& symbol) {
    state_[symbol].last_snap = snap;
}

std::vector<Alert> PatternDetector::drain_alerts() {
    std::vector<Alert> out;
    out.swap(alert_buffer_);
    return out;
}

void PatternDetector::detect_spoofing(SymbolState& s, const std::string& sym, TsNs now) {
    // Additional sweep: find clusters of large cancels within window
    uint32_t large_cancels = 0;
    for (const auto& ev : s.recent_orders) {
        if (ev.cancelled && !ev.filled &&
            now - ev.ts <= cfg_.spoof_cancel_window_ns * 4) {
            Qty depth = (ev.side == Side::Buy)
                ? s.last_snap.bid_depth : s.last_snap.ask_depth;
            if (depth > 0 && static_cast<double>(ev.qty) >= 3.0 * depth)
                ++large_cancels;
        }
    }
    if (large_cancels >= 3) {
        emit(AlertType::Spoofing, sym, now, 0.75,
             "Cluster of " + std::to_string(large_cancels) +
             " large cancels within extended window");
    }
}

void PatternDetector::detect_layering(SymbolState& s, const std::string& sym, TsNs now) {
    if (s.recent_orders.size() < cfg_.layering_min_orders) return;

    // Check last N orders on same side, within max_spread ticks
    std::vector<const OrderEvent*> bid_recent, ask_recent;
    size_t checked = 0;
    for (auto it = s.recent_orders.rbegin();
         it != s.recent_orders.rend() && checked < cfg_.layering_min_orders * 3;
         ++it, ++checked) {
        if (!it->cancelled && !it->filled) {
            if (it->side == Side::Buy) bid_recent.push_back(&*it);
            else                        ask_recent.push_back(&*it);
        }
    }

    auto check_side = [&](const std::vector<const OrderEvent*>& orders, Side side) {
        if (orders.size() < cfg_.layering_min_orders) return;
        Price min_p = orders[0]->price, max_p = orders[0]->price;
        for (const auto* o : orders) {
            min_p = std::min(min_p, o->price);
            max_p = std::max(max_p, o->price);
        }
        if (max_p - min_p <= cfg_.layering_max_spread) {
            std::ostringstream desc;
            desc << orders.size() << " open " << (side == Side::Buy ? "bid" : "ask")
                 << " orders within " << (max_p - min_p) << " ticks";
            emit(AlertType::Layering, sym, now, 0.70, desc.str());
        }
    };

    check_side(bid_recent, Side::Buy);
    check_side(ask_recent, Side::Sell);
}

void PatternDetector::detect_momentum(SymbolState& s, const std::string& sym, TsNs now) {
    uint32_t buy_fills = 0, sell_fills = 0;
    for (const auto& ev : s.recent_orders) {
        if (ev.filled && now - ev.ts <= cfg_.momentum_window_ns) {
            if (ev.side == Side::Buy) ++buy_fills;
            else                       ++sell_fills;
        }
    }
    if (buy_fills >= cfg_.momentum_min_trades) {
        emit(AlertType::MomentumIgnition, sym, now, 0.65,
             std::to_string(buy_fills) + " buy fills in momentum window");
    }
    if (sell_fills >= cfg_.momentum_min_trades) {
        emit(AlertType::MomentumIgnition, sym, now, 0.65,
             std::to_string(sell_fills) + " sell fills in momentum window");
    }
}

void PatternDetector::detect_quote_stuffing(SymbolState& s, const std::string& sym, TsNs now) {
    prune_window(s, now, 1'000'000'000ULL);  // 1s window
    if (s.msg_timestamps.size() >= cfg_.quote_stuffing_threshold) {
        emit(AlertType::QuoteStuffing, sym, now, 0.90,
             std::to_string(s.msg_timestamps.size()) + " messages in 1s window");
    }
}

void PatternDetector::prune_window(SymbolState& s, TsNs now, uint64_t window_ns) {
    while (!s.msg_timestamps.empty() &&
           now - s.msg_timestamps.front() > window_ns) {
        s.msg_timestamps.pop_front();
    }
    while (!s.recent_orders.empty() &&
           now - s.recent_orders.front().ts > window_ns) {
        s.recent_orders.pop_front();
    }
}

void PatternDetector::emit(AlertType type, const std::string& sym, TsNs ts,
                            double conf, const std::string& desc, OrderId subject) {
    Alert a;
    a.type = type; a.symbol = sym; a.timestamp = ts;
    a.confidence = conf; a.description = desc; a.subject_order_id = subject;
    alert_buffer_.push_back(std::move(a));
}

} // namespace mm
