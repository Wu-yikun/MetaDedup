#ifndef __MANAGEMODULE_H__
#define __MANAGEMODULE_H__

#include "chunking/chunk_module.h"
#include "common/common.h"
#include "compression/compression_module.h"
#include "io/io_module.h"
#include "manage_module.h"
#include "metadata/metadata_module.h"

namespace cache {

class ManageModule {
 public:
  static ManageModule &getInstance();
  int read(Chunk &chunk);
  int write(Chunk &chunk);
  void updateMetadata(Chunk &chunk);

 private:
  ManageModule();
  bool generateCacheWriteRequest(Chunk &chunk, DeviceType &deviceType, uint64_t &addr, uint8_t *&buf, uint32_t &len);
  bool generatePrimaryWriteRequest(Chunk &chunk, DeviceType &deviceType, uint64_t &addr, uint8_t *&buf, uint32_t &len);
  void generateReadRequest(Chunk &chunk, DeviceType &deviceType, uint64_t &addr, uint8_t *&buf, uint32_t &len);
};

}  // namespace cache
#endif  //__MANAGEMODULE_H__
