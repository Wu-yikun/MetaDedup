#ifndef __COMPRESSIONMODULE_H__
#define __COMPRESSIONMODULE_H__

#include "chunking/chunk_module.h"
#include "common/common.h"

namespace cache {

class CompressionModule {
  CompressionModule() = default;

 public:
  static CompressionModule &getInstance();
  static void compress(Chunk &chunk);
  static void decompress(Chunk &chunk);
};
}  // namespace cache

#endif  //__COMPRESSIONMODULE_H__
