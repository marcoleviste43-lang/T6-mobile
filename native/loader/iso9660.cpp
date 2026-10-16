#include "iso9660.h"
#include <algorithm>
#include <cctype>
#include <cstring>

namespace t6 {
namespace {
constexpr uint32_t kSector = 2048;
constexpr size_t kMaxEntries = 100000;
constexpr int kMaxDepth = 16;
constexpr uint32_t kMaxDirSize = 16u * 1024 * 1024;

uint32_t le32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (uint32_t(p[3]) << 24); }
std::string upper(std::string s) { for (auto& c : s) c = (char)std::toupper((unsigned char)c); return s; }
}  // namespace

bool MemorySource::read(uint64_t o, size_t n, uint8_t* out) const {
  if (o > data_.size() || n > data_.size() - o) return false;
  std::memcpy(out, data_.data() + o, n);
  return true;
}

FileSource::FileSource(const std::string& path) : f_(path, std::ios::binary) {
  if (f_) { f_.seekg(0, std::ios::end); size_ = (uint64_t)f_.tellg(); }
}
bool FileSource::read(uint64_t o, size_t n, uint8_t* out) const {
  if (!f_ || o > size_ || n > size_ - o) return false;
  f_.clear(); f_.seekg((std::streamoff)o);
  f_.read(reinterpret_cast<char*>(out), (std::streamsize)n);
  return (size_t)f_.gcount() == n;
}

static bool walk(const ByteSource& src, uint32_t lba, uint32_t size, const std::string& prefix, int depth,
                 std::vector<IsoEntry>& out, std::string* err) {
  if (depth > kMaxDepth) { if (err) *err = "directory nesting too deep"; return false; }
  if (size > kMaxDirSize) { if (err) *err = "directory too large"; return false; }
  std::vector<uint8_t> buf(size);
  if (!src.read((uint64_t)lba * kSector, size, buf.data())) { if (err) *err = "directory extent out of bounds"; return false; }
  size_t off = 0;
  while (off < size) {
    uint8_t len = buf[off];
    if (len == 0) { off = (off / kSector + 1) * kSector; continue; }  // padding to next sector
    if (len < 34 || off + len > size) { if (err) *err = "corrupt directory record"; return false; }
    uint8_t nameLen = buf[off + 32];
    if (33u + nameLen > len) { if (err) *err = "corrupt directory name"; return false; }
    uint32_t extent = le32(&buf[off + 2]), fsize = le32(&buf[off + 10]);
    bool isDir = buf[off + 25] & 2;
    std::string name(reinterpret_cast<char*>(&buf[off + 33]), nameLen);
    off += len;
    if (nameLen == 1 && (name[0] == 0 || name[0] == 1)) continue;  // "." and ".."
    auto semi = name.find(';'); if (semi != std::string::npos) name.resize(semi);
    IsoEntry e{prefix.empty() ? name : prefix + "/" + name, extent, fsize, isDir};
    if (out.size() >= kMaxEntries) { if (err) *err = "too many entries"; return false; }
    out.push_back(e);
    if (isDir && !walk(src, extent, fsize, e.path, depth + 1, out, err)) return false;
  }
  return true;
}

std::optional<IsoImage> IsoImage::open(std::shared_ptr<ByteSource> src, std::string* err) {
  uint8_t pvd[kSector];
  if (!src->read(16ull * kSector, kSector, pvd)) { if (err) *err = "image too small"; return std::nullopt; }
  if (pvd[0] != 1 || std::memcmp(pvd + 1, "CD001", 5) != 0) { if (err) *err = "no ISO9660 primary volume descriptor"; return std::nullopt; }
  const uint8_t* root = pvd + 156;
  IsoImage img; img.src_ = src;
  if (!walk(*src, le32(root + 2), le32(root + 10), "", 0, img.entries_, err)) return std::nullopt;
  return img;
}

std::optional<IsoEntry> IsoImage::find(const std::string& path) const {
  auto want = upper(path);
  for (auto& e : entries_) if (upper(e.path) == want) return e;
  return std::nullopt;
}

bool IsoImage::readFile(const IsoEntry& e, std::vector<uint8_t>& out) const {
  if (e.isDir) return false;
  out.resize(e.size);
  return src_->read((uint64_t)e.lba * kSector, e.size, out.data());
}

ExecKind classifyExecutable(const std::vector<uint8_t>& h) {
  if (h.size() >= 4 && std::memcmp(h.data(), "\x7f" "ELF", 4) == 0) return ExecKind::ElfPlain;
  if (h.size() >= 4 && std::memcmp(h.data(), "\0PBP", 4) == 0) return ExecKind::PbpContainer;
  if (h.size() >= 4 && std::memcmp(h.data(), "~PSP", 4) == 0) return ExecKind::EncryptedPsp;
  return ExecKind::Unknown;
}
const char* toString(ExecKind k) {
  switch (k) {
    case ExecKind::ElfPlain: return "ELF";
    case ExecKind::PbpContainer: return "PBP";
    case ExecKind::EncryptedPsp: return "~PSP (encrypted)";
    default: return "unknown";
  }
}
}  // namespace t6
