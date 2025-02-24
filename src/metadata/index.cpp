#include "index.h"

#include <utility>

#include "cache_policies/bucket_aware_lru.h"
#include "cache_policies/least_reference_count.h"
#include "cache_policies/lru.h"
#include "common/config.h"
#include "common/stats.h"
#include "reference_counter.h"

// 文件实现了缓存系统中的索引管理功能，主要涉及逻辑块地址（LBA）索引和指纹（Fingerprint, FP）索引
namespace cache {

Index::Index() = default;

LBAIndex::~LBAIndex() = default;
FPIndex::~FPIndex() = default;

void Index::setCachePolicy(std::unique_ptr<CachePolicy> cachePolicy) { cachePolicy_ = std::move(cachePolicy); }

LBAIndex::LBAIndex(std::shared_ptr<FPIndex> fpIndex) : fpIndex_(std::move(fpIndex)) {
  // 每个 LBA 签名的位数
  nBitsPerKey_ = Config::getInstance().getnBitsPerLbaSignature();
  // 每个指纹签名加上 FP 桶 ID 的总位数
  nBitsPerValue_ = Config::getInstance().getnBitsPerFpSignature() + Config::getInstance().getnBitsPerFpBucketId();
  // 每个桶中的槽位数
  nSlotsPerBucket_ = Config::getInstance().getnLBASlotsPerBucket();
  // 总桶数
  nBuckets_ = Config::getInstance().getnLbaBuckets();

  // 每个桶所需的字节数
  nBytesPerBucket_ = ((nBitsPerKey_ + nBitsPerValue_) * nSlotsPerBucket_ + 7) / 8;
  // 用于有效位标记的每个桶所需的字节数
  nBytesPerBucketForValid_ = (1 * nSlotsPerBucket_ + 7) / 8;
  // 分配用于存储 LBA 索引的内存
  data_ = std::make_unique<uint8_t[]>(nBytesPerBucket_ * nBuckets_ + 1);
  // 分配用于存储有效位标记的内存
  valid_ = std::make_unique<uint8_t[]>(nBytesPerBucketForValid_ * nBuckets_ + 1);
  // 如果配置启用了多线程，则为每个桶分配一个 std::mutex 数组，用于桶级别的锁，确保多线程环境下的索引一致性
  if (Config::getInstance().isMultiThreadingEnabled()) {
    mutexes_ = std::make_unique<std::mutex[]>(nBuckets_);
  }

  // 检查配置是否启用了紧凑的缓存策略
  if (Config::getInstance().isCompactCachePolicyEnabled()) {
    setCachePolicy(std::move(std::make_unique<BucketAwareLRU>()));
  } else {
    // setting the cache policy for fp index as LRU means that we disable the "ACDC" cache policy
    setCachePolicy(std::move(std::make_unique<LRU>(nBuckets_)));
  }
}

// 根据给定的 LBA 哈希值查找对应的指纹哈希值
bool LBAIndex::lookup(uint64_t lbaHash, uint64_t &fpHash) {
  // 将 lbaHash 右移 nBitsPerKey_ 位，得到对应的桶ID
  uint32_t bucketId = lbaHash >> nBitsPerKey_;
  // (1u << nBitsPerKey_) - 1 用于生成 nBitsPerKey_ 位的掩码 (常用的位掩码生成方法)
  // 使用位掩码提取 lbaHash 中的低 nBitsPerKey_ 位，作为签名
  uint32_t signature = lbaHash & ((1u << nBitsPerKey_) - 1);
  // 调用 getLBABucket 函数获取对应的 bucket，然后调用 "bucket.cc" 的 lookup 函数查找指纹哈希值
  return getLBABucket(bucketId)->lookup(signature, fpHash) != ~((uint32_t)0);
}

// 提升指定 LBA 哈希值对应条目的优先级（通常用于缓存替换策略中的更新）
void LBAIndex::promote(uint64_t lbaHash) {
  uint32_t bucketId = lbaHash >> nBitsPerKey_;
  uint32_t signature = lbaHash & ((1u << nBitsPerKey_) - 1);
  getLBABucket(bucketId)->promote(signature);
}

// If the request modify an existing LBA, return the previous fingerprint
// 更新指定 LBA 哈希值对应的指纹哈希值，如果更新导致某个条目被驱逐（evicted），则返回被驱逐的指纹哈希值
uint64_t LBAIndex::update(uint64_t lbaHash, uint64_t fpHash) {
  uint32_t bucketId = lbaHash >> nBitsPerKey_;
  uint32_t signature = lbaHash & ((1u << nBitsPerKey_) - 1);
  uint64_t evictedFPHash = getLBABucket(bucketId)->update(signature, fpHash, fpIndex_);

  return evictedFPHash;
}

FPIndex::FPIndex() {
  nBitsPerKey_ = Config::getInstance().getnBitsPerFpSignature();
  nBitsPerValue_ = 4;
  nSlotsPerBucket_ = Config::getInstance().getnFPSlotsPerBucket();
  nBuckets_ = Config::getInstance().getnFpBuckets();

  nBytesPerBucket_ = ((nBitsPerKey_ + nBitsPerValue_) * nSlotsPerBucket_ + 7) / 8;
  nBytesPerBucketForValid_ = (1 * nSlotsPerBucket_ + 7) / 8;
  data_ = std::make_unique<uint8_t[]>(nBytesPerBucket_ * nBuckets_ + 1);
  valid_ = std::make_unique<uint8_t[]>(nBytesPerBucketForValid_ * nBuckets_ + 1);
  if (Config::getInstance().isMultiThreadingEnabled()) {
    mutexes_ = std::make_unique<std::mutex[]>(nBuckets_);
  }

  if (Config::getInstance().isCompactCachePolicyEnabled()) {
    cachePolicy_ = std::move(std::make_unique<LeastReferenceCount>());
  } else {
    cachePolicy_ = std::move(std::make_unique<LRU>(nBuckets_));
  }
}

uint64_t FPIndex::computeCachedataLocation(uint32_t bucketId, uint32_t slotId) {
  return (bucketId * Config::getInstance().getnFPSlotsPerBucket() + slotId) * 1ull *
             Config::getInstance().getSubchunkSize() +
         1ull * Config::getInstance().getnFpBuckets() * Config::getInstance().getnFPSlotsPerBucket() *
             Config::getInstance().getMetadataSize();
}

uint64_t FPIndex::computeMetadataLocation(uint32_t bucketId, uint32_t slotId) {
  return (bucketId * Config::getInstance().getnFPSlotsPerBucket() + slotId) * 1ull *
         Config::getInstance().getMetadataSize();
}

uint64_t FPIndex::cachedataLocationToMetadataLocation(uint64_t cachedataLocation) {
  return (cachedataLocation - 1ull * Config::getInstance().getnFPSlotsPerBucket() *
                                  Config::getInstance().getMetadataSize() * Config::getInstance().getnFpBuckets()) /
         Config::getInstance().getSubchunkSize() * Config::getInstance().getMetadataSize();
}

bool FPIndex::lookup(uint64_t fpHash, uint32_t &nSubchunks, uint64_t &cachedataLocation, uint64_t &metadataLocation) {
  uint32_t bucketId = fpHash >> nBitsPerKey_, signature = fpHash & ((1u << nBitsPerKey_) - 1), nSlotsOccupied = 0;
  uint32_t index = getFPBucket(bucketId)->lookup(signature, nSlotsOccupied);
  if (index == ~0u) return false;

  nSubchunks = nSlotsOccupied;
  cachedataLocation = computeCachedataLocation(bucketId, index);
  metadataLocation = computeMetadataLocation(bucketId, index);

  return true;
}

void FPIndex::promote(uint64_t fpHash) {
  uint32_t bucketId = fpHash >> nBitsPerKey_, signature = fpHash & ((1u << nBitsPerKey_) - 1);
  getFPBucket(bucketId)->promote(signature);
}

void FPIndex::update(uint64_t fpHash, uint32_t nSubchunks, uint64_t &cachedataLocation, uint64_t &metadataLocation) {
  uint32_t bucketId = fpHash >> nBitsPerKey_,           // 低位是prefix，剩下的高位是suffix；
      signature = fpHash & ((1u << nBitsPerKey_) - 1),  // 取低位prefix；
      nSlotsToOccupy = nSubchunks;

  uint32_t slotId;
  // FP-index, metadata region and data region. Theses three bucket slot one one
  // map according to PAPER Figure 3.
  BEGIN_TIMER();
  slotId = getFPBucket(bucketId)->update(signature, nSlotsToOccupy);
  END_TIMER(FPBucket_update);

  BEGIN_TIMER();
  cachedataLocation = computeCachedataLocation(bucketId, slotId);
  END_TIMER(computeCachedataLocation);

  BEGIN_TIMER();
  metadataLocation = computeMetadataLocation(bucketId, slotId);
  END_TIMER(computeMetadataLocation);
}

std::unique_ptr<std::lock_guard<std::mutex>> LBAIndex::lock(uint64_t lbaHash) {
  uint32_t bucketId = lbaHash >> nBitsPerKey_;
  if (Config::getInstance().isMultiThreadingEnabled()) {
    return std::move(std::make_unique<std::lock_guard<std::mutex>>(mutexes_[bucketId]));
  } else {
    return nullptr;
  }
}

void LBAIndex::getFingerprints(std::set<uint64_t> &fpSet) {
  for (uint32_t i = 0; i < nBuckets_; ++i) {
    getLBABucket(i)->getFingerprints(fpSet);
  }
}
void FPIndex::getFingerprints(std::set<uint64_t> &fpSet) {
  for (uint32_t i = 0; i < nBuckets_; ++i) {
    getFPBucket(i)->getFingerprints(fpSet);
  }
}

std::unique_ptr<std::lock_guard<std::mutex>> FPIndex::lock(uint64_t fpHash) {
  uint32_t bucketId = fpHash >> nBitsPerKey_;
  if (Config::getInstance().isMultiThreadingEnabled()) {
    return std::move(std::make_unique<std::lock_guard<std::mutex>>(mutexes_[bucketId]));
  } else {
    return nullptr;
  }
}

void FPIndex::reference(uint64_t fpHash) { ReferenceCounter::getInstance().reference(fpHash); }
void FPIndex::dereference(uint64_t fpHash) { ReferenceCounter::getInstance().dereference(fpHash); }

}  // namespace cache
