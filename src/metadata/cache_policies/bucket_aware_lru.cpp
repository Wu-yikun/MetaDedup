#include "bucket_aware_lru.h"

#include <common/stats.h>

#include "common/config.h"
#include "metadata/reference_counter.h"
namespace cache {

BucketAwareLRUExecutor::BucketAwareLRUExecutor(Bucket *bucket) : CachePolicyExecutor(bucket) {}

void BucketAwareLRUExecutor::promote(uint32_t slotId, uint32_t nSlotsToOccupy) {
  uint32_t prevSlotId = slotId;
  uint32_t nSlots = bucket_->getnSlots();
  uint32_t k = bucket_->getKey(slotId);
  uint64_t v = bucket_->getValue(slotId);
  if (Config::getInstance().getCachePolicyForFPIndex() == CachePolicyEnum::tRecencyAwareLeastReferenceCount) {
    if (prevSlotId < Config::getInstance().getLBASlotSeperator()) {
      ReferenceCounter::getInstance().reference(v);
      if (bucket_->isValid(Config::getInstance().getLBASlotSeperator())) {
        ReferenceCounter::getInstance().dereference(bucket_->getValue(Config::getInstance().getLBASlotSeperator()));
      }
    }
  }
  // Move each slot to the tail
  for (; slotId < nSlots - nSlotsToOccupy; ++slotId) {
    bucket_->setInvalid(slotId);
    bucket_->setKey(slotId, bucket_->getKey(slotId + nSlotsToOccupy));
    bucket_->setValue(slotId, bucket_->getValue(slotId + nSlotsToOccupy));
    if (bucket_->isValid(slotId + nSlotsToOccupy)) {
      bucket_->setValid(slotId);
    }
  }
  // Store the promoted slots to the front (higher slotId is the front)
  for (; slotId < nSlots; ++slotId) {
    bucket_->setKey(slotId, k);
    bucket_->setValue(slotId, v);
    bucket_->setValid(slotId);
  }
}

// Only LBA Index would call this function
// LBA signature only takes one slot.
// So there is no need to worry about the entry taking contiguous slots.
void BucketAwareLRUExecutor::clearObsolete(std::shared_ptr<FPIndex> fpIndex) {
  for (uint32_t slotId = 0; slotId < bucket_->getnSlots(); ++slotId) {
    if (!bucket_->isValid(slotId)) continue;

    uint32_t size;
    uint64_t cachedataLocation, metadataLocation;  // dummy variables
    bool valid = false;
    uint64_t fpHash = bucket_->getValue(slotId);
    if (fpIndex != nullptr) valid = fpIndex->lookup(fpHash, size, cachedataLocation, metadataLocation);
    // if the slot has no mappings in ca index, it is an empty slot
    if (!valid) {
      bucket_->setKey(slotId, 0), bucket_->setValue(slotId, 0);
      bucket_->setInvalid(slotId);
    }
  }
}

uint32_t BucketAwareLRUExecutor::allocate(uint32_t nSlotsToOccupy) {
  uint32_t slotId = 0, nSlotsAvailable = 0, nSlots = bucket_->getnSlots();

  // 找到连续可用的slot
  for (; slotId < nSlots; ++slotId) {
    if (nSlotsAvailable == nSlotsToOccupy) break;
    // find an empty slot
    if (!bucket_->isValid(slotId)) {
      ++nSlotsAvailable;
    } else {
      nSlotsAvailable = 0;
    }
  }

  // 如果连续可用的slot不够ToOccupy的，那么则逐出；
  if (nSlotsAvailable < nSlotsToOccupy) {
    slotId = 0;
    // Evict Least Recently Used slots
    for (; slotId < nSlots;) {
      if (slotId >= nSlotsToOccupy) {
        break;
      }

      if (!bucket_->isValid(slotId)) {
        ++slotId;
        continue;
      }

      uint32_t key = bucket_->getKey(slotId);
      bucket_->setEvictedSignature(bucket_->getValue(slotId));
      while (slotId < nSlots && bucket_->getKey(slotId) == key) {
        bucket_->setInvalid(slotId);
        bucket_->setKey(slotId, 0);
        bucket_->setValue(slotId, 0);
        slotId++;
      }
    }
  }

  // slotId在要么是第一阶段变化的要么是第二阶段变化的；
  return slotId - nSlotsToOccupy;
}

BucketAwareLRU::BucketAwareLRU() = default;
CachePolicyExecutor *BucketAwareLRU::getExecutor(Bucket *bucket) { return new BucketAwareLRUExecutor(bucket); }

}  // namespace cache
