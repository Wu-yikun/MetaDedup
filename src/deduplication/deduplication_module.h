#ifndef __DEDUP_H__
#define __DEDUP_H__
#include <memory>

#include "chunking/chunk_module.h"
#include "common/common.h"
#include "compression/compression_module.h"
#include "metadata/metadata_module.h"

namespace cache {
class DeduplicationModule {
  DeduplicationModule();

 public:
  static void dedup(Chunk &chunk);
  static void lookup(Chunk &chunk);
};
}  // namespace cache

#endif
