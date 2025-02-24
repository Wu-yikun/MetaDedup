#include "manage_module.h"

#include <cassert>

#include "common/stats.h"
#include "utils/utils.h"

namespace cache {
ManageModule &ManageModule::getInstance() {
  static ManageModule instance;
  return instance;
}

ManageModule::ManageModule() {}

void ManageModule::generateReadRequest(cache::Chunk &chunk, cache::DeviceType &deviceType, uint64_t &addr,
                                       uint8_t *&buf, uint32_t &len) {
  if (chunk.lookupResult_ == HIT) {
    deviceType = CACHE_DEVICE;
    if (chunk.compressedLen_ != 0) {
      buf = chunk.compressedBuf_;
    } else {
      buf = chunk.buf_;
    }
    addr = chunk.cachedataLocation_;
    len = (chunk.nSubchunks_) * Config::getInstance().getSubchunkSize();
  } else {
    deviceType = PRIMARY_DEVICE;
    addr = chunk.addr_;
    buf = chunk.buf_;
    len = chunk.len_;
  }
}

int ManageModule::read(Chunk &chunk) {
  DeviceType deviceType;
  uint64_t addr;
  uint8_t *buf;
  uint32_t len;
  generateReadRequest(chunk, deviceType, addr, buf, len);
  IOModule::getInstance().read(deviceType, addr, buf, len);

  return 0;
}

bool ManageModule::generatePrimaryWriteRequest(Chunk &chunk, DeviceType &deviceType, uint64_t &addr, uint8_t *&buf,
                                               uint32_t &len) {
  // Only write through cache need to persist the write to the HDD in the first place
  if (Config::getInstance().getCacheMode() == CacheModeEnum::tWriteThrough) {
    deviceType = PRIMARY_DEVICE;
    // No lookup result means a write request - need to persist to the HDD
    if (chunk.lookupResult_ == LOOKUP_UNKNOWN) {
      addr = chunk.addr_;
      buf = chunk.buf_;
      len = chunk.len_;
      return true;
    }
  }

  return false;
}

bool ManageModule::generateCacheWriteRequest(Chunk &chunk, DeviceType &deviceType, uint64_t &addr, uint8_t *&buf,
                                             uint32_t &len) {
  deviceType = CACHE_DEVICE;
  if (chunk.dedupResult_ == NOT_DUP) {
    addr = chunk.cachedataLocation_;
    buf = chunk.compressedBuf_;
    len = (chunk.nSubchunks_) * Config::getInstance().getSubchunkSize();
    return true;
  }
  return false;
}

int ManageModule::write(Chunk &chunk) {
  DeviceType deviceType;
  uint64_t addr;
  uint8_t *buf;
  uint32_t len;

  if (generatePrimaryWriteRequest(chunk, deviceType, addr, buf, len)) {
    IOModule::getInstance().write(deviceType, addr, buf, len);
  }

  if (generateCacheWriteRequest(chunk, deviceType, addr, buf, len)) {
    IOModule::getInstance().write(deviceType, addr, buf, len);
  }
  return 0;
}

void ManageModule::updateMetadata(Chunk &chunk) { MetadataModule::getInstance().update(chunk); }
}  // namespace cache
