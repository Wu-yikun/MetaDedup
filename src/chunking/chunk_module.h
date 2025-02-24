#ifndef __CHUNK_H__
#define __CHUNK_H__

#include <cstdint>

#include "common/common.h"
#include "common/config.h"

namespace cache {

class Chunker {
 public:
  Chunker(uint64_t addr, void *buf, uint32_t len);
  bool next(Chunk &c);
  bool next(uint64_t &addr, uint8_t *&buf, uint32_t &len);

 protected:
  uint64_t addr_;
  uint8_t *buf_;
  uint32_t len_;
  uint32_t chunkSize_;
};

class ChunkModule {
 private:
  ChunkModule();

 public:
  static ChunkModule &getInstance();
  Chunker createChunker(uint64_t addr, void *buf, uint32_t len);
};
}  // namespace cache

#endif
