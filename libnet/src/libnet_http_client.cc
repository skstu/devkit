#include <libnet_http_client.h>

#include <curl/curl.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <string_view>
#include <utility>

namespace libnet {
    namespace {

        class CurlGlobal final {
        public:
            CurlGlobal() : ok_(curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK) {
            }
            ~CurlGlobal() {
                curl_global_cleanup();
            }
            bool ok() const {
                return ok_;
            }

        private:
            bool ok_;
        };

        bool EnsureCurlGlobal() {
            static CurlGlobal global;
            return global.ok();
        }

        std::string Trim(std::string value) {
            const auto first = value.find_first_not_of(" \t\r\n");
            if (first == std::string::npos)
                return {};
            const auto last = value.find_last_not_of(" \t\r\n");
            return value.substr(first, last - first + 1);
        }

        std::string Lower(std::string value) {
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return value;
        }

        bool IsHttpUrl(const std::string& url) {
            if (!EnsureCurlGlobal())
                return false;
            CURLU* handle = curl_url();
            if (!handle)
                return false;
            const CURLUcode set_result =
                curl_url_set(handle, CURLUPART_URL, url.c_str(), CURLU_NON_SUPPORT_SCHEME);
            char* scheme = nullptr;
            const CURLUcode get_result =
                set_result == CURLUE_OK
                    ? curl_url_get(handle, CURLUPART_SCHEME, &scheme, 0)
                    : set_result;
            const bool valid = get_result == CURLUE_OK && scheme &&
                               (std::string_view(scheme) == "http" ||
                                std::string_view(scheme) == "https");
            if (scheme)
                curl_free(scheme);
            curl_url_cleanup(handle);
            return valid;
        }

        bool HasHeader(const HttpClient::Headers& headers, std::string_view name) {
            const std::string expected = Lower(std::string(name));
            return std::any_of(headers.begin(), headers.end(), [&](const auto& entry) {
                return Lower(entry.first) == expected;
            });
        }

        curl_slist* AppendHeaders(curl_slist* list,
                                  const HttpClient::Headers& headers) {
            for (const auto& [name, value] : headers) {
                if (!name.empty())
                    list = curl_slist_append(list, (name + ": " + value).c_str());
            }
            return list;
        }

        curl_slist* BuildHeaders(const HttpClient::Options& options,
                                 const HttpClient::Headers& headers, bool json) {
            curl_slist* list = AppendHeaders(nullptr, options.headers);
            list = AppendHeaders(list, headers);
            if (json && !HasHeader(options.headers, "content-type") &&
                !HasHeader(headers, "content-type")) {
                list = curl_slist_append(list, "Content-Type: application/json");
            }
            return list;
        }

        struct ResponseState {
            HttpClient::Response* response = nullptr;
            HttpClient::HeadersHandler* handler = nullptr;
            HttpClient::Headers current_headers;
            long current_status = 0;
            bool canceled = false;
            const HttpClient::Options* options = nullptr;
        };

        std::size_t WriteHeader(char* data, std::size_t size, std::size_t count,
                                void* user_data) {
            if (size && count > (std::numeric_limits<std::size_t>::max)() / size)
                return 0;
            try {
                const std::size_t bytes = size * count;
                auto* state = static_cast<ResponseState*>(user_data);
                const std::string line(data, bytes);
                if (line.rfind("HTTP/", 0) == 0) {
                    state->current_headers.clear();
                    const auto first_space = line.find(' ');
                    if (first_space != std::string::npos)
                        state->current_status = std::strtol(line.c_str() + first_space + 1,
                                                            nullptr, 10);
                    return bytes;
                }
                if (line == "\r\n") {
                    state->response->headers = state->current_headers;
                    if (state->handler && *state->handler) {
                        bool keep_receiving = false;
                        try {
                            keep_receiving =
                                (*state->handler)(state->current_status, state->current_headers);
                        }
                        catch (...) {
                            keep_receiving = false;
                        }
                        if (!keep_receiving) {
                            state->canceled = true;
                            return 0;
                        }
                    }
                    return bytes;
                }
                const auto colon = line.find(':');
                if (colon != std::string::npos) {
                    const std::string name = Lower(Trim(line.substr(0, colon)));
                    if (!name.empty())
                        state->current_headers[name] = Trim(line.substr(colon + 1));
                }
                return bytes;
            }
            catch (...) {
                return 0;
            }
        }

        std::size_t WriteBody(char* data, std::size_t size, std::size_t count,
                              void* user_data) {
            auto& state = *static_cast<ResponseState*>(user_data);
            if (size && count > (std::numeric_limits<std::size_t>::max)() / size)
                return 0;
            const auto bytes = size * count;
            auto& body = state.response->body;
            if (bytes > state.options->max_response_bytes - body.size())
                return 0;
            try {
                body.append(data, bytes);
            }
            catch (...) {
                return 0;
            }
            return bytes;
        }

        int Progress(void* data, curl_off_t, curl_off_t, curl_off_t, curl_off_t) noexcept {
            auto& state = *static_cast<ResponseState*>(data);
            try {
                if (state.options->canceled && state.options->canceled())
                    state.canceled = true;
            }
            catch (...) {
                state.canceled = true;
            }
            return state.canceled ? 1 : 0;
        }

        std::int64_t InfoMilliseconds(CURL* curl, CURLINFO info) {
            curl_off_t microseconds = 0;
            if (curl_easy_getinfo(curl, info, &microseconds) != CURLE_OK ||
                microseconds <= 0) {
                return 0;
            }
            return static_cast<std::int64_t>((microseconds + 500) / 1000);
        }

        std::int64_t StageDuration(std::int64_t end, std::int64_t begin) {
            return end >= begin ? end - begin : 0;
        }

        void PopulateDiagnostics(CURL* curl, HttpClient::Response& response) {
            curl_easy_getinfo(curl, CURLINFO_OS_ERRNO, &response.os_error);
            curl_easy_getinfo(curl, CURLINFO_HTTP_VERSION, &response.http_version);
            curl_easy_getinfo(curl, CURLINFO_PRIMARY_PORT, &response.primary_port);
            curl_easy_getinfo(curl, CURLINFO_REDIRECT_COUNT, &response.redirect_count);
            curl_easy_getinfo(curl, CURLINFO_SSL_VERIFYRESULT,
                              &response.ssl_verify_result);
            curl_easy_getinfo(curl, CURLINFO_NUM_CONNECTS,
                              &response.connections_created);
            char* content_type = nullptr;
            if (curl_easy_getinfo(curl, CURLINFO_CONTENT_TYPE, &content_type) == CURLE_OK &&
                content_type) {
                response.content_type = content_type;
            }
            const auto dns = InfoMilliseconds(curl, CURLINFO_NAMELOOKUP_TIME_T);
            const auto connected = InfoMilliseconds(curl, CURLINFO_CONNECT_TIME_T);
            const auto tls = InfoMilliseconds(curl, CURLINFO_APPCONNECT_TIME_T);
            const auto ready = InfoMilliseconds(curl, CURLINFO_PRETRANSFER_TIME_T);
            const auto first_byte = InfoMilliseconds(curl, CURLINFO_STARTTRANSFER_TIME_T);
            response.dns_ms = dns;
            response.tcp_connect_ms = StageDuration(connected, dns);
            response.tls_handshake_ms = StageDuration(tls, connected);
            response.request_ready_ms = StageDuration(ready, tls > 0 ? tls : connected);
            response.server_wait_ms = StageDuration(first_byte, ready);
            response.total_ms = InfoMilliseconds(curl, CURLINFO_TOTAL_TIME_T);
        }

        std::string AppendParams(const std::string& url,
                                 const HttpClient::Params& params,
                                 std::string& error) {
            if (params.empty())
                return url;
            CURL* curl = curl_easy_init();
            if (!curl) {
                error = "curl_easy_init failed";
                return {};
            }
            std::string query;
            for (const auto& [name, value] : params) {
                char* escaped_name =
                    curl_easy_escape(curl, name.data(), static_cast<int>(name.size()));
                char* escaped_value =
                    curl_easy_escape(curl, value.data(), static_cast<int>(value.size()));
                if (!escaped_name || !escaped_value) {
                    if (escaped_name)
                        curl_free(escaped_name);
                    if (escaped_value)
                        curl_free(escaped_value);
                    curl_easy_cleanup(curl);
                    error = "query parameter encoding failed";
                    return {};
                }
                if (!query.empty())
                    query += '&';
                query += escaped_name;
                query += '=';
                query += escaped_value;
                curl_free(escaped_name);
                curl_free(escaped_value);
            }
            curl_easy_cleanup(curl);
            const auto fragment = url.find('#');
            const auto insert_at = fragment == std::string::npos ? url.size() : fragment;
            const bool has_query = url.find('?') < insert_at;
            std::string result = url;
            result.insert(insert_at, (has_query ? "&" : "?") + query);
            return result;
        }

        HttpClient::Response Perform(const std::string& method, const std::string& url,
                                     const std::string& body,
                                     const HttpClient::Headers& headers,
                                     const HttpClient::Options& options,
                                     HttpClient::HeadersHandler headers_handler,
                                     bool json) {
            HttpClient::Response response;
            if (method.empty()) {
                response.curl_code = CURLE_BAD_FUNCTION_ARGUMENT;
                response.error = "HTTP method is empty";
                return response;
            }
            if (!IsHttpUrl(url)) {
                response.curl_code = CURLE_URL_MALFORMAT;
                response.error = "invalid HTTP URL";
                return response;
            }
            CURL* curl = curl_easy_init();
            if (!curl) {
                response.curl_code = CURLE_FAILED_INIT;
                response.error = "curl_easy_init failed";
                return response;
            }
            const std::string normalized_method = Lower(method);
            curl_slist* header_list = BuildHeaders(options, headers, json);
            char error_buffer[CURL_ERROR_SIZE] = {};
            curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
            curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, options.follow_redirects ? 1L : 0L);
            curl_easy_setopt(curl, CURLOPT_MAXREDIRS, (std::max)(0L, options.max_redirects));
            curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, options.connect_timeout_ms);
            if (options.timeout_ms > 0)
                curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, options.timeout_ms);
            curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, options.low_speed_limit);
            curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, options.low_speed_time);
            curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
            if (options.accept_encoding)
                curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
            curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, options.tcp_keepalive ? 1L : 0L);
            if (!options.user_agent.empty())
                curl_easy_setopt(curl, CURLOPT_USERAGENT, options.user_agent.c_str());
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, options.verify_tls ? 1L : 0L);
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, options.verify_tls ? 2L : 0L);
            curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, error_buffer);
            if (header_list)
                curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_list);

            curl_blob ca{const_cast<char*>(options.ca_pem.data()), options.ca_pem.size(), CURL_BLOB_COPY};
            if (!options.ca_pem.empty())
                curl_easy_setopt(curl, CURLOPT_CAINFO_BLOB, &ca);
            if (options.proxy)
                curl_easy_setopt(curl, CURLOPT_PROXY, options.proxy->c_str());
            ResponseState state{&response, &headers_handler, {}, 0, false, &options};
            if (options.canceled) {
                curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
                curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, Progress);
                curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &state);
            }
            curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, WriteHeader);
            curl_easy_setopt(curl, CURLOPT_HEADERDATA, &state);
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteBody);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &state);
            if (normalized_method == "head") {
                curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
            }
            else if (normalized_method == "post") {
                curl_easy_setopt(curl, CURLOPT_POST, 1L);
            }
            else if (normalized_method != "get") {
                curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method.c_str());
            }
            if (!body.empty() && normalized_method != "head") {
                curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.data());
                curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE,
                                 static_cast<curl_off_t>(body.size()));
            }

            const CURLcode code = curl_easy_perform(curl);
            response.curl_code = static_cast<int>(code);
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.status_code);
            PopulateDiagnostics(curl, response);
            response.canceled = state.canceled;
            response.ok = code == CURLE_OK && response.status_code >= 200 &&
                          response.status_code < 300;
            if (!response.ok && !response.canceled) {
                if (error_buffer[0])
                    response.error = error_buffer;
                else if (code != CURLE_OK)
                    response.error = curl_easy_strerror(code);
                else
                    response.error = "HTTP status " + std::to_string(response.status_code);
            }
            if (response.canceled)
                response.error = "response canceled";
            curl_slist_free_all(header_list);
            curl_easy_cleanup(curl);
            return response;
        }

    } // namespace

    const char* HttpClient::Version() {
        return EnsureCurlGlobal() ? curl_version() : "libcurl initialization failed";
    }

    HttpClient::Response HttpClient::Request(const std::string& method,
                                             const std::string& url,
                                             const std::string& body,
                                             const Headers& headers) {
        return Request(method, url, body, headers, Options{});
    }

    HttpClient::Response HttpClient::Request(
        const std::string& method, const std::string& url, const std::string& body,
        const Headers& headers, const Options& options,
        HeadersHandler headers_handler) {
        return Perform(method, url, body, headers, options,
                       std::move(headers_handler), false);
    }

    HttpClient::Response HttpClient::Head(const std::string& url,
                                          const Headers& headers) {
        return Head(url, headers, Options{});
    }

    HttpClient::Response HttpClient::Head(const std::string& url,
                                          const Headers& headers,
                                          const Options& options,
                                          HeadersHandler headers_handler) {
        return Perform("HEAD", url, {}, headers, options, std::move(headers_handler),
                       false);
    }

    HttpClient::Response HttpClient::Get(const std::string& url,
                                         const Headers& headers) {
        return Get(url, headers, Options{});
    }

    HttpClient::Response HttpClient::Get(const std::string& url,
                                         const Headers& headers,
                                         const Options& options,
                                         HeadersHandler headers_handler) {
        return Perform("GET", url, {}, headers, options, std::move(headers_handler),
                       false);
    }

    HttpClient::Response HttpClient::GetWithParams(const std::string& url,
                                                   const Params& params,
                                                   const Headers& headers) {
        return GetWithParams(url, params, headers, Options{});
    }

    HttpClient::Response HttpClient::GetWithParams(
        const std::string& url, const Params& params, const Headers& headers,
        const Options& options, HeadersHandler headers_handler) {
        Response response;
        const std::string request_url = AppendParams(url, params, response.error);
        if (request_url.empty()) {
            response.curl_code = CURLE_URL_MALFORMAT;
            return response;
        }
        return Get(request_url, headers, options, std::move(headers_handler));
    }

    HttpClient::Response HttpClient::Post(const std::string& url,
                                          const std::string& body,
                                          const Headers& headers) {
        return Post(url, body, headers, Options{});
    }

    HttpClient::Response HttpClient::Post(const std::string& url,
                                          const std::string& body,
                                          const Headers& headers,
                                          const Options& options,
                                          HeadersHandler headers_handler) {
        return Perform("POST", url, body, headers, options,
                       std::move(headers_handler), false);
    }

    HttpClient::Response HttpClient::PostJson(const std::string& url,
                                              const std::string& body,
                                              const Headers& headers) {
        return PostJson(url, body, headers, Options{});
    }

    HttpClient::Response HttpClient::PostJson(
        const std::string& url, const std::string& body, const Headers& headers,
        const Options& options, HeadersHandler headers_handler) {
        return Perform("POST", url, body, headers, options,
                       std::move(headers_handler), true);
    }

    HttpClient::Response HttpClient::Put(const std::string& url,
                                         const std::string& body,
                                         const Headers& headers) {
        return Put(url, body, headers, Options{});
    }

    HttpClient::Response HttpClient::Put(const std::string& url,
                                         const std::string& body,
                                         const Headers& headers,
                                         const Options& options,
                                         HeadersHandler headers_handler) {
        return Perform("PUT", url, body, headers, options,
                       std::move(headers_handler), false);
    }

    HttpClient::Response HttpClient::PutJson(const std::string& url,
                                             const std::string& body,
                                             const Headers& headers) {
        return PutJson(url, body, headers, Options{});
    }

    HttpClient::Response HttpClient::PutJson(
        const std::string& url, const std::string& body, const Headers& headers,
        const Options& options, HeadersHandler headers_handler) {
        return Perform("PUT", url, body, headers, options,
                       std::move(headers_handler), true);
    }

} // namespace libnet