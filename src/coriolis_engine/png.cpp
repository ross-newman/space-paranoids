#include "coriolis/png.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace coriolis {

namespace {

std::uint32_t Crc32(const std::uint8_t* data, std::size_t size, std::uint32_t crc = 0) {
  static const std::array<std::uint32_t, 256> table = [] {
    std::array<std::uint32_t, 256> t{};
    for (std::uint32_t n = 0; n < 256; ++n) {
      std::uint32_t c = n;
      for (int k = 0; k < 8; ++k) {
        c = (c & 1U) != 0U ? 0xEDB88320U ^ (c >> 1) : c >> 1;
      }
      t[n] = c;
    }
    return t;
  }();
  crc = ~crc;
  for (std::size_t i = 0; i < size; ++i) {
    crc = table[(crc ^ data[i]) & 0xFFU] ^ (crc >> 8);
  }
  return ~crc;
}

void Put32(std::vector<std::uint8_t>& out, std::uint32_t value) {
  out.push_back(static_cast<std::uint8_t>(value >> 24));
  out.push_back(static_cast<std::uint8_t>(value >> 16));
  out.push_back(static_cast<std::uint8_t>(value >> 8));
  out.push_back(static_cast<std::uint8_t>(value));
}

void PutChunk(std::vector<std::uint8_t>& out, const char* type, const std::vector<std::uint8_t>& data) {
  Put32(out, static_cast<std::uint32_t>(data.size()));
  std::vector<std::uint8_t> body(type, type + 4);
  body.insert(body.end(), data.begin(), data.end());
  out.insert(out.end(), body.begin(), body.end());
  Put32(out, Crc32(body.data(), body.size()));
}

}  // namespace

bool WritePng(const Image& image, const std::string& path) {
  const std::uint32_t width = image.width();
  const std::uint32_t height = image.height();
  if (width == 0 || height == 0) {
    return false;
  }

  // Each row is a filter byte (0, none) and the pixels, stored in uncompressed deflate blocks
  std::vector<std::uint8_t> raw;
  raw.reserve(static_cast<std::size_t>(width) * height * 3 + height);
  for (std::uint32_t y = 0; y < height; ++y) {
    raw.push_back(0);
    const std::uint8_t* row = image.data() + static_cast<std::size_t>(y) * width * 3;
    raw.insert(raw.end(), row, row + static_cast<std::size_t>(width) * 3);
  }

  std::vector<std::uint8_t> zlib = {0x78, 0x01};
  std::uint32_t a = 1;
  std::uint32_t b = 0;
  for (const std::uint8_t byte : raw) {
    a = (a + byte) % 65521U;
    b = (b + a) % 65521U;
  }
  for (std::size_t offset = 0; offset < raw.size(); offset += 65535) {
    const std::size_t length = std::min<std::size_t>(65535, raw.size() - offset);
    zlib.push_back(offset + length >= raw.size() ? 1 : 0);
    zlib.push_back(static_cast<std::uint8_t>(length));
    zlib.push_back(static_cast<std::uint8_t>(length >> 8));
    zlib.push_back(static_cast<std::uint8_t>(~length));
    zlib.push_back(static_cast<std::uint8_t>((~length) >> 8));
    zlib.insert(zlib.end(), raw.begin() + static_cast<std::ptrdiff_t>(offset),
                raw.begin() + static_cast<std::ptrdiff_t>(offset + length));
  }
  Put32(zlib, (b << 16) | a);

  std::vector<std::uint8_t> file = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
  std::vector<std::uint8_t> header;
  Put32(header, width);
  Put32(header, height);
  header.insert(header.end(), {8, 2, 0, 0, 0});  // 8 bit RGB
  PutChunk(file, "IHDR", header);
  PutChunk(file, "IDAT", zlib);
  PutChunk(file, "IEND", {});

  std::FILE* f = std::fopen(path.c_str(), "wb");
  if (f == nullptr) {
    return false;
  }
  const bool ok = std::fwrite(file.data(), 1, file.size(), f) == file.size();
  return std::fclose(f) == 0 && ok;
}

}  // namespace coriolis
