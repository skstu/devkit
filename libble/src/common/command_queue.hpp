#pragma once
#include "backend.hpp"
#include <deque>
#include <optional>
#include <utility>

namespace dkble {
// Caller holds its worker mutex. Stop uses a nonallocating control slot and
// fences commands already removed by the worker, as well as queued commands.
class CommandQueue {
public:
  struct Stop { uint64_t generation, ticket; };
  int push(Command command) {
    if (!allows(command.generation))
      return DKBLE_STATE;
    if (commands.size() >= 128 || bytes + command.data.size() > 262144)
      return DKBLE_BUSY;
    const auto size = command.data.size();
    commands.push_back(std::move(command));
    bytes += size;
    return DKBLE_OK;
  }
  uint64_t request_stop(uint64_t generation) {
    clear();
    barrier = generation;
    control = Stop{generation, ++serial};
    return serial;
  }
  std::optional<Stop> take_stop() { return std::exchange(control, {}); }
  uint64_t take(std::deque<Command> &batch) {
    batch.swap(commands);
    bytes = 0;
    return serial;
  }
  bool allows(uint64_t generation, uint64_t revision) const {
    return revision == serial && allows(generation);
  }
  bool allows(uint64_t generation) const { return generation >= barrier; }
  void clear() {
    commands.clear();
    bytes = 0;
  }
private:
  std::deque<Command> commands;
  size_t bytes = 0;
  uint64_t barrier = 0, serial = 0;
  std::optional<Stop> control;
};
} // namespace dkble
