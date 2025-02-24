#include "compression_module.h"

#include <csignal>
#include <cstring>
#include <iostream>

#include "common/config.h"
#include "common/stats.h"
#include "lz4.h"
#include "selective_compression_module.h"
#include "utils/utils.h"

namespace cache {
CompressionModule &CompressionModule::getInstance() {
  static CompressionModule instance;
  return instance;
}

void CompressionModule::compress(Chunk &chunk) {
  BEGIN_TIMER();
  if (Config::getInstance().isSC()) {
    if (SelectiveCompressionModule::getInstance().compressible(chunk)) {
      chunk.compressedLen_ =
          LZ4_compress_default((const char *)chunk.buf_, (char *)chunk.compressedBuf_, chunk.len_, chunk.len_ * 0.75);
    } else {
      chunk.compressedLen_ = chunk.len_;
    }
  } else {
    chunk.compressedLen_ =
        LZ4_compress_default((const char *)chunk.buf_, (char *)chunk.compressedBuf_, chunk.len_, chunk.len_ * 0.75);
  }

  if (!Config::getInstance().isSynthenticCompressionEnabled()) {
    chunk.compressedLen_ = 0;
  }

  uint32_t numMaxSubchunks = Config::getInstance().getMaxSubchunks();
  if (chunk.compressedLen_ == 0) {
    chunk.nSubchunks_ = numMaxSubchunks;
  } else {
    chunk.nSubchunks_ =
        (chunk.compressedLen_ + Config::getInstance().getSubchunkSize() - 1) / Config::getInstance().getSubchunkSize();
  }
  if (chunk.nSubchunks_ == numMaxSubchunks) {
    chunk.compressedBuf_ = chunk.buf_;
  }
  END_TIMER(compression);
}

void CompressionModule::decompress(Chunk &chunk) {
  BEGIN_TIMER();
  if (chunk.compressedLen_ != 0) {
    if (!Config::getInstance().isFakeIOEnabled()) {
      LZ4_decompress_safe((const char *)chunk.compressedBuf_, (char *)chunk.buf_, chunk.compressedLen_, chunk.len_);
    }
  }
  END_TIMER(decompression);
}
}  // namespace cache
