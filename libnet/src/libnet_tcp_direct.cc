#include "libnet_tcp_direct.h"
#include "libnet_uv.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <deque>
#include <mutex>
#include <nlohmann/json.hpp>
#include <openssl/crypto.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <set>
#include <stdexcept>
#include <thread>
#ifndef _WIN32
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#endif

namespace libnet {
namespace {
using Json = nlohmann::json;
using Bytes = std::vector<std::uint8_t>;
using Clock = std::chrono::steady_clock;
constexpr std::string_view kDescription = "sovkit-tcp/1\n";
constexpr std::size_t kRecordLimit = 65507, kQueueLimit = 1024 * 1024;
std::string Hex(std::span<const std::uint8_t> bytes) {
  std::string out;
  for (auto b : bytes) {
    out += "0123456789abcdef"[b >> 4];
    out += "0123456789abcdef"[b & 15];
  }
  return out;
}
Bytes Unhex(std::string_view text, std::size_t size) {
  if (text.size() != size * 2 ||
      text.find_first_not_of("0123456789abcdef") != text.npos)
    throw std::runtime_error("invalid-credential");
  Bytes out(size);
  for (std::size_t i = 0; i < size; ++i) {
    auto digit = [](char c) { return c <= '9' ? c - '0' : c - 'a' + 10; };
    out[i] = static_cast<std::uint8_t>((digit(text[i * 2]) << 4) |
                                       digit(text[i * 2 + 1]));
  }
  return out;
}
Bytes Random(std::size_t size) {
  Bytes bytes(size);
  if (RAND_bytes(bytes.data(), static_cast<int>(size)) != 1)
    throw std::runtime_error("random-failed");
  return bytes;
}
void Wipe(Bytes &bytes) {
  OPENSSL_cleanse(bytes.data(), bytes.size());
  bytes.clear();
}
void Put32(Bytes &out, std::uint32_t n) {
  out.push_back(static_cast<std::uint8_t>(n >> 24));
  out.push_back(static_cast<std::uint8_t>(n >> 16));
  out.push_back(static_cast<std::uint8_t>(n >> 8));
  out.push_back(static_cast<std::uint8_t>(n));
}
std::uint32_t Read32(const std::uint8_t *p) {
  return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) |
         (std::uint32_t(p[2]) << 8) | p[3];
}
Bytes Framed(std::span<const std::uint8_t> data) {
  Bytes out;
  Put32(out, static_cast<std::uint32_t>(data.size()));
  out.insert(out.end(), data.begin(), data.end());
  return out;
}
Endpoint Candidate(const Json &j) {
  if (!j.is_object() || j.size() != 3 || !j.at("address").is_string() ||
      !j.at("port").is_number_unsigned() ||
      (j.at("type") != "host" && j.at("type") != "srflx"))
    throw std::runtime_error("invalid-candidate");
  auto port = j.at("port").get<std::uint64_t>();
  auto ep =
      Endpoint::Parse(j.at("address").get<std::string>(),
                      port <= 65535 ? static_cast<std::uint16_t>(port) : 0);
  if (!ep || !port || port > 65535 || ep->family() != AddressFamily::ipv4)
    throw std::runtime_error("invalid-candidate");
  auto ip = ntohl(reinterpret_cast<const sockaddr_in *>(ep->sockaddr_ptr())
                      ->sin_addr.s_addr);
  if (!ip || ip >= 0xe0000000U)
    throw std::runtime_error("invalid-candidate");
  return *ep;
}
Json CandidateJson(const Endpoint &ep, const char *type) {
  return {{"address", ep.address()}, {"port", ep.port()}, {"type", type}};
}
} // namespace

struct TcpDirectAgent::Impl {
  mutable std::mutex mutex;
  std::mutex lifecycle;
  std::thread worker;
  std::atomic<bool> stopping{false};
  IceConfig config;
  IceState state = IceState::disconnected;
  TcpDirectStats stats;
  IceQueueStats queues;
  IceGatherStats gathering;
  IceSelectedPath selected;
  Json local_candidates = Json::array(), remote_candidates = Json::array();
  Bytes local_key, remote_key, key, round;
  std::string remote_description;
  bool begun = false;
  std::deque<IceEvent> events;
  std::deque<Bytes> outgoing;
  std::size_t queued_bytes = 0, incoming_bytes = 0;

  void Event(IceEvent event) {
    // Caller holds mutex. Wake callbacks only signal, never reenter the agent.
    if (events.size() >= 256 ||
        incoming_bytes + event.datagram.size() > kQueueLimit) {
      ++queues.dropped_datagrams;
      if (!event.datagram.empty())
        throw std::runtime_error("receive-queue-limit");
      return;
    }
    incoming_bytes += event.datagram.size();
    events.push_back(std::move(event));
    queues.peak_events = std::max(queues.peak_events, events.size());
    if (config.event_ready)
      config.event_ready();
  }
  void State(IceState next, std::string phase, std::string error = {}) {
    std::lock_guard lock(mutex);
    state = next;
    stats.phase = std::move(phase);
    stats.error = std::move(error);
    Event({IceEventType::state_changed, state, {}, {}});
  }
  void DeriveKey() {
    Bytes material = config.tcp_offerer ? local_key : remote_key;
    const auto &second = config.tcp_offerer ? remote_key : local_key;
    material.insert(material.end(), second.begin(), second.end());
    material.insert(material.end(), round.begin(), round.end());
    key.resize(32);
    SHA256(material.data(), material.size(), key.data());
    Wipe(material);
  }
  Bytes Control(std::string_view magic) const {
    Bytes out(magic.begin(), magic.end());
    out.insert(out.end(), {1, std::uint8_t(config.tcp_offerer ? 1 : 2), 0, 0});
    out.insert(out.end(), round.begin(), round.end());
    return out;
  }
#ifndef _WIN32
  struct Socket {
    int fd = -1;
    bool connected = false, hello = false, ack = false, chosen = false;
    Bytes input, output, nonce, peer_nonce;
    Clock::time_point created = Clock::now();
    explicit Socket(int value = -1) : fd(value) {}
    ~Socket() { Close(); }
    Socket(const Socket &) = delete;
    Socket &operator=(const Socket &) = delete;
    void Close() {
      if (fd >= 0)
        close(fd);
      fd = -1;
      Wipe(input);
      Wipe(output);
    }
  };
  static void Require(bool ok, const char *error) {
    if (!ok)
      throw std::runtime_error(error);
  }
  static void Nonblocking(int fd) {
    Require(fcntl(fd, F_SETFL, O_NONBLOCK) == 0, "nonblocking-failed");
    Require(fcntl(fd, F_SETFD, FD_CLOEXEC) == 0, "close-on-exec-failed");
#ifdef SO_NOSIGPIPE
    int one = 1;
    Require(setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one)) == 0,
            "sigpipe-option-failed");
#endif
  }
  static Endpoint End(int fd, bool peer = false) {
    sockaddr_storage address{};
    socklen_t size = sizeof(address);
    Require(
        (peer ? getpeername(fd, reinterpret_cast<sockaddr *>(&address), &size)
              : getsockname(fd, reinterpret_cast<sockaddr *>(&address),
                            &size)) == 0,
        "endpoint-failed");
    auto ep =
        Endpoint::FromSockaddr(reinterpret_cast<sockaddr *>(&address), size);
    Require(ep.has_value(), "endpoint-failed");
    return *ep;
  }
  std::unique_ptr<Socket> Bound(std::uint16_t port) {
    auto s = std::make_unique<Socket>(socket(AF_INET, SOCK_STREAM, 0));
    Require(s->fd >= 0, "socket-failed");
    int one = 1;
    Require(setsockopt(s->fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) == 0,
            "port-reuse-unavailable");
#ifdef SO_REUSEPORT
    Require(setsockopt(s->fd, SOL_SOCKET, SO_REUSEPORT, &one, sizeof(one)) == 0,
            "port-reuse-unavailable");
#endif
    Require(PrepareNetworkPathSocket(config.network_path, s->fd, AF_INET),
            "network-binding-failed");
    auto ep = Endpoint::Parse(config.network_path.selected()
                                  ? config.network_path.local_address
                                  : config.bind_address,
                              port);
    Require(ep.has_value(), "source-unavailable");
    Require(bind(s->fd, ep->sockaddr_ptr(), ep->sockaddr_length()) == 0,
            "port-bind-failed");
    Nonblocking(s->fd);
    return s;
  }
  static void Connect(Socket &s, const Endpoint &ep) {
    s.created = Clock::now();
    const int rc = connect(s.fd, ep.sockaddr_ptr(), ep.sockaddr_length());
    Require(rc == 0 || errno == EINPROGRESS, "connect-failed");
    s.connected = rc == 0;
  }
  static void IO(Socket &s, short events, std::size_t limit) {
    if (events & (POLLOUT | POLLERR | POLLHUP)) {
      if (!s.connected) {
        int error = 0;
        socklen_t size = sizeof(error);
        Require(getsockopt(s.fd, SOL_SOCKET, SO_ERROR, &error, &size) == 0 &&
                    !error,
                "connect-failed");
        s.connected = true;
      }
      if (!s.output.empty()) {
#ifdef MSG_NOSIGNAL
        constexpr int flags = MSG_NOSIGNAL;
#else
        constexpr int flags = 0;
#endif
        auto n = send(s.fd, s.output.data(), s.output.size(), flags);
        if (n > 0)
          s.output.erase(s.output.begin(), s.output.begin() + n);
        else
          Require(n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK ||
                            errno == EINTR),
                  "send-failed");
      }
    }
    if (events & (POLLIN | POLLHUP)) {
      std::array<std::uint8_t, 8192> buffer{};
      auto n = recv(s.fd, buffer.data(), buffer.size(), 0);
      Require(n != 0, "peer-closed");
      if (n < 0)
        Require(errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR,
                "receive-failed");
      else {
        Require(s.input.size() + std::size_t(n) <= limit, "receive-limit");
        s.input.insert(s.input.end(), buffer.begin(), buffer.begin() + n);
      }
    }
  }
  static pollfd Poll(const Socket &s) {
    return {s.fd,
            short(POLLIN | ((!s.connected || !s.output.empty()) ? POLLOUT : 0)),
            0};
  }
  static bool Pop(Socket &s, Bytes &out, std::size_t limit) {
    if (s.input.size() < 4)
      return false;
    auto length = Read32(s.input.data());
    Require(length && length <= limit, "frame-limit");
    if (s.input.size() < length + 4)
      return false;
    out.assign(s.input.begin() + 4, s.input.begin() + length + 4);
    s.input.erase(s.input.begin(), s.input.begin() + length + 4);
    return true;
  }
  Bytes Proof(Socket &s, std::uint8_t type) {
    Bytes message{
        'N', 'V', 'T', 'P', 1, type, std::uint8_t(config.tcp_offerer ? 1 : 2),
        0};
    message.insert(message.end(), round.begin(), round.end());
    message.insert(message.end(), s.nonce.begin(), s.nonce.end());
    const Bytes echo = type == 1 ? Bytes(16, 0) : s.peer_nonce;
    message.insert(message.end(), echo.begin(), echo.end());
    std::array<unsigned char, 32> mac{};
    unsigned length = 0;
    Require(HMAC(EVP_sha256(), key.data(), static_cast<int>(key.size()),
                 message.data(), message.size(), mac.data(), &length) &&
                length == mac.size(),
            "proof-failed");
    message.insert(message.end(), mac.begin(), mac.end());
    return Framed(message);
  }
  void CheckProof(Socket &s, const Bytes &message, Socket *&choice) {
    Require(message.size() == 88 &&
                std::equal(message.begin(), message.begin() + 4, "NVTP") &&
                message[4] == 1 && message[6] == (config.tcp_offerer ? 2 : 1) &&
                !message[7] &&
                std::equal(round.begin(), round.end(), message.begin() + 8),
            "peer-proof-invalid");
    std::array<unsigned char, 32> mac{};
    unsigned length = 0;
    Require(HMAC(EVP_sha256(), key.data(), static_cast<int>(key.size()),
                 message.data(), 56, mac.data(), &length) &&
                length == 32 &&
                CRYPTO_memcmp(mac.data(), message.data() + 56, 32) == 0,
            "peer-proof-invalid");
    auto type = message[5];
    Bytes nonce(message.begin() + 24, message.begin() + 40);
    if (type == 1) {
      Require(!s.hello &&
                  std::all_of(message.begin() + 40, message.begin() + 56,
                              [](auto b) { return b == 0; }),
              "peer-proof-replayed");
      s.peer_nonce = std::move(nonce);
      s.hello = true;
      auto ack = Proof(s, 2);
      s.output.insert(s.output.end(), ack.begin(), ack.end());
    } else {
      Require(
          s.hello && nonce == s.peer_nonce &&
              std::equal(s.nonce.begin(), s.nonce.end(), message.begin() + 40),
          "peer-proof-replayed");
      if (type == 2) {
        Require(!s.ack, "peer-proof-replayed");
        s.ack = true;
      } else if (type == 3) {
        Require(!config.tcp_offerer && s.ack && (!choice || choice == &s),
                "peer-selection-invalid");
        choice = &s;
        s.chosen = true;
        auto ack = Proof(s, 4);
        s.output.insert(s.output.end(), ack.begin(), ack.end());
      } else if (type == 4) {
        Require(config.tcp_offerer && choice == &s && !s.chosen,
                "peer-selection-invalid");
        s.chosen = true;
      } else
        throw std::runtime_error("peer-proof-invalid");
    }
  }
  void Run() {
    try {
      auto listener = Bound(config.port_range_begin);
      auto local = End(listener->fd);
      auto coordinator = Bound(local.port());
      std::array<std::unique_ptr<Socket>, 2> pending{Bound(local.port()),
                                                     Bound(local.port())};
      Require(listen(listener->fd, 8) == 0, "listen-failed");
      Json candidates = Json::array({CandidateJson(local, "host")});
      bool reflected = config.stun_host.empty(), ready_sent = false,
           go = config.stun_host.empty();
      if (reflected)
        coordinator->Close();
      if (!reflected) {
        auto endpoint = Endpoint::Parse(config.stun_host, config.stun_port);
        Require(endpoint.has_value(), "coordinator-address-invalid");
        Connect(*coordinator, *endpoint);
        coordinator->output = Control("NVTC");
      }
      auto began_at = Clock::now(),
           connection_deadline = Clock::time_point::max();
      std::vector<std::unique_ptr<Socket>> streams;
      bool connecting = false, selected_stream = false;
      Socket *choice = nullptr;
      while (!stopping) {
        const auto now = Clock::now();
        if (!reflected && now - began_at >= std::chrono::seconds(5))
          throw std::runtime_error("coordinator-timeout");
        if (!connecting && now - began_at >= std::chrono::seconds(300))
          throw std::runtime_error("invitation-expired");
        if (connecting && !selected_stream && now >= connection_deadline)
          throw std::runtime_error("connect-timeout");
        bool remote_ready;
        {
          std::lock_guard lock(mutex);
          remote_ready = !remote_description.empty();
        }
        if (reflected) {
          std::lock_guard lock(mutex);
          if (!gathering.complete) {
            local_candidates = candidates;
            gathering.complete = gathering.signaling_ready = true;
            gathering.local.host = 1;
            gathering.local.srflx = candidates.size() - 1;
            gathering.local.ipv4 = candidates.size();
            state = IceState::connecting;
            stats.phase = "waiting-peer";
            Event({IceEventType::gathering_done, state, {}, {}});
          }
        }
        if (reflected && remote_ready && !ready_sent && coordinator->fd >= 0 &&
            !config.stun_host.empty()) {
          auto ready = Control("NVTD");
          coordinator->output.insert(coordinator->output.end(), ready.begin(),
                                     ready.end());
          ready_sent = true;
        }
        if (reflected && remote_ready && go && !connecting) {
          connecting = true;
          connection_deadline = now + std::chrono::seconds(12);
          State(IceState::connecting, "connecting");
          Json targets;
          {
            std::lock_guard lock(mutex);
            targets = remote_candidates;
          }
          for (std::size_t i = 0; i < targets.size(); ++i) {
            auto s = std::move(pending[i]);
            try {
              Connect(*s, Candidate(targets[i]));
              s->nonce = Random(16);
              s->output = Proof(*s, 1);
              streams.push_back(std::move(s));
              std::lock_guard lock(mutex);
              ++stats.attempts;
            } catch (...) {
              std::lock_guard lock(mutex);
              stats.error = "connect-failed";
            }
          }
        }
        std::vector<pollfd> polls{{remote_ready ? listener->fd : -1, POLLIN, 0},
                                  Poll(*coordinator)};
        for (const auto &stream : streams)
          polls.push_back(Poll(*stream));
        poll(polls.data(), static_cast<nfds_t>(polls.size()), 25);
        if (coordinator->fd >= 0 && !config.stun_host.empty()) {
          IO(*coordinator, polls[1].revents, 1024);
          while (coordinator->input.size() >= (reflected ? 24U : 28U)) {
            auto &input = coordinator->input;
            const auto size = reflected ? 24U : 28U;
            if (!reflected) {
              Require(std::equal(input.begin(), input.begin() + 4, "NVTR") &&
                          input[4] == 1 && !input[5] &&
                          std::equal(round.begin(), round.end(),
                                     input.begin() + 12),
                      "coordinator-rejected");
              char ip[INET_ADDRSTRLEN]{};
              Require(inet_ntop(AF_INET, input.data() + 8, ip, sizeof(ip)) !=
                          nullptr,
                      "coordinator-reply-invalid");
              auto port = std::uint16_t((input[6] << 8) | input[7]);
              auto ep = Endpoint::Parse(ip, port);
              Require(ep && port, "coordinator-reply-invalid");
              auto j = CandidateJson(*ep, "srflx");
              Candidate(j);
              if (ep->ToString() != local.ToString())
                candidates.push_back(j);
              reflected = true;
            } else {
              Require(
                  input[4] == 1 && !input[5] && !input[6] && !input[7] &&
                      std::equal(round.begin(), round.end(), input.begin() + 8),
                  "coordinator-reply-invalid");
              if (std::equal(input.begin(), input.begin() + 4, "NVTS")) {
                Require(ready_sent, "coordinator-start-early");
                go = true;
                std::lock_guard lock(mutex);
                stats.coordinated = true;
              } else
                Require(std::equal(input.begin(), input.begin() + 4, "NVTK"),
                        "coordinator-reply-invalid");
            }
            input.erase(input.begin(), input.begin() + size);
          }
        }
        // Process established streams before appending listener accepts.
        for (std::size_t i = 0; i < streams.size(); ++i) {
          auto &s = *streams[i];
          if (s.fd < 0)
            continue;
          try {
            if (selected_stream && choice == &s) {
              std::lock_guard lock(mutex);
              while (!outgoing.empty() &&
                     s.output.size() + outgoing.front().size() <= kQueueLimit) {
                auto &packet = outgoing.front();
                s.output.insert(s.output.end(), packet.begin(), packet.end());
                queued_bytes -= packet.size();
                outgoing.pop_front();
              }
            }
            // The final admission proof and the first application record can
            // arrive in one read; allow a bounded full record before selection.
            IO(s, polls[i + 2].revents,
               selected_stream ? kQueueLimit : kRecordLimit + 4 + 4 * 92);
            if (!selected_stream) {
              if (now - s.created >= std::chrono::seconds(5))
                throw std::runtime_error("peer-proof-timeout");
              Bytes message;
              while (!s.chosen && Pop(s, message, 128))
                CheckProof(s, message, choice);
              if (config.tcp_offerer && !choice && s.ack) {
                choice = &s;
                auto select = Proof(s, 3);
                s.output.insert(s.output.end(), select.begin(), select.end());
              }
              if (choice == &s && s.chosen && s.output.empty()) {
                selected_stream = true;
                listener->Close();
                coordinator->Close();
                for (auto &other : streams)
                  if (other.get() != choice)
                    other->Close();
                auto peer = End(s.fd, true), source = End(s.fd);
                {
                  std::lock_guard lock(mutex);
                  selected.local_family = selected.remote_family = "ipv4";
                  selected.local_address = source.address();
                  selected.remote_address = peer.address();
                  selected.local_type = "host";
                  selected.remote_type = "prflx";
                  for (const auto &j : remote_candidates)
                    if (Candidate(j).ToString() == peer.ToString())
                      selected.remote_type = j.at("type");
                }
                State(IceState::connected, "connected");
              }
            }
            if (selected_stream && choice == &s) {
              Bytes packet;
              while (Pop(s, packet, kRecordLimit)) {
                std::lock_guard lock(mutex);
                Event({IceEventType::datagram, state, {}, std::move(packet)});
              }
            }
          } catch (const std::exception &) {
            if (selected_stream && choice == &s)
              throw;
            if (choice == &s)
              choice = nullptr;
            s.Close();
            std::lock_guard lock(mutex);
            ++stats.rejected;
          }
        }
        if (!selected_stream && remote_ready && (polls[0].revents & POLLIN)) {
          int fd = accept(listener->fd, nullptr, nullptr);
          if (fd >= 0) {
            auto s = std::make_unique<Socket>(fd);
            std::uint64_t count;
            {
              std::lock_guard lock(mutex);
              count = ++stats.accepted;
            }
            if (count <= 8) {
              Nonblocking(fd);
              s->connected = true;
              s->nonce = Random(16);
              s->output = Proof(*s, 1);
              streams.push_back(std::move(s));
            }
          }
        }
      }
      State(IceState::disconnected, "stopped");
    } catch (const std::exception &error) {
      State(IceState::failed, "failed", error.what());
    }
  }
#endif
};

TcpDirectAgent::TcpDirectAgent() : impl_(std::make_unique<Impl>()) {}
TcpDirectAgent::~TcpDirectAgent() { Stop(); }
bool TcpDirectAgent::Supported() {
#ifdef _WIN32
  return false;
#else
  return true;
#endif
}
bool TcpDirectAgent::Start(const IceConfig &config) {
  Stop();
  std::lock_guard lifecycle(impl_->lifecycle);
  std::lock_guard lock(impl_->mutex);
  try {
    auto source = Endpoint::Parse(config.network_path.selected()
                                      ? config.network_path.local_address
                                      : config.bind_address,
                                  0);
    if (!Supported() || !config.tcp_direct || !source ||
        source->family() != AddressFamily::ipv4 ||
        config.address_policy == IceAddressPolicy::ipv6 ||
        config.manage_network_paths || !config.network_paths.empty() ||
        config.allow_relay || config.relay_only ||
        !config.turn_servers.empty() || config.mapping_second_port ||
        config.filtering_second_port || !config.pcp_gateway.empty() ||
        config.port_range_begin != config.port_range_end ||
        !ValidateNetworkPath(config.network_path))
      return false;
    if (!config.stun_host.empty()) {
      auto server = Endpoint::Parse(config.stun_host, config.stun_port);
      if (!server || server->family() != AddressFamily::ipv4 || !server->port())
        return false;
    }
    impl_->round = Unhex(config.tcp_round, 16);
    impl_->local_key = Random(32);
    impl_->config = config;
    impl_->stats = {};
    impl_->stats.phase = "ready";
    impl_->state = IceState::disconnected;
    return true;
  } catch (...) {
    return false;
  }
}
void TcpDirectAgent::Stop() {
  std::lock_guard lifecycle(impl_->lifecycle);
  impl_->stopping = true;
  if (impl_->worker.joinable())
    impl_->worker.join();
  std::lock_guard lock(impl_->mutex);
  impl_->state = IceState::disconnected;
  impl_->begun = false;
  impl_->gathering = {};
  impl_->selected = {};
  impl_->queues = {};
  impl_->events.clear();
  impl_->outgoing.clear();
  impl_->incoming_bytes = impl_->queued_bytes = 0;
  impl_->local_candidates = Json::array();
  impl_->remote_candidates = Json::array();
  OPENSSL_cleanse(impl_->remote_description.data(),
                  impl_->remote_description.size());
  impl_->remote_description.clear();
  Wipe(impl_->key);
  Wipe(impl_->remote_key);
  Wipe(impl_->local_key);
  Wipe(impl_->round);
  impl_->config = {};
}
bool TcpDirectAgent::BeginGather() {
  std::lock_guard lifecycle(impl_->lifecycle);
  std::lock_guard lock(impl_->mutex);
  if (impl_->local_key.empty())
    return false;
  if (impl_->begun)
    return true;
#ifndef _WIN32
  impl_->stopping = false;
  impl_->state = IceState::gathering;
  impl_->stats.phase = "gathering";
  try {
    impl_->worker = std::thread([this] { impl_->Run(); });
    impl_->begun = true;
    return true;
  } catch (...) {
    return false;
  }
#else
  return false;
#endif
}
bool TcpDirectAgent::Gathered() const {
  std::lock_guard lock(impl_->mutex);
  return impl_->gathering.complete;
}
IceState TcpDirectAgent::state() const {
  std::lock_guard lock(impl_->mutex);
  return impl_->state;
}
std::string TcpDirectAgent::LocalDescription() const {
  std::lock_guard lock(impl_->mutex);
  if (!impl_->gathering.complete)
    return {};
  return std::string(kDescription) +
         Json{{"role", impl_->config.tcp_offerer ? "offer" : "answer"},
              {"key", Hex(impl_->local_key)},
              {"round", Hex(impl_->round)},
              {"candidates", impl_->local_candidates}}
             .dump();
}
bool TcpDirectAgent::SetRemoteDescription(std::string_view description) {
  std::lock_guard lock(impl_->mutex);
  if (description.size() >= 4096 || !description.starts_with(kDescription) ||
      !impl_->remote_description.empty() || impl_->local_key.empty())
    return false;
  try {
    auto j = Json::parse(description.substr(kDescription.size()));
    if (!j.is_object() || j.size() != 4 ||
        j.at("role") != (impl_->config.tcp_offerer ? "answer" : "offer") ||
        j.at("round") != Hex(impl_->round) || !j.at("candidates").is_array() ||
        j.at("candidates").empty() || j.at("candidates").size() > 2)
      return false;
    auto remote_key = Unhex(j.at("key").get<std::string>(), 32);
    if (remote_key == impl_->local_key)
      return false;
    std::set<std::string> endpoints, types;
    for (const auto &candidate : j.at("candidates")) {
      auto ep = Candidate(candidate);
      if (!endpoints.insert(ep.ToString()).second ||
          !types.insert(candidate.at("type")).second)
        return false;
    }
    impl_->remote_key = std::move(remote_key);
    impl_->remote_candidates = j.at("candidates");
    impl_->DeriveKey();
    impl_->remote_description = description;
    impl_->gathering.remote_description_set = true;
    impl_->gathering.remote.host = types.contains("host");
    impl_->gathering.remote.srflx = types.contains("srflx");
    impl_->gathering.remote.ipv4 = endpoints.size();
    return true;
  } catch (...) {
    return false;
  }
}
bool TcpDirectAgent::Send(std::string_view record) {
  std::lock_guard lock(impl_->mutex);
  if (impl_->state != IceState::connected || record.empty() ||
      record.size() > kRecordLimit ||
      impl_->queued_bytes + record.size() + 4 > kQueueLimit)
    return false;
  auto packet = Framed(std::span(
      reinterpret_cast<const std::uint8_t *>(record.data()), record.size()));
  impl_->queued_bytes += packet.size();
  impl_->outgoing.push_back(std::move(packet));
  return true;
}
IceSelectedPath TcpDirectAgent::SelectedPath() const {
  std::lock_guard lock(impl_->mutex);
  return impl_->selected;
}
std::vector<IceEvent> TcpDirectAgent::TakeEvents() {
  std::lock_guard lock(impl_->mutex);
  std::vector<IceEvent> events;
  while (!impl_->events.empty()) {
    events.push_back(std::move(impl_->events.front()));
    impl_->events.pop_front();
  }
  impl_->incoming_bytes = 0;
  return events;
}
IceGatherStats TcpDirectAgent::GatherStatistics() const {
  std::lock_guard lock(impl_->mutex);
  return impl_->gathering;
}
IceQueueStats TcpDirectAgent::QueueStatistics() const {
  std::lock_guard lock(impl_->mutex);
  return impl_->queues;
}
IceCaptureEndpoints TcpDirectAgent::CaptureEndpoints() const {
  std::lock_guard lock(impl_->mutex);
  IceCaptureEndpoints result;
  for (const auto &j : impl_->local_candidates) {
    auto ep = Candidate(j);
    result.local.push_back({ep.address(), j.at("type"), ep.port()});
  }
  for (const auto &j : impl_->remote_candidates) {
    auto ep = Candidate(j);
    result.remote.push_back({ep.address(), j.at("type"), ep.port()});
  }
  return result;
}
NetworkPathStatus TcpDirectAgent::NetworkPathStatistics() const {
  std::lock_guard lock(impl_->mutex);
  return {impl_->config.network_path.selected(),
          impl_->stats.phase == "failed" ? "failed" : "bound", "ipv4",
          NetworkPathBindingMethod()};
}
TcpDirectStats TcpDirectAgent::Statistics() const {
  std::lock_guard lock(impl_->mutex);
  return impl_->stats;
}
} // namespace libnet
