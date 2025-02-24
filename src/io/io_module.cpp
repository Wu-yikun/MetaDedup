#include "io_module.h"

#include <csignal>
#include <fstream>
#include <memory>

#include "common/config.h"
#include "device.h"
#include "utils/utils.h"

namespace cache {
IOModule::IOModule() = default;
IOModule::~IOModule() = default;

IOModule &IOModule::getInstance() {
  static IOModule ioModule;
  return ioModule;
}

uint32_t IOModule::addPrimaryDevice(char *filename) {
  uint64_t size = Config::getInstance().getPrimaryDeviceSize();
  primaryDevice_ = std::make_unique<BlockDevice>();
  primaryDevice_->_direct_io = Config::getInstance().isDirectIOEnabled();
  primaryDevice_->open(filename, size);
  return 0;
}

uint32_t IOModule::addCacheDevice(char *filename) {
  //   uint64_t size = Config::getInstance().getCacheDeviceSize();
  //   cacheDevice_ = std::make_unique<BlockDevice>();
  //   cacheDevice_->_direct_io = Config::getInstance().isDirectIOEnabled();
  //   cacheDevice_->open(filename, size + 1ull * Config::getInstance().getnFpBuckets() *
  //                                           Config::getInstance().getnFPSlotsPerBucket() *
  //                                           Config::getInstance().getMetadataSize());
  uint64_t size = Config::getInstance().getCacheDeviceSize();
  cacheDevice_ = std::make_unique<ByteDevice>();
  cacheDevice_->open(filename, size);
  return 0;
}

uint32_t IOModule::read(DeviceType deviceType, uint64_t addr, void *buf, uint32_t len) {
  uint32_t ret = 0;
  if (deviceType == PRIMARY_DEVICE) {
    Stats::getInstance().add_bytes_read_from_ssd(len);
    BEGIN_TIMER();
    ret = primaryDevice_->read(addr, static_cast<uint8_t *>(buf), len);
    END_TIMER(io_ssd);
  } else if (deviceType == CACHE_DEVICE) {
    if (len == 512) {
      Stats::getInstance().add_metadata_bytes_read_from_pm(512);
      BEGIN_TIMER();
      ret = cacheDevice_->read(addr, static_cast<uint8_t *>(buf), len);
      END_TIMER(io_pm);
    }
    Stats::getInstance().add_bytes_read_from_pm(len);
  }
  return ret;
}

uint32_t IOModule::write(DeviceType deviceType, uint64_t addr, void *buf, uint32_t len) {
  if (deviceType == PRIMARY_DEVICE) {
    Stats::getInstance().add_bytes_written_to_ssd(len);
    BEGIN_TIMER();
    primaryDevice_->write(addr, (uint8_t *)buf, len);
    END_TIMER(io_ssd);
  } else if (deviceType == CACHE_DEVICE) {
    if (len == 512) {
      Stats::getInstance().add_metadata_bytes_written_to_pm(512);
      BEGIN_TIMER();
      cacheDevice_->write(addr, static_cast<uint8_t *>(buf), len);
      END_TIMER(io_pm);
    }
    Stats::getInstance().add_bytes_written_to_pm(len);
  }
  return 0;
}

void IOModule::flush(uint64_t addr, uint64_t bufferOffset, uint32_t len) {}

}  // namespace cache
