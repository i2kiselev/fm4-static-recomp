#pragma once
// Portable SHA-256 for the Linux build of FM4 Static Recomp (see linux/PORTING.md, bug 1).
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <istream>
#include <string>
#include <vector>

namespace fm4_linux {
namespace sha256_detail {

inline constexpr std::array<uint32_t, 64> kK = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

inline constexpr uint32_t Rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

// Process one 64-byte block: build the 64-word message schedule w (first 16 words read big-endian
// from `block`), run the 64 rounds with kK, and add the result into `h`.
inline void Compress(std::array<uint32_t, 8>& h, const unsigned char* block) {
  std::array<uint32_t, 64> w{};
  for (int i = 0; i < 16; ++i) {
    w[i] = uint32_t(block[i * 4]) << 24 | uint32_t(block[i * 4 + 1]) << 16 |
           uint32_t(block[i * 4 + 2]) << 8 | uint32_t(block[i * 4 + 3]);
  }
  for (int i = 16; i < 64; ++i) {
    const uint32_t s0 = Rotr(w[i - 15], 7) ^ Rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
    const uint32_t s1 = Rotr(w[i - 2], 17) ^ Rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
    w[i] = w[i - 16] + s0 + w[i - 7] + s1;
  }
  uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
  for (int i = 0; i < 64; ++i) {
    const uint32_t t1 = hh + (Rotr(e, 6) ^ Rotr(e, 11) ^ Rotr(e, 25)) + ((e & f) ^ (~e & g)) + kK[i] + w[i];
    const uint32_t t2 = (Rotr(a, 2) ^ Rotr(a, 13) ^ Rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
    hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
  }
  h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
}

}  // namespace sha256_detail

// Reads `in` to the end in 1 MiB chunks and returns its SHA-256 as 64 uppercase hex characters,
// or "" if a read fails (in.bad()). Keep a 64-byte pending buffer: call Compress for every full
// block, then pad the tail (0x80, zeros, total length in BITS as 64-bit big-endian) and compress
// the last one or two blocks. Initial h = 6a09e667 bb67ae85 3c6ef372 a54ff53a 510e527f 9b05688c
// 1f83d9ab 5be0cd19. Output h[0..7] big-endian with "0123456789ABCDEF".
inline std::string Sha256Hex(std::istream& in) {
  using sha256_detail::Compress;
  std::array<uint32_t, 8> h = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                               0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
  std::array<unsigned char, 64> pending{};
  size_t pending_size = 0;
  uint64_t total = 0;
  std::vector<char> buffer(1 << 20);
  while (in) {
    in.read(buffer.data(), std::streamsize(buffer.size()));
    const size_t read = size_t(in.gcount());
    if (in.bad()) {
      return {};
    }
    total += read;
    for (size_t i = 0; i < read;) {
      const size_t take = std::min(read - i, pending.size() - pending_size);
      std::memcpy(pending.data() + pending_size, buffer.data() + i, take);
      pending_size += take;
      i += take;
      if (pending_size == pending.size()) {
        Compress(h, pending.data());
        pending_size = 0;
      }
    }
  }
  pending[pending_size++] = 0x80;
  if (pending_size > 56) {
    std::memset(pending.data() + pending_size, 0, pending.size() - pending_size);
    Compress(h, pending.data());
    pending_size = 0;
  }
  std::memset(pending.data() + pending_size, 0, 56 - pending_size);
  const uint64_t bits = total * 8;
  for (int i = 0; i < 8; ++i) {
    pending[56 + i] = static_cast<unsigned char>(bits >> (56 - 8 * i));
  }
  Compress(h, pending.data());
  static constexpr char kHex[] = "0123456789ABCDEF";
  std::string hex;
  hex.reserve(64);
  for (const uint32_t word : h) {
    for (int shift = 28; shift >= 0; shift -= 4) {
      hex += kHex[(word >> shift) & 15];
    }
  }
  return hex;
}

}  // namespace fm4_linux
