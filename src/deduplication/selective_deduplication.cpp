#include "selective_deduplication.h"

#include "common/config.h"
#include "common/stats.h"
#include "utils/utils.h"

namespace cache {

SelectiveDeduplicationModule::SelectiveDeduplicationModule() = default;
std::map<std::string, int> SelectiveDeduplicationModule::GFFI;

bool SelectiveDeduplicationModule::isHighFreq(Chunk &chunk) {
  int fingerprintCnts = Config::getInstance().getFingerprintCnts((char *)chunk.fingerprint_);

  if (fingerprintCnts >= SELECTIVE_DEDUPLICATION_TRESHOULD) {
    return true;
  }

  return false;
}
}  // namespace cache