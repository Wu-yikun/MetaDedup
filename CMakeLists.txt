cmake_minimum_required(VERSION 3.14)
project(MetaDedup)

set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_FLAGS_RELEASE "-march=native")
set(CMAKE_BUILD_TYPE "Debug")

include_directories(include)
include_directories(src)
include_directories(/usr/local/include)     # Update this to where pmemobj.h is located
link_directories(/usr/local/lib)            # Update this to where libpmemobj.so or libpmemobj.a is located

list(APPEND CacheLibSources
        src/metadata/bucket.cpp
        src/metadata/index.cpp
        src/metadata/meta_verification.cpp

        src/chunking/chunk_module.cpp

        src/deduplication/deduplication_module.cpp
        src/deduplication/selective_deduplication.cpp

        src/compression/compression_module.cpp
        src/compression/selective_compression_module.cpp

        src/meta/meta_dedup.cpp

        src/io/device.cpp
        src/io/io_module.cpp

        src/manage/manage_module.cpp

        src/utils/xxhash.c
        src/metadata/reference_counter.cpp
        src/metadata/cache_policies/lru.cpp
        src/metadata/cache_policies/bucket_aware_lru.cpp
        src/metadata/cache_policies/least_reference_count.cpp
        src/metadata/cache_policies/cache_policy.cpp
        )

list(APPEND CacheLibSources src/metadata/metadata_module.cpp)

add_library(cache ${CacheLibSources})

# Linking libraries for compression and crypto
include_directories(${CMAKE_SOURCE_DIR}/third_party/lz4-1.9.1/lib)
link_directories(${CMAKE_SOURCE_DIR}/third_party/lz4-1.9.1/lib)
include_directories(${CMAKE_SOURCE_DIR}/third_party/isa-l_crypto/include)
link_directories(${CMAKE_SOURCE_DIR}/third_party/isa-l_crypto/lib)

target_link_libraries(cache lz4 pthread isal_crypto)

# Linking the persistent memory (PMEM) object library
target_link_libraries(cache pmemobj)  # Ensure libpmemobj is linked

################################
# Micro Benchmarks
################################
add_executable(run src/benchmark/run.cpp src/utils/cJSON.c)
target_link_libraries(run cache)

# Ensure linking with PMEMobj for benchmark
target_link_libraries(run pmemobj)
