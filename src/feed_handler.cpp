#include "feed_handler.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace mm {

FeedHandler::FeedHandler() = default;

void FeedHandler::set_detector_config(PatternDetector::Config cfg) {
    detector_ = PatternDetector(cfg);
}

void FeedHandler::process(const uint8_t* buf, size_t len) {
    std::vector<ItchMessage> messages;
    messages.reserve(1024);
    parser_.parse_all(buf, len, messages);
    for (const auto& msg : messages) {
        handle_message(msg);
        ++stats_.messages_processed;
    }
    flush_alerts();
}

uint64_t FeedHandler::process_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) throw std::runtime_error("Cannot open file: " + path);

    auto size = f.tellg();
    f.seekg(0);

    std::vector<uint8_t> buf(static_cast<size_t>(size));
    f.read(reinterpret_cast<char*>(buf.data()), size);

    uint64_t before = stats_.messages_processed;
    process(buf.data(), buf.size());
    return stats_.messages_processed - before;
}

const OrderBook* FeedHandler::book(const std::string& symbol) const {
    auto it = books_.find(symbol);
    return it != books_.end() ? it->second.get() : nullptr;
}

std::vector<std::string> FeedHandler::symbols() const {
    std::vector<std::string> out;
    out.reserve(books_.size());
    for (const auto& [sym, _] : books_) out.push_back(sym);
    return out;
}

OrderBook& FeedHandler::get_or_create_book(const std::string& symbol) {
    auto it = books_.find(symbol);
    if (it == books_.end()) {
        auto [new_it, _] = books_.emplace(symbol, std::make_unique<OrderBook>());
        return *new_it->second;
    }
    return *it->second;
}

void FeedHandler::handle_message(const ItchMessage& msg) {
    std::string sym(msg.symbol);
    if (sym.empty()) return;

    auto& book = get_or_create_book(sym);

    switch (msg.type) {
        case MsgType::Add:
            if (book.add_order(msg.order_id, msg.price, msg.qty, msg.side, msg.timestamp_ns)) {
                ++stats_.orders_added;
                detector_.on_add(msg.order_id, msg.price, msg.qty,
                                 msg.side, msg.timestamp_ns, sym);
                detector_.on_book_update(book.snapshot(msg.timestamp_ns), sym);
            }
            break;

        case MsgType::Cancel:
            if (book.cancel_order(msg.order_id)) {
                ++stats_.orders_cancelled;
                detector_.on_cancel(msg.order_id, msg.timestamp_ns, sym);
                detector_.on_book_update(book.snapshot(msg.timestamp_ns), sym);
            }
            break;

        case MsgType::Execute:
            if (book.execute_order(msg.order_id, msg.qty, msg.timestamp_ns)) {
                ++stats_.trades;
                detector_.on_trade(msg.order_id, msg.price, msg.qty,
                                   msg.side, msg.timestamp_ns, sym);
            }
            break;

        case MsgType::Delete:
            book.cancel_order(msg.order_id);
            break;

        case MsgType::Replace:
            book.replace_order(msg.order_id, msg.price, msg.qty, msg.timestamp_ns);
            break;

        default:
            ++stats_.parse_errors;
            break;
    }
}

void FeedHandler::flush_alerts() {
    auto alerts = detector_.drain_alerts();
    stats_.alerts_fired += alerts.size();
    if (on_alert_) {
        for (const auto& a : alerts) on_alert_(a);
    }
}

} // namespace mm
