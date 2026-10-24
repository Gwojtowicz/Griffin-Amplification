// 1.70.3 PACK PICKER: open a capture-pack .zip directly. It's unpacked once into a cache,
// %LOCALAPPDATA%\GriffinAmplification\PackCache\<zip name>-<hash of path + size + time>, and that
// folder is the pack. Unchanged zip: the cache is reused; changed zip: a new cache. The original zip
// is only ever read. Runs on a worker thread (see _PackOpenFolder / _PackPollZip), never the audio thread.
// Only .nam .wav .json .txt are extracted (plus .zip one level deep, unpacked into a subfolder);
// entries with absolute paths, drive letters or ".." are skipped (zip-slip); caps: 2 GB, 5000 files.
// Included from the end of NeuralAmpModeler.cpp (one translation unit, so no project-file change).

#include "miniz/miniz.h"

namespace griffinpack
{
static uint64_t Fnv1a(const std::string& s)
{
  uint64_t h = 1469598103934665603ull;
  for (unsigned char c : s)
  {
    h ^= c;
    h *= 1099511628211ull;
  }
  return h;
}

std::string PackCacheRoot()
{
  const char* base = std::getenv("LOCALAPPDATA");
  return std::string(base ? base : ".") + "\\GriffinAmplification\\PackCache";
}

// A zip entry name as UTF-8: as stored when the zip says it's UTF-8 (flag bit 11), else from code page 437.
static std::string EntryNameUtf8(const mz_zip_archive_file_stat& st)
{
  std::string raw = st.m_filename;
  bool ascii = true;
  for (unsigned char c : raw)
    ascii = ascii && c < 0x80;
  if (ascii || (st.m_bit_flag & (1 << 11)))
    return raw;
#ifdef _WIN32
  const int wn = MultiByteToWideChar(437, 0, raw.c_str(), (int)raw.size(), nullptr, 0);
  std::wstring w(wn, L'\0');
  MultiByteToWideChar(437, 0, raw.c_str(), (int)raw.size(), w.data(), wn);
  return U8(std::filesystem::path(w));
#else
  return raw;
#endif
}

// Zip-slip guard: a relative path with no "..", no drive letter, no leading slash. "" if unsafe.
static std::string SafeRelative(std::string name)
{
  std::replace(name.begin(), name.end(), '\\', '/');
  if (name.empty() || name[0] == '/' || name.find(':') != std::string::npos)
    return "";
  std::string out, part;
  size_t i = 0;
  while (i <= name.size())
  {
    const size_t j = name.find('/', i);
    part = name.substr(i, j == std::string::npos ? std::string::npos : j - i);
    if (part == "..")
      return "";
    if (!part.empty() && part != ".")
      out += (out.empty() ? "" : "/") + part;
    if (j == std::string::npos)
      break;
    i = j + 1;
  }
  if (out.rfind("__MACOSX", 0) == 0)
    return "";
  return out;
}

struct ZipBudget
{
  uint64_t bytes = 0;
  int files = 0;
};

static bool ExtractZip(const std::string& zipPath, const std::filesystem::path& dest, int depth, ZipBudget& budget,
                       std::string& log, std::string& err)
{
  namespace fs = std::filesystem;
  const uint64_t kMaxBytes = 2ull * 1024 * 1024 * 1024;
  const int kMaxFiles = 5000;
  mz_zip_archive zip;
  memset(&zip, 0, sizeof(zip));
  if (!mz_zip_reader_init_file(&zip, zipPath.c_str(), 0))
  {
    err = "can't open the zip (" + std::string(mz_zip_get_error_string(mz_zip_get_last_error(&zip))) + ")";
    return false;
  }
  const mz_uint n = mz_zip_reader_get_num_files(&zip);
  int skipped = 0;
  for (mz_uint i = 0; i < n; i++)
  {
    mz_zip_archive_file_stat st;
    if (!mz_zip_reader_file_stat(&zip, i, &st) || st.m_is_directory)
      continue;
    const std::string rel = SafeRelative(EntryNameUtf8(st));
    if (rel.empty())
    {
      skipped++;
      continue;
    }
    const std::string ext = Lower(U8(fs::u8path(rel).extension()));
    const bool nested = ext == ".zip";
    if (!(ext == ".nam" || ext == ".wav" || ext == ".json" || ext == ".txt" || (nested && depth == 0)))
    {
      skipped++;
      continue;
    }
    if (budget.files + 1 > kMaxFiles || budget.bytes + st.m_uncomp_size > kMaxBytes)
    {
      err = "the pack is too big (over 5000 files or 2 GB)";
      mz_zip_reader_end(&zip);
      return false;
    }
    budget.files++;
    budget.bytes += st.m_uncomp_size;
    const fs::path out = dest / fs::u8path(rel);
    std::error_code ec;
    fs::create_directories(out.parent_path(), ec);
    if (!mz_zip_reader_extract_to_file(&zip, i, U8(out).c_str(), 0))
    {
      log += "    failed: " + rel + "\n";
      continue;
    }
    if (nested)
    {
      // One level deep: into a folder named after the inner zip, then the inner zip file goes.
      const fs::path sub = out.parent_path() / out.stem();
      std::string e2;
      if (!ExtractZip(U8(out), sub, depth + 1, budget, log, e2))
        log += "    inner zip " + rel + ": " + e2 + "\n";
      fs::remove(out, ec);
    }
  }
  mz_zip_reader_end(&zip);
  log += "    " + U8(fs::u8path(zipPath).filename()) + ": " + std::to_string(n) + " entries, " + std::to_string(skipped)
         + " skipped (other types / unsafe paths)\n";
  return true;
}

// The cache folder for a zip, unpacking it if needed. "" + err on failure. Any thread but the audio thread.
std::string ResolveZip(const std::string& zipUtf8, std::string& err, std::string& log, bool& reused)
{
  namespace fs = std::filesystem;
  reused = false;
  std::error_code ec;
  const fs::path zp = fs::u8path(zipUtf8);
  if (!fs::is_regular_file(zp, ec))
  {
    err = "the zip isn't there anymore";
    return "";
  }
  const uint64_t size = fs::file_size(zp, ec);
  const auto mtime = fs::last_write_time(zp, ec).time_since_epoch().count();
  char hash[32];
  snprintf(hash, sizeof(hash), "%016llx",
           (unsigned long long)Fnv1a(Lower(zipUtf8) + "|" + std::to_string(size) + "|" + std::to_string((long long)mtime)));
  std::string stem = U8(zp.stem());
  if (stem.size() > 60)
    stem = stem.substr(0, 60);
  for (char& c : stem)
    if (c == '.' || c == ' ')
      c = c == ' ' ? ' ' : '_';
  const fs::path dir = fs::u8path(PackCacheRoot()) / fs::u8path(stem + "-" + hash);
  if (fs::exists(dir / ".complete", ec))
  {
    reused = true;
    log += "  cache reused: " + U8(dir) + "\n";
    return U8(dir);
  }
  const fs::path tmp = fs::u8path(U8(dir) + ".partial");
  fs::remove_all(tmp, ec);
  fs::create_directories(tmp, ec);
  ZipBudget budget;
  if (!ExtractZip(zipUtf8, tmp, 0, budget, log, err))
  {
    fs::remove_all(tmp, ec);
    return "";
  }
  fs::remove_all(dir, ec);
  fs::rename(tmp, dir, ec);
  if (ec)
  {
    err = "can't move the unpacked files into the cache (" + ec.message() + ")";
    return "";
  }
  std::ofstream(dir / ".complete") << zipUtf8 << "\n";
  log += "  unpacked " + std::to_string(budget.files) + " files (" + std::to_string(budget.bytes / 1024) + " KB) to "
         + U8(dir) + "\n";
  return U8(dir);
}

// Removes every cached pack but `keep` (the one in use, if any). Only ever touches PackCache.
int ClearPackCache(const std::string& keep)
{
  namespace fs = std::filesystem;
  std::error_code ec;
  int removed = 0;
  const fs::path root = fs::u8path(PackCacheRoot());
  if (!fs::is_directory(root, ec))
    return 0;
  for (const auto& e : fs::directory_iterator(root, ec))
  {
    if (!keep.empty() && fs::equivalent(e.path(), fs::u8path(keep), ec))
      continue;
    if (fs::remove_all(e.path(), ec) > 0)
      removed++;
  }
  return removed;
}
} // namespace griffinpack
