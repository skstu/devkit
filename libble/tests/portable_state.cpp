#include "../src/common/backend.hpp"
#include "../src/common/command_queue.hpp"
#include "../src/common/read_session.hpp"
#include <stdexcept>
#include <cstdio>
#include <cstdlib>
#include <thread>
using namespace dkble;
#define CHECK(v)                                                               \
  do {                                                                         \
    if (!(v)) {                                                                \
      std::fprintf(stderr, "CHECK line %d: %s\n", __LINE__, #v);               \
      std::abort();                                                            \
    }                                                                          \
  } while (0)
namespace {
Emit emit;
int stops = 0, destroyed = 0, stopFailure = 0, offEvents = 0;
bool active = false, throwStop = false;
uint64_t epoch = 0;
struct Fake : Backend {
  int set_read_interval(uint32_t) override { return 0; }
  int set_options(const dkble_options &) override { return 0; }
  int command(Command c) override {
    if (c.op == Op::stop) {
      ++stops;
      if (throwStop)
        throw std::runtime_error("injected stop failure");
      if (stopFailure)
        return stopFailure;
      active = false;
    } else if (c.op == Op::startScan || c.op == Op::startAdvertise)
      active = true;
    epoch = c.generation;
    return 0;
  }
  void close() override { ++destroyed; }
};
dkble_context *context = nullptr;
int wakes = 0, count = 0;
void wake(void *) {
  ++wakes;
  CHECK(dkble_stop(context) == DKBLE_BUSY);
}
void receive(void *, const dkble_event *e) {
  ++count;
  CHECK(dkble_destroy(context) == DKBLE_BUSY);
  CHECK(dkble_dispatch(context, 1, receive, nullptr) == DKBLE_BUSY);
  if (e->type == DKBLE_EVENT_STATE && std::string(e->detail) == "off")
    ++offEvents;
  if (e->type == DKBLE_EVENT_ERROR)
    CHECK(e->status == DKBLE_OVERFLOW);
}
void inject(uint32_t type, std::string link = "route", size_t size = 0,
            uint64_t generation = 0) {
  Event e;
  e.type = type;
  e.link = std::move(link);
  e.generation = generation ? generation : epoch;
  e.data.resize(size);
  emit(std::move(e));
}
} // namespace
namespace dkble {
std::unique_ptr<Backend> make_backend(Config, Emit e, Wake) {
  emit = std::move(e);
  return std::make_unique<Fake>();
}
} // namespace dkble
void queueTests() {
  CommandQueue queue;
  std::deque<Command> held;
  for (int i = 0; i < 128; ++i)
    CHECK(queue.push({Op::recover, 1}) == 0);
  CHECK(queue.push({Op::recover, 1}) == DKBLE_BUSY);
  const auto first = queue.request_stop(2);
  auto stop = queue.take_stop();
  CHECK(stop && stop->generation == 2 && stop->ticket == first);
  CHECK(!queue.take_stop());
  queue.take(held);
  CHECK(held.empty());
  CHECK(queue.push({Op::startScan, 1}) == DKBLE_STATE);
  for (int i = 0; i < 4; ++i) {
    Command command{Op::send, 2};
    command.data.resize(65536);
    CHECK(queue.push(std::move(command)) == 0);
  }
  Command extra{Op::send, 2};
  extra.data.resize(1);
  CHECK(queue.push(std::move(extra)) == DKBLE_BUSY);
  const auto revision = queue.take(held);
  CHECK(held.size() == 4 && queue.allows(2, revision));
  // Overflow closes a session without advancing its ABI generation. Its
  // stop must also fence a batch already taken by the worker.
  const auto second = queue.request_stop(2);
  CHECK(second > first && !queue.allows(2, revision));
  stop = queue.take_stop();
  CHECK(stop && stop->ticket == second);
  held.clear();
  queue.take(held);
  CHECK(held.empty());
  CHECK(queue.push({Op::startScan, 3}) == 0);
}
void sessionTests() {
  using namespace read_session;
  Registry registry;
  Epoch a{1}, b{2}, parsed{};
  CHECK(registry.accept("peer", a, 2) == Registry::fresh);
  CHECK(registry.accept("peer", a, 2) == Registry::current);
  CHECK(registry.accept("peer", b, 2) == Registry::fresh);
  CHECK(registry.accept("peer", a, 2) == Registry::rejected);
  const uint8_t raw[] = {0, 1, 2, 255};
  auto frame = packet(read_session::write, a, raw);
  uint8_t kind = 0;
  CHECK(parse(frame, kind, parsed) && kind == read_session::write && parsed != b);
  auto response = packet(payload, b, raw);
  CHECK(parse(response, kind, parsed) && parsed == b);
  CHECK(std::equal(raw, raw + 4, response.begin() + header));
  CHECK(!parse(std::vector<uint8_t>{0}, kind, parsed));
  CHECK(!parse(packet(read_session::write, b), kind, parsed));
  CHECK(!parse(packet(hello, b, raw), kind, parsed));
  CHECK(!parse(packet(idle, Epoch{}), kind, parsed));
  registry.retire("peer", b);
  CHECK(registry.accept("peer", b, 2) == Registry::rejected);
  for (uint8_t i = 3; i <= 64; ++i)
    CHECK(registry.accept("peer", Epoch{i}, 2) == Registry::fresh);
  CHECK(registry.accept("peer", Epoch{65}, 2) == Registry::rejected);
  CHECK(registry.accept("second", a, 2) == Registry::fresh);
  CHECK(registry.accept("third", a, 2) == Registry::rejected);
  registry.disconnected("peer");
  CHECK(registry.accept("peer", a, 2) == Registry::fresh);
}
int main() {
  queueTests();
  sessionTests();
  dkble_config c{sizeof(c),
                 1,
                 0,
                 0,
                 "00000000-0000-0000-0000-000000000001",
                 "00000000-0000-0000-0000-000000000002",
                 "00000000-0000-0000-0000-000000000003",
                 wake,
                 nullptr};
  CHECK(dkble_create(&c, &context) == 0);
  CHECK(stops == 0 && wakes == 0);
  dkble_options opts{sizeof(opts), 1, 2000, 3000, 500, 1000, 2, 4};
  CHECK(dkble_set_read_interval(context, 9) == DKBLE_INVALID);
  CHECK(dkble_set_read_interval(context, 50) == 0);
  CHECK(dkble_set_options(context, &opts) == 0);
  opts.maximum_links = 5;
  CHECK(dkble_set_options(context, &opts) == DKBLE_INVALID);
  opts.maximum_links = 2;
  std::thread wrong([] { CHECK(dkble_stop(context) == DKBLE_THREAD); });
  wrong.join();
  CHECK(dkble_connect(context, "peer") == DKBLE_STATE);
  CHECK(dkble_start(context, 1) == 0);
  auto previous = epoch;
  CHECK(dkble_set_options(context, &opts) == DKBLE_STATE);
  CHECK(dkble_set_read_interval(context, 50) == DKBLE_STATE);
  inject(DKBLE_EVENT_LINK);
  CHECK(dkble_dispatch(context, 256, receive, nullptr) == 1);
  const uint8_t data[] = {1, 2, 3};
  CHECK(dkble_send(context, "route", data, 3, 99) == 0);
  CHECK(dkble_send(context, "route", data, 3, 100) == DKBLE_BUSY);
  inject(DKBLE_EVENT_SEND_COMPLETE);
  CHECK(dkble_dispatch(context, 256, receive, nullptr) == 1);
  CHECK(dkble_send(context, "route", data, 3, 100) == 0);
  CHECK(dkble_stop(context) == 0);
  inject(DKBLE_EVENT_DATA, "route", 20, previous);
  CHECK(dkble_dispatch(context, 256, receive, nullptr) == 1);
  CHECK(dkble_start(context, 2) == 0 && active);
  const auto oldOff = offEvents;
  stopFailure = DKBLE_BUSY;
  CHECK(dkble_stop(context) == DKBLE_BUSY && active);
  CHECK(dkble_dispatch(context, 256, receive, nullptr) == 0);
  CHECK(offEvents == oldOff);
  CHECK(dkble_send(context, "route", data, 3, 1) == DKBLE_STATE);
  CHECK(dkble_set_options(context, &opts) == DKBLE_STATE);
  CHECK(dkble_start(context, 1) == DKBLE_BUSY && active);
  stopFailure = 0;
  CHECK(dkble_stop(context) == 0 && !active);
  CHECK(dkble_dispatch(context, 256, receive, nullptr) == 1);
  CHECK(offEvents == oldOff + 1);
  CHECK(dkble_start(context, 1) == 0 && active);
  throwStop = true;
  CHECK(dkble_stop(context) == DKBLE_IO && active);
  CHECK(dkble_dispatch(context, 256, receive, nullptr) == 0);
  CHECK(offEvents == oldOff + 1);
  throwStop = false;
  CHECK(dkble_stop(context) == 0 && !active);
  CHECK(dkble_start(context, 2) == 0);
  stopFailure = DKBLE_BUSY;
  for (int i = 0; i < 257; ++i)
    inject(DKBLE_EVENT_DATA, "route", 1);
  const auto oldCount = count;
  CHECK(dkble_dispatch(context, 256, receive, nullptr) == DKBLE_BUSY);
  CHECK(count == oldCount + 1 && active);
  CHECK(dkble_send(context, "route", data, 3, 1) == DKBLE_STATE);
  const auto attempts = stops;
  stopFailure = 0;
  CHECK(dkble_dispatch(context, 256, receive, nullptr) == 0);
  CHECK(stops == attempts + 1 && !active);
  CHECK(dkble_start(context, 1) == 0);
  for (int i = 0; i < 5; ++i)
    inject(DKBLE_EVENT_DATA, "route", 65560);
  CHECK(dkble_dispatch(context, 256, receive, nullptr) == 1);
  CHECK(dkble_start(context, 1) == 0);
  for (int i = 0; i < 300; ++i) {
    Event e;
    e.type = DKBLE_EVENT_CANDIDATE;
    e.peer = "same";
    e.generation = epoch;
    emit(std::move(e));
  }
  CHECK(dkble_dispatch(context, 256, receive, nullptr) == 1);
  CHECK(dkble_destroy(context) == 0 && destroyed == 1);
  auto oldWakes = wakes;
  inject(DKBLE_EVENT_DATA);
  CHECK(wakes == oldWakes);
  puts("portable ABI: lifetime, thread ownership, reentrancy, generations, "
       "send admission, count/byte overflow and discovery coalescing passed; "
       "stop failure/retry, saturated stop control and stale batch fencing "
       "and read-session epoch/replay bounds passed; no radio accessed");
}
