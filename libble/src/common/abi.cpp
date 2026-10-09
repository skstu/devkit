#include "backend.hpp"
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstring>
#include <deque>
#include <map>
#include <mutex>
#include <thread>
#include <utility>
namespace {
bool text(const char *s, size_t limit, bool empty = false) {
  if (!s)
    return false;
  size_t n = 0;
  while (n <= limit && s[n]) {
    if (static_cast<unsigned char>(s[n]) < 32)
      return false;
    ++n;
  }
  return n <= limit && (empty || n != 0);
}
std::string uuid(const char *s) {
  if (!text(s, 36) || std::strlen(s) != 36)
    return {};
  std::string out(s);
  for (size_t i = 0; i < 36; ++i) {
    if (i == 8 || i == 13 || i == 18 || i == 23) {
      if (s[i] != '-')
        return {};
    } else if (!std::isxdigit(static_cast<unsigned char>(s[i])))
      return {};
    out[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(s[i])));
  }
  return out;
}
struct Mailbox {
  std::mutex mutex;
  std::deque<dkble::Event> events;
  size_t bytes = 0;
  uint64_t generation = 0;
  bool accepting = false, dead = false, overflow = false;
  std::atomic<bool> notifying{false};
  dkble_wake_fn wake = nullptr;
  void *user = nullptr;
  void signal_locked() {
    if (wake && !notifying.exchange(true)) {
      wake(user);
      notifying = false;
    }
  }
  void signal() {
    std::lock_guard lock(mutex);
    if (!dead)
      signal_locked();
  }
  void push(dkble::Event e) {
    std::lock_guard lock(mutex);
    if (dead || !accepting || overflow || e.generation != generation)
      return;
    if (e.type == DKBLE_EVENT_CANDIDATE) {
      for (auto &old : events)
        if (old.type == e.type && old.peer == e.peer) {
          old = std::move(e);
          return;
        }
    }
    if ((e.type == DKBLE_EVENT_ERROR && e.status == DKBLE_OVERFLOW) ||
        events.size() >= 256 || bytes + e.data.size() > 262144) {
      events.clear();
      bytes = 0;
      overflow = true;
      e = {};
      e.type = DKBLE_EVENT_ERROR;
      e.generation = generation;
      e.status = DKBLE_OVERFLOW;
      e.detail = "mailbox-overflow";
    }
    const bool empty = events.empty();
    bytes += e.data.size();
    events.push_back(std::move(e));
    if (empty)
      signal_locked();
  }
};
} // namespace
struct dkble_context {
  std::thread::id owner = std::this_thread::get_id();
  std::shared_ptr<Mailbox> mailbox = std::make_shared<Mailbox>();
  std::unique_ptr<dkble::Backend> backend;
  bool dispatching = false, stopped = true, failed = false;
  bool overflowStopPending = false;
  uint64_t generation = 0;
  // Updated before delivery: a callback may immediately send on a new link.
  std::map<std::string, bool> links;
};
namespace {
int access(dkble_context *c) {
  if (!c)
    return DKBLE_INVALID;
  if (c->owner != std::this_thread::get_id())
    return DKBLE_THREAD;
  if (c->mailbox->notifying)
    return DKBLE_BUSY;
  return DKBLE_OK;
}
int invoke(dkble_context *c, dkble::Command command) {
  command.generation = c->generation;
  try {
    return c->backend->command(std::move(command));
  } catch (...) {
    return DKBLE_IO;
  }
}
int invalidate(dkble_context *c, bool off) {
  c->stopped = false;
  c->failed = true;
  c->overflowStopPending = false;
  ++c->generation;
  c->links.clear();
  {
    std::lock_guard lock(c->mailbox->mutex);
    c->mailbox->accepting = false;
    c->mailbox->generation = c->generation;
    c->mailbox->events.clear();
    c->mailbox->bytes = 0;
    c->mailbox->overflow = false;
  }
  dkble::Command command{};
  command.op = dkble::Op::stop;
  const auto status = invoke(c, std::move(command));
  if (status)
    return status;
  c->stopped = true;
  c->failed = false;
  if (off) try {
    std::lock_guard lock(c->mailbox->mutex);
    dkble::Event e;
    e.type = DKBLE_EVENT_STATE;
    e.detail = "off";
    e.generation = c->generation;
    c->mailbox->events.push_back(std::move(e));
    c->mailbox->signal_locked();
  } catch (...) {
    return DKBLE_IO;
  }
  return DKBLE_OK;
}
int op(dkble_context *c, dkble::Op operation, const char *value = nullptr,
       const char *extra = nullptr) {
  const auto status = access(c);
  if (status)
    return status;
  if (value && !text(value, 128))
    return DKBLE_INVALID;
  if (extra && !text(extra, 128, true))
    return DKBLE_INVALID;
  if (c->stopped || c->failed)
    return DKBLE_STATE;
  try {
    return invoke(
        c, {operation, 0, 0, value ? value : "", extra ? extra : "", {}});
  } catch (...) {
    return DKBLE_IO;
  }
}
} // namespace
extern "C" {
int32_t DKBLE_CALL dkble_set_read_interval(dkble_context *c,
                                           uint32_t interval) {
  int status = access(c);
  if (status)
    return status;
  if (interval < 10 || interval > 1000)
    return DKBLE_INVALID;
  if (!c->stopped)
    return DKBLE_STATE;
  try {
    return c->backend->set_read_interval(interval);
  } catch (...) {
    return DKBLE_IO;
  }
}

int32_t DKBLE_CALL dkble_set_options(dkble_context *c,
                                     const dkble_options *options) {
  int status = access(c);
  if (status)
    return status;
  if (!options || options->struct_size != sizeof(*options) ||
      options->abi_version != 1 || options->connect_timeout_ms < 1000 ||
      options->connect_timeout_ms > 120000 || options->send_timeout_ms < 1000 ||
      options->send_timeout_ms > 120000 || options->retry_delay_ms < 250 ||
      options->retry_delay_ms > 60000 || options->candidate_ttl_ms < 1000 ||
      options->candidate_ttl_ms > 120000 || !options->maximum_links ||
      options->maximum_links > 4 || !options->maximum_candidates ||
      options->maximum_candidates > 64)
    return DKBLE_INVALID;
  if (!c->stopped)
    return DKBLE_STATE;
  try {
    return c->backend->set_options(*options);
  } catch (...) {
    return DKBLE_IO;
  }
}
int32_t DKBLE_CALL dkble_create(const dkble_config *config,
                                dkble_context **output) {
  if (!output)
    return DKBLE_INVALID;
  *output = nullptr;
  if (!config || config->struct_size != sizeof(*config) ||
      config->abi_version != 1 || config->reserved ||
      (config->flags & ~DKBLE_HINT_REBIND))
    return DKBLE_INVALID;
  try {
    auto service = uuid(config->service_uuid),
         receive = uuid(config->receive_uuid),
         notify = uuid(config->notify_uuid);
    if (service.empty() || receive.empty() || notify.empty() ||
        service == receive || service == notify || receive == notify)
      return DKBLE_INVALID;
    auto c = std::make_unique<dkble_context>();
    c->mailbox->wake = config->wake;
    c->mailbox->user = config->user;
    auto box = c->mailbox;
    c->backend = dkble::make_backend(
        {service, receive, notify, (config->flags & DKBLE_HINT_REBIND) != 0},
        [box](dkble::Event e) { box->push(std::move(e)); },
        [box] { box->signal(); });
    if (!c->backend)
      return DKBLE_UNSUPPORTED;
    *output = c.release();
    return DKBLE_OK;
  } catch (...) {
    return DKBLE_IO;
  }
}
int32_t DKBLE_CALL dkble_destroy(dkble_context *c) {
  int status = access(c);
  if (status)
    return status;
  if (c->dispatching)
    return DKBLE_BUSY;
  {
    std::lock_guard lock(c->mailbox->mutex);
    c->mailbox->dead = true;
    c->mailbox->wake = nullptr;
    c->mailbox->events.clear();
    c->mailbox->bytes = 0;
  }
  try {
    c->backend->close();
  } catch (...) {
  }
  delete c;
  return DKBLE_OK;
}
int32_t DKBLE_CALL dkble_start(dkble_context *c, uint32_t mode) {
  int status = access(c);
  if (status)
    return status;
  if (mode != DKBLE_MODE_SCAN && mode != DKBLE_MODE_ADVERTISE)
    return DKBLE_INVALID;
  status = invalidate(c, false);
  if (status)
    return status;
  c->stopped = false;
  {
    std::lock_guard lock(c->mailbox->mutex);
    c->mailbox->accepting = true;
  }
  dkble::Command command{};
  command.op = mode == DKBLE_MODE_SCAN ? dkble::Op::startScan
                                       : dkble::Op::startAdvertise;
  status = invoke(c, std::move(command));
  if (status)
    invalidate(c, false);
  return status;
}
int32_t DKBLE_CALL dkble_stop(dkble_context *c) {
  int status = access(c);
  if (status)
    return status;
  return invalidate(c, true);
}
int32_t DKBLE_CALL dkble_connect(dkble_context *c, const char *peer) {
  if (!text(peer, 128))
    return DKBLE_INVALID;
  return op(c, dkble::Op::connect, peer);
}
int32_t DKBLE_CALL dkble_probe(dkble_context *c, const char *peer,
                               const char *token) {
  if (!text(peer, 128) || !text(token, 64))
    return DKBLE_INVALID;
  return op(c, dkble::Op::probe, peer, token);
}
int32_t DKBLE_CALL dkble_cancel_probe(dkble_context *c, const char *token) {
  if (!text(token, 64))
    return DKBLE_INVALID;
  return op(c, dkble::Op::cancel, token);
}
int32_t DKBLE_CALL dkble_adopt_probe(dkble_context *c, const char *token,
                                     const char *previous) {
  if (!text(token, 64) || (previous && !text(previous, 128, true)))
    return DKBLE_INVALID;
  return op(c, dkble::Op::adopt, token, previous);
}
int32_t DKBLE_CALL dkble_disconnect(dkble_context *c, const char *link,
                                    uint32_t retry) {
  if (!text(link, 128) || retry > 1)
    return DKBLE_INVALID;
  auto result = op(c, retry ? dkble::Op::reset : dkble::Op::disconnect, link);
  if (!result)
    c->links.erase(link);
  return result;
}
int32_t DKBLE_CALL dkble_send(dkble_context *c, const char *link,
                              const uint8_t *data, size_t size,
                              uint64_t request) {
  int status = access(c);
  if (status)
    return status;
  if (!text(link, 128) || !data || !size || size > 65560)
    return DKBLE_INVALID;
  if (c->stopped || c->failed)
    return DKBLE_STATE;
  auto found = c->links.find(link);
  if (found == c->links.end())
    return DKBLE_STATE;
  if (found->second)
    return DKBLE_BUSY;
  try {
    auto copy = std::vector<uint8_t>(data, data + size);
    found->second = true;
    status =
        invoke(c, {dkble::Op::send, 0, request, link, "", std::move(copy)});
    if (status)
      found->second = false;
    return status;
  } catch (...) {
    return DKBLE_IO;
  }
}
int32_t DKBLE_CALL dkble_recover(dkble_context *c) {
  return op(c, dkble::Op::recover);
}
int32_t DKBLE_CALL dkble_dispatch(dkble_context *c, uint32_t maximum,
                                  dkble_event_fn callback, void *user) {
  int status = access(c);
  if (status)
    return status;
  if (!callback || !maximum || maximum > 256)
    return DKBLE_INVALID;
  if (c->dispatching)
    return DKBLE_BUSY;
  c->dispatching = true;
  struct Reset {
    bool &value;
    ~Reset() { value = false; }
  } reset{c->dispatching};
  try {
    c->backend->poll();
  } catch (...) {
    return DKBLE_IO;
  }
  bool flooded = false;
  {
    std::lock_guard lock(c->mailbox->mutex);
    flooded = c->mailbox->overflow;
  }
  if (flooded && !c->failed) {
    c->failed = true;
    c->links.clear();
    c->overflowStopPending = true;
  }
  int stopStatus = DKBLE_OK;
  if (c->overflowStopPending) {
    dkble::Command command{};
    command.op = dkble::Op::stop;
    stopStatus = invoke(c, std::move(command));
    if (!stopStatus) {
      c->overflowStopPending = false;
      c->stopped = true;
    }
  }
  const auto generation = c->generation;
  int32_t count = 0;
  for (; count < static_cast<int32_t>(maximum) && generation == c->generation;
       ++count) {
    dkble::Event e;
    {
      std::lock_guard lock(c->mailbox->mutex);
      if (c->mailbox->events.empty())
        break;
      e = std::move(c->mailbox->events.front());
      c->mailbox->events.pop_front();
      c->mailbox->bytes -= e.data.size();
    }
    if (e.type == DKBLE_EVENT_LINK)
      c->links[e.link] = false;
    else if (e.type == DKBLE_EVENT_DISCONNECTED)
      c->links.erase(e.link);
    else if (e.type == DKBLE_EVENT_SEND_COMPLETE) {
      auto found = c->links.find(e.link);
      if (found != c->links.end())
        found->second = false;
    }
    dkble_event value{
        sizeof(value),  e.type,         e.generation,          e.request,
        e.status,       e.rssi,         e.initiator ? 1u : 0u, 0,
        e.link.c_str(), e.peer.c_str(), e.probe.c_str(),       e.detail.c_str(),
        e.data.data(),  e.data.size()};
    callback(user, &value);
  }
  {
    std::lock_guard lock(c->mailbox->mutex);
    if (!c->mailbox->events.empty())
      c->mailbox->signal_locked();
  }
  return stopStatus ? stopStatus : count;
}
}
