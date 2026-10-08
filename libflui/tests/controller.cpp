#include "../examples/quotes/controller.hpp"
#include <cstdlib>
#include <iostream>
#define REQUIRE(x)                                                             \
  do {                                                                         \
    if (!(x)) {                                                                \
      std::cerr << #x << '\n';                                                 \
      return 1;                                                                \
    }                                                                          \
  } while (0)
int main() {
  quote_sample::Controller c;
  c.dispatch("quote.select", "gold");
  c.dispatch("quantity.changed", "2");
  for (int i = 0; i < 1000; ++i)
    c.tick();
  REQUIRE(c.quantity == "2" && c.selected == "gold");
  c.dispatch("order.submit", "");
  REQUIRE(c.submissions == 1 && c.result.find("GOLD × 2") != std::string::npos);
  for (const auto *invalid :
       {"", "-1", "0", "11", "2x", "1.5", "999999999999999999999"}) {
    c.dispatch("quantity.changed", invalid);
    c.dispatch("order.submit", "");
    REQUIRE(c.submissions == 1 && c.result.find("1–10") != std::string::npos);
  }
  c.dispatch("quote.select", "missing");
  REQUIRE(c.selected == "gold");
  c.dispatch("quantity.changed", "\"\\\n");
  REQUIRE(c.state_json().find("\\\"\\\\\\u000a") != std::string::npos);
  std::cout << "PASS shared controller: selection, exact quantity, live "
               "updates, validation, result\n";
}
