#ifndef __SELECTIVECOMPRESSIONMODULE_H__
#define __SELECTIVECOMPRESSIONMODULE_H__

#include "chunking/chunk_module.h"
#include "common/common.h"

#define COMPRESSION_RATIO_TRESHOULD 1.5

namespace cache {
class SelectiveCompressionModule {
  SelectiveCompressionModule() = default;

 public:
  static SelectiveCompressionModule& getInstance();
  bool compressible(const Chunk& chunk);
};
}  // namespace cache

#endif  //__SELECTIVECOMPRESSIONMODULE_H__