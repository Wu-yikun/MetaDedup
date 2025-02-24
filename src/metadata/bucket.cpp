#include "bucket.h"

#include <cassert>
#include <cstring>
#include <utility>

#include "bitmap.h"
#include "cache_policies/cache_policy.h"
#include "common/stats.h"
#include "index.h"
#include "reference_counter.h"

namespace cache {
Bucket::Bucket(uint32_t nBitsPerKey, uint32_t nBitsPerValue, uint32_t nSlots, uint8_t *data, uint8_t *valid,
               CachePolicy *cachePolicy, uint32_t slotId)
    : nBitsPerKey_(nBitsPerKey),
      nBitsPerValue_(nBitsPerValue),
      nBitsPerSlot_(nBitsPerKey + nBitsPerValue),
      nSlots_(nSlots),
      data_(data),
      valid_(valid),
      bucketId_(slotId) {
  if (cachePolicy != nullptr) {
    cachePolicyExecutor_ = cachePolicy->getExecutor(this);
  } else {
    cachePolicyExecutor_ = nullptr;
  }
}

Bucket::~Bucket() {
  if (cachePolicyExecutor_ != nullptr) {
    free(cachePolicyExecutor_);
  }
}

uint32_t LBABucket::lookup(uint32_t lbaSignature, uint64_t &fpHash) {
  for (uint32_t slotId = 0; slotId < nSlots_; slotId++) {
    if (!isValid(slotId)) continue;
    uint32_t _lbaSignature = getKey(slotId);
    if (_lbaSignature == lbaSignature) {
      fpHash = getValue(slotId);
      return slotId;
    }
  }
  return ~((uint32_t)0);
}

void LBABucket::promote(uint32_t lbaSignature) {
  uint64_t fingerprintHash = 0;
  uint32_t slotId = lookup(lbaSignature, fingerprintHash);
  cachePolicyExecutor_->promote(slotId);
}

// If the request modified an existing chunk,
// return the previous fingerprintHash for
// decreasing its reference count
uint64_t LBABucket::update(uint32_t lbaSignature, uint64_t fingerprintHash, std::shared_ptr<FPIndex> fingerprintIndex) {
  uint64_t _fingerprintHash = 0;
  uint32_t slotId = lookup(lbaSignature, _fingerprintHash);
  if (slotId != ~((uint32_t)0)) {
    if (fingerprintHash == getValue(slotId)) {
      promote(lbaSignature);
      return evictedSignature_;
    } else {
      setEvictedSignature(getValue(slotId));
      if (Config::getInstance().getCachePolicyForFPIndex() == CachePolicyEnum::tRecencyAwareLeastReferenceCount &&
          slotId >= Config::getInstance().getLBASlotSeperator()) {
        ReferenceCounter::getInstance().dereference(getValue(slotId));
      }
      setInvalid(slotId);
    }
  }

  slotId = cachePolicyExecutor_->allocate();
  setKey(slotId, lbaSignature);
  setValue(slotId, fingerprintHash);
  setValid(slotId);
  if (Config::getInstance().getCachePolicyForFPIndex() == CachePolicyEnum::tRecencyAwareLeastReferenceCount &&
      slotId >= Config::getInstance().getLBASlotSeperator()) {
    ReferenceCounter::getInstance().reference(fingerprintHash);
  }
  cachePolicyExecutor_->promote(slotId);
  return evictedSignature_;
}

void LBABucket::getFingerprints(std::set<uint64_t> &fpSet) {
  for (uint32_t i = 0; i < nSlots_; ++i) {
    if (isValid(i)) {
      fpSet.insert(getValue(i));
    }
  }
}

/*
 * memory is byte addressable
 * alignment issue needs to be dealed for each element
 *
 */
uint32_t FPBucket::lookup(uint64_t fpSignature, uint32_t &nSlotsOccupied) {
  uint32_t slotId = 0;
  nSlotsOccupied = 0;
  for (; slotId < nSlots_;) {
    if (!isValid(slotId)  // 找到第一个符合的slotId
        || fpSignature != getKey(slotId)) {
      ++slotId;
      continue;
    }
    while (slotId < nSlots_ && isValid(slotId) && fpSignature == getKey(slotId)) {
      ++nSlotsOccupied;
      ++slotId;
    }  // PAPER 3-2 每个slot都保存对应的FP-hash，算是重复保存了
    return slotId - nSlotsOccupied;  // 最后一个符合的ID减占用的slot数量既第一个符合的ID
  }
  return ~((uint32_t)0);
}

void FPBucket::promote(uint64_t fpSignature) {
  uint32_t slot_id = 0, compressibility_level = 0, n_slots_occupied;
  slot_id = lookup(fpSignature, n_slots_occupied);

  cachePolicyExecutor_->promote(slot_id, n_slots_occupied);
}

uint32_t FPBucket::update(uint64_t fpSignature, uint32_t nSlotsToOccupy) {
  uint32_t nSlotsOccupied = 0;
  uint32_t slotId;

  slotId = lookup(fpSignature, nSlotsOccupied);

  if (slotId != ~((uint32_t)0)) {
    // if (Config::getInstance().getCacheMode() == tWriteBack) {
    //   DirtyList::getInstance().addEvictedChunk(
    //       /* Compute ssd location of the evicted data */
    //       /* Actually, full Fingerprint and address is sufficient. */
    //       FPIndex::computeCachedataLocation(bucketId_, slotId),
    //       nSlotsOccupied * Config::getInstance().getSubchunkSize());
    // }

    for (uint32_t _slotId = slotId; _slotId < slotId + nSlotsOccupied; ++_slotId) {
      setInvalid(_slotId);
    }
  }

  slotId = cachePolicyExecutor_->allocate(nSlotsToOccupy);

  for (uint32_t _slotId = slotId; _slotId < slotId + nSlotsToOccupy; ++_slotId) {
    setKey(_slotId, fpSignature);  // fp-hash prefix
    setValid(_slotId);
  }

  return slotId;
}

void FPBucket::evict(uint64_t fpSignature) {
  for (uint32_t index = 0; index < nSlots_; index++) {
    if (getKey(index) == fpSignature) {
      setInvalid(index);
    }
  }
}

void FPBucket::getFingerprints(std::set<uint64_t> &fpSet) {
  for (uint32_t i = 0; i < nSlots_; ++i) {
    if (isValid(i)) {
      fpSet.insert((bucketId_ << nBitsPerKey_) | getKey(i));
    }
  }
}
}  // namespace cache
