#if !defined(__5E8FE5ED_075D_4376_80A7_313F619C3C4F__)
#define __5E8FE5ED_075D_4376_80A7_313F619C3C4F__

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <limits>
#include <string>
#include <string_view>
#include <uriparser/Uri.h>

class GURL {
public:
  inline GURL() = default;
  inline explicit GURL(std::string url) {
    Reset(std::move(url));
  }
  inline explicit GURL(const char *url) {
    Reset(url ? std::string(url) : std::string());
  }
  inline ~GURL() = default;
  inline GURL(const GURL &) = default;
  inline GURL(GURL &&) noexcept = default;
  inline GURL &operator=(const GURL &) = default;
  inline GURL &operator=(GURL &&) noexcept = default;

  inline void Reset(std::string url) {
    Clear();
    original_ = std::move(url);
    spec_ = TrimAscii(original_);
    Parse();
  }

  inline bool is_empty() const {
    return spec_.empty();
  }
  inline bool is_valid() const {
    return valid_;
  }
  inline bool valid() const {
    return is_valid();
  }

  inline const std::string &spec() const {
    return spec_;
  }
  inline const std::string &possibly_invalid_spec() const {
    return spec_;
  }
  inline const std::string &source() const {
    return spec_;
  }
  inline const std::string &scheme() const {
    return scheme_;
  }
  inline const std::string &host() const {
    return host_;
  }
  inline const std::string &path() const {
    return path_;
  }
  inline const std::string &query() const {
    return query_;
  }
  inline const std::string &ref() const {
    return fragment_;
  }
  inline const std::string &fragment() const {
    return fragment_;
  }
  inline const std::string &username() const {
    return username_;
  }
  inline const std::string &password() const {
    return password_;
  }

  inline bool has_scheme() const {
    return !scheme_.empty();
  }
  inline bool has_host() const {
    return !host_.empty();
  }
  inline bool has_port() const {
    return port_specified_;
  }
  inline bool has_query() const {
    return !query_.empty();
  }
  inline bool has_ref() const {
    return !fragment_.empty();
  }

  inline unsigned short port() const {
    return static_cast<unsigned short>(effective_port_);
  }
  inline int IntPort() const {
    return port_specified_ ? port_ : -1;
  }
  inline int EffectiveIntPort() const {
    return effective_port_;
  }
  inline std::string port_string() const {
    return port_specified_ ? std::to_string(port_) : std::string();
  }

  inline std::string host_port_pair() const {
    if (host_.empty())
      return {};
    if (!port_specified_)
      return host_;
    return host_ + ":" + std::to_string(port_);
  }
  inline std::string authority() const {
    std::string result;
    if (!username_.empty()) {
      result.append(username_);
      if (!password_.empty())
        result.append(":").append(password_);
      result.push_back('@');
    }
    result.append(host_port_pair());
    return result;
  }
  inline std::string GetOrigin() const {
    if (scheme_.empty() || host_.empty())
      return {};
    std::string result = scheme_ + "://" + host_;
    if (port_specified_ && effective_port_ != DefaultPortForScheme(scheme_))
      result.append(":").append(std::to_string(port_));
    return result;
  }
  inline std::string PathForRequest() const {
    std::string result = path_.empty() ? "/" : path_;
    if (!query_.empty())
      result.append("?").append(query_);
    return result;
  }

  inline bool SchemeIs(std::string_view scheme) const {
    return EqualsLowerAscii(scheme_, scheme);
  }
  inline bool SchemeIsHTTPOrHTTPS() const {
    return SchemeIs("http") || SchemeIs("https");
  }
  inline bool HostIsIPAddress() const {
    if (host_.empty())
      return false;
    bool has_colon = false;
    bool has_dot = false;
    for (unsigned char ch : host_) {
      if (std::isdigit(ch) || ch == '.')
        has_dot = has_dot || ch == '.';
      else if (std::isxdigit(ch) || ch == ':')
        has_colon = has_colon || ch == ':';
      else
        return false;
    }
    return has_dot || has_colon;
  }
  inline bool DomainIs(std::string_view domain) const {
    if (domain.empty() || host_.empty())
      return false;
    std::string needle = ToLowerAscii(domain);
    std::string haystack = ToLowerAscii(host_);
    if (!needle.empty() && needle.front() == '.')
      needle.erase(needle.begin());
    if (needle.empty())
      return false;
    if (haystack == needle)
      return true;
    if (haystack.size() <= needle.size())
      return false;
    const size_t offset = haystack.size() - needle.size();
    return haystack[offset - 1] == '.' &&
           haystack.compare(offset, needle.size(), needle) == 0;
  }

  inline bool operator==(const GURL &other) const {
    return spec_ == other.spec_;
  }
  inline bool operator!=(const GURL &other) const {
    return !(*this == other);
  }

private:
  static inline std::string ToLowerAscii(std::string_view value) {
    std::string result;
    result.reserve(value.size());
    for (unsigned char ch : value)
      result.push_back(static_cast<char>(std::tolower(ch)));
    return result;
  }

  static inline bool EqualsLowerAscii(const std::string &lhs,
                                      std::string_view rhs) {
    if (lhs.size() != rhs.size())
      return false;
    for (size_t i = 0; i < lhs.size(); ++i) {
      if (static_cast<unsigned char>(lhs[i]) !=
          static_cast<unsigned char>(std::tolower(
              static_cast<unsigned char>(rhs[i])))) {
        return false;
      }
    }
    return true;
  }

  static inline std::string TrimAscii(const std::string &value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
      return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
  }

  static inline std::string PieceToString(const UriTextRangeA &range) {
    if (!range.first || !range.afterLast || range.afterLast < range.first)
      return {};
    return std::string(range.first, range.afterLast);
  }

  static inline int DefaultPortForScheme(const std::string &scheme) {
    if (scheme == "http")
      return 80;
    if (scheme == "https")
      return 443;
    if (scheme == "ws")
      return 80;
    if (scheme == "wss")
      return 443;
    if (scheme == "ftp")
      return 21;
    return 0;
  }

  static inline bool ParsePort(const std::string &value, int &port) {
    if (value.empty())
      return false;
    int parsed = 0;
    for (unsigned char ch : value) {
      if (!std::isdigit(ch))
        return false;
      parsed = parsed * 10 + (ch - '0');
      if (parsed > std::numeric_limits<unsigned short>::max())
        return false;
    }
    port = parsed;
    return true;
  }

  inline void Clear() {
    original_.clear();
    spec_.clear();
    scheme_.clear();
    username_.clear();
    password_.clear();
    host_.clear();
    path_.clear();
    query_.clear();
    fragment_.clear();
    port_ = -1;
    effective_port_ = 0;
    port_specified_ = false;
    valid_ = false;
  }

  inline void Parse() {
    if (spec_.empty())
      return;

    UriUriA uri{};
    UriParserStateA state;
    state.uri = &uri;
    if (uriParseUriA(&state, spec_.c_str()) != URI_SUCCESS)
      return;

    scheme_ = ToLowerAscii(PieceToString(uri.scheme));
    username_ = PieceToString(uri.userInfo);
    const size_t password_sep = username_.find(':');
    if (password_sep != std::string::npos) {
      password_ = username_.substr(password_sep + 1);
      username_.erase(password_sep);
    }
    host_ = ToLowerAscii(PieceToString(uri.hostText));
    query_ = PieceToString(uri.query);
    fragment_ = PieceToString(uri.fragment);

    const std::string portText = PieceToString(uri.portText);
    if (!portText.empty()) {
      port_specified_ = true;
      if (!ParsePort(portText, port_)) {
        uriFreeUriMembersA(&uri);
        ClearButKeepSpec();
        return;
      }
    }

    path_.clear();
    UriPathSegmentA *seg = uri.pathHead;
    while (seg) {
      path_.push_back('/');
      path_.append(PieceToString(seg->text));
      seg = seg->next;
    }
    if (path_.empty())
      path_ = "/";

    effective_port_ = port_specified_ ? port_ : DefaultPortForScheme(scheme_);
    valid_ = !scheme_.empty();

    uriFreeUriMembersA(&uri);
  }

  inline void ClearButKeepSpec() {
    const std::string original = std::move(original_);
    const std::string spec = std::move(spec_);
    Clear();
    original_ = original;
    spec_ = spec;
  }

  std::string original_;
  std::string spec_;
  std::string scheme_;
  std::string username_;
  std::string password_;
  std::string host_;
  std::string path_{"/"};
  std::string query_;
  std::string fragment_;
  int port_ = -1;
  int effective_port_ = 0;
  bool port_specified_ = false;
  bool valid_ = false;
};

/// /*_ Memade®（新生™） _**/
/// /*_____ https://www.skstu.com/ _____ **/
#endif ///__5E8FE5ED_075D_4376_80A7_313F619C3C4F__
