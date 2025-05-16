#include "metadata_module.h"

#include <cassert>

#include "common/config.h"
#include "common/env.h"
#include "common/stats.h"
#include "utils/utils.h"

namespace cache {
MetadataModule &MetadataModule::getInstance() {
  static MetadataModule instance;
  return instance;
}

MetadataModule::MetadataModule() {
  fpIndex_ = std::make_shared<FPIndex>();
  lbaIndex_ = std::make_shared<LBAIndex>(fpIndex_);
  metaVerification_ = std::make_unique<MetaVerification>();
  std::cout << "Number of LBA buckets: " << Config::getInstance().getnLbaBuckets() << std::endl;
  std::cout << "Number of Fingerprint buckets: " << Config::getInstance().getnFpBuckets() << std::endl;
}

MetadataModule::~MetadataModule() { dumpStats(); }

void MetadataModule::dumpStats() {
  std::set<uint64_t> fpSetLbaIndex;
  std::set<uint64_t> fpSetFpIndex;
  lbaIndex_->getFingerprints(fpSetLbaIndex);
  fpIndex_->getFingerprints(fpSetFpIndex);
  uint32_t nTotalFingerprints = 0, nInvalidFingerprints = 0;
  for (uint64_t fingerprint : fpSetFpIndex) {
    nTotalFingerprints += 1;
    if (fpSetLbaIndex.find(fingerprint) == fpSetLbaIndex.end()) {
      nInvalidFingerprints += 1;
    }
  }
}

void MetadataModule::dedup(Chunk &chunk) {
  uint64_t fpHash = ~0ull;
  // 如果为空，表示尚未获取锁，需要获取对应 lbaHash_ 的桶锁
  if (chunk.lbaBucketLock_ == nullptr) {
    chunk.lbaBucketLock_ = std::move(lbaIndex_->lock(chunk.lbaHash_));
  }
  // 1. lbaIndex_->lookup
  // 2. getLBABucket(bucketId)->lookup
  chunk.hitLBAIndex_ = lbaIndex_->lookup(chunk.lbaHash_, fpHash) && (fpHash == chunk.fingerprintHash_);

  if (chunk.fpBucketLock_ == nullptr) {
    chunk.fpBucketLock_ = std::move(fpIndex_->lock(chunk.fingerprintHash_));
  }
  chunk.hitFPIndex_ =
      fpIndex_->lookup(chunk.fingerprintHash_, chunk.nSubchunks_, chunk.cachedataLocation_, chunk.metadataLocation_);

  // 命中 FP 索引，验证该 chunk 是否为哈希冲突（即是否存在于 Metadata 中的 LBA 列表中）
  if (chunk.hitFPIndex_) {
    chunk.verficationResult_ = metaVerification_->verify(chunk);
  }

  if (chunk.hitFPIndex_ &&
      (chunk.verficationResult_ == ONLY_FP_VALID || chunk.verficationResult_ == BOTH_LBA_AND_FP_VALID)) {
    // duplicate content
    chunk.dedupResult_ = DUP_CONTENT;
  } else {
    // not duplicate
    chunk.dedupResult_ = NOT_DUP;
  }
}

// 判断数据块是否可以直接从缓存 SSD 中获取: lbaIndex_ 命中, 且 fpIndex_ 命中, 才能去缓存 SSD 中获取完整的 FP 来验证
void MetadataModule::lookup(Chunk &chunk) {
  // Obtain LBA bucket lock
  chunk.lbaBucketLock_ = std::move(lbaIndex_->lock(chunk.lbaHash_));
  chunk.hitLBAIndex_ = lbaIndex_->lookup(chunk.lbaHash_, chunk.fingerprintHash_);
  if (chunk.hitLBAIndex_) {
    chunk.fpBucketLock_ = std::move(fpIndex_->lock(chunk.fingerprintHash_));
    chunk.hitFPIndex_ =
        fpIndex_->lookup(chunk.fingerprintHash_, chunk.nSubchunks_, chunk.cachedataLocation_, chunk.metadataLocation_);
    if (chunk.hitFPIndex_) {
      chunk.verficationResult_ = metaVerification_->verify(chunk);
    }
  }

  if (chunk.verficationResult_ == VerificationResult::ONLY_LBA_VALID) {
    chunk.compressedLen_ = chunk.metadata_.compressedLen_;
    chunk.lookupResult_ = HIT;
  } else {
    chunk.fpBucketLock_.reset();
    assert(chunk.fpBucketLock_.get() == nullptr);
    chunk.lookupResult_ = NOT_HIT;
  }
}

void MetadataModule::update(Chunk &chunk) {
  uint64_t removedFingerprintHash = ~0ull;

  BEGIN_TIMER();
  if (chunk.lookupResult_ == HIT) {
    fpIndex_->promote(chunk.fingerprintHash_);
    lbaIndex_->promote(chunk.lbaHash_);
  } else {
    // Note that for a cache-candidate chunk, hitLBAIndex indicates both hit in the LBA index and fpHash match
    // deduplication focus on the fingerprint part
    if (chunk.hitLBAIndex_) {
      lbaIndex_->promote(chunk.lbaHash_);
    } else {
      removedFingerprintHash = lbaIndex_->update(chunk.lbaHash_, chunk.fingerprintHash_);
      fpIndex_->reference(chunk.fingerprintHash_);
    }
    if (chunk.dedupResult_ == DUP_CONTENT) {
      fpIndex_->promote(chunk.fingerprintHash_);
    } else {
      fpIndex_->update(chunk.fingerprintHash_, chunk.nSubchunks_, chunk.cachedataLocation_, chunk.metadataLocation_);
    }
  }
  END_TIMER(update_index);

  // Cases when an on-ssd metadata update is needed
  // 1. DUP_CONTENT (update lba lists in the on-ssd metadata if the lba is not in the list)
  // 2. NOT_DUP (write a new metadata and a new cached data)
  /*
    如果数据块是新的（未命中），需要将其指纹和 LBA 信息插入到索引中
    如果数据块是重复的（命中），需要提升其在索引中的优先级（例如 LRU 策略）
  */
  if ((chunk.dedupResult_ == DUP_CONTENT && chunk.verficationResult_ != BOTH_LBA_AND_FP_VALID) ||
      chunk.dedupResult_ == NOT_DUP) {
    metaVerification_->update(chunk);
  }

  // if (Config::getInstance().getCacheMode() == tWriteBack) {
  //   DirtyList::getInstance().addLatestUpdate(chunk.addr_, chunk.cachedataLocation_,
  //                                            (chunk.nSubchunks_) * Config::getInstance().getSubchunkSize());
  // }

  if (removedFingerprintHash != ~0ull && removedFingerprintHash != chunk.fingerprintHash_) {
    fpIndex_->dereference(removedFingerprintHash);
  }
}
}  // namespace cache
