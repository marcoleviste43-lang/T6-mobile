// Minimal, bounds-checked ISO9660 reader for PSP UMD images. No third-party code.
#pragma once
#include <cstdint>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace t6 {

class ByteSource {
public:
  virtual ~ByteSource() = default;
  virtual uint64_t size() const = 0;
  // Returns false if the range is out of bounds.
  virtual bool read(uint64_t offset, size_t len, uint8_t* out) const = 0;
};

class MemorySource : public ByteSource {
public:
  explicit MemorySource(std::vector<uint8_t> d) : data_(std::move(d)) {}
  uint64_t size() const override { return data_.size(); }
  bool read(uint64_t o, size_t n, uint8_t* out) const override;
private:
  std::vector<uint8_t> data_;
};

class FileSource : public ByteSource {
public:
  explicit FileSource(const std::string& path);
  bool ok() const { return f_.is_open(); }
  uint64_t size() const override { return size_; }
  bool read(uint64_t o, size_t n, uint8_t* out) const override;
private:
  mutable std::ifstream f_;
  uint64_t size_ = 0;
};

struct IsoEntry {
  std::string path;  // e.g. "PSP_GAME/SYSDIR/EBOOT.BIN" (version suffix ";1" stripped)
  uint32_t lba = 0;
  uint32_t size = 0;
  bool isDir = false;
};

enum class ExecKind { Unknown, ElfPlain, PbpContainer, EncryptedPsp };

class IsoImage {
public:
  static std::optional<IsoImage> open(std::shared_ptr<ByteSource> src, std::string* err = nullptr);
  const std::vector<IsoEntry>& entries() const { return entries_; }
  std::optional<IsoEntry> find(const std::string& path) const;  // case-insensitive
  bool readFile(const IsoEntry& e, std::vector<uint8_t>& out) const;
private:
  std::shared_ptr<ByteSource> src_;
  std::vector<IsoEntry> entries_;
};

ExecKind classifyExecutable(const std::vector<uint8_t>& head);
const char* toString(ExecKind k);

}  // namespace t6
