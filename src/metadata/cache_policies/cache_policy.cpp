
#include "cache_policy.h"

namespace cache {
CachePolicyExecutor::CachePolicyExecutor(Bucket *bucket) : bucket_(bucket) {}
CachePolicy::CachePolicy() = default;
}  // namespace cache
