#ifndef __SDEDUP_H__
#define __SDEDUP_H__
#include <map>
#include <memory>

#include "chunking/chunk_module.h"
#include "common/common.h"
#include "compression/compression_module.h"
#include "metadata/metadata_module.h"

#define SELECTIVE_DEDUPLICATION_TRESHOULD 10

namespace cache {
class SelectiveDeduplicationModule {
  static std::map<std::string, int> GFFI;
  SelectiveDeduplicationModule();

 public:
  static bool isHighFreq(Chunk &chunk);
};
}  // namespace cache

#endif