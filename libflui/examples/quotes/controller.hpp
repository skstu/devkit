#pragma once
// The same business controller is compiled into both samples. No UI dependency.
#include <array>
#include <charconv>
#include <string>
#include <string_view>
namespace quote_sample {
struct Quote {
  std::string id, symbol, bid, ask;
};
class Controller {
public:
  std::array<Quote, 3> quotes{{{"eurusd", "EUR/USD", "1.08234", "1.08246"},
                               {"usdjpy", "USD/JPY", "149.251", "149.269"},
                               {"gold", "GOLD", "2350.10", "2350.40"}}};
  std::string selected = "eurusd", quantity = "1",
              result = "请选择报价并输入数量。";
  unsigned submissions = 0;
  const Quote &selection() const {
    for (const auto &q : quotes)
      if (q.id == selected)
        return q;
    return quotes[0];
  }
  void dispatch(std::string_view action, std::string_view value) {
    if (action == "quote.select") {
      for (const auto &q : quotes)
        if (q.id == value) {
          selected = q.id;
          return;
        }
    } else if (action == "quantity.changed") {
      if (value.size() <= 128)
        quantity = value;
    } else if (action == "order.submit") {
      unsigned count = 0;
      auto parsed = std::from_chars(quantity.data(),
                                    quantity.data() + quantity.size(), count);
      if (parsed.ec != std::errc{} ||
          parsed.ptr != quantity.data() + quantity.size() || count < 1 ||
          count > 10) {
        result = "数量必须是 1–10 的整数。";
        return;
      }
      ++submissions;
      result = "模拟完成：" + selection().symbol + " × " +
               std::to_string(count) + "（仅本地，不发送订单）";
    }
  }
  void tick() {
    ++ticks_;
    quotes[0].bid = ticks_ % 2 ? "1.08235" : "1.08234";
    quotes[0].ask = ticks_ % 2 ? "1.08247" : "1.08246";
  }
  static std::string json_string(std::string_view text) {
    static const char hex[] = "0123456789abcdef";
    std::string out = "\"";
    for (unsigned char c : text) {
      if (c == '"' || c == '\\') {
        out += '\\';
        out += char(c);
      } else if (c < 32) {
        out += "\\u00";
        out += hex[c >> 4];
        out += hex[c & 15];
      } else
        out += char(c);
    }
    return out + '"';
  }
  std::string state_json() const {
    std::string out = "{\"quotes\":[";
    for (size_t i = 0; i < quotes.size(); ++i) {
      const auto &q = quotes[i];
      if (i)
        out += ',';
      out += "{\"id\":" + json_string(q.id) +
             ",\"symbol\":" + json_string(q.symbol) +
             ",\"bid\":" + json_string(q.bid) +
             ",\"ask\":" + json_string(q.ask) + "}";
    }
    return out + "],\"selected\":" + json_string(selected) +
           ",\"selection\":" + json_string("当前：" + selection().symbol) +
           ",\"quantity\":" + json_string(quantity) +
           ",\"result\":" + json_string(result) + "}";
  }

private:
  unsigned ticks_ = 0;
};
} // namespace quote_sample
