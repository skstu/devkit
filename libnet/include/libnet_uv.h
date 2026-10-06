#ifndef LIBNET_UV_H_
#define LIBNET_UV_H_

#include <uv.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace libnet {

using StatusHandler = std::function<void(int)>;
using DataHandler = std::function<void(std::string_view)>;

enum class AddressFamily : std::uint8_t { ipv4 = 4, ipv6 = 6 };

class Endpoint final {
public:
  Endpoint() = default;

  static std::optional<Endpoint> Parse(std::string_view address,
                                       std::uint16_t port);
  static std::optional<Endpoint> FromSockaddr(const sockaddr *address,
                                              std::size_t length);

  bool valid() const { return length_ != 0; }
  AddressFamily family() const;
  std::string address() const;
  std::uint16_t port() const;
  std::string ToString() const;
  /// True only for an address that is globally routable by address class.
  /// This is a diagnostic/routing hint, never an authorization decision.
  bool IsGlobal() const;
  const sockaddr *sockaddr_ptr() const;
  int sockaddr_length() const;

private:
  sockaddr_storage storage_{};
  int length_ = 0;
};

using ResolveHandler =
    std::function<void(int status, std::vector<Endpoint> endpoints)>;

/// One-shot asynchronous DNS resolver. Cancellation completes through the
/// normal callback with UV_ECANCELED. All methods are loop-thread-affine.
class Resolver final {
public:
  Resolver() = default;
  ~Resolver();
  Resolver(const Resolver &) = delete;
  Resolver &operator=(const Resolver &) = delete;

  bool Resolve(uv_loop_t *loop, std::string host, std::uint16_t port,
               ResolveHandler completed);
  bool Cancel();
  bool pending() const { return state_ != nullptr; }

private:
  struct State;
  static void OnResolved(uv_getaddrinfo_t *request, int status,
                         addrinfo *addresses);
  State *state_ = nullptr;
};

/// TCP client attached to a caller-owned libuv loop. Operations, Close, and
/// destruction are loop-thread-affine. Native handle storage remains alive
/// until libuv delivers its close callback.
class TcpClient final {
public:
  TcpClient() = default;
  ~TcpClient();
  TcpClient(const TcpClient &) = delete;
  TcpClient &operator=(const TcpClient &) = delete;

  bool Initialize(uv_loop_t *loop);
  bool Connect(const Endpoint &endpoint, StatusHandler completed);
  bool Connect(const std::string &address, std::uint16_t port,
               StatusHandler completed);
  bool Accept(uv_stream_t *server);
  // keep_open_on_eof permits a TCP half-close (writes remain possible).
  bool StartRead(DataHandler data, StatusHandler closed = {}, bool keep_open_on_eof = false);
  bool Send(std::string_view data, StatusHandler completed = {});
  bool Close(StatusHandler completed = {});
  bool initialized() const { return state_ != nullptr; }
  std::optional<Endpoint> local_endpoint() const;
  std::optional<Endpoint> peer_endpoint() const;
  uv_tcp_t *native_handle();

private:
  struct State;
  struct Write;
  static void OnConnect(uv_connect_t *request, int status);
  static void OnAlloc(uv_handle_t *handle, std::size_t suggested,
                      uv_buf_t *buffer);
  static void OnRead(uv_stream_t *stream, ssize_t count,
                     const uv_buf_t *buffer);
  static void OnWrite(uv_write_t *request, int status);
  static void OnClosed(uv_handle_t *handle);
  State *state_ = nullptr;
};

class TcpServer final {
public:
  TcpServer() = default;
  ~TcpServer();
  TcpServer(const TcpServer &) = delete;
  TcpServer &operator=(const TcpServer &) = delete;

  bool Listen(uv_loop_t *loop, const Endpoint &endpoint,
              StatusHandler connection, int backlog = 128);
  bool Listen(uv_loop_t *loop, const std::string &address, std::uint16_t port,
              StatusHandler connection, int backlog = 128);
  std::optional<Endpoint> local_endpoint() const;
  std::uint16_t Port() const;
  uv_stream_t *native_handle();
  bool Close(StatusHandler completed = {});
  bool initialized() const { return state_ != nullptr; }

private:
  struct State;
  static void OnConnection(uv_stream_t *server, int status);
  static void OnClosed(uv_handle_t *handle);
  State *state_ = nullptr;
};

using DatagramHandler = std::function<void(
    std::string_view data, const Endpoint &source, unsigned flags)>;

class UdpSocket final {
public:
  UdpSocket() = default;
  ~UdpSocket();
  UdpSocket(const UdpSocket &) = delete;
  UdpSocket &operator=(const UdpSocket &) = delete;

  bool Bind(uv_loop_t *loop, const Endpoint &endpoint, unsigned flags = 0);
  bool Bind(uv_loop_t *loop, const std::string &address, std::uint16_t port,
            unsigned flags = 0);
  bool StartReceive(DatagramHandler data, StatusHandler error = {});
  bool StartReceive(DataHandler data, StatusHandler error = {});
  bool StopReceive();
  bool Send(const Endpoint &endpoint, std::string_view data,
            StatusHandler completed = {});
  bool Send(const std::string &address, std::uint16_t port,
            std::string_view data, StatusHandler completed = {});
  std::optional<Endpoint> local_endpoint() const;
  uv_udp_t *native_handle();
  // Loop-thread only. Replace the descriptor after uv_close completes, keeping
  // its bound endpoint and receive callbacks. Close() cancels a pending rebind.
  // Multicast memberships/options must be reapplied by the completion handler.
  bool Rebind(StatusHandler completed = {});
  bool Close(StatusHandler completed = {});
  bool initialized() const { return state_ != nullptr; }

private:
  struct State;
  struct SendRequest;
  static void OnAlloc(uv_handle_t *handle, std::size_t suggested,
                      uv_buf_t *buffer);
  static void OnReceive(uv_udp_t *socket, ssize_t count, const uv_buf_t *buffer,
                        const sockaddr *source, unsigned flags);
  static void OnSend(uv_udp_send_t *request, int status);
  static void OnClosed(uv_handle_t *handle);
  State *state_ = nullptr;
};

// libuv pipes map to named pipes on Windows and Unix-domain pipes on POSIX.
class PipeServer final {
public:
  PipeServer() = default;
  ~PipeServer();
  PipeServer(const PipeServer &) = delete;
  PipeServer &operator=(const PipeServer &) = delete;

  bool Listen(uv_loop_t *loop, const std::string &name,
              StatusHandler connection, int backlog = 128);
  bool Accept(uv_pipe_t &client);
  bool Close(StatusHandler completed = {});

private:
  struct State;
  static void OnConnection(uv_stream_t *server, int status);
  static void OnClosed(uv_handle_t *handle);
  State *state_ = nullptr;
};

class PipeClient final {
public:
  PipeClient() = default;
  ~PipeClient();
  PipeClient(const PipeClient &) = delete;
  PipeClient &operator=(const PipeClient &) = delete;

  bool Initialize(uv_loop_t *loop);
  bool Connect(const std::string &name, StatusHandler completed);
  bool StartRead(DataHandler data, StatusHandler closed = {});
  bool Send(std::string_view data, StatusHandler completed = {});
  bool Close(StatusHandler completed = {});

private:
  struct State;
  struct Write;
  static void OnConnect(uv_connect_t *request, int status);
  static void OnAlloc(uv_handle_t *handle, std::size_t suggested,
                      uv_buf_t *buffer);
  static void OnRead(uv_stream_t *stream, ssize_t count,
                     const uv_buf_t *buffer);
  static void OnWrite(uv_write_t *request, int status);
  static void OnClosed(uv_handle_t *handle);
  State *state_ = nullptr;
};

} // namespace libnet

#endif // LIBNET_UV_H_
