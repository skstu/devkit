#include "libnet_quic.h"

#include "libnet_uv.h"

#include <ngtcp2/ngtcp2.h>
#include <ngtcp2/ngtcp2_crypto.h>
#include <ngtcp2/ngtcp2_crypto_ossl.h>

#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace libnet {
namespace {

constexpr std::array<unsigned char, 9> kAlpn = {8,   's', 'o', 'v', 'k',
                                                'i', 't', '/', '1'};
constexpr std::size_t kPacketBytes = 1350;
constexpr std::size_t kMaximumTxPacketBytes = 1200;
constexpr std::size_t kMaximumProviderRecordBytes = 64 * 1024 + 20;

std::mutex g_crypto_mutex;
std::size_t g_crypto_users = 0;

bool AcquireCryptoRuntime() {
  std::lock_guard<std::mutex> lock(g_crypto_mutex);
  if (g_crypto_users == 0 && ngtcp2_crypto_ossl_init() != 0)
    return false;
  ++g_crypto_users;
  return true;
}

void ReleaseCryptoRuntime() {
  std::lock_guard<std::mutex> lock(g_crypto_mutex);
  if (g_crypto_users != 0 && --g_crypto_users == 0)
    ngtcp2_crypto_ossl_free();
}

using SslContext = std::unique_ptr<SSL_CTX, decltype(&SSL_CTX_free)>;
using PrivateKey = std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)>;
using Certificate = std::unique_ptr<X509, decltype(&X509_free)>;

ngtcp2_tstamp Timestamp() {
  return static_cast<ngtcp2_tstamp>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count());
}

bool Random(std::span<std::uint8_t> output) {
  return output.empty() ||
         RAND_bytes(output.data(), static_cast<int>(output.size())) == 1;
}

bool InstallEphemeralCertificate(SSL_CTX *context) {
  PrivateKey key(EVP_PKEY_Q_keygen(nullptr, nullptr, "ED25519"), EVP_PKEY_free);
  Certificate certificate(X509_new(), X509_free);
  if (!key || !certificate || X509_set_version(certificate.get(), 2) != 1 ||
      ASN1_INTEGER_set(X509_get_serialNumber(certificate.get()), 1) != 1 ||
      X509_gmtime_adj(X509_getm_notBefore(certificate.get()), -60) == nullptr ||
      X509_gmtime_adj(X509_getm_notAfter(certificate.get()), 3600) == nullptr ||
      X509_set_pubkey(certificate.get(), key.get()) != 1)
    return false;
  X509_NAME *name = X509_get_subject_name(certificate.get());
  constexpr unsigned char common_name[] = "SovKit ephemeral QUIC bearer";
  if (name == nullptr ||
      X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, common_name,
                                 static_cast<int>(sizeof(common_name) - 1), -1,
                                 0) != 1 ||
      X509_set_issuer_name(certificate.get(), name) != 1 ||
      X509_sign(certificate.get(), key.get(), nullptr) <= 0 ||
      SSL_CTX_use_certificate(context, certificate.get()) != 1 ||
      SSL_CTX_use_PrivateKey(context, key.get()) != 1 ||
      SSL_CTX_check_private_key(context) != 1)
    return false;
  return true;
}

int SelectAlpn(SSL *, const unsigned char **output, unsigned char *output_len,
               const unsigned char *input, unsigned int input_len, void *) {
  std::size_t offset = 0;
  while (offset < input_len) {
    const std::size_t length = input[offset++];
    if (length > input_len - offset)
      return SSL_TLSEXT_ERR_ALERT_FATAL;
    if (length == kAlpn[0] &&
        std::equal(input + offset, input + offset + length,
                   kAlpn.begin() + 1)) {
      *output = input + offset;
      *output_len = static_cast<unsigned char>(length);
      return SSL_TLSEXT_ERR_OK;
    }
    offset += length;
  }
  return SSL_TLSEXT_ERR_ALERT_FATAL;
}

SslContext CreateClientContext() {
  SslContext context(SSL_CTX_new(TLS_client_method()), SSL_CTX_free);
  if (!context ||
      SSL_CTX_set_min_proto_version(context.get(), TLS1_3_VERSION) != 1 ||
      SSL_CTX_set_max_proto_version(context.get(), TLS1_3_VERSION) != 1 ||
      SSL_CTX_set1_groups_list(context.get(), "X25519:P-256") != 1)
    return {nullptr, SSL_CTX_free};
  SSL_CTX_set_verify(context.get(), SSL_VERIFY_NONE, nullptr);
  SSL_CTX_set_options(context.get(), SSL_OP_NO_TICKET);
  SSL_CTX_set_max_early_data(context.get(), 0);
  return context;
}

SslContext CreateServerContext() {
  SslContext context(SSL_CTX_new(TLS_server_method()), SSL_CTX_free);
  if (!context ||
      SSL_CTX_set_min_proto_version(context.get(), TLS1_3_VERSION) != 1 ||
      SSL_CTX_set_max_proto_version(context.get(), TLS1_3_VERSION) != 1 ||
      SSL_CTX_set1_groups_list(context.get(), "X25519:P-256") != 1 ||
      !InstallEphemeralCertificate(context.get()))
    return {nullptr, SSL_CTX_free};
  SSL_CTX_set_options(context.get(), SSL_OP_NO_TICKET);
  SSL_CTX_set_num_tickets(context.get(), 0);
  SSL_CTX_set_max_early_data(context.get(), 0);
  SSL_CTX_set_alpn_select_cb(context.get(), SelectAlpn, nullptr);
  return context;
}

class Connection final {
public:
  enum class Role { client, server };
  struct StreamBuffer {
    std::vector<std::uint8_t> bytes;
    std::size_t sent = 0;
    std::uint64_t end = 0;
  };
  struct Stream {
    std::deque<StreamBuffer> output;
    std::vector<std::uint8_t> input;
    std::uint64_t enqueued = 0, received = 0, consumed = 0;
    std::size_t buffered = 0;
    bool closed = false, finishing = false, fin_sent = false;
  };

  Connection(Role role, SSL_CTX *ssl_context, Endpoint local, Endpoint remote)
      : role_(role), ssl_context_(ssl_context), local_(std::move(local)),
        remote_(std::move(remote)) {
    random_failed_ = !Random(static_secret_);
  }

  ~Connection() {
    if (connection_ != nullptr)
      ngtcp2_conn_del(connection_);
    if (ssl_ != nullptr) {
      SSL_set_app_data(ssl_, nullptr);
      SSL_free(ssl_);
    }
    if (crypto_context_ != nullptr)
      ngtcp2_crypto_ossl_ctx_del(crypto_context_);
  }

  Connection(const Connection &) = delete;
  Connection &operator=(const Connection &) = delete;

  void ConfigurePath(Endpoint local, Endpoint remote) {
    if (connection_ != nullptr)
      return;
    local_ = std::move(local);
    remote_ = std::move(remote);
  }

  bool StartClient() {
    if (role_ != Role::client || ssl_context_ == nullptr || random_failed_)
      return Fail("invalid client state");
    ngtcp2_cid scid{};
    ngtcp2_cid dcid{};
    scid.datalen = 18;
    dcid.datalen = 18;
    if (!Random({scid.data, scid.datalen}) ||
        !Random({dcid.data, dcid.datalen}))
      return Fail("connection ID generation failed");
    RememberLocalCid(scid);
    ngtcp2_settings settings = Settings();
    ngtcp2_transport_params params = TransportParameters();
    ngtcp2_path path = Path();
    ngtcp2_callbacks callbacks = Callbacks(true);
    const int status = ngtcp2_conn_client_new(
        &connection_, &dcid, &scid, &path, NGTCP2_PROTO_VER_V1, &callbacks,
        &settings, &params, nullptr, this);
    if (status != 0)
      return Fail(ngtcp2_strerror(status));
    return InitializeTls(true) && Flush();
  }

  bool Receive(std::span<const std::uint8_t> packet) {
    if (packet.empty())
      return Fail("empty QUIC packet");
    if (connection_ == nullptr && role_ == Role::server &&
        !AcceptInitial(packet))
      return false;
    if (connection_ == nullptr)
      return Fail("QUIC connection is not initialized");
    ngtcp2_path path = Path();
    ngtcp2_pkt_info packet_info{};
    const int status =
        ngtcp2_conn_read_pkt(connection_, &path, &packet_info, packet.data(),
                             packet.size(), Timestamp());
    if (random_failed_)
      return Fail("secure random generation failed");
    if (status != 0)
      return Fail(ngtcp2_strerror(status));
    return Flush();
  }

  bool QueueStream(std::string_view data, int64_t stream_id = -1) {
    if (!handshake_completed_ || data.empty() || !send_data_.empty())
      return Fail("stream is not writable");
    if (stream_id == -1) {
      const int status =
          ngtcp2_conn_open_bidi_stream(connection_, &stream_id, nullptr);
      if (status != 0)
        return Fail(ngtcp2_strerror(status));
    }
    send_stream_id_ = stream_id;
    send_data_.assign(data.begin(), data.end());
    send_offset_ = 0;
    send_fin_ = true;
    return Flush();
  }

  bool CanQueueOptionalRecord() const {
    return handshake_completed_ && pending_records_.size() < 128;
  }

  bool QueueRecord(std::string_view data) {
    if (!handshake_completed_ || data.empty() ||
        data.size() > kMaximumProviderRecordBytes)
      return Fail("invalid QUIC application record");
    std::vector<std::uint8_t> framed;
    framed.reserve(4 + data.size());
    const std::uint32_t length = static_cast<std::uint32_t>(data.size());
    framed.push_back(static_cast<std::uint8_t>(length >> 24U));
    framed.push_back(static_cast<std::uint8_t>(length >> 16U));
    framed.push_back(static_cast<std::uint8_t>(length >> 8U));
    framed.push_back(static_cast<std::uint8_t>(length));
    framed.insert(framed.end(), data.begin(), data.end());
    if (send_stream_id_ < 0) {
      const int status =
          ngtcp2_conn_open_bidi_stream(connection_, &send_stream_id_, nullptr);
      if (status != 0)
        return Fail(ngtcp2_strerror(status));
    }
    if (!send_data_.empty() && send_offset_ < send_data_.size()) {
      if (pending_records_.size() >= 256)
        return Fail("QUIC application queue is full");
      pending_records_.push_back(std::move(framed));
    } else {
      RetainSentBuffer();
      send_data_ = std::move(framed);
      send_offset_ = 0;
      send_fin_ = false;
    }
    return Flush();
  }
  void EnableFraming() { framed_mode_ = true; }

  bool EnableStreams(std::size_t maximum) {
    if (!handshake_completed_ || maximum == 0 || maximum > 4096)
      return false;
    if (maximum > maximum_streams_)
      ngtcp2_conn_extend_max_streams_bidi(connection_,
                                          maximum - maximum_streams_);
    maximum_streams_ = std::max(maximum_streams_, maximum);
    return true;
  }
  std::int64_t OpenStream() {
    if (!maximum_streams_ || streams_.size() >= maximum_streams_ ||
        send_stream_id_ < 0)
      return -1;
    std::int64_t id = -1;
    if (ngtcp2_conn_open_bidi_stream(connection_, &id, nullptr) != 0)
      return -1;
    streams_.emplace(id, std::make_shared<Stream>());
    return id;
  }
  QuicStreamSendResult QueueStreamRecord(std::int64_t id,
                                         std::string_view record) {
    const auto found = streams_.find(id);
    if (found == streams_.end() || found->second->closed ||
        found->second->finishing || record.empty() || record.size() > 32768)
      return QuicStreamSendResult::unavailable;
    auto &s = *found->second;
    const auto size = record.size() + 4;
    // Includes all unacknowledged buffers borrowed by ngtcp2, not only unsent
    // bytes.
    if (s.buffered + size > 256 * 1024 ||
        stream_buffered_ + size > 8 * 1024 * 1024)
      return QuicStreamSendResult::busy;
    StreamBuffer buffer;
    for (int shift : {24, 16, 8, 0})
      buffer.bytes.push_back(static_cast<std::uint8_t>(record.size() >> shift));
    buffer.bytes.insert(buffer.bytes.end(), record.begin(), record.end());
    s.enqueued += size;
    buffer.end = s.enqueued;
    s.buffered += size;
    stream_buffered_ += size;
    s.output.push_back(std::move(buffer));
    if (!Flush())
      return QuicStreamSendResult::unavailable;
    return QuicStreamSendResult::accepted;
  }
  void ConsumeStream(std::int64_t id, std::size_t bytes) {
    const auto found = streams_.find(id);
    if (found == streams_.end() || found->second->closed)
      return;
    auto &s = *found->second;
    const auto wire_bytes = bytes + 4;
    if (wire_bytes > s.received - s.consumed)
      return;
    s.consumed += wire_bytes;
    ngtcp2_conn_extend_max_stream_offset(connection_, id, wire_bytes);
    ngtcp2_conn_extend_max_offset(connection_, wire_bytes);
  }
  void ResetStream(std::int64_t id) {
    const auto found = streams_.find(id);
    if (found == streams_.end() || found->second->closed)
      return;
    MarkStreamClosed(id, *found->second);
    (void)ngtcp2_conn_shutdown_stream(connection_, 0, id, 0x53564b31);
  }
  void FinishStream(std::int64_t id) {
    const auto found = streams_.find(id);
    if (found != streams_.end() && !found->second->closed)
      found->second->finishing = true;
  }
  std::vector<QuicProviderEvent> TakeStreamEvents() {
    std::vector<QuicProviderEvent> result;
    result.swap(stream_events_);
    return result;
  }

  QuicConnectionStats Statistics() const {
    ngtcp2_conn_info info{};
    if (connection_ == nullptr)
      return {};
    ngtcp2_conn_get_conn_info2(connection_, &info);
    return {info.smoothed_rtt / NGTCP2_MICROSECONDS,
            info.cwnd,
            info.bytes_in_flight,
            info.pkt_sent,
            info.pkt_lost,
            ngtcp2_conn_get_send_quantum2(connection_),
            maximum_batch_datagrams_,
            streams_.size(),
            stream_buffered_};
  }

  bool HandleExpiry() {
    if (connection_ == nullptr)
      return true;
    const ngtcp2_tstamp now = Timestamp();
    if (ngtcp2_conn_get_expiry2(connection_) > now)
      return Flush();
    const int status = ngtcp2_conn_handle_expiry(connection_, now);
    if (status != 0)
      return Fail(ngtcp2_strerror(status));
    return Flush();
  }

  std::vector<std::vector<std::uint8_t>> TakePackets() {
    std::vector<std::vector<std::uint8_t>> result;
    result.swap(outgoing_);
    return result;
  }

  bool handshake_completed() const { return handshake_completed_; }
  bool stream_send_completed() const {
    return !send_data_.empty() && send_offset_ == send_data_.size();
  }
  const std::vector<std::uint8_t> &received() const { return received_; }
  int64_t received_stream_id() const { return received_stream_id_; }
  const std::string &error() const { return error_; }
  std::string alpn() const {
    const unsigned char *data = nullptr;
    unsigned int size = 0;
    if (ssl_ != nullptr)
      SSL_get0_alpn_selected(ssl_, &data, &size);
    return data == nullptr
               ? std::string{}
               : std::string(reinterpret_cast<const char *>(data), size);
  }
  std::string cipher() const {
    const char *value = ssl_ == nullptr ? nullptr : SSL_get_cipher_name(ssl_);
    return value == nullptr ? std::string{} : value;
  }
  bool early_data_accepted() const {
    return ssl_ != nullptr &&
           SSL_get_early_data_status(ssl_) == SSL_EARLY_DATA_ACCEPTED;
  }
  std::vector<std::vector<std::uint8_t>> TakeRecords() {
    std::vector<std::vector<std::uint8_t>> result;
    result.swap(received_records_);
    return result;
  }
  std::vector<std::vector<std::uint8_t>> TakeLocalCids() {
    std::vector<std::vector<std::uint8_t>> result;
    result.swap(new_local_cids_);
    return result;
  }
  const Endpoint &peer() const { return remote_; }

private:
  static ngtcp2_conn *GetConnection(ngtcp2_crypto_conn_ref *reference) {
    return static_cast<Connection *>(reference->user_data)->connection_;
  }

  static int HandshakeCompleted(ngtcp2_conn *, void *user_data) {
    static_cast<Connection *>(user_data)->handshake_completed_ = true;
    return 0;
  }

  void MarkStreamClosed(std::int64_t id, Stream &s) {
    if (s.closed)
      return;
    s.closed = true;
    ngtcp2_conn_extend_max_offset(connection_, s.received - s.consumed);
    s.consumed = s.received;
    s.input.clear();
    stream_events_.push_back(
        {QuicProviderEventType::stream_closed, 0, {}, {}, {}, id});
  }
  static int StreamClosed(ngtcp2_conn *connection, std::uint32_t,
                          std::int64_t id, std::uint64_t, void *user, void *) {
    auto *self = static_cast<Connection *>(user);
    const bool remote = (id & 1) != (self->role_ == Role::server ? 1 : 0);
    // A RESET before the first application byte has no map entry, but still
    // needs to return concurrent-stream credit (rapid browser cancellation).
    if (remote && !(id & 2) && self->maximum_streams_ &&
        id != self->send_stream_id_ && id != self->received_stream_id_)
      ngtcp2_conn_extend_max_streams_bidi(connection, 1);
    const auto it = self->streams_.find(id);
    if (it == self->streams_.end())
      return 0;
    self->MarkStreamClosed(id, *it->second);
    self->stream_buffered_ -= it->second->buffered;
    self->streams_.erase(it);
    return 0;
  }
  static int StreamReset(ngtcp2_conn *, std::int64_t id,
                         std::uint64_t final_size, std::uint64_t, void *user,
                         void *) {
    auto *self = static_cast<Connection *>(user);
    const auto it = self->streams_.find(id);
    if (it == self->streams_.end()) {
      if (self->maximum_streams_ && id != self->send_stream_id_ &&
          id != self->received_stream_id_) {
        ngtcp2_conn_extend_max_offset(self->connection_, final_size);
        (void)ngtcp2_conn_shutdown_stream(self->connection_, 0, id, 0x53564b31);
      }
      return 0;
    }
    auto &s = *it->second;
    if (final_size > s.received) {
      ngtcp2_conn_extend_max_offset(self->connection_, final_size - s.received);
    }
    self->ResetStream(id);
    return 0;
  }
  int ReceiveExtraStream(std::int64_t id, std::uint64_t offset,
                         const std::uint8_t *data, std::size_t size) {
    if (!maximum_streams_ || (id & 2))
      return NGTCP2_ERR_CALLBACK_FAILURE;
    auto it = streams_.find(id);
    if (it == streams_.end()) {
      if (streams_.size() >= maximum_streams_)
        return NGTCP2_ERR_CALLBACK_FAILURE;
      it = streams_.emplace(id, std::make_shared<Stream>()).first;
    }
    auto &s = *it->second;
    if (s.closed)
      return 0;
    if (offset != s.received)
      return NGTCP2_ERR_CALLBACK_FAILURE;
    s.received += size;
    if (s.input.size() + size > 65536) {
      ResetStream(id);
      return 0;
    }
    s.input.insert(s.input.end(), data, data + size);
    while (s.input.size() >= 4) {
      const std::uint32_t length = (std::uint32_t(s.input[0]) << 24) |
                                   (std::uint32_t(s.input[1]) << 16) |
                                   (std::uint32_t(s.input[2]) << 8) |
                                   s.input[3];
      if (!length || length > 32768) {
        ResetStream(id);
        return 0;
      }
      if (s.input.size() < 4 + length)
        break;
      stream_events_.push_back(
          {QuicProviderEventType::stream_record,
           0,
           {},
           {s.input.begin() + 4, s.input.begin() + 4 + length},
           {},
           id});
      s.input.erase(s.input.begin(), s.input.begin() + 4 + length);
    }
    // Credit advances only when the consumer accepts a complete record.
    return 0;
  }
  static int ReceiveStream(ngtcp2_conn *connection, std::uint32_t,
                           int64_t stream_id, std::uint64_t offset,
                           const std::uint8_t *data, std::size_t size,
                           void *user_data, void *) {
    auto *self = static_cast<Connection *>(user_data);
    if (self->framed_mode_ && self->received_stream_id_ >= 0 &&
        self->received_stream_id_ != stream_id)
      return self->ReceiveExtraStream(stream_id, offset, data, size);
    if (offset != self->received_offset_ ||
        (self->received_stream_id_ >= 0 &&
         self->received_stream_id_ != stream_id))
      return NGTCP2_ERR_CALLBACK_FAILURE;
    self->received_stream_id_ = stream_id;
    self->received_offset_ += size;
    // The loopback probe retains its result. Framed production connections
    // retain only incomplete records, never the entire file/session history.
    if (!self->framed_mode_)
      self->received_.insert(self->received_.end(), data, data + size);
    if (self->framed_mode_ && self->send_stream_id_ < 0)
      self->send_stream_id_ = stream_id;
    if (self->framed_mode_)
      self->record_input_.insert(self->record_input_.end(), data, data + size);
    while (self->framed_mode_ && self->record_input_.size() >= 4) {
      const std::uint32_t length =
          static_cast<std::uint32_t>(self->record_input_[0]) << 24U |
          static_cast<std::uint32_t>(self->record_input_[1]) << 16U |
          static_cast<std::uint32_t>(self->record_input_[2]) << 8U |
          static_cast<std::uint32_t>(self->record_input_[3]);
      if (length == 0 || length > kMaximumProviderRecordBytes)
        return NGTCP2_ERR_CALLBACK_FAILURE;
      if (self->record_input_.size() < 4 + length)
        break;
      self->received_records_.emplace_back(self->record_input_.begin() + 4,
                                           self->record_input_.begin() + 4 +
                                               length);
      self->record_input_.erase(self->record_input_.begin(),
                                self->record_input_.begin() + 4 + length);
    }
    ngtcp2_conn_extend_max_stream_offset(connection, stream_id, size);
    ngtcp2_conn_extend_max_offset(connection, size);
    return 0;
  }

  static int AcknowledgedStream(ngtcp2_conn *, int64_t stream_id,
                                std::uint64_t offset, std::uint64_t size,
                                void *user_data, void *) {
    auto *self = static_cast<Connection *>(user_data);
    if (stream_id != self->send_stream_id_) {
      const auto it = self->streams_.find(stream_id);
      if (it == self->streams_.end())
        return 0;
      auto &s = *it->second;
      while (!s.output.empty() && s.output.front().end <= offset + size) {
        self->stream_buffered_ -= s.output.front().bytes.size();
        s.buffered -= s.output.front().bytes.size();
        s.output.pop_front();
      }
      return 0;
    }
    self->acknowledged_offset_ = offset + size;
    while (!self->retained_send_.empty() &&
           self->retained_send_.front().end <= self->acknowledged_offset_)
      self->retained_send_.pop_front();
    return 0;
  }

  void RetainSentBuffer() {
    if (send_data_.empty())
      return;
    send_base_offset_ += send_data_.size();
    // ngtcp2 borrows stream bytes until its ACK callback; serialization into
    // a UDP packet is not delivery. Moving the vector keeps its storage alive.
    if (send_base_offset_ > acknowledged_offset_)
      retained_send_.push_back({send_base_offset_, std::move(send_data_)});
  }

  static void FillRandom(std::uint8_t *data, std::size_t size,
                         const ngtcp2_rand_ctx *context) {
    if (!Random({data, size})) {
      if (context != nullptr && context->native_handle != nullptr)
        static_cast<Connection *>(context->native_handle)->random_failed_ =
            true;
      std::fill(data, data + size, 0);
    }
  }

  static int NewConnectionId(ngtcp2_conn *, ngtcp2_cid *cid,
                             ngtcp2_stateless_reset_token *token,
                             std::size_t cid_length, void *user_data) {
    auto *self = static_cast<Connection *>(user_data);
    if (cid_length > sizeof(cid->data) || !Random({cid->data, cid_length}))
      return NGTCP2_ERR_CALLBACK_FAILURE;
    cid->datalen = cid_length;
    self->RememberLocalCid(*cid);
    return ngtcp2_crypto_generate_stateless_reset_token(
               token->data, self->static_secret_.data(),
               self->static_secret_.size(), cid) == 0
               ? 0
               : NGTCP2_ERR_CALLBACK_FAILURE;
  }

  static ngtcp2_callbacks Callbacks(bool client) {
    ngtcp2_callbacks callbacks{};
    callbacks.client_initial =
        client ? ngtcp2_crypto_client_initial_cb : nullptr;
    callbacks.recv_client_initial =
        client ? nullptr : ngtcp2_crypto_recv_client_initial_cb;
    callbacks.recv_crypto_data = ngtcp2_crypto_recv_crypto_data_cb;
    callbacks.handshake_completed = HandshakeCompleted;
    callbacks.encrypt = ngtcp2_crypto_encrypt_cb;
    callbacks.decrypt = ngtcp2_crypto_decrypt_cb;
    callbacks.hp_mask = ngtcp2_crypto_hp_mask_cb;
    callbacks.recv_stream_data = ReceiveStream;
    callbacks.acked_stream_data_offset = AcknowledgedStream;
    callbacks.stream_close = StreamClosed;
    callbacks.stream_reset = StreamReset;
    callbacks.recv_retry = client ? ngtcp2_crypto_recv_retry_cb : nullptr;
    callbacks.rand = FillRandom;
    callbacks.get_new_connection_id2 = NewConnectionId;
    callbacks.update_key = ngtcp2_crypto_update_key_cb;
    callbacks.delete_crypto_aead_ctx = ngtcp2_crypto_delete_crypto_aead_ctx_cb;
    callbacks.delete_crypto_cipher_ctx =
        ngtcp2_crypto_delete_crypto_cipher_ctx_cb;
    callbacks.version_negotiation = ngtcp2_crypto_version_negotiation_cb;
    callbacks.get_path_challenge_data2 =
        ngtcp2_crypto_get_path_challenge_data2_cb;
    return callbacks;
  }

  ngtcp2_settings Settings() {
    ngtcp2_settings settings;
    ngtcp2_settings_default(&settings);
    settings.initial_ts = Timestamp();
    settings.rand_ctx.native_handle = this;
    settings.handshake_timeout = 3 * NGTCP2_SECONDS;
    settings.max_tx_udp_payload_size = kMaximumTxPacketBytes;
    settings.no_tx_udp_payload_size_shaping = 1;
    settings.no_pmtud = 1;
    return settings;
  }

  static ngtcp2_transport_params TransportParameters() {
    ngtcp2_transport_params params;
    ngtcp2_transport_params_default(&params);
    params.initial_max_stream_data_bidi_local = 1024 * 1024;
    params.initial_max_stream_data_bidi_remote = 1024 * 1024;
    params.initial_max_stream_data_uni = 1024 * 1024;
    params.initial_max_data = 4 * 1024 * 1024;
    params.initial_max_streams_bidi = 8;
    params.initial_max_streams_uni = 0;
    params.max_idle_timeout = 5 * NGTCP2_SECONDS;
    params.active_connection_id_limit = 2;
    return params;
  }

  ngtcp2_path Path() {
    ngtcp2_path path{};
    path.local.addr = const_cast<sockaddr *>(local_.sockaddr_ptr());
    path.local.addrlen = local_.sockaddr_length();
    path.remote.addr = const_cast<sockaddr *>(remote_.sockaddr_ptr());
    path.remote.addrlen = remote_.sockaddr_length();
    return path;
  }

  bool AcceptInitial(std::span<const std::uint8_t> packet) {
    ngtcp2_pkt_hd header{};
    const int accepted = ngtcp2_accept(&header, packet.data(), packet.size());
    if (accepted != 0 || header.type != NGTCP2_PKT_INITIAL)
      return Fail("invalid QUIC Initial packet");
    ngtcp2_cid server_scid{};
    server_scid.datalen = 18;
    if (!Random({server_scid.data, server_scid.datalen}))
      return Fail("server connection ID generation failed");
    RememberLocalCid(server_scid);
    ngtcp2_settings settings = Settings();
    ngtcp2_transport_params params = TransportParameters();
    params.original_dcid = header.dcid;
    params.original_dcid_present = 1;
    ngtcp2_path path = Path();
    ngtcp2_callbacks callbacks = Callbacks(false);
    const int status = ngtcp2_conn_server_new(
        &connection_, &header.scid, &server_scid, &path, header.version,
        &callbacks, &settings, &params, nullptr, this);
    if (status != 0)
      return Fail(ngtcp2_strerror(status));
    return InitializeTls(false);
  }

  bool InitializeTls(bool client) {
    if (ngtcp2_crypto_ossl_ctx_new(&crypto_context_, nullptr) != 0)
      return Fail("ngtcp2 OpenSSL context creation failed");
    ssl_ = SSL_new(ssl_context_);
    if (ssl_ == nullptr)
      return Fail("OpenSSL session creation failed");
    ngtcp2_crypto_ossl_ctx_set_ssl(crypto_context_, ssl_);
    const int configured =
        client ? ngtcp2_crypto_ossl_configure_client_session(ssl_)
               : ngtcp2_crypto_ossl_configure_server_session(ssl_);
    if (configured != 0)
      return Fail("ngtcp2 OpenSSL session configuration failed");
    connection_reference_.get_conn = GetConnection;
    connection_reference_.user_data = this;
    SSL_set_app_data(ssl_, &connection_reference_);
    if (client) {
      SSL_set_connect_state(ssl_);
      if (SSL_set_alpn_protos(ssl_, kAlpn.data(),
                              static_cast<unsigned int>(kAlpn.size())) != 0)
        return Fail("client ALPN configuration failed");
    } else {
      SSL_set_accept_state(ssl_);
    }
    ngtcp2_conn_set_tls_native_handle(connection_, crypto_context_);
    return true;
  }

  bool Flush() {
    if (connection_ == nullptr)
      return true;
    if (random_failed_)
      return Fail("secure random generation failed");
    // ngtcp2 paces a send quantum, not each individual datagram within it.
    // Updating the tx time after every packet prematurely closes the burst
    // and makes the remaining writable data wait for another receive/tick.
    const ngtcp2_tstamp now = Timestamp();
    const std::size_t quantum = ngtcp2_conn_get_send_quantum2(connection_);
    std::size_t batch_bytes = 0;
    std::uint64_t batch_datagrams = 0;
    for (std::size_t count = 0;
         count < 64 && quantum - batch_bytes >= kMaximumTxPacketBytes;
         ++count) {
      std::array<std::uint8_t, kPacketBytes> packet{};
      ngtcp2_pkt_info packet_info{};
      ngtcp2_ssize accepted = -1;
      const bool has_control =
          send_stream_id_ >= 0 && send_offset_ < send_data_.size();
      std::shared_ptr<Stream> extra;
      StreamBuffer *extra_buffer = nullptr;
      std::int64_t selected_id = send_stream_id_;
      // Reserve every fourth packet for the ordered control stream. Rotate
      // other runnable streams one packet at a time; a slow reader cannot own
      // the entire connection's send queue.
      if (!has_control || count % 4 != 0) {
        auto it = streams_.upper_bound(last_stream_);
        for (std::size_t scanned = 0; scanned < streams_.size(); ++scanned) {
          if (it == streams_.end())
            it = streams_.begin();
          auto candidate = it++;
          if (candidate->second->closed)
            continue;
          auto &buffers = candidate->second->output;
          auto writable =
              std::find_if(buffers.begin(), buffers.end(), [](const auto &b) {
                return b.sent < b.bytes.size();
              });
          if (writable == buffers.end() &&
              (!candidate->second->finishing || candidate->second->fin_sent))
            continue;
          last_stream_ = selected_id = candidate->first;
          extra = candidate->second;
          if (writable != buffers.end())
            extra_buffer = &*writable;
          break;
        }
      }
      const bool has_stream = extra || has_control;
      const std::uint8_t *data =
          extra_buffer ? extra_buffer->bytes.data() + extra_buffer->sent
          : !extra && has_control ? send_data_.data() + send_offset_
                                  : nullptr;
      const std::size_t size =
          extra_buffer ? extra_buffer->bytes.size() - extra_buffer->sent
          : !extra && has_control ? send_data_.size() - send_offset_
                                  : 0;
      const bool finishing =
          extra ? extra->finishing && !extra_buffer : send_fin_;
      const std::uint32_t flags = has_stream && finishing
                                      ? NGTCP2_WRITE_STREAM_FLAG_FIN
                                      : NGTCP2_WRITE_STREAM_FLAG_NONE;
      const ngtcp2_ssize written = ngtcp2_conn_write_stream(
          connection_, nullptr, &packet_info, packet.data(), packet.size(),
          &accepted, flags, has_stream ? selected_id : -1, data, size, now);
      if (random_failed_)
        return Fail("secure random generation failed");
      if (written == 0)
        break;
      if (written < 0) {
        if (written == NGTCP2_ERR_STREAM_DATA_BLOCKED)
          continue;
        if (written == NGTCP2_ERR_STREAM_SHUT_WR ||
            written == NGTCP2_ERR_STREAM_NOT_FOUND) {
          if (extra) {
            ResetStream(selected_id);
            continue;
          }
          break;
        }
        return Fail(ngtcp2_strerror(static_cast<int>(written)));
      }
      if (accepted > 0) {
        if (extra_buffer)
          extra_buffer->sent += static_cast<std::size_t>(accepted);
        else
          send_offset_ += static_cast<std::size_t>(accepted);
      }
      if (extra && finishing && accepted >= 0)
        extra->fin_sent = true;
      outgoing_.emplace_back(packet.begin(), packet.begin() + written);
      batch_bytes += static_cast<std::size_t>(written);
      ++batch_datagrams;
      if (!extra && send_offset_ == send_data_.size() &&
          !pending_records_.empty()) {
        RetainSentBuffer();
        send_data_ = std::move(pending_records_.front());
        pending_records_.pop_front();
        send_offset_ = 0;
        send_fin_ = false;
      }
    }
    if (batch_bytes != 0)
      ngtcp2_conn_update_pkt_tx_time(connection_, now);
    maximum_batch_datagrams_ =
        (std::max)(maximum_batch_datagrams_, batch_datagrams);
    // A per-tick work budget is not a connection error. Continue on the next
    // timer/receive callback so a large writable burst cannot kill the bearer.
    return true;
  }

  bool Fail(std::string value) {
    if (error_.empty())
      error_ = std::move(value);
    return false;
  }

  void RememberLocalCid(const ngtcp2_cid &cid) {
    new_local_cids_.emplace_back(cid.data, cid.data + cid.datalen);
  }

  Role role_;
  SSL_CTX *ssl_context_ = nullptr;
  Endpoint local_;
  Endpoint remote_;
  std::array<std::uint8_t, 32> static_secret_{};
  ngtcp2_conn *connection_ = nullptr;
  ngtcp2_crypto_ossl_ctx *crypto_context_ = nullptr;
  SSL *ssl_ = nullptr;
  ngtcp2_crypto_conn_ref connection_reference_{};
  bool handshake_completed_ = false;
  bool random_failed_ = false;
  std::uint64_t maximum_batch_datagrams_ = 0;
  std::vector<std::vector<std::uint8_t>> outgoing_;
  std::vector<std::uint8_t> received_;
  std::uint64_t received_offset_ = 0;
  std::vector<std::uint8_t> record_input_;
  std::vector<std::vector<std::uint8_t>> received_records_;
  std::vector<std::vector<std::uint8_t>> new_local_cids_;
  int64_t received_stream_id_ = -1;
  int64_t send_stream_id_ = -1;
  std::vector<std::uint8_t> send_data_;
  struct RetainedSend {
    std::uint64_t end;
    std::vector<std::uint8_t> data;
  };
  std::deque<RetainedSend> retained_send_;
  std::uint64_t send_base_offset_ = 0;
  std::uint64_t acknowledged_offset_ = 0;
  std::deque<std::vector<std::uint8_t>> pending_records_;
  std::size_t send_offset_ = 0;
  bool send_fin_ = false;
  bool framed_mode_ = false;
  std::string error_;
  std::map<std::int64_t, std::shared_ptr<Stream>> streams_;
  std::vector<QuicProviderEvent> stream_events_;
  std::size_t maximum_streams_ = 0, stream_buffered_ = 0;
  std::int64_t last_stream_ = -1;
};

class UdpLoopbackLink final {
public:
  UdpLoopbackLink() = default;
  ~UdpLoopbackLink() { Close(); }

  UdpLoopbackLink(const UdpLoopbackLink &) = delete;
  UdpLoopbackLink &operator=(const UdpLoopbackLink &) = delete;

  bool Initialize(Connection *client, Connection *server) {
    client_ = client;
    server_ = server;
    if (client_ == nullptr || server_ == nullptr || uv_loop_init(&loop_) != 0)
      return Fail("libuv loop initialization failed");
    loop_initialized_ = true;
    if (!client_socket_.Bind(&loop_, "127.0.0.1", 0) ||
        !server_socket_.Bind(&loop_, "127.0.0.1", 0))
      return Fail("QUIC loopback UDP bind failed");
    client_endpoint_ = client_socket_.local_endpoint();
    server_endpoint_ = server_socket_.local_endpoint();
    if (!client_endpoint_ || !server_endpoint_)
      return Fail("QUIC loopback endpoint lookup failed");
    client_->ConfigurePath(*client_endpoint_, *server_endpoint_);
    server_->ConfigurePath(*server_endpoint_, *client_endpoint_);
    if (!client_socket_.StartReceive(
            [this](std::string_view bytes, const Endpoint &source, unsigned) {
              Receive(*client_, *server_endpoint_, bytes, source);
            },
            [this](int status) { NetworkError("client receive", status); }) ||
        !server_socket_.StartReceive(
            [this](std::string_view bytes, const Endpoint &source, unsigned) {
              Receive(*server_, *client_endpoint_, bytes, source);
            },
            [this](int status) { NetworkError("server receive", status); }))
      return Fail("QUIC loopback UDP receive start failed");
    receiving_ = true;
    return true;
  }

  bool Pump(Connection &client, Connection &server,
            std::chrono::steady_clock::time_point deadline,
            const auto &completed) {
    while (std::chrono::steady_clock::now() < deadline) {
      if (!Send(client, client_socket_, *server_endpoint_) ||
          !Send(server, server_socket_, *client_endpoint_) ||
          uv_run(&loop_, UV_RUN_NOWAIT) < 0 || !error_.empty() ||
          !client.HandleExpiry() || !server.HandleExpiry())
        return false;
      if (completed())
        return true;
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
  }

  std::size_t packet_count() const { return packet_count_; }
  const std::string &error() const { return error_; }

private:
  bool Send(Connection &connection, UdpSocket &socket,
            const Endpoint &destination) {
    for (const auto &packet : connection.TakePackets()) {
      const std::string_view bytes(
          reinterpret_cast<const char *>(packet.data()), packet.size());
      if (!socket.Send(destination, bytes))
        return Fail("QUIC loopback UDP send failed");
      ++packet_count_;
    }
    return true;
  }

  void Receive(Connection &connection, const Endpoint &expected_source,
               std::string_view bytes, const Endpoint &source) {
    if (!error_.empty())
      return;
    if (source.address() != expected_source.address() ||
        source.port() != expected_source.port()) {
      Fail("QUIC loopback packet has an unexpected source");
      return;
    }
    const auto *data = reinterpret_cast<const std::uint8_t *>(bytes.data());
    if (!connection.Receive({data, bytes.size()}))
      Fail(connection.error());
  }

  void NetworkError(std::string_view operation, int status) {
    Fail(std::string(operation) + ": " + uv_strerror(status));
  }

  bool Fail(std::string value) {
    if (error_.empty())
      error_ = std::move(value);
    return false;
  }

  void Close() {
    if (!loop_initialized_)
      return;
    if (receiving_) {
      client_socket_.StopReceive();
      server_socket_.StopReceive();
      receiving_ = false;
    }
    if (client_socket_.initialized())
      client_socket_.Close();
    if (server_socket_.initialized())
      server_socket_.Close();
    while (uv_loop_alive(&loop_))
      uv_run(&loop_, UV_RUN_NOWAIT);
    uv_loop_close(&loop_);
    loop_initialized_ = false;
    client_ = nullptr;
    server_ = nullptr;
  }

  uv_loop_t loop_{};
  bool loop_initialized_ = false;
  bool receiving_ = false;
  UdpSocket client_socket_;
  UdpSocket server_socket_;
  std::optional<Endpoint> client_endpoint_;
  std::optional<Endpoint> server_endpoint_;
  Connection *client_ = nullptr;
  Connection *server_ = nullptr;
  std::size_t packet_count_ = 0;
  std::string error_;
};

} // namespace

struct QuicProvider::Impl {
  struct Entry {
    std::uint64_t id = 0;
    std::unique_ptr<Connection> connection;
    bool ready_reported = false;
  };

  static std::string CidKey(std::span<const std::uint8_t> cid) {
    return std::string(reinterpret_cast<const char *>(cid.data()), cid.size());
  }

  Endpoint LocalFor(const Endpoint &peer) const {
    return peer.family() == AddressFamily::ipv6 && local_ipv6.valid()
               ? local_ipv6
               : local_ipv4;
  }

  bool Start(Endpoint ipv4, Endpoint ipv6, DatagramSender value,
             std::size_t maximum) {
    if (started || !ipv4.valid() || !value || maximum == 0 || maximum > 256)
      return false;
    if (!AcquireCryptoRuntime())
      return false;
    crypto_initialized = true;
    client_context = CreateClientContext();
    server_context = CreateServerContext();
    if (!client_context || !server_context) {
      Stop();
      return false;
    }
    local_ipv4 = std::move(ipv4);
    local_ipv6 = std::move(ipv6);
    sender = std::move(value);
    maximum_connections = maximum;
    started = true;
    return true;
  }

  void Stop() {
    entries.clear();
    cid_routes.clear();
    peer_routes.clear();
    events.clear();
    sender = {};
    client_context.reset();
    server_context.reset();
    if (crypto_initialized) {
      ReleaseCryptoRuntime();
      crypto_initialized = false;
    }
    started = false;
  }

  std::uint64_t Connect(const Endpoint &peer) {
    if (!started || !peer.valid() || entries.size() >= maximum_connections ||
        peer_routes.contains(peer.ToString()))
      return 0;
    std::uint64_t id = next_id++;
    if (id == 0)
      id = next_id++;
    auto entry = std::make_unique<Entry>();
    entry->id = id;
    entry->connection = std::make_unique<Connection>(
        Connection::Role::client, client_context.get(), LocalFor(peer), peer);
    entry->connection->EnableFraming();
    if (!entry->connection->StartClient())
      return 0;
    peer_routes[peer.ToString()] = id;
    entries[id] = std::move(entry);
    if (!Refresh(id)) {
      Erase(id, QuicProviderEventType::failed, "QUIC Initial send failed");
      return 0;
    }
    return id;
  }

  bool Receive(std::string_view datagram, const Endpoint &source) {
    if (!started || datagram.empty() || !source.valid())
      return false;
    const auto bytes =
        std::span(reinterpret_cast<const std::uint8_t *>(datagram.data()),
                  datagram.size());
    ngtcp2_version_cid decoded{};
    const int decoded_status =
        ngtcp2_pkt_decode_version_cid(&decoded, bytes.data(), bytes.size(), 18);
    std::uint64_t id = 0;
    if ((decoded_status == 0 ||
         decoded_status == NGTCP2_ERR_VERSION_NEGOTIATION) &&
        decoded.dcid != nullptr) {
      const auto found =
          cid_routes.find(CidKey({decoded.dcid, decoded.dcidlen}));
      if (found != cid_routes.end())
        id = found->second;
    }
    if (id == 0) {
      ngtcp2_pkt_hd header{};
      if ((bytes.front() & 0x80U) == 0 ||
          ngtcp2_accept(&header, bytes.data(), bytes.size()) != 0)
        return false;
      if (entries.size() >= maximum_connections)
        return true;
      id = next_id++;
      if (id == 0)
        id = next_id++;
      auto entry = std::make_unique<Entry>();
      entry->id = id;
      entry->connection = std::make_unique<Connection>(
          Connection::Role::server, server_context.get(), LocalFor(source),
          source);
      entry->connection->EnableFraming();
      entries[id] = std::move(entry);
      peer_routes[source.ToString()] = id;
      cid_routes[CidKey({header.dcid.data, header.dcid.datalen})] = id;
    }
    const auto found = entries.find(id);
    if (found == entries.end() ||
        found->second->connection->peer().ToString() != source.ToString())
      return true;
    if (!found->second->connection->Receive(bytes)) {
      const std::string error = found->second->connection->error();
      Erase(id, QuicProviderEventType::failed,
            error.empty() ? "QUIC receive failed" : error);
      return true;
    }
    if (!Refresh(id))
      Erase(id, QuicProviderEventType::failed, "QUIC send failed");
    return true;
  }

  bool CanQueueOptionalRecord(std::uint64_t id) const {
    const auto found = entries.find(id);
    return found != entries.end() && found->second->ready_reported &&
           found->second->connection->CanQueueOptionalRecord();
  }

  bool SendRecord(std::uint64_t id, std::string_view record) {
    const auto found = entries.find(id);
    if (found == entries.end() || !found->second->ready_reported)
      return false;
    if (!found->second->connection->QueueRecord(record)) {
      const std::string error = found->second->connection->error();
      Erase(id, QuicProviderEventType::failed,
            error.empty() ? "QUIC application queue rejected" : error);
      return false;
    }
    if (Refresh(id))
      return true;
    Erase(id, QuicProviderEventType::failed, "QUIC record send failed");
    return false;
  }

  void Tick() {
    std::vector<std::uint64_t> ids;
    ids.reserve(entries.size());
    for (const auto &[id, entry] : entries) {
      (void)entry;
      ids.push_back(id);
    }
    for (const std::uint64_t id : ids) {
      const auto found = entries.find(id);
      if (found == entries.end())
        continue;
      if (!found->second->connection->HandleExpiry()) {
        const std::string error = found->second->connection->error();
        Erase(id, QuicProviderEventType::failed,
              error.empty() ? "QUIC timer failed" : error);
      } else if (!Refresh(id)) {
        Erase(id, QuicProviderEventType::failed, "QUIC timer send failed");
      }
    }
  }

  bool Refresh(std::uint64_t id) {
    const auto found = entries.find(id);
    if (found == entries.end())
      return false;
    Entry &entry = *found->second;
    for (auto &cid : entry.connection->TakeLocalCids())
      cid_routes[CidKey(cid)] = id;
    if (!entry.ready_reported && entry.connection->handshake_completed()) {
      entry.ready_reported = true;
      events.push_back({QuicProviderEventType::bearer_ready,
                        id,
                        entry.connection->peer(),
                        {},
                        {}});
    }
    for (auto &record : entry.connection->TakeRecords())
      events.push_back({QuicProviderEventType::record,
                        id,
                        entry.connection->peer(),
                        std::move(record),
                        {}});
    for (auto &event : entry.connection->TakeStreamEvents()) {
      event.connection_id = id;
      event.peer = entry.connection->peer();
      events.push_back(std::move(event));
    }
    for (const auto &packet : entry.connection->TakePackets()) {
      const std::string_view bytes(
          reinterpret_cast<const char *>(packet.data()), packet.size());
      if (!sender(entry.connection->peer(), bytes))
        return false;
    }
    return true;
  }

  void Erase(std::uint64_t id, QuicProviderEventType type, std::string detail) {
    const auto found = entries.find(id);
    if (found == entries.end())
      return;
    const Endpoint peer = found->second->connection->peer();
    std::erase_if(cid_routes,
                  [id](const auto &value) { return value.second == id; });
    const auto peer_route = peer_routes.find(peer.ToString());
    if (peer_route != peer_routes.end() && peer_route->second == id)
      peer_routes.erase(peer_route);
    entries.erase(found);
    events.push_back({type, id, peer, {}, std::move(detail)});
  }

  bool started = false;
  bool crypto_initialized = false;
  std::size_t maximum_connections = 0;
  std::uint64_t next_id = 1;
  Endpoint local_ipv4;
  Endpoint local_ipv6;
  DatagramSender sender;
  SslContext client_context{nullptr, SSL_CTX_free};
  SslContext server_context{nullptr, SSL_CTX_free};
  std::map<std::uint64_t, std::unique_ptr<Entry>> entries;
  std::map<std::string, std::uint64_t> cid_routes;
  std::map<std::string, std::uint64_t> peer_routes;
  std::vector<QuicProviderEvent> events;
};

QuicProvider::QuicProvider() : impl_(std::make_unique<Impl>()) {}
QuicProvider::~QuicProvider() { impl_->Stop(); }

bool QuicProvider::Start(Endpoint local_ipv4, Endpoint local_ipv6,
                         DatagramSender sender,
                         std::size_t maximum_connections) {
  return impl_->Start(std::move(local_ipv4), std::move(local_ipv6),
                      std::move(sender), maximum_connections);
}

void QuicProvider::Stop() { impl_->Stop(); }
bool QuicProvider::started() const { return impl_->started; }

std::uint64_t QuicProvider::Connect(const Endpoint &peer) {
  return impl_->Connect(peer);
}

bool QuicProvider::ReceiveDatagram(std::string_view datagram,
                                   const Endpoint &source) {
  return impl_->Receive(datagram, source);
}

bool QuicProvider::CanQueueOptionalRecord(std::uint64_t connection_id) const {
  return impl_->CanQueueOptionalRecord(connection_id);
}

bool QuicProvider::SendRecord(std::uint64_t connection_id,
                              std::string_view record) {
  return impl_->SendRecord(connection_id, record);
}

void QuicProvider::Tick() { impl_->Tick(); }

bool QuicProvider::EnableStreams(std::uint64_t id, std::size_t maximum) {
  const auto it = impl_->entries.find(id);
  return it != impl_->entries.end() && it->second->ready_reported &&
         it->second->connection->EnableStreams(maximum);
}
std::int64_t QuicProvider::OpenStream(std::uint64_t id) {
  const auto it = impl_->entries.find(id);
  return it == impl_->entries.end() ? -1 : it->second->connection->OpenStream();
}
QuicStreamSendResult QuicProvider::SendStreamRecord(std::uint64_t id,
                                                    std::int64_t stream,
                                                    std::string_view record) {
  const auto it = impl_->entries.find(id);
  if (it == impl_->entries.end())
    return QuicStreamSendResult::unavailable;
  const auto result = it->second->connection->QueueStreamRecord(stream, record);
  if (!it->second->connection->error().empty() || !impl_->Refresh(id)) {
    impl_->Erase(id, QuicProviderEventType::failed, "QUIC stream send failed");
    return QuicStreamSendResult::unavailable;
  }
  return result;
}
void QuicProvider::ConsumeStreamRecord(std::uint64_t id, std::int64_t stream,
                                       std::size_t bytes) {
  const auto it = impl_->entries.find(id);
  if (it != impl_->entries.end())
    it->second->connection->ConsumeStream(stream, bytes);
}
void QuicProvider::ResetStream(std::uint64_t id, std::int64_t stream) {
  const auto it = impl_->entries.find(id);
  if (it != impl_->entries.end())
    it->second->connection->ResetStream(stream);
}
void QuicProvider::FinishStream(std::uint64_t id, std::int64_t stream) {
  const auto it = impl_->entries.find(id);
  if (it != impl_->entries.end())
    it->second->connection->FinishStream(stream);
}

std::optional<QuicConnectionStats>
QuicProvider::Statistics(std::uint64_t id) const {
  const auto found = impl_->entries.find(id);
  if (found == impl_->entries.end())
    return std::nullopt;
  return found->second->connection->Statistics();
}

void QuicProvider::Close(std::uint64_t connection_id, std::string detail) {
  impl_->Erase(connection_id, QuicProviderEventType::closed, std::move(detail));
}

std::vector<QuicProviderEvent> QuicProvider::TakeEvents() {
  std::vector<QuicProviderEvent> result;
  result.swap(impl_->events);
  return result;
}

QuicLoopbackResult RunQuicLoopbackProbe(std::string_view payload,
                                        std::chrono::milliseconds timeout) {
  static std::mutex probe_mutex;
  const std::lock_guard<std::mutex> probe_lock(probe_mutex);
  QuicLoopbackResult result;
  if (payload.empty() || payload.size() > 64 * 1024 || timeout.count() <= 0) {
    result.error = "invalid QUIC loopback probe arguments";
    return result;
  }
  if (!AcquireCryptoRuntime()) {
    result.error = "ngtcp2 OpenSSL initialization failed";
    return result;
  }
  struct CryptoCleanup {
    ~CryptoCleanup() { ReleaseCryptoRuntime(); }
  } cleanup;
  SslContext client_context = CreateClientContext();
  SslContext server_context = CreateServerContext();
  if (!client_context || !server_context) {
    result.error = "QUIC TLS context initialization failed";
    return result;
  }
  Connection client(Connection::Role::client, client_context.get(),
                    *Endpoint::Parse("127.0.0.1", 41001),
                    *Endpoint::Parse("127.0.0.1", 41002));
  Connection server(Connection::Role::server, server_context.get(),
                    *Endpoint::Parse("127.0.0.1", 41002),
                    *Endpoint::Parse("127.0.0.1", 41001));
  UdpLoopbackLink link;
  if (!link.Initialize(&client, &server)) {
    result.error = link.error();
    return result;
  }
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  if (!client.StartClient() || !link.Pump(client, server, deadline, [&] {
        return client.handshake_completed() && server.handshake_completed();
      })) {
    result.error = !client.error().empty()   ? client.error()
                   : !server.error().empty() ? server.error()
                   : !link.error().empty()   ? link.error()
                                             : "QUIC handshake timeout";
    return result;
  }
  result.handshake_completed = true;
  result.udp_socket_io = true;
  result.early_data_enabled = client.early_data_accepted();
  result.alpn = client.alpn();
  result.cipher = client.cipher();
  if (result.alpn != "sovkit/1" || result.cipher.empty()) {
    result.error = "QUIC TLS negotiation result is invalid";
    return result;
  }
  if (!client.QueueStream(payload) || !link.Pump(client, server, deadline, [&] {
        return server.received().size() == payload.size();
      })) {
    result.error = !client.error().empty()   ? client.error()
                   : !server.error().empty() ? server.error()
                   : !link.error().empty()   ? link.error()
                                             : "QUIC request timeout";
    return result;
  }
  if (!std::equal(server.received().begin(), server.received().end(),
                  payload.begin(), payload.end()) ||
      !server.QueueStream(payload, server.received_stream_id()) ||
      !link.Pump(client, server, deadline,
                 [&] { return client.received().size() == payload.size(); })) {
    result.error = !client.error().empty()   ? client.error()
                   : !server.error().empty() ? server.error()
                   : !link.error().empty()   ? link.error()
                                             : "QUIC response timeout";
    return result;
  }
  result.bidirectional_stream_completed =
      std::equal(client.received().begin(), client.received().end(),
                 payload.begin(), payload.end()) &&
      client.stream_send_completed() && server.stream_send_completed();
  if (!result.bidirectional_stream_completed)
    result.error = "QUIC bidirectional stream verification failed";
  result.packet_count = link.packet_count();
  return result;
}

} // namespace libnet
