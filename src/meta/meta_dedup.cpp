#include "meta_dedup.h"

#include <malloc.h>
#include <unistd.h>

#include <cassert>
#include <chrono>
#include <csignal>
#include <thread>
#include <vector>

#include "common/config.h"
#include "common/env.h"

namespace cache {
Meta::Meta() {
  IOModule::getInstance().addPrimaryDevice(Config::getInstance().getPrimaryDeviceName());
  IOModule::getInstance().addCacheDevice(Config::getInstance().getCacheDeviceName());
}

Meta::~Meta() {
  Stats::getInstance().dump();
  Stats::getInstance().release();
  Config::getInstance().release();
  malloc_trim(0);
  double vm, rss;
  dumpMemoryUsage(vm, rss);
  std::cout << std::fixed << "VM: " << vm << "; RSS: " << rss << std::endl;
}

void Meta::read(uint64_t addr, void *buf, uint32_t len) {
  Stats::getInstance().setCurrentRequestType(0);  // read
  Chunker chunker = ChunkModule::getInstance().createChunker(addr, buf, len);

  alignas(512) Chunk chunk;
  while (chunker.next(chunk)) {
    internalRead(chunk);
    chunk.fpBucketLock_.reset();
    chunk.lbaBucketLock_.reset();
  }
}

void Meta::write(uint64_t addr, void *buf, uint32_t len, double cb) {
  Stats::getInstance().setCurrentRequestType(1);  // write
  Chunker chunker = ChunkModule::getInstance().createChunker(addr, buf, len);
  alignas(512) Chunk c;

  while (chunker.next(c)) {
    c.compressibility = cb;
    internalWrite(c);
    c.fpBucketLock_.reset();
    c.lbaBucketLock_.reset();
  }
}

void Meta::internalRead(Chunk &chunk) {
  alignas(512) uint8_t compressedBuf[Config::getInstance().getChunkSize()];
  chunk.compressedBuf_ = compressedBuf;

  DeduplicationModule::lookup(chunk);
  // {
  //   Stats::getInstance().addReadLookupStatistics(chunk);
  // }
  ManageModule::getInstance().read(chunk);
  if (chunk.lookupResult_ == HIT) {
    CompressionModule::decompress(chunk);
  }

  if (chunk.lookupResult_ == NOT_HIT) {
    chunk.computeFingerprint();
    CompressionModule::compress(chunk);
    DeduplicationModule::dedup(chunk);
    ManageModule::getInstance().updateMetadata(chunk);
    if (chunk.dedupResult_ == NOT_DUP) {
      ManageModule::getInstance().write(chunk);
    }
    Stats::getInstance().add_read_post_dedup_stat(chunk);
  } else {
    ManageModule::getInstance().updateMetadata(chunk);
  }
}

void Meta::internalWrite(Chunk &chunk) {
  // chunk.lookupResult_ = LOOKUP_UNKNOWN;
  alignas(512) uint8_t tempBuf[Config::getInstance().getChunkSize()];
  chunk.compressedBuf_ = tempBuf;
  chunk.computeFingerprint();
  DeduplicationModule::dedup(chunk);
  if (chunk.dedupResult_ == NOT_DUP) {  // 唯一块需要压缩
    CompressionModule::compress(chunk);
  }
  ManageModule::getInstance().updateMetadata(chunk);
  ManageModule::getInstance().write(chunk);
  Stats::getInstance().add_write_stat(chunk);
}

}  // namespace cache