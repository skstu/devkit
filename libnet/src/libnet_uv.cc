#include <libnet_uv.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <unordered_set>
#include <utility>

namespace libnet {
namespace {

void Allocate(uv_buf_t *buffer, std::size_t suggested) {
  const std::size_t size = (std::max)(suggested, std::size_t{1});
  buffer->base = new char[size];
  buffer->len = static_cast<unsigned int>(size);
}

std::optional<Endpoint> SocketEndpoint(int (*query)(const void *, sockaddr *,
                                                    int *),
                                       const void *handle) {
  sockaddr_storage storage{};
  int length = sizeof(storage);
  if (query(handle, reinterpret_cast<sockaddr *>(&storage), &length) != 0)
    return std::nullopt;
  return Endpoint::FromSockaddr(reinterpret_cast<const sockaddr *>(&storage),
                                static_cast<std::size_t>(length));
}

int TcpSockName(const void *handle, sockaddr *address, int *length) {
  return uv_tcp_getsockname(
      const_cast<uv_tcp_t *>(static_cast<const uv_tcp_t *>(handle)), address,
      length);
}

int TcpPeerName(const void *handle, sockaddr *address, int *length) {
  return uv_tcp_getpeername(
      const_cast<uv_tcp_t *>(static_cast<const uv_tcp_t *>(handle)), address,
      length);
}

int UdpSockName(const void *handle, sockaddr *address, int *length) {
  return uv_udp_getsockname(
      const_cast<uv_udp_t *>(static_cast<const uv_udp_t *>(handle)), address,
      length);
}

} // namespace

std::optional<Endpoint> Endpoint::Parse(std::string_view address,
                                        std::uint16_t port) {
  if (address.empty())
    return std::nullopt;
  Endpoint endpoint;
  const std::string text(address);
  sockaddr_in address4{};
  if (uv_ip4_addr(text.c_str(), port, &address4) == 0) {
    std::memcpy(&endpoint.storage_, &address4, sizeof(address4));
    endpoint.length_ = sizeof(address4);
    return endpoint;
  }
  sockaddr_in6 address6{};
  // Numeric zone IDs are portable in our stored endpoints. libuv interprets
  // zones as interface names on Unix and as numbers on Windows.
  std::string host = text;
  std::uint32_t scope = 0;
  const auto zone = text.find('%');
  if (zone != std::string::npos) {
    const auto suffix = text.substr(zone + 1);
    if (suffix.empty()) return std::nullopt;
    if (suffix.find_first_not_of("0123456789") == std::string::npos) {
      std::uint64_t value = 0;
      for (const char digit : suffix) {
        value = value * 10 + static_cast<unsigned>(digit - '0');
        if (value > UINT32_MAX) return std::nullopt;
      }
      if (value == 0) return std::nullopt;
      scope = static_cast<std::uint32_t>(value);
      host.resize(zone);
    }
#ifdef _WIN32
    else {
      return std::nullopt; // libuv on Windows accepts numeric interface IDs only.
    }
#endif
  }
  if (uv_ip6_addr(host.c_str(), port, &address6) == 0) {
    if (scope != 0) address6.sin6_scope_id = scope;
    if (zone != std::string::npos && address6.sin6_scope_id == 0)
      return std::nullopt;
    std::memcpy(&endpoint.storage_, &address6, sizeof(address6));
    endpoint.length_ = sizeof(address6);
    return endpoint;
  }
  return std::nullopt;
}

std::optional<Endpoint> Endpoint::FromSockaddr(const sockaddr *address,
                                               std::size_t length) {
  if (address == nullptr)
    return std::nullopt;
  const std::size_t required =
      address->sa_family == AF_INET    ? sizeof(sockaddr_in)
      : address->sa_family == AF_INET6 ? sizeof(sockaddr_in6)
                                       : 0;
  if (required == 0 || length < required)
    return std::nullopt;
  Endpoint endpoint;
  std::memcpy(&endpoint.storage_, address, required);
  endpoint.length_ = static_cast<int>(required);
  return endpoint;
}

AddressFamily Endpoint::family() const {
  return storage_.ss_family == AF_INET6 ? AddressFamily::ipv6
                                        : AddressFamily::ipv4;
}

std::string Endpoint::address() const {
  if (!valid())
    return {};
  char text[INET6_ADDRSTRLEN]{};
  const int status =
      storage_.ss_family == AF_INET
          ? uv_ip4_name(reinterpret_cast<const sockaddr_in *>(&storage_), text,
                        sizeof(text))
          : uv_ip6_name(reinterpret_cast<const sockaddr_in6 *>(&storage_), text,
                        sizeof(text));
  if (status != 0) return {};
  std::string result(text);
  if (storage_.ss_family == AF_INET6) {
    const auto scope = reinterpret_cast<const sockaddr_in6 *>(&storage_)->sin6_scope_id;
    if (scope != 0) result += "%" + std::to_string(scope);
  }
  return result;
}

std::uint16_t Endpoint::port() const {
  if (!valid())
    return 0;
  return storage_.ss_family == AF_INET
             ? ntohs(reinterpret_cast<const sockaddr_in *>(&storage_)->sin_port)
             : ntohs(reinterpret_cast<const sockaddr_in6 *>(&storage_)
                         ->sin6_port);
}

std::string Endpoint::ToString() const {
  const std::string host = address();
  if (host.empty())
    return {};
  return family() == AddressFamily::ipv6
             ? "[" + host + "]:" + std::to_string(port())
             : host + ":" + std::to_string(port());
}

bool Endpoint::IsGlobal() const {
  if (!valid())
    return false;
  if (storage_.ss_family == AF_INET) {
    const auto *value = reinterpret_cast<const sockaddr_in *>(&storage_);
    const std::uint32_t address = ntohl(value->sin_addr.s_addr);
    const std::uint8_t first = static_cast<std::uint8_t>(address >> 24U);
    const std::uint8_t second =
        static_cast<std::uint8_t>((address >> 16U) & 0xffU);
    const std::uint8_t third =
        static_cast<std::uint8_t>((address >> 8U) & 0xffU);
    return first != 0 && first != 10 && first != 127 &&
           !(first == 100 && second >= 64 && second <= 127) &&
           !(first == 169 && second == 254) &&
           !(first == 172 && second >= 16 && second <= 31) &&
           !(first == 192 && second == 168) &&
           !(first == 192 && second == 0 && third == 2) &&
           !(first == 198 && (second == 18 || second == 19)) &&
           !(first == 198 && second == 51 && third == 100) &&
           !(first == 203 && second == 0 && third == 113) && first < 224;
  }
  const auto *value = reinterpret_cast<const sockaddr_in6 *>(&storage_);
  const auto &bytes = value->sin6_addr.s6_addr;
  const bool unspecified = std::all_of(
      bytes, bytes + 16, [](std::uint8_t byte) { return byte == 0; });
  const bool loopback =
      std::all_of(bytes, bytes + 15,
                  [](std::uint8_t byte) { return byte == 0; }) &&
      bytes[15] == 1;
  const bool unique_local = (bytes[0] & 0xfeU) == 0xfcU;
  const bool link_local = bytes[0] == 0xfeU && (bytes[1] & 0xc0U) == 0x80U;
  const bool multicast = bytes[0] == 0xffU;
  const bool documentation = bytes[0] == 0x20U && bytes[1] == 0x01U &&
                             bytes[2] == 0x0dU && bytes[3] == 0xb8U;
  return !unspecified && !loopback && !unique_local && !link_local &&
         !multicast && !documentation && (bytes[0] & 0xe0U) == 0x20U;
}

const sockaddr *Endpoint::sockaddr_ptr() const {
  return valid() ? reinterpret_cast<const sockaddr *>(&storage_) : nullptr;
}

int Endpoint::sockaddr_length() const { return length_; }

struct Resolver::State {
  uv_getaddrinfo_t request{};
  Resolver *owner = nullptr;
  ResolveHandler completed;
  std::string host;
  std::string service;
};

Resolver::~Resolver() {
  if (state_ == nullptr)
    return;
  State *state = state_;
  state_ = nullptr;
  state->owner = nullptr;
  state->completed = {};
  uv_cancel(reinterpret_cast<uv_req_t *>(&state->request));
}

bool Resolver::Resolve(uv_loop_t *loop, std::string host, std::uint16_t port,
                       ResolveHandler completed) {
  if (loop == nullptr || host.empty() || !completed || state_ != nullptr)
    return false;
  auto state = std::make_unique<State>();
  state->owner = this;
  state->completed = std::move(completed);
  state->host = std::move(host);
  state->service = std::to_string(port);
  state->request.data = state.get();
  addrinfo hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;
  const int status =
      uv_getaddrinfo(loop, &state->request, OnResolved, state->host.c_str(),
                     state->service.c_str(), &hints);
  if (status != 0)
    return false;
  state_ = state.release();
  return true;
}

bool Resolver::Cancel() {
  return state_ != nullptr &&
         uv_cancel(reinterpret_cast<uv_req_t *>(&state_->request)) == 0;
}

void Resolver::OnResolved(uv_getaddrinfo_t *request, int status,
                          addrinfo *addresses) {
  std::unique_ptr<State> state(static_cast<State *>(request->data));
  if (state->owner != nullptr && state->owner->state_ == state.get())
    state->owner->state_ = nullptr;
  std::vector<Endpoint> endpoints;
  std::unordered_set<std::string> seen;
  if (status == 0) {
    for (addrinfo *current = addresses; current != nullptr;
         current = current->ai_next) {
      const auto endpoint = Endpoint::FromSockaddr(
          current->ai_addr, static_cast<std::size_t>(current->ai_addrlen));
      if (endpoint && seen.insert(endpoint->ToString()).second)
        endpoints.push_back(*endpoint);
    }
  }
  if (addresses != nullptr)
    uv_freeaddrinfo(addresses);
  ResolveHandler completed = std::move(state->completed);
  state.reset();
  if (completed)
    completed(status, std::move(endpoints));
}

struct TcpClient::State {
  uv_tcp_t handle{};
  uv_connect_t connect{};
  TcpClient *owner = nullptr;
  DataHandler data;
  StatusHandler closed;
  StatusHandler connected;
  StatusHandler close_completed;
  bool connecting = false;
  bool callbacks_enabled = true;
  bool keep_open_on_eof = false;
};

struct TcpClient::Write {
  uv_write_t request{};
  State *state = nullptr;
  std::string data;
  StatusHandler completed;
};

TcpClient::~TcpClient() {
  if (state_ == nullptr)
    return;
  state_->data = {};
  state_->closed = {};
  state_->connected = {};
  state_->callbacks_enabled = false;
  Close();
}

bool TcpClient::Initialize(uv_loop_t *loop) {
  if (loop == nullptr || state_ != nullptr)
    return false;
  auto state = std::make_unique<State>();
  if (uv_tcp_init(loop, &state->handle) != 0)
    return false;
  state->owner = this;
  state->handle.data = state.get();
  state_ = state.release();
  return true;
}

bool TcpClient::Connect(const Endpoint &endpoint, StatusHandler completed) {
  if (state_ == nullptr || !endpoint.valid() || !completed ||
      state_->connecting ||
      uv_is_closing(reinterpret_cast<uv_handle_t *>(&state_->handle)))
    return false;
  state_->connected = std::move(completed);
  state_->connecting = true;
  state_->connect.data = state_;
  const int status = uv_tcp_connect(&state_->connect, &state_->handle,
                                    endpoint.sockaddr_ptr(), OnConnect);
  if (status == 0)
    return true;
  state_->connecting = false;
  state_->connected = {};
  return false;
}

bool TcpClient::Connect(const std::string &address, std::uint16_t port,
                        StatusHandler completed) {
  const auto endpoint = Endpoint::Parse(address, port);
  return endpoint && Connect(*endpoint, std::move(completed));
}

bool TcpClient::Accept(uv_stream_t *server) {
  return state_ != nullptr && server != nullptr &&
         !uv_is_closing(reinterpret_cast<uv_handle_t *>(&state_->handle)) &&
         uv_accept(server, reinterpret_cast<uv_stream_t *>(&state_->handle)) ==
             0;
}

bool TcpClient::StartRead(DataHandler data, StatusHandler closed, bool keep_open_on_eof) {
  if (state_ == nullptr || !data ||
      uv_is_closing(reinterpret_cast<uv_handle_t *>(&state_->handle)))
    return false;
  state_->data = std::move(data);
  state_->closed = std::move(closed);
  state_->keep_open_on_eof = keep_open_on_eof;
  return uv_read_start(reinterpret_cast<uv_stream_t *>(&state_->handle),
                       OnAlloc, OnRead) == 0;
}

bool TcpClient::Send(std::string_view data, StatusHandler completed) {
  if (state_ == nullptr ||
      uv_is_closing(reinterpret_cast<uv_handle_t *>(&state_->handle)))
    return false;
  auto write = std::make_unique<Write>();
  write->state = state_;
  write->data.assign(data);
  write->completed = std::move(completed);
  write->request.data = write.get();
  uv_buf_t buffer = uv_buf_init(write->data.data(),
                                static_cast<unsigned int>(write->data.size()));
  const int status = uv_write(&write->request,
                              reinterpret_cast<uv_stream_t *>(&state_->handle),
                              &buffer, 1, OnWrite);
  if (status != 0)
    return false;
  write.release();
  return true;
}

bool TcpClient::Close(StatusHandler completed) {
  if (state_ == nullptr)
    return false;
  State *state = state_;
  state_ = nullptr;
  state->owner = nullptr;
  state->close_completed = std::move(completed);
  if (!uv_is_closing(reinterpret_cast<uv_handle_t *>(&state->handle)))
    uv_close(reinterpret_cast<uv_handle_t *>(&state->handle), OnClosed);
  return true;
}

std::optional<Endpoint> TcpClient::local_endpoint() const {
  return state_ == nullptr ? std::nullopt
                           : SocketEndpoint(TcpSockName, &state_->handle);
}

std::optional<Endpoint> TcpClient::peer_endpoint() const {
  return state_ == nullptr ? std::nullopt
                           : SocketEndpoint(TcpPeerName, &state_->handle);
}

uv_tcp_t *TcpClient::native_handle() {
  return state_ == nullptr ? nullptr : &state_->handle;
}

void TcpClient::OnConnect(uv_connect_t *request, int status) {
  auto *state = static_cast<State *>(request->data);
  state->connecting = false;
  StatusHandler completed = std::move(state->connected);
  if (completed)
    completed(status);
}

void TcpClient::OnAlloc(uv_handle_t *, std::size_t suggested,
                        uv_buf_t *buffer) {
  Allocate(buffer, suggested);
}

void TcpClient::OnRead(uv_stream_t *stream, ssize_t count,
                       const uv_buf_t *buffer) {
  auto *state = static_cast<State *>(stream->data);
  if (count > 0 && state->data)
    state->data(
        std::string_view(buffer->base, static_cast<std::size_t>(count)));
  delete[] buffer->base;
  if (count < 0) {
    StatusHandler closed = std::move(state->closed);
    if (count == UV_EOF && state->keep_open_on_eof)
      uv_read_stop(stream);
    else if (!uv_is_closing(reinterpret_cast<uv_handle_t *>(stream)))
      uv_close(reinterpret_cast<uv_handle_t *>(stream), OnClosed);
    if (closed)
      closed(static_cast<int>(count));
  }
}

void TcpClient::OnWrite(uv_write_t *request, int status) {
  std::unique_ptr<Write> write(static_cast<Write *>(request->data));
  if (write->state->callbacks_enabled && write->completed)
    write->completed(status);
}

void TcpClient::OnClosed(uv_handle_t *handle) {
  std::unique_ptr<State> state(static_cast<State *>(handle->data));
  if (state->owner != nullptr && state->owner->state_ == state.get())
    state->owner->state_ = nullptr;
  StatusHandler completed = std::move(state->close_completed);
  state.reset();
  if (completed)
    completed(0);
}

struct TcpServer::State {
  uv_tcp_t handle{};
  TcpServer *owner = nullptr;
  StatusHandler connection;
  StatusHandler close_completed;
};

TcpServer::~TcpServer() {
  if (state_ != nullptr) {
    state_->connection = {};
    Close();
  }
}

bool TcpServer::Listen(uv_loop_t *loop, const Endpoint &endpoint,
                       StatusHandler connection, int backlog) {
  if (loop == nullptr || !endpoint.valid() || !connection || backlog <= 0 ||
      state_ != nullptr)
    return false;
  auto state = std::make_unique<State>();
  if (uv_tcp_init(loop, &state->handle) != 0)
    return false;
  state->owner = this;
  state->connection = std::move(connection);
  state->handle.data = state.get();
  state_ = state.release();
  const unsigned bind_flags =
      endpoint.family() == AddressFamily::ipv6 ? UV_TCP_IPV6ONLY : 0;
  if (uv_tcp_bind(&state_->handle, endpoint.sockaddr_ptr(), bind_flags) != 0 ||
      uv_listen(reinterpret_cast<uv_stream_t *>(&state_->handle), backlog,
                OnConnection) != 0) {
    Close();
    return false;
  }
  return true;
}

bool TcpServer::Listen(uv_loop_t *loop, const std::string &address,
                       std::uint16_t port, StatusHandler connection,
                       int backlog) {
  const auto endpoint =
      Endpoint::Parse(address.empty() ? "0.0.0.0" : address, port);
  return endpoint && Listen(loop, *endpoint, std::move(connection), backlog);
}

std::optional<Endpoint> TcpServer::local_endpoint() const {
  return state_ == nullptr ? std::nullopt
                           : SocketEndpoint(TcpSockName, &state_->handle);
}

std::uint16_t TcpServer::Port() const {
  const auto endpoint = local_endpoint();
  return endpoint ? endpoint->port() : 0;
}

uv_stream_t *TcpServer::native_handle() {
  return state_ == nullptr ? nullptr
                           : reinterpret_cast<uv_stream_t *>(&state_->handle);
}

bool TcpServer::Close(StatusHandler completed) {
  if (state_ == nullptr)
    return false;
  State *state = state_;
  state_ = nullptr;
  state->owner = nullptr;
  state->close_completed = std::move(completed);
  if (!uv_is_closing(reinterpret_cast<uv_handle_t *>(&state->handle)))
    uv_close(reinterpret_cast<uv_handle_t *>(&state->handle), OnClosed);
  return true;
}

void TcpServer::OnConnection(uv_stream_t *server, int status) {
  auto *state = static_cast<State *>(server->data);
  if (state->connection)
    state->connection(status);
}

void TcpServer::OnClosed(uv_handle_t *handle) {
  std::unique_ptr<State> state(static_cast<State *>(handle->data));
  if (state->owner != nullptr && state->owner->state_ == state.get())
    state->owner->state_ = nullptr;
  StatusHandler completed = std::move(state->close_completed);
  state.reset();
  if (completed)
    completed(0);
}

struct UdpSocket::State {
  uv_udp_t handle{};
  UdpSocket *owner = nullptr;
  Endpoint bound_endpoint;
  unsigned bind_flags = 0;
  bool receiving = false;
  bool rebind = false;
  DatagramHandler data;
  StatusHandler error;
  StatusHandler close_completed;
  bool callbacks_enabled = true;
};

struct UdpSocket::SendRequest {
  uv_udp_send_t request{};
  State *state = nullptr;
  std::string data;
  StatusHandler completed;
};

UdpSocket::~UdpSocket() {
  if (state_ != nullptr) {
    state_->data = {};
    state_->error = {};
    state_->callbacks_enabled = false;
    Close();
  }
}

bool UdpSocket::Bind(uv_loop_t *loop, const Endpoint &endpoint,
                     unsigned flags) {
  if (loop == nullptr || !endpoint.valid() || state_ != nullptr)
    return false;
  auto state = std::make_unique<State>();
  if (uv_udp_init(loop, &state->handle) != 0)
    return false;
  state->owner = this;
  state->handle.data = state.get();
  state_ = state.release();
  if (uv_udp_bind(&state_->handle, endpoint.sockaddr_ptr(), flags) != 0) {
    Close();
    return false;
  }
  state_->bound_endpoint = local_endpoint().value_or(endpoint);
  state_->bind_flags = flags;
  return true;
}

bool UdpSocket::Bind(uv_loop_t *loop, const std::string &address,
                     std::uint16_t port, unsigned flags) {
  const auto endpoint =
      Endpoint::Parse(address.empty() ? "0.0.0.0" : address, port);
  return endpoint && Bind(loop, *endpoint, flags);
}

bool UdpSocket::StartReceive(DatagramHandler data, StatusHandler error) {
  if (state_ == nullptr || !data ||
      uv_is_closing(reinterpret_cast<uv_handle_t *>(&state_->handle)))
    return false;
  state_->data = std::move(data);
  state_->error = std::move(error);
  const int status = uv_udp_recv_start(&state_->handle, OnAlloc, OnReceive);
  if (status == 0) state_->receiving = true;
  return status == 0;
}

bool UdpSocket::StartReceive(DataHandler data, StatusHandler error) {
  if (!data)
    return false;
  return StartReceive([handler = std::move(data)](std::string_view bytes,
                                                  const Endpoint &,
                                                  unsigned) { handler(bytes); },
                      std::move(error));
}

bool UdpSocket::StopReceive() {
  if (state_ == nullptr || uv_udp_recv_stop(&state_->handle) != 0)
    return false;
  state_->receiving = false;
  return true;
}

bool UdpSocket::Send(const Endpoint &endpoint, std::string_view data,
                     StatusHandler completed) {
  if (state_ == nullptr || !endpoint.valid() ||
      uv_is_closing(reinterpret_cast<uv_handle_t *>(&state_->handle)))
    return false;
  auto request = std::make_unique<SendRequest>();
  request->state = state_;
  request->data.assign(data);
  request->completed = std::move(completed);
  request->request.data = request.get();
  uv_buf_t buffer = uv_buf_init(
      request->data.data(), static_cast<unsigned int>(request->data.size()));
  const int status = uv_udp_send(&request->request, &state_->handle, &buffer, 1,
                                 endpoint.sockaddr_ptr(), OnSend);
  if (status != 0)
    return false;
  request.release();
  return true;
}

bool UdpSocket::Send(const std::string &address, std::uint16_t port,
                     std::string_view data, StatusHandler completed) {
  const auto endpoint = Endpoint::Parse(address, port);
  return endpoint && Send(*endpoint, data, std::move(completed));
}

std::optional<Endpoint> UdpSocket::local_endpoint() const {
  return state_ == nullptr ? std::nullopt
                           : SocketEndpoint(UdpSockName, &state_->handle);
}

uv_udp_t *UdpSocket::native_handle() {
  return state_ == nullptr ? nullptr : &state_->handle;
}

bool UdpSocket::Rebind(StatusHandler completed) {
  if (state_ == nullptr ||
      uv_is_closing(reinterpret_cast<uv_handle_t *>(&state_->handle)))
    return false;
  state_->rebind = true;
  state_->close_completed = std::move(completed);
  uv_close(reinterpret_cast<uv_handle_t *>(&state_->handle), OnClosed);
  return true;
}

bool UdpSocket::Close(StatusHandler completed) {
  if (state_ == nullptr)
    return false;
  State *state = state_;
  state_ = nullptr;
  state->owner = nullptr;
  state->rebind = false;
  state->close_completed = std::move(completed);
  if (!uv_is_closing(reinterpret_cast<uv_handle_t *>(&state->handle)))
    uv_close(reinterpret_cast<uv_handle_t *>(&state->handle), OnClosed);
  return true;
}

void UdpSocket::OnAlloc(uv_handle_t *, std::size_t suggested,
                        uv_buf_t *buffer) {
  Allocate(buffer, suggested);
}

void UdpSocket::OnReceive(uv_udp_t *socket, ssize_t count,
                          const uv_buf_t *buffer, const sockaddr *source,
                          unsigned flags) {
  auto *state = static_cast<State *>(socket->data);
  if (count >= 0 && source != nullptr && state->data) {
    const std::size_t source_length = source->sa_family == AF_INET
                                          ? sizeof(sockaddr_in)
                                          : sizeof(sockaddr_in6);
    const auto endpoint = Endpoint::FromSockaddr(source, source_length);
    if (endpoint)
      state->data(
          std::string_view(buffer->base, static_cast<std::size_t>(count)),
          *endpoint, flags);
  } else if (count < 0 && state->error) {
    state->error(static_cast<int>(count));
  }
  delete[] buffer->base;
}

void UdpSocket::OnSend(uv_udp_send_t *request, int status) {
  std::unique_ptr<SendRequest> send(static_cast<SendRequest *>(request->data));
  if (send->state->callbacks_enabled && send->completed)
    send->completed(status);
}

void UdpSocket::OnClosed(uv_handle_t *handle) {
  std::unique_ptr<State> state(static_cast<State *>(handle->data));
  auto *owner = state->owner;
  if (owner != nullptr && owner->state_ == state.get()) owner->state_ = nullptr;
  const bool rebind = state->rebind;
  const bool receiving = state->receiving;
  const auto endpoint = state->bound_endpoint;
  const auto flags = state->bind_flags;
  auto *loop = handle->loop;
  auto data = std::move(state->data);
  auto error = std::move(state->error);
  StatusHandler completed = std::move(state->close_completed);
  if (!state->callbacks_enabled) completed = {};
  state.reset();
  int status = 0;
  if (rebind) {
    if (owner == nullptr) {
      status = UV_ECANCELED;
    } else if (!owner->Bind(loop, endpoint, flags) ||
               (receiving && !owner->StartReceive(std::move(data), std::move(error)))) {
      owner->Close();
      status = UV_EIO;
    }
  }
  if (completed)
    completed(status);
}

struct PipeServer::State {
  uv_pipe_t handle{};
  PipeServer *owner = nullptr;
  StatusHandler connection;
  StatusHandler close_completed;
};

PipeServer::~PipeServer() {
  if (state_ != nullptr) {
    state_->connection = {};
    Close();
  }
}

bool PipeServer::Listen(uv_loop_t *loop, const std::string &name,
                        StatusHandler connection, int backlog) {
  if (loop == nullptr || name.empty() || !connection || backlog <= 0 ||
      state_ != nullptr)
    return false;
  auto state = std::make_unique<State>();
  if (uv_pipe_init(loop, &state->handle, 0) != 0)
    return false;
  state->owner = this;
  state->connection = std::move(connection);
  state->handle.data = state.get();
  state_ = state.release();
  if (uv_pipe_bind(&state_->handle, name.c_str()) != 0 ||
      uv_listen(reinterpret_cast<uv_stream_t *>(&state_->handle), backlog,
                OnConnection) != 0) {
    Close();
    return false;
  }
  return true;
}

bool PipeServer::Accept(uv_pipe_t &client) {
  return state_ != nullptr &&
         uv_accept(reinterpret_cast<uv_stream_t *>(&state_->handle),
                   reinterpret_cast<uv_stream_t *>(&client)) == 0;
}

bool PipeServer::Close(StatusHandler completed) {
  if (state_ == nullptr)
    return false;
  State *state = state_;
  state_ = nullptr;
  state->owner = nullptr;
  state->close_completed = std::move(completed);
  if (!uv_is_closing(reinterpret_cast<uv_handle_t *>(&state->handle)))
    uv_close(reinterpret_cast<uv_handle_t *>(&state->handle), OnClosed);
  return true;
}

void PipeServer::OnConnection(uv_stream_t *server, int status) {
  auto *state = static_cast<State *>(server->data);
  if (state->connection)
    state->connection(status);
}

void PipeServer::OnClosed(uv_handle_t *handle) {
  std::unique_ptr<State> state(static_cast<State *>(handle->data));
  if (state->owner != nullptr && state->owner->state_ == state.get())
    state->owner->state_ = nullptr;
  StatusHandler completed = std::move(state->close_completed);
  state.reset();
  if (completed)
    completed(0);
}

struct PipeClient::State {
  uv_pipe_t handle{};
  uv_connect_t connect{};
  PipeClient *owner = nullptr;
  DataHandler data;
  StatusHandler closed;
  StatusHandler connected;
  StatusHandler close_completed;
  bool connecting = false;
  bool callbacks_enabled = true;
};

struct PipeClient::Write {
  uv_write_t request{};
  State *state = nullptr;
  std::string data;
  StatusHandler completed;
};

PipeClient::~PipeClient() {
  if (state_ == nullptr)
    return;
  state_->data = {};
  state_->closed = {};
  state_->connected = {};
  state_->callbacks_enabled = false;
  Close();
}

bool PipeClient::Initialize(uv_loop_t *loop) {
  if (loop == nullptr || state_ != nullptr)
    return false;
  auto state = std::make_unique<State>();
  if (uv_pipe_init(loop, &state->handle, 0) != 0)
    return false;
  state->owner = this;
  state->handle.data = state.get();
  state_ = state.release();
  return true;
}

bool PipeClient::Connect(const std::string &name, StatusHandler completed) {
  if (state_ == nullptr || name.empty() || !completed || state_->connecting ||
      uv_is_closing(reinterpret_cast<uv_handle_t *>(&state_->handle)))
    return false;
  state_->connected = std::move(completed);
  state_->connecting = true;
  state_->connect.data = state_;
  uv_pipe_connect(&state_->connect, &state_->handle, name.c_str(), OnConnect);
  return true;
}

bool PipeClient::StartRead(DataHandler data, StatusHandler closed) {
  if (state_ == nullptr || !data ||
      uv_is_closing(reinterpret_cast<uv_handle_t *>(&state_->handle)))
    return false;
  state_->data = std::move(data);
  state_->closed = std::move(closed);
  return uv_read_start(reinterpret_cast<uv_stream_t *>(&state_->handle),
                       OnAlloc, OnRead) == 0;
}

bool PipeClient::Send(std::string_view data, StatusHandler completed) {
  if (state_ == nullptr ||
      uv_is_closing(reinterpret_cast<uv_handle_t *>(&state_->handle)))
    return false;
  auto write = std::make_unique<Write>();
  write->state = state_;
  write->data.assign(data);
  write->completed = std::move(completed);
  write->request.data = write.get();
  uv_buf_t buffer = uv_buf_init(write->data.data(),
                                static_cast<unsigned int>(write->data.size()));
  const int status = uv_write(&write->request,
                              reinterpret_cast<uv_stream_t *>(&state_->handle),
                              &buffer, 1, OnWrite);
  if (status != 0)
    return false;
  write.release();
  return true;
}

bool PipeClient::Close(StatusHandler completed) {
  if (state_ == nullptr)
    return false;
  State *state = state_;
  state_ = nullptr;
  state->owner = nullptr;
  state->close_completed = std::move(completed);
  if (!uv_is_closing(reinterpret_cast<uv_handle_t *>(&state->handle)))
    uv_close(reinterpret_cast<uv_handle_t *>(&state->handle), OnClosed);
  return true;
}

void PipeClient::OnConnect(uv_connect_t *request, int status) {
  auto *state = static_cast<State *>(request->data);
  state->connecting = false;
  StatusHandler completed = std::move(state->connected);
  if (completed)
    completed(status);
}

void PipeClient::OnAlloc(uv_handle_t *, std::size_t suggested,
                         uv_buf_t *buffer) {
  Allocate(buffer, suggested);
}

void PipeClient::OnRead(uv_stream_t *stream, ssize_t count,
                        const uv_buf_t *buffer) {
  auto *state = static_cast<State *>(stream->data);
  if (count > 0 && state->data)
    state->data(
        std::string_view(buffer->base, static_cast<std::size_t>(count)));
  delete[] buffer->base;
  if (count < 0) {
    StatusHandler closed = std::move(state->closed);
    if (!uv_is_closing(reinterpret_cast<uv_handle_t *>(stream)))
      uv_close(reinterpret_cast<uv_handle_t *>(stream), OnClosed);
    if (closed)
      closed(static_cast<int>(count));
  }
}

void PipeClient::OnWrite(uv_write_t *request, int status) {
  std::unique_ptr<Write> write(static_cast<Write *>(request->data));
  if (write->state->callbacks_enabled && write->completed)
    write->completed(status);
}

void PipeClient::OnClosed(uv_handle_t *handle) {
  std::unique_ptr<State> state(static_cast<State *>(handle->data));
  if (state->owner != nullptr && state->owner->state_ == state.get())
    state->owner->state_ = nullptr;
  StatusHandler completed = std::move(state->close_completed);
  state.reset();
  if (completed)
    completed(0);
}

} // namespace libnet
