#ifndef __COMMON_H__
#define __COMMON_H__

#define DIRECT_IO

#include <atomic>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <mutex>

#include "common/config.h"
#include "common/env.h"
#include "utils/utils.h"

namespace cache {

/**
 * @brief Metadata is an on-ssd data structure storing Full-CA and Full-LBAs
 *        When a chunk matches both LBA index and CA index for prefix matching,
 *        metadata is fetched from SSD to verify if the chunk is duplicate or not.
 */
// MetadataSize_: 60 * 8 + 20 + 4 + 4 + 4 = 512
struct Metadata {
  uint64_t LBAs_[MAX_NUM_LBAS_PER_CACHED_CHUNK];  // 8 byte * 60 = 480
  uint8_t fingerprint_[20];
  uint32_t nextEvict_;
  uint32_t numLBAs_;
  // If the data is compressed, the compressed_len is valid, otherwise, it is 0.
  uint32_t compressedLen_;
};

enum DedupResult { DUP_CONTENT, NOT_DUP, DEDUP_UNKNOWN };

enum LookupResult { HIT, NOT_HIT, LOOKUP_UNKNOWN };

enum VerificationResult {
  BOTH_LBA_AND_FP_VALID,
  ONLY_FP_VALID,
  ONLY_LBA_VALID,
  BOTH_LBA_AND_FP_NOT_VALID,
  VERIFICATION_UNKNOWN
};

enum DeviceType { PRIMARY_DEVICE, CACHE_DEVICE };

/*
 * The basic read/write/evict unit.
 * An object of class Chunk is passed along the data path of a single request.
 * Members:
 *   1. address, buffer, length of data to be read/write
 *     Here the buffer refers to the one passed in the corresponding request.
 *     Note: The write data or read buffer of the original request is managed by the caller. It would be efficient if we
 *     can manage to pass the original buffer to the underlying I/O request.
 *   2. LBA-hash, FP-hash, and CA
 *      These members are Deduplication and index related
 *   3. lookup_result (write dup, read hit, etc.), verification_result (lba valid, ca valid, etc.)
 *   4. lba_hit, ca_hit, for performance statistics
 *   5. compressed_buf, compressed_len, compress_level
 *      Compression related.
 *      Compress_level varies from [0, 1, 2, 3]
 *      which specifies 25%, 50%, 75%, 100% compression ratio, respectively.
 *
 *   6. (CacheDedup-CDARC-specific) weu_id, weu_offset, and evicted_weu_id from
 *the decision of the CDARCFPIndex.
 **/

struct Chunk {
  Metadata metadata_;

  uint64_t addr_;
  uint32_t len_;
  uint8_t *buf_;

  uint8_t *compressedBuf_;
  uint32_t compressedLen_;

  uint32_t nSubchunks_;  // compression level: 0, 1, 2, 3 representing 1, 2, 3, 4 * 8 KiB
  double compressibility;

  uint8_t fingerprint_[20];
  uint64_t lbaHash_;
  uint64_t fingerprintHash_;
  
  bool hasFingerprint_;

  uint64_t cachedataLocation_;
  uint64_t metadataLocation_;

  bool hitLBAIndex_;
  bool hitFPIndex_;

  DedupResult dedupResult_;
  LookupResult lookupResult_;
  VerificationResult verficationResult_;

  // For multithreading, indexing update must be serialized
  // Bucket-level locks are used to guarantee the consistency of index
  std::unique_ptr<std::lock_guard<std::mutex>> lbaBucketLock_;
  std::unique_ptr<std::lock_guard<std::mutex>> fpBucketLock_;

  Chunk() = default;
  Chunk(const Chunk &c) {
    addr_ = c.addr_;
    len_ = c.len_;
    buf_ = c.buf_;

    hasFingerprint_ = false;
    hitLBAIndex_ = false;
    hitFPIndex_ = false;
    verficationResult_ = VERIFICATION_UNKNOWN;
    lookupResult_ = LOOKUP_UNKNOWN;

    lbaHash_ = Chunk::computeLBAHash(addr_);
  }

  /**
   * @brief compute fingerprint of current chunk.
   */
  void computeFingerprint();
  static uint64_t computeFingerprintHash(uint8_t *fingerprint);
  static uint64_t computeLBAHash(uint64_t lba);
  inline bool aligned() {
#ifdef DIRECT_IO
    return len_ == Config::getInstance().getChunkSize() && (long long)buf_ % 512 == 0;
#else
    return len_ == Config::getInstance().getChunkSize();
#endif
  }
};

}  // namespace cache
#endif  //__COMMON_H__