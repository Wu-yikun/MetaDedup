#ifndef __METADATAMODULE_H__
#define __METADATAMODULE_H__

#include "chunking/chunk_module.h"
#include "common/common.h"
#include "index.h"
#include "meta_verification.h"

namespace cache {

class MetadataModule {
 public:
  static MetadataModule &getInstance();
  ~MetadataModule();
  void dedup(Chunk &chunk);
  void lookup(Chunk &chunk);
  void update(Chunk &chunk);
  void dumpStats();

  std::shared_ptr<LBAIndex> lbaIndex_;
  std::shared_ptr<FPIndex> fpIndex_;
  std::unique_ptr<MetaVerification> metaVerification_;

 private:
  MetadataModule();
};

}  // namespace cache

#endif
