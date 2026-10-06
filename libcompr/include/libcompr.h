#if !defined(__8F227512_937E_4B7C_9330_5007A91EAE6B__)
#define __8F227512_937E_4B7C_9330_5007A91EAE6B__
#include <libstl.hpp>
class Compress final {
public:
  Compress() = default;
  ~Compress() = default;

public:
  static bool IsBrotli(const std::string &buffer);
  static bool brotliCompress(const std::string &src, std::string &dst);
  static bool brotliUnCompress(const std::string &src, std::string &dst);
  static bool IsZstd(const std::string &buffer);
  static bool zstCompress(const std::string &src, std::string &dst);
  static bool zstUnCompress(const std::string &src, std::string &dst);
  static bool IsZipCompress(const std::string &buffer);
  static bool zipCompress(const std::string &src, std::string &dst);
  static bool zipUnCompress(const std::string &src, const size_t &nraw,
                            std::string &dst);
  static bool gzipCompress(const std::string &src, std::string &dst,
                           int level = -1);
  static bool gzipUnCompress(const std::string &src, std::string &dst);

  typedef bool (*tfzipUnCompressProgressCb)(size_t total, size_t current,
                                            void *route);
  typedef bool (*tfzipUnCompressCb)(const stl::path &path,
                                    const std::string &buffer, void *route);

  struct ArchiveExtractOptions {
    // Untrusted archives must be bounded by default. Set a limit to zero only
    // when the caller has an independent resource-control policy.
    bool overwrite = true;
    bool strip_top_level_dir = false;
    bool preserve_permissions = false;
    bool allow_symlinks = false;
    bool secure_symlinks = true;
    bool skip_macos_metadata = false;
    uint64_t progress_interval_bytes = 1024ULL * 1024ULL;
    uint64_t max_entries = 10000;
    uint64_t max_total_bytes = 1024ULL * 1024ULL * 1024ULL;
    uint64_t max_file_bytes = 256ULL * 1024ULL * 1024ULL;
    uint64_t max_compression_ratio = 200;
  };

  struct ArchiveExtractResult {
    bool ok = false;
    bool progress = false;
    std::string state;
    stl::path archive_path;
    stl::path target_dir;
    uint64_t files = 0;
    uint64_t directories = 0;
    uint64_t skipped = 0;
    uint64_t entries = 0;
    uint64_t bytes = 0;
    uint64_t archive_bytes = 0;
    uint64_t archive_total_bytes = 0;
    uint64_t current_file_bytes = 0;
    std::string current_entry;
    std::string error;
  };

  using tfArchiveExtractProgressCb =
      std::function<bool(const ArchiveExtractResult &)>;

  static bool zipCompressDirectory(const stl::path &inputDirpath,
                                   const stl::path &outputZipfile);
  static bool zipUnCompress(const stl::path &inputZipFile,
                            const stl::path &outputUnzipPath);
  static bool zipUnCompress(const stl::path &inputZipFile,
                            const stl::path &outputUnzipPath,
                            const tfzipUnCompressProgressCb &,void*);
  static bool zipUnCompress(const stl::path &inputZipFile,
                            const tfzipUnCompressCb &uncompress_cb,
                            void *route = nullptr);
  static std::string zipLastError();
  // Apply Unix file-mode bits and recreate symlinks stored in a zip archive
  // onto an already-extracted directory tree.  No-op on Windows.
  // Call this once after zipUnCompress() when targeting macOS or Linux.
  static void zipApplyUnixAttrs(const stl::path &inputZipFile,
                                const stl::path &extractedDir);

  static bool archiveExtract(const stl::path &archivePath,
                             const stl::path &targetDir,
                             const ArchiveExtractOptions &options,
                             const tfArchiveExtractProgressCb &progress_cb = {},
                             ArchiveExtractResult *result = nullptr);
  static bool archiveLooksLike(const std::string &path);
  static bool archiveLooksLikeHeader(const std::string &head);
  static std::string archiveLastError();
  static std::string archiveVersion();
};

/// /*_ Memade®（新生™） _**/
/// /*_ Sun, 17 Nov 2024 01:20:08 GMT _**/
/// /*_____ https://www.skstu.com/ _____ **/
#endif ///__8F227512_937E_4B7C_9330_5007A91EAE6B__
