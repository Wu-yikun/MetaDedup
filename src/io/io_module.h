#ifndef __IOMODULE_H__
#define __IOMODULE_H__

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <list>
#include <memory>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

#include "common/common.h"
#include "common/env.h"
#include "common/stats.h"
#include "device.h"
#include "utils/thread_pool.h"

namespace cache {
class IOModule {
 private:
  IOModule();
  ~IOModule();

 public:
  static IOModule &getInstance();
  uint32_t addPrimaryDevice(char *filename);
  uint32_t addCacheDevice(char *filename);
  uint32_t read(DeviceType deviceType, uint64_t addr, void *buf, uint32_t len);
  uint32_t write(DeviceType deviceType, uint64_t addr, void *buf, uint32_t len);
  void flush(uint64_t addr, uint64_t bufferOffset, uint32_t len);

  inline void sync() {
    primaryDevice_->sync();
    cacheDevice_->sync();
  }

 private:
  std::unique_ptr<BlockDevice> primaryDevice_;
  std::unique_ptr<ByteDevice> cacheDevice_;

  Stats *stats_{};

  std::mutex mutex_;
};

}  // namespace cache

#endif  //__IOMODULE_H__
