#include <libnet_downloader.h>

#include <curl/curl.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <memory>
#include <mutex>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace libnet {
namespace {

class CurlGlobal final {
public:
  CurlGlobal() : ok_(curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK) {}
  ~CurlGlobal() { curl_global_cleanup(); }
  bool ok() const { return ok_; }

private:
  bool ok_;
};

bool EnsureCurlGlobal() {
  static CurlGlobal global;
  return global.ok();
}

bool IsHttpUrl(const std::string &url) {
  if (!EnsureCurlGlobal())
    return false;
  CURLU *handle = curl_url();
  if (!handle)
    return false;
  const CURLUcode set_result =
      curl_url_set(handle, CURLUPART_URL, url.c_str(), CURLU_NON_SUPPORT_SCHEME);
  char *scheme = nullptr;
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

std::uint64_t FileSize(const std::filesystem::path &path) {
  std::error_code error;
  const auto size = std::filesystem::file_size(path, error);
  return error ? 0 : static_cast<std::uint64_t>(size);
}

void RemoveFile(const std::filesystem::path &path) {
  std::error_code error;
  std::filesystem::remove(path, error);
}

bool CreateParentDirectory(const std::filesystem::path &target,
                           std::string &error) {
  const auto parent = target.parent_path();
  if (parent.empty())
    return true;
  std::error_code filesystem_error;
  std::filesystem::create_directories(parent, filesystem_error);
  if (!filesystem_error)
    return true;
  error = "create target directory failed: " + filesystem_error.message();
  return false;
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

struct ProbeResult {
  bool ok = false;
  bool ranges = false;
  bool content_range_total = false;
  std::uint64_t content_length = 0;
  long status_code = 0;
  CURLcode curl_code = CURLE_OK;
  std::string error;
};

std::string HeaderValue(const std::string &line, std::string_view name) {
  const auto colon = line.find(':');
  if (colon == std::string::npos ||
      Lower(Trim(line.substr(0, colon))) != Lower(std::string(name))) {
    return {};
  }
  return Trim(line.substr(colon + 1));
}

bool ParseUnsigned(std::string_view value, std::uint64_t &result) {
  if (value.empty())
    return false;
  const std::string text(value);
  char *end = nullptr;
  const auto parsed = std::strtoull(text.c_str(), &end, 10);
  if (!end || *end != '\0')
    return false;
  result = static_cast<std::uint64_t>(parsed);
  return true;
}

std::size_t ProbeHeader(char *data, std::size_t size, std::size_t count,
                        void *user_data) {
  const std::size_t bytes = size * count;
  auto *probe = static_cast<ProbeResult *>(user_data);
  const std::string line(data, bytes);
  const std::string accept_ranges = HeaderValue(line, "accept-ranges");
  if (Lower(accept_ranges) == "bytes")
    probe->ranges = true;

  const std::string content_length = HeaderValue(line, "content-length");
  std::uint64_t parsed = 0;
  if (ParseUnsigned(content_length, parsed))
    probe->content_length = parsed;

  const std::string content_range = HeaderValue(line, "content-range");
  const auto slash = content_range.find('/');
  if (slash != std::string::npos &&
      ParseUnsigned(Trim(content_range.substr(slash + 1)), parsed)) {
    probe->ranges = true;
    probe->content_range_total = true;
    probe->content_length = parsed;
  }
  return bytes;
}

std::size_t StopBody(char *, std::size_t, std::size_t, void *) {
  return 0;
}

curl_slist *BuildHeaders(const DownloadOptions &options) {
  curl_slist *headers = nullptr;
  for (const auto &[name, value] : options.headers) {
    if (!name.empty())
      headers = curl_slist_append(headers, (name + ": " + value).c_str());
  }
  return headers;
}

void ApplyOptions(CURL *curl, const std::string &url,
                  const DownloadOptions &options, curl_slist *headers) {
  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_MAXREDIRS, options.max_redirects);
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, options.connect_timeout_ms);
  if (options.timeout_ms > 0)
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, options.timeout_ms);
  curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, options.low_speed_limit);
  curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, options.low_speed_time);
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
  curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
  curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);
  curl_easy_setopt(curl, CURLOPT_USERAGENT, options.user_agent.c_str());
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, options.verify_tls ? 1L : 0L);
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, options.verify_tls ? 2L : 0L);
  if (headers)
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
}

std::string CurlError(CURLcode code, long status_code) {
  if (code != CURLE_OK)
    return curl_easy_strerror(code);
  return "unexpected HTTP status: " + std::to_string(status_code);
}

ProbeResult Probe(const std::string &url, const DownloadOptions &options,
                  bool use_range) {
  ProbeResult result;
  CURL *curl = curl_easy_init();
  if (!curl) {
    result.curl_code = CURLE_FAILED_INIT;
    result.error = "curl_easy_init failed";
    return result;
  }
  curl_slist *headers = BuildHeaders(options);
  ApplyOptions(curl, url, options, headers);
  curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, ProbeHeader);
  curl_easy_setopt(curl, CURLOPT_HEADERDATA, &result);
  if (use_range) {
    curl_easy_setopt(curl, CURLOPT_RANGE, "0-0");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, StopBody);
  } else {
    curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
  }
  result.curl_code = curl_easy_perform(curl);
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &result.status_code);
  result.ok = (result.curl_code == CURLE_OK ||
               (use_range && result.curl_code == CURLE_WRITE_ERROR)) &&
              result.status_code >= 200 && result.status_code < 400;
  if (use_range) {
    result.ranges = result.status_code == 206 && result.content_range_total;
    result.ok = result.ok && result.ranges;
  }
  if (!result.ok)
    result.error = CurlError(result.curl_code, result.status_code);
  curl_slist_free_all(headers);
  curl_easy_cleanup(curl);
  return result;
}

struct ProgressState {
  std::uint64_t total = 0;
  std::uint64_t last = 0;
  std::atomic<std::uint64_t> *downloaded = nullptr;
  std::atomic<bool> *canceled = nullptr;
  std::mutex *callback_mutex = nullptr;
  DownloadProgress *callback = nullptr;
};

int ReportProgress(void *user_data, curl_off_t, curl_off_t downloaded_now,
                   curl_off_t, curl_off_t) {
  auto *state = static_cast<ProgressState *>(user_data);
  if (state->canceled->load(std::memory_order_acquire))
    return 1;
  const auto now = downloaded_now > 0
                       ? static_cast<std::uint64_t>(downloaded_now)
                       : 0;
  if (now >= state->last)
    state->downloaded->fetch_add(now - state->last, std::memory_order_relaxed);
  state->last = now;
  if (state->callback && *state->callback) {
    std::lock_guard<std::mutex> lock(*state->callback_mutex);
    const auto current =
        state->downloaded->load(std::memory_order_relaxed);
    bool keep_downloading = false;
    try {
      keep_downloading = (*state->callback)(state->total, current);
    } catch (...) {
      keep_downloading = false;
    }
    if (!keep_downloading) {
      state->canceled->store(true, std::memory_order_release);
      return 1;
    }
  }
  return 0;
}

struct FileWriter {
  std::ofstream *stream = nullptr;
};

std::size_t WriteFile(char *data, std::size_t size, std::size_t count,
                      void *user_data) {
  const std::size_t bytes = size * count;
  auto *writer = static_cast<FileWriter *>(user_data);
  writer->stream->write(data, static_cast<std::streamsize>(bytes));
  return writer->stream->good() ? bytes : 0;
}

struct TransferResult {
  bool ok = false;
  bool canceled = false;
  long status_code = 0;
  CURLcode curl_code = CURLE_OK;
  std::string error;
};

TransferResult TransferRange(const std::string &url,
                             const std::filesystem::path &path,
                             std::uint64_t begin, std::uint64_t end,
                             const DownloadOptions &options,
                             std::uint64_t total,
                             std::atomic<std::uint64_t> &downloaded,
                             std::atomic<bool> &canceled,
                             std::mutex &progress_mutex,
                             DownloadProgress &progress) {
  TransferResult result;
  const std::uint64_t expected = end - begin + 1;
  for (int attempt = 0; attempt <= options.retry_count; ++attempt) {
    if (canceled.load(std::memory_order_acquire)) {
      result.canceled = true;
      result.error = "download canceled";
      return result;
    }
    std::uint64_t existing = options.resume ? FileSize(path) : 0;
    if (existing > expected) {
      RemoveFile(path);
      existing = 0;
    }
    if (existing == expected) {
      result.ok = true;
      result.status_code = 206;
      return result;
    }
    if (!options.resume)
      RemoveFile(path);

    std::ofstream stream(path, std::ios::binary | std::ios::out |
                                   (existing ? std::ios::app : std::ios::trunc));
    if (!stream.is_open()) {
      result.error = "open part file failed: " + path.string();
      return result;
    }
    CURL *curl = curl_easy_init();
    if (!curl) {
      result.curl_code = CURLE_FAILED_INIT;
      result.error = "curl_easy_init failed";
      return result;
    }
    curl_slist *headers = BuildHeaders(options);
    ApplyOptions(curl, url, options, headers);
    const std::string range =
        std::to_string(begin + existing) + "-" + std::to_string(end);
    curl_easy_setopt(curl, CURLOPT_RANGE, range.c_str());
    FileWriter writer{&stream};
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteFile);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &writer);
    ProgressState progress_state{total, 0, &downloaded, &canceled,
                                 &progress_mutex, &progress};
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, ReportProgress);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &progress_state);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    result.curl_code = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &result.status_code);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    stream.close();

    const std::uint64_t actual = FileSize(path);
    const std::uint64_t counted = existing + progress_state.last;
    if (actual > counted)
      downloaded.fetch_add(actual - counted, std::memory_order_relaxed);
    else if (counted > actual)
      downloaded.fetch_sub(counted - actual, std::memory_order_relaxed);
    if (result.curl_code == CURLE_OK && result.status_code == 206 &&
        actual == expected) {
      result.ok = true;
      return result;
    }
    result.canceled = canceled.load(std::memory_order_acquire) ||
                      result.curl_code == CURLE_ABORTED_BY_CALLBACK;
    result.error = result.canceled
                       ? "download canceled"
                       : CurlError(result.curl_code, result.status_code);
    if (result.canceled || attempt == options.retry_count)
      return result;
    if (options.retry_delay_ms > 0)
      std::this_thread::sleep_for(
          std::chrono::milliseconds(options.retry_delay_ms));
  }
  return result;
}

DownloadResult DownloadSingle(const std::string &url,
                              const std::filesystem::path &target,
                              const DownloadOptions &options,
                              std::uint64_t expected,
                              DownloadProgress progress) {
  DownloadResult result;
  for (int attempt = 0; attempt <= options.retry_count; ++attempt) {
    std::uint64_t existing = options.resume ? FileSize(target) : 0;
    if (expected > 0 && existing == expected) {
      result.ok = true;
      result.status_code = 200;
      result.total_bytes = expected;
      if (progress)
        progress(expected, expected);
      return result;
    }
    if ((expected > 0 && existing > expected) || !options.resume) {
      RemoveFile(target);
      existing = 0;
    }
    std::ofstream stream(target, std::ios::binary | std::ios::out |
                                     (existing ? std::ios::app : std::ios::trunc));
    if (!stream.is_open()) {
      result.error = "open target file failed: " + target.string();
      return result;
    }
    CURL *curl = curl_easy_init();
    if (!curl) {
      result.curl_code = CURLE_FAILED_INIT;
      result.error = "curl_easy_init failed";
      return result;
    }
    curl_slist *headers = BuildHeaders(options);
    ApplyOptions(curl, url, options, headers);
    if (existing > 0)
      curl_easy_setopt(curl, CURLOPT_RESUME_FROM_LARGE,
                       static_cast<curl_off_t>(existing));
    FileWriter writer{&stream};
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteFile);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &writer);
    std::atomic<std::uint64_t> downloaded{existing};
    std::atomic<bool> canceled{false};
    std::mutex progress_mutex;
    ProgressState progress_state{expected, 0, &downloaded, &canceled,
                                 &progress_mutex, &progress};
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, ReportProgress);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &progress_state);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    const CURLcode code = curl_easy_perform(curl);
    result.curl_code = static_cast<int>(code);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &result.status_code);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    stream.close();

    const std::uint64_t actual = FileSize(target);
    const bool status_ok = existing > 0 ? result.status_code == 206
                                        : result.status_code >= 200 &&
                                              result.status_code < 300;
    if (code == CURLE_OK && status_ok &&
        (expected == 0 || actual == expected)) {
      result.ok = true;
      result.total_bytes = actual;
      return result;
    }
    result.canceled = canceled.load(std::memory_order_acquire) ||
                      code == CURLE_ABORTED_BY_CALLBACK;
    if (existing > 0 && result.status_code != 206)
      RemoveFile(target);
    result.error = result.canceled ? "download canceled"
                                   : CurlError(code, result.status_code);
    if (!result.canceled && code == CURLE_OK && expected > 0 &&
        actual != expected) {
      result.error = "download size mismatch: expected " +
                     std::to_string(expected) + ", actual " +
                     std::to_string(actual);
    }
    if (result.canceled || attempt == options.retry_count)
      return result;
    if (options.retry_delay_ms > 0)
      std::this_thread::sleep_for(
          std::chrono::milliseconds(options.retry_delay_ms));
  }
  return result;
}

bool MergeParts(const std::vector<std::filesystem::path> &parts,
                const std::filesystem::path &target, std::string &error) {
  auto merged = target;
  merged += ".merge";
  RemoveFile(merged);
  std::ofstream output(merged, std::ios::binary | std::ios::trunc);
  if (!output.is_open()) {
    error = "open merge file failed: " + merged.string();
    return false;
  }
  std::vector<char> buffer(1024 * 1024);
  for (const auto &part : parts) {
    std::ifstream input(part, std::ios::binary);
    if (!input.is_open()) {
      error = "open part file failed: " + part.string();
      output.close();
      RemoveFile(merged);
      return false;
    }
    while (input) {
      input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
      output.write(buffer.data(), input.gcount());
    }
    if (!output) {
      error = "write merge file failed: " + merged.string();
      output.close();
      RemoveFile(merged);
      return false;
    }
  }
  output.close();
  std::error_code filesystem_error;
  std::filesystem::remove(target, filesystem_error);
  filesystem_error.clear();
  std::filesystem::rename(merged, target, filesystem_error);
  if (!filesystem_error)
    return true;
  error = "replace target failed: " + filesystem_error.message();
  RemoveFile(merged);
  return false;
}

} // namespace

const char *Downloader::Version() {
  return EnsureCurlGlobal() ? curl_version() : "libcurl initialization failed";
}

DownloadResult Downloader::Download(const std::string &url,
                                    const std::filesystem::path &target,
                                    DownloadProgress progress) {
  return Download(url, target, DownloadOptions{}, std::move(progress));
}

DownloadResult Downloader::Download(const std::string &url,
                                    const std::filesystem::path &target,
                                    const DownloadOptions &options,
                                    DownloadProgress progress) {
  DownloadResult result;
  if (!IsHttpUrl(url)) {
    result.curl_code = CURLE_URL_MALFORMAT;
    result.error = "invalid HTTP download URL";
    return result;
  }
  if (target.empty()) {
    result.error = "target path is empty";
    return result;
  }
  if (!CreateParentDirectory(target, result.error))
    return result;

  DownloadOptions normalized = options;
  normalized.parallelism = std::max<std::size_t>(1, normalized.parallelism);
  normalized.parallelism = std::min<std::size_t>(64, normalized.parallelism);
  normalized.min_part_size =
      std::max<std::uint64_t>(1, normalized.min_part_size);
  normalized.retry_count = std::max(0, normalized.retry_count);
  normalized.retry_delay_ms = std::max(0L, normalized.retry_delay_ms);
  normalized.max_redirects = std::max(0L, normalized.max_redirects);

  ProbeResult probe = Probe(url, normalized, false);
  if (!probe.ranges) {
    ProbeResult range_probe = Probe(url, normalized, true);
    if (range_probe.ok && (range_probe.ranges || !probe.ok))
      probe = std::move(range_probe);
  }
  const std::uint64_t total = probe.ok ? probe.content_length : 0;
  if (!probe.ranges || total == 0 || normalized.parallelism == 1 ||
      total / normalized.min_part_size < 2) {
    if (!probe.ranges)
      normalized.resume = false;
    return DownloadSingle(url, target, normalized, total, std::move(progress));
  }

  if (normalized.resume && FileSize(target) == total) {
    result.ok = true;
    result.status_code = 200;
    result.total_bytes = total;
    if (progress)
      progress(total, total);
    return result;
  }
  if (FileSize(target) > 0)
    RemoveFile(target);

  const std::size_t workers = static_cast<std::size_t>(std::min<std::uint64_t>(
      normalized.parallelism, total / normalized.min_part_size));
  const std::uint64_t part_size = (total + workers - 1) / workers;
  std::vector<std::filesystem::path> part_paths;
  part_paths.reserve(workers);
  std::atomic<std::uint64_t> downloaded{0};
  for (std::size_t index = 0; index < workers; ++index) {
    auto part = target;
    part += ".part" + std::to_string(index);
    const std::uint64_t begin = index * part_size;
    const std::uint64_t end = std::min(total - 1, begin + part_size - 1);
    const std::uint64_t expected = end - begin + 1;
    if (!normalized.resume || FileSize(part) > expected)
      RemoveFile(part);
    downloaded.fetch_add(FileSize(part), std::memory_order_relaxed);
    part_paths.push_back(std::move(part));
  }
  if (progress && !progress(total, downloaded.load(std::memory_order_relaxed))) {
    result.canceled = true;
    result.error = "download canceled";
    return result;
  }

  std::atomic<bool> canceled{false};
  std::atomic<bool> failed{false};
  std::mutex progress_mutex;
  std::mutex result_mutex;
  std::vector<std::thread> threads;
  threads.reserve(workers);
  for (std::size_t index = 0; index < workers; ++index) {
    threads.emplace_back([&, index]() {
      const std::uint64_t begin = index * part_size;
      const std::uint64_t end = std::min(total - 1, begin + part_size - 1);
      TransferResult part_result =
          TransferRange(url, part_paths[index], begin, end, normalized, total,
                        downloaded, canceled, progress_mutex, progress);
      if (!part_result.ok) {
        failed.store(true, std::memory_order_release);
        canceled.store(true, std::memory_order_release);
        std::lock_guard<std::mutex> lock(result_mutex);
        if (result.error.empty()) {
          result.canceled = part_result.canceled;
          result.status_code = part_result.status_code;
          result.curl_code = static_cast<int>(part_result.curl_code);
          result.error = std::move(part_result.error);
        }
      }
    });
  }
  for (auto &thread : threads)
    thread.join();
  if (failed.load(std::memory_order_acquire))
    return result;

  if (!MergeParts(part_paths, target, result.error))
    return result;
  if (FileSize(target) != total) {
    result.error = "merged download size mismatch";
    return result;
  }
  for (const auto &part : part_paths)
    RemoveFile(part);
  result.ok = true;
  result.status_code = 200;
  result.total_bytes = total;
  if (progress)
    progress(total, total);
  return result;
}

} // namespace libnet