#include "deduplication_module.h"

#include <fstream>

#include "common/stats.h"
#include "selective_deduplication.h"
#include "utils/utils.h"

namespace cache {
DeduplicationModule::DeduplicationModule() = default;

void DeduplicationModule::dedup(Chunk &chunk) {
  BEGIN_TIMER();
  if (Config::getInstance().isSD()) {
    if (SelectiveDeduplicationModule::isHighFreq(chunk)) {
      MetadataModule::getInstance().dedup(chunk);
    } else {
      chunk.dedupResult_ = NOT_DUP;
      chunk.verficationResult_ = BOTH_LBA_AND_FP_NOT_VALID;
      chunk.hitFPIndex_ = false;
      chunk.hitLBAIndex_ = false;
    }
  } else {
    MetadataModule::getInstance().dedup(chunk);
  }
  END_TIMER(dedup);
}

void DeduplicationModule::lookup(Chunk &chunk) {
  BEGIN_TIMER();
  MetadataModule::getInstance().lookup(chunk);
  END_TIMER(lookup);
}

}  // namespace cache
