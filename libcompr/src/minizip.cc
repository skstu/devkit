#include <libcompr.h>
#include <fstream>
#include <zlib.h>
#include <sstream>
#include <vector>
#include <minizip/unzip.h>
#include <minizip/zip.h>
#if !defined(_WIN32)
#include <sys/stat.h>
#include <unistd.h>
#include <filesystem>
#endif
#if !defined(MAX_PATH)
#define MAX_PATH 260
#endif
namespace {
constexpr size_t kZipIoBufferSize = 64 * 1024;
thread_local std::string g_zip_last_error;

void SetZipLastError(const std::string &message) { g_zip_last_error = message; }

std::string ZipStatusName(int status) {
  switch (status) {
  case UNZ_OK:
    return "UNZ_OK";
  case UNZ_END_OF_LIST_OF_FILE:
    return "UNZ_END_OF_LIST_OF_FILE";
  case UNZ_ERRNO:
    return "UNZ_ERRNO";
  case UNZ_PARAMERROR:
    return "UNZ_PARAMERROR";
  case UNZ_BADZIPFILE:
    return "UNZ_BADZIPFILE";
  case UNZ_INTERNALERROR:
    return "UNZ_INTERNALERROR";
  case UNZ_CRCERROR:
    return "UNZ_CRCERROR";
  default:
    break;
  }
  std::ostringstream oss;
  oss << "UNZ_STATUS_" << status;
  return oss.str();
}

std::string ZipFileError(const std::string &entry, const std::string &action,
                         int status) {
  std::ostringstream oss;
  oss << "zip " << action << " failed";
  if (!entry.empty())
    oss << " entry=" << entry;
  oss << " status=" << ZipStatusName(status) << "(" << status << ")";
  return oss.str();
}

bool IsZipDirEntry(const std::string &name) {
  return !name.empty() && (name.back() == '/' || name.back() == '\\');
}

bool IsSafeZipEntryName(const std::string &name) {
  if (name.empty())
    return false;
  const stl::path entry(name);
  if (entry.is_absolute())
    return false;
  for (const auto &part : entry) {
    if (part.string() == "..")
      return false;
  }
  return true;
}

bool ReadCurrentZipFileInfo(unzFile zip_file, unz_file_info &file_info,
                            std::string &filename_in_zip) {
  file_info = {};
  int status = unzGetCurrentFileInfo(zip_file, &file_info, nullptr, 0, nullptr,
                                     0, nullptr, 0);
  if (UNZ_OK != status) {
    SetZipLastError(ZipFileError("", "read info", status));
    return false;
  }

  std::vector<char> filename(file_info.size_filename + 1, 0);
  status = unzGetCurrentFileInfo(zip_file, &file_info, filename.data(),
                                 static_cast<uLong>(filename.size()), nullptr,
                                 0, nullptr, 0);
  if (UNZ_OK != status) {
    SetZipLastError(ZipFileError("", "read name", status));
    return false;
  }
  filename_in_zip.assign(filename.data(), file_info.size_filename);
  if (!IsSafeZipEntryName(filename_in_zip)) {
    SetZipLastError("zip unsafe entry name: " + filename_in_zip);
    return false;
  }
  return true;
}

bool PrepareZipOutputFile(const stl::path &file) {
  if (!stl::Utils::MakeDirectory(file.parent_path())) {
    SetZipLastError("zip create parent directory failed: " + file.string());
    return false;
  }
  if (stl::Utils::FileExists(file) && !stl::Utils::RemoveFile(file)) {
    SetZipLastError("zip remove existing file failed: " + file.string());
    return false;
  }
  return true;
}

bool ReadCurrentZipFileToBuffer(unzFile zip_file, const unz_file_info &file_info,
                                std::string &fileBuffer) {
  if (UNZ_OK != unzOpenCurrentFile(zip_file))
    return false;

  bool ok = true;
  size_t written = 0;
  std::vector<char> buffer(kZipIoBufferSize);
  while (ok) {
    const int read_bytes =
        unzReadCurrentFile(zip_file, buffer.data(),
                           static_cast<unsigned int>(buffer.size()));
    if (read_bytes < 0) {
      ok = false;
      break;
    }
    if (read_bytes == 0)
      break;
    fileBuffer.append(buffer.data(), static_cast<size_t>(read_bytes));
    written += static_cast<size_t>(read_bytes);
  }

  const int close_status = unzCloseCurrentFile(zip_file);
  if (close_status != UNZ_OK)
    ok = false;
  if (ok && written != static_cast<size_t>(file_info.uncompressed_size))
    ok = false;
  return ok;
}

bool WriteCurrentZipFileToDisk(
    unzFile zip_file, const stl::path &output_file,
    const std::string &filename_in_zip,
    const unz_file_info &file_info,
    const Compress::tfzipUnCompressProgressCb &progress_cb, void *route,
    size_t progress_total, size_t &progress_current) {
  int status = unzOpenCurrentFile(zip_file);
  if (UNZ_OK != status) {
    SetZipLastError(ZipFileError(filename_in_zip, "open entry", status));
    return false;
  }
  bool ok = PrepareZipOutputFile(output_file);
  size_t written = 0;
  std::ofstream output;
  if (ok) {
    output.open(output_file, std::ios::binary | std::ios::out | std::ios::trunc);
    if (!output.is_open()) {
      SetZipLastError("zip open output file failed: " + output_file.string());
      ok = false;
    }
  }

  std::vector<char> buffer(kZipIoBufferSize);
  while (ok) {
    const int read_bytes =
        unzReadCurrentFile(zip_file, buffer.data(),
                           static_cast<unsigned int>(buffer.size()));
    if (read_bytes < 0) {
      SetZipLastError(ZipFileError(filename_in_zip, "read entry", read_bytes));
      ok = false;
      break;
    }
    if (read_bytes == 0)
      break;

    output.write(buffer.data(), read_bytes);
    if (!output.good()) {
      SetZipLastError("zip write output file failed: " + output_file.string());
      ok = false;
      break;
    }
    written += static_cast<size_t>(read_bytes);
    if (progress_cb) {
      progress_current += static_cast<size_t>(read_bytes);
      if (!progress_cb(progress_total, progress_current, route)) {
        SetZipLastError("zip extraction cancelled by progress callback");
        ok = false;
        break;
      }
    }
  }

  if (output.is_open()) {
    output.flush();
    if (!output.good()) {
      SetZipLastError("zip flush output file failed: " + output_file.string());
      ok = false;
    }
    output.close();
    if (!output.good()) {
      SetZipLastError("zip close output file failed: " + output_file.string());
      ok = false;
    }
  }

  const int close_status = unzCloseCurrentFile(zip_file);
  if (close_status != UNZ_OK) {
    SetZipLastError(
        ZipFileError(filename_in_zip, "close entry", close_status));
    ok = false;
  }
  if (ok && written != static_cast<size_t>(file_info.uncompressed_size)) {
    std::ostringstream oss;
    oss << "zip extracted size mismatch entry=" << filename_in_zip
        << " expected=" << file_info.uncompressed_size << " actual="
        << written;
    SetZipLastError(oss.str());
    ok = false;
  }
  if (ok &&
      stl::Utils::FileSize(output_file) !=
          static_cast<size_t>(file_info.uncompressed_size)) {
    std::ostringstream oss;
    oss << "zip output file size mismatch entry=" << filename_in_zip
        << " expected=" << file_info.uncompressed_size << " actual="
        << stl::Utils::FileSize(output_file);
    SetZipLastError(oss.str());
    ok = false;
  }
  if (!ok)
    stl::Utils::RemoveFile(output_file);
  return ok;
}

bool CalculateZipUncompressedTotal(unzFile zip_file, size_t &total) {
  total = 0;
  if (UNZ_OK != unzGoToFirstFile(zip_file))
    return false;
  bool ok = true;
  do {
    unz_file_info file_info;
    std::string filename_in_zip;
    if (!ReadCurrentZipFileInfo(zip_file, file_info, filename_in_zip)) {
      ok = false;
      break;
    }
    if (!IsZipDirEntry(filename_in_zip))
      total += static_cast<size_t>(file_info.uncompressed_size);
  } while (unzGoToNextFile(zip_file) == UNZ_OK);
  if (UNZ_OK != unzGoToFirstFile(zip_file))
    ok = false;
  return ok && total > 0;
}
} // namespace

bool Compress::zipUnCompress(const stl::path &inputZipFile,
                             const tfzipUnCompressCb &uncompress_cb,
                             void *route) {
  bool result = false;
  SetZipLastError("");
  unzFile zip_file = NULL;
  do {
    if (!uncompress_cb)
      break;
    if (!stl::Utils::FileExists(inputZipFile))
      break;
    zip_file = unzOpen(inputZipFile.string().c_str());
    if (!zip_file)
      break;
    if (UNZ_OK != unzGoToFirstFile(zip_file))
      break;
    bool ok = true;
    do {
      unz_file_info file_info;
      std::string filename_in_zip;
      if (!ReadCurrentZipFileInfo(zip_file, file_info, filename_in_zip)) {
        ok = false;
        break;
      }
      // convert filename (utf-8 null-terminated) from zip to stl::path (u16)
      std::u16string strfilename_in_zip =
          stl::path(filename_in_zip).u16string();
      if (!strfilename_in_zip.empty() &&
          !(*std::prev(strfilename_in_zip.end()) == u'\\' ||
            *std::prev(strfilename_in_zip.end()) == u'/')) {
        std::string fileBuffer;
        if (!ReadCurrentZipFileToBuffer(zip_file, file_info, fileBuffer)) {
          ok = false;
          break;
        }
        if (!uncompress_cb(strfilename_in_zip, fileBuffer, route)) {
          ok = false;
          break;
        }
      }
    } while (unzGoToNextFile(zip_file) == UNZ_OK);
    result = ok;
  } while (0);
  if (zip_file) {
    unzClose(zip_file);
    zip_file = NULL;
  }
  return result;
}

bool Compress::zipUnCompress(const stl::path &inputZipFile,
                             const stl::path &outputUnzipPath) {
  bool result = false;
  SetZipLastError("");
  unzFile zip_file = NULL;
  do {
    if (!stl::Utils::FileExists(inputZipFile))
      break;
    zip_file = unzOpen(inputZipFile.string().c_str());
    if (!zip_file)
      break;
    if (UNZ_OK != unzGoToFirstFile(zip_file))
      break;
    if (!stl::Utils::MakeDirectory(outputUnzipPath))
      break;
    bool ok = true;
    do {
      unz_file_info file_info;
      std::string filename_in_zip;
      if (!ReadCurrentZipFileInfo(zip_file, file_info, filename_in_zip)) {
        ok = false;
        break;
      }
      const stl::path filename_in_zip_full =
          (outputUnzipPath / filename_in_zip).lexically_normal();

      // A trailing '/' or '\' in the entry name means it is a directory.
      const std::string fname_str(filename_in_zip);

      if (IsZipDirEntry(fname_str)) {
        if (!stl::Utils::MakeDirectory(filename_in_zip_full)) {
          ok = false;
          break;
        }
      } else {
        size_t progress_current = 0;
        size_t progress_total = 0;
        if (!WriteCurrentZipFileToDisk(zip_file, filename_in_zip_full,
                                       filename_in_zip, file_info, nullptr,
                                       nullptr, progress_total,
                                       progress_current)) {
          ok = false;
          break;
        }
      }
    } while (unzGoToNextFile(zip_file) == UNZ_OK);
    result = ok;
  } while (0);
  if (zip_file) {
    unzClose(zip_file);
    zip_file = NULL;
  }
  return result;
}

bool Compress::zipUnCompress(const stl::path &inputZipFile,
                             const stl::path &outputUnzipPath,
                             const tfzipUnCompressProgressCb &progress_cb,
                             void *route) {
  bool result = false;
  SetZipLastError("");
  unzFile zip_file = NULL;
  do {
    if (!stl::Utils::FileExists(inputZipFile))
      break;
    zip_file = unzOpen(inputZipFile.string().c_str());
    if (!zip_file)
      break;
    if (UNZ_OK != unzGoToFirstFile(zip_file))
      break;
    std::size_t total = 0;
    if (!CalculateZipUncompressedTotal(zip_file, total))
      break;
    std::size_t current = 0;
    if (!stl::Utils::MakeDirectory(outputUnzipPath))
      break;
    bool ok = true;
    do {
      unz_file_info file_info;
      std::string filename_in_zip;
      if (!ReadCurrentZipFileInfo(zip_file, file_info, filename_in_zip)) {
        ok = false;
        break;
      }
      const stl::path filename_in_zip_full =
          (outputUnzipPath / filename_in_zip).lexically_normal();

      const std::string fname_str(filename_in_zip);

      if (IsZipDirEntry(fname_str)) {
        if (!stl::Utils::MakeDirectory(filename_in_zip_full)) {
          ok = false;
          break;
        }
      } else {
        if (!WriteCurrentZipFileToDisk(zip_file, filename_in_zip_full,
                                       filename_in_zip, file_info, progress_cb,
                                       route, total, current)) {
          ok = false;
          break;
        }
      }
    } while (unzGoToNextFile(zip_file) == UNZ_OK);
    result = ok;
  } while (0);
  if (zip_file) {
    unzClose(zip_file);
    zip_file = NULL;
  }
  return result;
}

std::string Compress::zipLastError() { return g_zip_last_error; }

void Compress::zipApplyUnixAttrs(const stl::path &inputZipFile,
                                 const stl::path &extractedDir) {
#if !defined(_WIN32)
  unzFile zf = unzOpen(inputZipFile.string().c_str());
  if (!zf)
    return;
  if (UNZ_OK != unzGoToFirstFile(zf)) {
    unzClose(zf);
    return;
  }
  char fname[MAX_PATH];
  unz_file_info fi;
  do {
    if (unzGetCurrentFileInfo(zf, &fi, fname, sizeof(fname),
                              nullptr, 0, nullptr, 0) != UNZ_OK)
      break;
    const uint32_t unix_mode = static_cast<uint32_t>(fi.external_fa >> 16);
    if (unix_mode == 0)
      continue;
    const stl::path full_path = (extractedDir / fname).lexically_normal();
    if (S_ISLNK(unix_mode)) {
      if (UNZ_OK == unzOpenCurrentFile(zf)) {
        char link_target[MAX_PATH] = {0};
        const int n =
            unzReadCurrentFile(zf, link_target, sizeof(link_target) - 1);
        unzCloseCurrentFile(zf);
        if (n > 0) {
          std::error_code ec;
          std::filesystem::remove(full_path, ec);
          ::symlink(link_target, full_path.string().c_str());
        }
      }
    } else {
      ::chmod(full_path.string().c_str(), unix_mode & 07777);
    }
  } while (unzGoToNextFile(zf) == UNZ_OK);
  unzClose(zf);
#else
  (void)inputZipFile;
  (void)extractedDir;
#endif
}

bool Compress::zipCompressDirectory(const stl::path &inputDirpath,
                                    const stl::path &outputZipfile) {
  bool result = false;
  zipFile zf = NULL;
  do {
    std::map<stl::path, stl::path> dirs, files;
    stl::Utils::EnumDirectory(inputDirpath, dirs, files, true);

    zf = zipOpen(outputZipfile.string().c_str(), APPEND_STATUS_CREATE);
    if (zf == NULL)
      break;
    result = true;
    for (auto &node : files) {
      do {
        if (ZIP_OK != zipOpenNewFileInZip4_64(
                          zf, node.first.string().c_str(), NULL, NULL, 0, NULL,
                          0, NULL, Z_DEFLATED, Z_BEST_COMPRESSION, 0,
                          -MAX_WBITS, DEF_MEM_LEVEL, Z_DEFAULT_STRATEGY, NULL,
                          0, 36, 1 << 11, 0)) {
          result = false;
          break;
        }
        std::string buffer = stl::Utils::ReadFile(node.second);
        if (ZIP_OK !=
            zipWriteInFileInZip(zf, buffer.data(),
                                static_cast<unsigned int>(buffer.size()))) {
          result = false;
          break;
        }
      } while (0);
      zipCloseFileInZip(zf);
      if (!result)
        break;
    }
  } while (0);
  if (zf) {
    zipClose(zf, NULL);
    zf = NULL;
  }
  return result;
}
