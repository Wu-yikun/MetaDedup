#ifndef __METAVERIFICATION_H__
#define __METAVERIFICATION_H__
#include <cstdint>

#include "common/common.h"
#include "compression/compression_module.h"
#include "index.h"
#include "io/io_module.h"

namespace cache {
class MetaVerification {
 public:
  MetaVerification();
  VerificationResult verify(Chunk &chunk);
  void clean(Chunk &chunk);
  void update(Chunk &chunk);
};
}  // namespace cache
#endif
