#include "libnet_assistance_node.h"

#include "libnet_uv.h"
#include <juice/juice.h>
#include <algorithm>
#include <cctype>

namespace libnet {

AssistanceNode::~AssistanceNode() { Stop(); }

bool AssistanceNode::Start(const Config &config) {
  std::lock_guard lock(mutex_);
  if (server_ || config.byte_limit < 65536 || config.byte_limit > (1ULL << 40) ||
      config.bytes_per_second < 8192 || config.bytes_per_second > (1ULL << 30) ||
      !ValidateNetworkPath(config.network_path) ||
      !Endpoint::Parse(config.external_address, 0) ||
      (!config.bind_address.empty() && !Endpoint::Parse(config.bind_address, 0)))
    return false;
  config_ = config;
  charged_ = 0;
  dropped_ = 0;
  tokens_ = static_cast<double>(config.bytes_per_second);
  refill_ = std::chrono::steady_clock::now();
  juice_server_config_t options{};
  options.bind_address = config.bind_address.empty() ? nullptr : config.bind_address.c_str();
  options.external_address = config.external_address.c_str();
  options.port = config.port;
  options.realm = "sovkit-assistance-v1";
  options.max_allocations = 16;
  options.max_peers = 2;
  options.sovkit_paired_only = true;
  options.sovkit_relay_budget = &Charge;
  options.sovkit_user_ptr = this;
  options.sovkit_prepare_socket = config.network_path.selected() ? &Prepare : nullptr;
  // libjuice starts with no credentials. The first short-term grant enables
  // TURN; there is deliberately no permanent or shared default password.
  server_ = juice_server_create(&options);
  if (!server_) return false;
  port_ = juice_server_get_port(server_);
  grants_.clear();
  return true;
}

void AssistanceNode::Stop() {
  std::lock_guard lock(mutex_);
  if (server_) juice_server_destroy(server_);
  server_ = nullptr;
  port_ = 0;
  grants_.clear();
}

bool AssistanceNode::Grant(const std::string &username, const std::string &password,
                           std::uint32_t lifetime_seconds) {
  std::lock_guard lock(mutex_);
  if (!server_ || username.size() < 32 || username.size() > 96 ||
      password.size() < 32 || password.size() > 128 || !lifetime_seconds ||
      lifetime_seconds > 3600 || charged_ >= config_.byte_limit)
    return false;
  for (const auto *value : {&username, &password})
    if (!std::all_of(value->begin(), value->end(), [](unsigned char c) {
          return std::isalnum(c) || c == '-' || c == '_';
        })) return false;
  const auto now = std::chrono::steady_clock::now();
  std::erase_if(grants_, [now](const auto &entry) { return entry.second <= now; });
  // Retransmission does not renew a credential or create a second quota.
  if (grants_.contains(username)) return true;
  if (grants_.size() >= 8) return false;
  juice_server_credentials_t credentials{username.c_str(), password.c_str(), 2};
  if (juice_server_add_credentials(server_, &credentials, lifetime_seconds * 1000UL) != 0)
    return false;
  grants_.emplace(username, now + std::chrono::seconds(lifetime_seconds));
  return true;
}

AssistanceNode::Stats AssistanceNode::Snapshot() const {
  std::lock_guard lock(mutex_);
  return {server_ != nullptr, port_, charged_.load(), dropped_.load(),
          charged_ >= config_.byte_limit, grants_.size()};
}

void AssistanceNode::Revoke(const std::string &username) {
  std::lock_guard lock(mutex_);
  if (server_) juice_server_remove_credentials(server_, username.c_str());
  grants_.erase(username);
}

bool AssistanceNode::Charge(std::size_t bytes, void *self) {
  auto &node = *static_cast<AssistanceNode *>(self);
  if (!node.allowed_) return false;
  if (node.config_.can_forward && !node.config_.can_forward()) return false;
  const auto now = std::chrono::steady_clock::now();
  node.tokens_ = std::min(static_cast<double>(node.config_.bytes_per_second),
      node.tokens_ + std::chrono::duration<double>(now - node.refill_).count() *
                         static_cast<double>(node.config_.bytes_per_second));
  node.refill_ = now;
  const auto used = node.charged_.load();
  if (used >= node.config_.byte_limit || bytes > node.config_.byte_limit - used) {
    // Latch exhaustion even when the last packet cannot fit in the remainder.
    node.charged_ = node.config_.byte_limit;
    ++node.dropped_;
    return false;
  }
  if (static_cast<double>(bytes) > node.tokens_) {
    ++node.dropped_;
    return false;
  }
  node.tokens_ -= static_cast<double>(bytes);
  node.charged_ += bytes;
  return true;
}

int AssistanceNode::Prepare(std::uintptr_t socket, int family, void *self) {
  return PrepareNetworkPathSocket(static_cast<AssistanceNode *>(self)->config_.network_path,
                                  socket, family) ? 0 : -1;
}

} // namespace libnet
