#pragma once

// 1.70.0 PACK PICKER: scans one unzipped capture-pack folder (e.g. a TONE3000 download) and groups its
// .nam files, so the AMP block can switch between them from a dropdown. UI thread only (never the audio
// thread); reads only the first and last 16 KB of each file for its metadata, not the weights.
// Grouping comes from the file name first (TONE3000 metadata often has placeholder make/model), with
// the .nam metadata's gear_type and loudness as extras.

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
#include <string>
#include <vector>

namespace griffinpack
{
struct Capture
{
  std::string path; // Full path to the .nam (UTF-8)
  std::string file; // File name
  std::string group; // "[AMP]", "Amp", "Captures"...
  std::string key; // The capture with its size/variant tag removed: what the capture dropdown lists
  std::string size; // "STD", "xSTD", "HOTONE"... ("" when the name has none)
  std::string gearType; // From metadata ("" if absent)
  bool hasLoudness = false;
  double loudness = 0.0;
  bool ampOnly = false; // No cab in the capture: the pack's IR is worth pairing
};

struct Pack
{
  std::string folder;
  std::vector<Capture> captures; // Sorted by group, key, size
  std::vector<std::string> irs; // Full paths of the folder's .wav files, sorted
  std::string defaultIR; // The one to auto-pair ("" if none can be guessed)
  std::vector<std::string> zips; // 1.70.1: .zip files found (not read: they need unzipping first)
  int subfolders = 0; // 1.70.1: subfolders that had captures
  std::string log; // 1.70.1: what the scan saw, for the scan log
  double scanMs = 0.0;

  bool Empty() const { return captures.empty(); }
  // Groups in order, each with its keys in order.
  std::vector<std::pair<std::string, std::vector<std::string>>> Groups() const
  {
    std::vector<std::pair<std::string, std::vector<std::string>>> out;
    for (const auto& c : captures)
    {
      if (out.empty() || out.back().first != c.group)
        out.push_back({c.group, {}});
      auto& keys = out.back().second;
      if (keys.empty() || keys.back() != c.key)
        keys.push_back(c.key);
    }
    return out;
  }
  std::vector<const Capture*> Sizes(const std::string& group, const std::string& key) const
  {
    std::vector<const Capture*> out;
    for (const auto& c : captures)
      if (c.group == group && c.key == key)
        out.push_back(&c);
    return out;
  }
  const Capture* Find(const std::string& group, const std::string& key, const std::string& size) const
  {
    const Capture* first = nullptr;
    for (const auto& c : captures)
      if (c.group == group && c.key == key)
      {
        if (c.size == size)
          return &c;
        if (!first)
          first = &c;
      }
    return first;
  }
  const Capture* FindPath(const std::string& path) const
  {
    for (const auto& c : captures)
      if (c.path == path)
        return &c;
    return nullptr;
  }
};

// UTF-8 path text as std::string (u8string is char8_t under C++20).
inline std::string U8(const std::filesystem::path& p)
{
  const auto s = p.u8string();
  return std::string(s.begin(), s.end());
}

inline std::string Lower(std::string s)
{
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char ch) { return (char)std::tolower(ch); });
  return s;
}

inline std::string Trim(std::string s)
{
  const char* ws = " \t-_";
  const auto b = s.find_first_not_of(ws);
  if (b == std::string::npos)
    return "";
  const auto e = s.find_last_not_of(ws);
  return s.substr(b, e - b + 1);
}

// Pulls a top-level-ish metadata value out of a chunk of the JSON: "name": "text" or "name": number.
inline bool FindJsonString(const std::string& text, const char* name, std::string& out)
{
  const std::string needle = std::string("\"") + name + "\"";
  auto p = text.find(needle);
  if (p == std::string::npos)
    return false;
  p = text.find(':', p + needle.size());
  if (p == std::string::npos)
    return false;
  p = text.find_first_not_of(" \t\r\n", p + 1);
  if (p == std::string::npos || text[p] != '"')
    return false;
  const auto e = text.find('"', p + 1);
  if (e == std::string::npos)
    return false;
  out = text.substr(p + 1, e - p - 1);
  return true;
}
inline bool FindJsonNumber(const std::string& text, const char* name, double& out)
{
  const std::string needle = std::string("\"") + name + "\"";
  auto p = text.find(needle);
  if (p == std::string::npos)
    return false;
  p = text.find(':', p + needle.size());
  if (p == std::string::npos)
    return false;
  p = text.find_first_not_of(" \t\r\n", p + 1);
  if (p == std::string::npos)
    return false;
  try
  {
    size_t used = 0;
    out = std::stod(text.substr(p, 40), &used);
    return used > 0;
  }
  catch (...)
  {
    return false;
  }
}

// The first and last 16 KB of a file (the metadata block sits at either end, depending on the trainer).
inline std::string ReadEnds(const std::filesystem::path& p)
{
  std::ifstream f(p, std::ios::binary);
  if (!f)
    return "";
  f.seekg(0, std::ios::end);
  const std::streamoff size = f.tellg();
  const std::streamoff n = 16384;
  std::string head((size_t)std::min(size, n), '\0');
  f.seekg(0);
  f.read(head.data(), (std::streamsize)head.size());
  if (size <= 2 * n)
  {
    std::string rest((size_t)std::max<std::streamoff>(0, size - (std::streamoff)head.size()), '\0');
    f.read(rest.data(), (std::streamsize)rest.size());
    return head + rest;
  }
  std::string tail((size_t)n, '\0');
  f.seekg(size - n);
  f.read(tail.data(), (std::streamsize)tail.size());
  return head + "\n" + tail;
}

// Splits a file stem into (group, key, size). Size/variant tags it knows, at the end of the name:
// STD, xSTD, lite, nano, feather, standard, complex (each optionally -01), and device targets
// (HOTONE, VALETON, SONICAKE, "NAM VST FULL NAM LOADER PEDALS", NAM VST).
inline void ParseName(const std::string& stem, std::string& group, std::string& key, std::string& size)
{
  std::string s = stem;
  group.clear();
  static const std::regex kBracket(R"(^\s*\[([^\]]+)\]\s*(.*)$)");
  std::smatch m;
  if (std::regex_match(s, m, kBracket))
  {
    group = "[" + m[1].str() + "]";
    s = m[2].str();
  }
  static const std::regex kSize(
    R"(^(.*?)[\s_\-]+(x?std|lite|nano|feather|standard|complex|hotone|valeton|sonicake|nam vst full nam loader pedals|nam vst)((?:[\s_\-]*\d+)?)\s*$)",
    std::regex::icase);
  size.clear();
  if (std::regex_match(s, m, kSize) && Trim(m[1].str()).size())
  {
    key = Trim(m[1].str());
    size = m[2].str() + m[3].str();
  }
  else
    key = Trim(s);
  if (key.empty())
    key = stem;
}

inline bool LooksLikeCab(const std::string& lowerName)
{
  for (const char* w : {"cab", "full rig", "fullrig", "mic", "sm57", "e609", "ir "})
    if (lowerName.find(w) != std::string::npos)
      return true;
  return false;
}

// 1.70.1: scans the folder AND its subfolders (up to 4 levels, 4000 files), so picking a parent like
// "Neural Amp - Sims and IRs" still finds the Lynx pack inside it. Captures in a subfolder are grouped
// under that subfolder's name. A .zip path (or a folder inside one) is refused: unzip it first.
inline bool IsZipPath(const std::string& path)
{
  const std::string l = Lower(path);
  return (l.size() >= 4 && l.compare(l.size() - 4, 4, ".zip") == 0) || l.find(".zip\\") != std::string::npos
         || l.find(".zip/") != std::string::npos;
}

inline Pack Scan(const std::string& folderUtf8)
{
  namespace fs = std::filesystem;
  const auto t0 = std::chrono::steady_clock::now();
  Pack pack;
  pack.folder = folderUtf8;
  std::error_code ec;
  const fs::path dir = fs::u8path(folderUtf8);
  pack.log = "folder: " + folderUtf8 + "\n";
  if (IsZipPath(folderUtf8))
  {
    pack.log += "  refused: a .zip (unzip it first)\n";
    pack.zips.push_back(folderUtf8);
    return pack;
  }
  if (!fs::is_directory(dir, ec))
  {
    pack.log += "  not a folder (" + ec.message() + ")\n";
    return pack;
  }
  std::map<std::string, int> perFolder;
  int files = 0;
  fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end;
  for (; !ec && it != end; it.increment(ec))
  {
    if (it.depth() >= 4)
    {
      it.disable_recursion_pending();
      continue;
    }
    std::error_code ec2;
    if (!it->is_regular_file(ec2))
      continue;
    if (++files > 4000)
    {
      pack.log += "  stopped at 4000 files\n";
      break;
    }
    const fs::path p = it->path();
    const std::string ext = Lower(U8(p.extension()));
    if (ext == ".zip")
    {
      pack.zips.push_back(U8(p));
      continue;
    }
    if (ext == ".wav")
    {
      pack.irs.push_back(U8(p));
      continue;
    }
    if (ext != ".nam")
      continue;
    Capture c;
    c.path = U8(p);
    c.file = U8(p.filename());
    ParseName(U8(p.stem()), c.group, c.key, c.size);
    const std::string meta = ReadEnds(p);
    FindJsonString(meta, "gear_type", c.gearType);
    c.hasLoudness = FindJsonNumber(meta, "loudness", c.loudness);
    if (c.group.empty())
    {
      if (c.gearType.size())
      {
        c.group = c.gearType;
        c.group[0] = (char)std::toupper((unsigned char)c.group[0]);
      }
      else
        c.group = "Captures";
    }
    const std::string lname = Lower(c.file);
    const std::string lgroup = Lower(c.group);
    const std::string lgear = Lower(c.gearType);
    const bool ampish = lgroup == "[amp]" || lgroup == "[pre]" || lgroup == "[pow]" || lgear == "amp"
                        || lgear == "preamp" || lname.find(" di") != std::string::npos;
    c.ampOnly = ampish && !LooksLikeCab(lname) && lgear != "amp_cab" && lgear != "full_rig";
    // Subfolder captures: grouped under the subfolder (relative path).
    const fs::path rel = p.parent_path().lexically_relative(dir);
    const std::string relS = (rel.empty() || rel == ".") ? std::string() : U8(rel);
    perFolder[relS.empty() ? "(this folder)" : relS]++;
    if (!relS.empty())
      c.group = relS + "  " + c.group;
    pack.captures.push_back(std::move(c));
  }
  if (ec)
    pack.log += "  listing error: " + ec.message() + "\n";
  std::sort(pack.captures.begin(), pack.captures.end(), [](const Capture& a, const Capture& b) {
    if (a.group != b.group)
      return a.group < b.group;
    if (a.key != b.key)
      return a.key < b.key;
    return a.size < b.size;
  });
  std::sort(pack.irs.begin(), pack.irs.end());
  if (pack.irs.size() == 1)
    pack.defaultIR = pack.irs[0];
  else
    for (const auto& ir : pack.irs)
    {
      const std::string l = Lower(U8(fs::u8path(ir).filename()));
      if (l.find("delay") != std::string::npos || l.find("reverb") != std::string::npos)
        continue;
      if (l.find("blend") != std::string::npos || l.find("cab") != std::string::npos
          || l.find(" ir") != std::string::npos || l.find("57") != std::string::npos)
      {
        pack.defaultIR = ir;
        break;
      }
    }
  for (const auto& f : perFolder)
    if (f.first != "(this folder)")
      pack.subfolders++;
  pack.scanMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  char line[256];
  snprintf(line, sizeof(line), "  %zu captures, %zu IRs, %zu zips, %d files looked at, %.1f ms\n",
           pack.captures.size(), pack.irs.size(), pack.zips.size(), files, pack.scanMs);
  pack.log += line;
  for (const auto& f : perFolder)
    pack.log += "    " + f.first + ": " + std::to_string(f.second) + " captures\n";
  for (const auto& z : pack.zips)
    pack.log += "    zip (not read): " + z + "\n";
  return pack;
}

// 1.70.1: the folders under a library root that hold captures (for the PACK menu's quick list), and the
// zips there that still need unzipping. Bounded: 4 levels, 6000 entries.
inline void FindPackFolders(const std::string& rootUtf8, std::vector<std::pair<std::string, int>>& folders,
                            std::vector<std::string>& zips)
{
  namespace fs = std::filesystem;
  std::error_code ec;
  const fs::path root = fs::u8path(rootUtf8);
  if (!fs::is_directory(root, ec))
    return;
  std::map<std::string, int> counts;
  int n = 0;
  fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
  for (; !ec && it != end && ++n < 6000; it.increment(ec))
  {
    if (it.depth() >= 4)
    {
      it.disable_recursion_pending();
      continue;
    }
    std::error_code ec2;
    if (!it->is_regular_file(ec2))
      continue;
    const std::string ext = Lower(U8(it->path().extension()));
    if (ext == ".nam")
      counts[U8(it->path().parent_path())]++;
    else if (ext == ".zip")
      zips.push_back(U8(it->path()));
  }
  for (const auto& c : counts)
    folders.push_back(c);
}
// 1.70.3 (PackZip.cpp): .zip packs, unpacked into a cache.
std::string PackCacheRoot();
std::string ResolveZip(const std::string& zipUtf8, std::string& err, std::string& log, bool& reused);
int ClearPackCache(const std::string& keep);
} // namespace griffinpack