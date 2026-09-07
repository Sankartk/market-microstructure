#include "feed_handler.hpp"
#include <iostream>
#include <iomanip>
#include <string>
#include <chrono>

using namespace mm;

static const char* alert_type_str(AlertType t) {
    switch (t) {
        case AlertType::Spoofing:         return "SPOOFING";
        case AlertType::Layering:         return "LAYERING";
        case AlertType::MomentumIgnition: return "MOMENTUM_IGNITION";
        case AlertType::QuoteStuffing:    return "QUOTE_STUFFING";
        default:                          return "UNKNOWN";
    }
}

static void print_usage(const char* prog) {
    std::cerr << "Usage: " << prog << " <itch_file> [--stats] [--alerts] [--book SYMBOL]\n"
              << "\n"
              << "Options:\n"
              << "  --stats        Print processing statistics at end\n"
              << "  --alerts       Print pattern detection alerts as they fire\n"
              << "  --book SYMBOL  Print top-of-book for SYMBOL after processing\n";
}

int main(int argc, char* argv[]) {
    if (argc < 2) { print_usage(argv[0]); return 1; }

    std::string filepath = argv[1];
    bool show_stats  = false;
    bool show_alerts = false;
    std::string book_symbol;

    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];
        if      (arg == "--stats")  show_stats  = true;
        else if (arg == "--alerts") show_alerts = true;
        else if (arg == "--book" && i + 1 < argc) book_symbol = argv[++i];
    }

    FeedHandler handler;

    if (show_alerts) {
        handler.set_alert_callback([](const Alert& a) {
            std::cout << "[ALERT] " << std::left << std::setw(20) << alert_type_str(a.type)
                      << " symbol=" << std::setw(10) << a.symbol
                      << " conf=" << std::fixed << std::setprecision(2) << a.confidence
                      << "  " << a.description << "\n";
        });
    }

    auto t0 = std::chrono::high_resolution_clock::now();

    try {
        uint64_t n = handler.process_file(filepath);
        auto t1 = std::chrono::high_resolution_clock::now();
        double elapsed_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

        std::cout << "[feed] processed " << n << " messages in "
                  << std::fixed << std::setprecision(1) << elapsed_ms << "ms"
                  << " (" << std::setprecision(0)
                  << (n / (elapsed_ms / 1000.0)) / 1e6 << "M msgs/sec)\n";
    } catch (const std::exception& e) {
        std::cerr << "[error] " << e.what() << "\n";
        return 1;
    }

    if (show_stats) {
        const auto& s = handler.stats();
        std::cout << "\n── Stats ─────────────────────────────────\n"
                  << "  messages  : " << s.messages_processed << "\n"
                  << "  added     : " << s.orders_added       << "\n"
                  << "  cancelled : " << s.orders_cancelled   << "\n"
                  << "  trades    : " << s.trades             << "\n"
                  << "  alerts    : " << s.alerts_fired       << "\n"
                  << "  errors    : " << s.parse_errors        << "\n";
    }

    if (!book_symbol.empty()) {
        const auto* book = handler.book(book_symbol);
        if (!book) {
            std::cerr << "[book] symbol not found: " << book_symbol << "\n";
            return 1;
        }
        auto snap = book->snapshot(0);
        std::cout << "\n── Book: " << book_symbol << " ─────────────────────────\n"
                  << "  best bid : " << std::fixed << std::setprecision(4)
                  << static_cast<double>(snap.best_bid) / 10000.0
                  << "  x" << snap.bid_depth << "\n"
                  << "  best ask : " << std::fixed << std::setprecision(4)
                  << static_cast<double>(snap.best_ask) / 10000.0
                  << "  x" << snap.ask_depth << "\n"
                  << "  spread   : " << std::setprecision(2) << snap.spread_bps() << " bps\n"
                  << "  levels   : " << book->bid_levels() << " bid / "
                  << book->ask_levels() << " ask\n";

        auto bids = book->top_levels(Side::Buy, 5);
        auto asks = book->top_levels(Side::Sell, 5);
        std::cout << "\n  Top 5 bids:\n";
        for (const auto& l : bids)
            std::cout << "    " << std::setprecision(4)
                      << static_cast<double>(l.price) / 10000.0
                      << "  x" << l.total_qty << " (" << l.order_count << " orders)\n";
        std::cout << "  Top 5 asks:\n";
        for (const auto& l : asks)
            std::cout << "    " << std::setprecision(4)
                      << static_cast<double>(l.price) / 10000.0
                      << "  x" << l.total_qty << " (" << l.order_count << " orders)\n";
    }

    return 0;
}
