#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <malloc.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <climits>
#include <fstream>
#include <functional>
#include <random>
#include <set>
#include <thread>
#include <vector>

#include "lz4.h"
#include "meta/meta_dedup.h"
#include "utils/cJSON.h"
#include "utils/gen_zipf.h"
#include "utils/utils.h"
struct Request {
  Request() {
    address_ = 0;
    length_ = 0;
    isRead_ = 0;
    compressionLength_ = 0;
    double compressibility_ = 1;
  }

  uint64_t address_;
  int length_;
  bool isRead_;
  uint32_t compressionLength_;
  char sha1_[42];
  double compressibility_;
};

namespace cache {
class RunSystem {
 public:
  RunSystem() {}
  void clear() {}
  ~RunSystem() {}

  void parse_argument(int argc, char **argv) {
    char source[2000 + 1];
    FILE *fp = fopen(argv[1], "r");
    if (fp != NULL) {
      size_t newLen = fread(source, sizeof(char), 1001, fp);
      if (ferror(fp) != 0) {
        fputs("Error reading file", stderr);
      } else {
        source[newLen++] = '\0'; /* Just to be safe. */
      }
      fclose(fp);
    } else {
      perror("open json file failed");
    }

    cJSON *config = cJSON_Parse(source);
    for (cJSON *param = config->child->next->child; param != nullptr; param = param->next) {
      char *name = param->string;
      char *valuestring = param->valuestring;
      uint64_t valuell = (uint64_t)param->valuedouble;

      if (strcmp(name, "primaryDeviceName") == 0) {
        Config::getInstance().setPrimaryDeviceName(valuestring);
      } else if (strcmp(name, "cacheDeviceName") == 0) {
        Config::getInstance().setCacheDeviceName(valuestring);
      } else if (strcmp(name, "primaryDeviceSize") == 0) {
        Config::getInstance().setPrimaryDeviceSize(valuell);
      } else if (strcmp(name, "cacheDeviceSize") == 0) {
        Config::getInstance().setCacheDeviceSize(valuell);
      } else if (strcmp(name, "workingSetSize") == 0) {
        Config::getInstance().setWorkingSetSize(valuell);
      } else if (strcmp(name, "chunkSize") == 0) {
        Config::getInstance().setChunkSize(valuell);
        chunkSize_ = valuell;
      } else if (strcmp(name, "subchunkSize") == 0) {
        Config::getInstance().setSubchunkSize(valuell);
      } else if (strcmp(name, "nSlotsPerFpBucket") == 0) {
        Config::getInstance().setnSlotsPerFpBucket(valuell);
      } else if (strcmp(name, "nSlotsPerLbaBucket") == 0) {
        Config::getInstance().setnSlotsPerLbaBucket(valuell);
      } else if (strcmp(name, "nBitsPerFpSignature") == 0) {
        Config::getInstance().setnBitsPerFpSignature(valuell);
      } else if (strcmp(name, "nBitsPerLbaSignature") == 0) {
        Config::getInstance().setnBitsPerLbaSignature(valuell);
      } else if (strcmp(name, "lbaAmplifier") == 0) {
        Config::getInstance().setLBAAmplifier(valuell);
      } else if (strcmp(name, "syntheticCompression") == 0) {
        Config::getInstance().enableSynthenticCompression(valuell);
      } else if (strcmp(name, "compactCachePolicy") == 0) {
        Config::getInstance().enableCompactCachePolicy(valuell);
      } else if (strcmp(name, "sketchBasedReferenceCounter") == 0) {
        Config::getInstance().enableSketchRF(valuell);
      } else if (strcmp(name, "multiThreading") == 0) {
        Config::getInstance().enableMultiThreading(valuell);
      } else if (strcmp(name, "nThreads") == 0) {
        Config::getInstance().setnThreads(valuell);
      } else if (strcmp(name, "cacheMode") == 0) {
        if (strcmp(valuestring, "WriteThrough") == 0) {
          Config::getInstance().setCacheMode(CacheModeEnum::tWriteThrough);
        } else if (strcmp(valuestring, "WriteBack") == 0) {
          Config::getInstance().setCacheMode(CacheModeEnum::tWriteBack);
        }
      } else if (strcmp(name, "directIO") == 0) {
        Config::getInstance().enableDirectIO(valuell);
      } else if (strcmp(name, "traceReplay") == 0) {
        Config::getInstance().enableTraceReplay(valuell);
      } else if (strcmp(name, "fakeIO") == 0) {
        Config::getInstance().enableFakeIO(valuell);
      } else if (strcmp(name, "SelectiveCompression") == 0) {
        Config::getInstance().enableSC(valuell);
      } else if (strcmp(name, "SelectiveDeduplication") == 0) {
        Config::getInstance().enableSD(valuell);
      }
    }

    if (Config::getInstance().isSynthenticCompressionEnabled()) {
      generateCompression();
    }

    printf("primary device size: %" PRId64 " GiB\n", Config::getInstance().getPrimaryDeviceSize() / 1024 / 1024 / 1024);
    printf("cache device size: %" PRId64 " GiB\n", Config::getInstance().getCacheDeviceSize() / 1024 / 1024 / 1024);
    printf("woring set size: %" PRId64 " GiB\n", Config::getInstance().getWorkingSetSize() / 1024 / 1024 / 1024);
    Meta_ = std::make_unique<Meta>();

    assert(readFIUTrace(config->child->valuestring) == 0);
    cJSON_Delete(config);
  }

  void memoryRepeat(char *chunkData, char *fingerprint) {
    int offset = 40;
    int size = 40;
    memcpy(chunkData, fingerprint, 40);
    for (; offset + size < chunkSize_; size <<= 1, offset <<= 1) {
      memcpy(chunkData + offset, chunkData, size);
    }
    memcpy(chunkData + offset, chunkData, chunkSize_ - offset);
  }

  int readFIUTrace(char *fileName) {
    FILE *f = fopen(fileName, "r");
    char op[2];
    const int chunkSize = Config::getInstance().getChunkSize();

    int chunk_id, length;
    assert(f != nullptr);
    Request req;
    static uint64_t cnt = 0;

    double compressibility;

    while (fscanf(f, "%lu %d %s %s %lf", &req.address_, &req.length_, op, req.sha1_, &compressibility) != -1) {
      cnt++;

      if (Config::getInstance().isSynthenticCompressionEnabled()) {
        int clen = (double)chunkSize / compressibility;
        if (clen >= chunkSize) clen = chunkSize;
        req.compressionLength_ = clen;
        while (req.compressionLength_ < chunkSize) {
          if (compressedChunks_[req.compressionLength_] != nullptr) {
            break;
          }
          req.compressionLength_ += 1;
        }
      }

      req.isRead_ = (op[0] == 'r' || op[0] == 'R');
      req.compressibility_ = compressibility;

      reqs_.emplace_back(req);
    }
    printf("%s: Go through %lu operations, selected %lu\n", fileName, cnt, reqs_.size());

    fclose(f);
    return 0;
  }

  void sendRequest(Request &req) {
    uint64_t lba;
    int len;
    char sha1[43];
    alignas(512) char rwdata[chunkSize_];

    lba = req.address_;
    len = req.length_;

    std::string s = std::string(req.sha1_);
    convertStr2Sha1(req.sha1_, sha1);

    Config::getInstance().setFingerprint(req.address_, sha1);

    if (Config::getInstance().isSynthenticCompressionEnabled()) {
      if (Config::getInstance().isFakeIOEnabled() || !req.isRead_) {
        memcpy(rwdata, originalChunks_[req.compressionLength_], len);
      }
    } else {
      if (Config::getInstance().isFakeIOEnabled() || !req.isRead_) {
        memoryRepeat(rwdata, req.sha1_);
      }
    }

    // [LBA] [R/W bytes] [R/W] [SHA1 of block data] [Compressity]
    //  -->
    // [LBA] [R/W bytes] [R/W] [原始数据/压缩后数据] [Compressity]
    if (req.isRead_) {
      Meta_->read(lba, rwdata, len);
    } else {
      Meta_->write(lba, rwdata, len, req.compressibility_);
    }
  }

  void convertStr2Sha1(char *s, char *sha1) {
    assert(strlen(s) >= 40);
    for (int i = 0; i < 40; i += 2) {
      sha1[i / 2] = 0;
      if (s[i] <= '9')
        sha1[i / 2] += ((s[i] - '0') << 4);
      else if (s[i] <= 'F')
        sha1[i / 2] += ((s[i] - 'A' + 10) << 4);
      else if (s[i] <= 'f')
        sha1[i / 2] += ((s[i] - 'a' + 10) << 4);
      if (s[i + 1] <= '9')
        sha1[i / 2] += s[i + 1];
      else if (s[i + 1] <= 'F')
        sha1[i / 2] += s[i + 1] - 'A' + 10;
      else if (s[i + 1] <= 'f')
        sha1[i / 2] += s[i + 1] - 'a' + 10;
    }
  }

  void work(std::atomic<uint64_t> &total_bytes) {
    int nThreads = 1;
    uint32_t chunkSize = Config::getInstance().getChunkSize();
    if (Config::getInstance().isMultiThreadingEnabled()) {
      nThreads = Config::getInstance().getMaxNumGlobalThreads();
    }

    AThreadPool *threadPool = new AThreadPool(nThreads);
    char sha1[23];
    for (uint32_t i = 0; i < reqs_.size(); ++i) {
      threadPool->doJob([this, i]() {
        {
          std::unique_lock<std::mutex> l(mutex_);
          while (accessSet_.find(reqs_[i].address_) != accessSet_.end()) {
            condVar_.wait(l);
          }
          accessSet_.insert(reqs_[i].address_);
        }
        if (i % 100000 == 0) printf("req %u\n", i);
        sendRequest(reqs_[i]);
        {
          std::unique_lock<std::mutex> l(mutex_);
          accessSet_.erase(reqs_[i].address_);
          condVar_.notify_all();
        }
      });
      total_bytes += chunkSize;
    }
    condVar_.notify_all();
    delete threadPool;
    sync();
  }

  void generateCompression() {
    uint32_t chunkSize = Config::getInstance().getChunkSize();
    int tmp = posix_memalign(reinterpret_cast<void **>(&compressedChunks_), 512, sizeof(char *) * (1 + chunkSize));
    if (tmp < 0) {
      std::cout << "Cannot allocate memory!" << std::endl;
      exit(-1);
    }
    tmp = posix_memalign(reinterpret_cast<void **>(&originalChunks_), 512, sizeof(char *) * (1 + chunkSize));
    if (tmp < 0) {
      std::cout << "Cannot allocate memory!" << std::endl;
      exit(-1);
    }

    char originalChunk[chunkSize], compressedChunk[chunkSize];
    memset(originalChunk, 1, sizeof(char) * chunkSize);
    memset(compressedChunk, 1, sizeof(char) * chunkSize);

    for (uint32_t i = 0; i <= chunkSize; ++i) {
      compressedChunks_[i] = nullptr;
      originalChunks_[i] = nullptr;
    }

    using random_bytes_engine = std::independent_bits_engine<std::default_random_engine, CHAR_BIT, unsigned char>;
    random_bytes_engine rbe(107u);
    for (uint32_t i = 0; i < chunkSize; i += 8) {
      if (i) std::generate(originalChunk, originalChunk + i, std::ref(rbe));
      int clen = LZ4_compress_default(originalChunk, compressedChunk, chunkSize, chunkSize - 1);
      if (clen == 0) clen = chunkSize;

      if (compressedChunks_[clen] == nullptr) {
        int tmp = posix_memalign(reinterpret_cast<void **>(&compressedChunks_[clen]), 512, chunkSize);
        if (tmp < 0) {
          std::cout << "Cannot allocate memory!" << std::endl;
          exit(-1);
        }
        tmp = posix_memalign(reinterpret_cast<void **>(&originalChunks_[clen]), 512, chunkSize);
        if (tmp < 0) {
          std::cout << "Cannot allocate memory!" << std::endl;
          exit(-1);
        }

        memcpy(compressedChunks_[clen], compressedChunk, sizeof(char) * clen);
        memcpy(originalChunks_[clen], originalChunk, sizeof(char) * chunkSize);
      }
    }
  }

 private:
  uint64_t workingSetSize_;
  uint32_t chunkSize_;

  char **compressedChunks_;
  char **originalChunks_;
  std::unique_ptr<Meta> Meta_;
  std::vector<Request> reqs_;
  std::set<uint64_t> accessSet_;
  std::mutex mutex_;
  std::condition_variable condVar_;
};
}  // namespace cache

int main(int argc, char **argv) {
  // debug
  setbuf(stdout, NULL);
  setbuf(stderr, NULL);

  if (cache::Config::getInstance().isTraceReplayEnabled()) {
    printf("defined: REPLAY_FIU\n");
  }

  srand(0);
  cache::RunSystem run_system;

  run_system.parse_argument(argc, argv);

  std::atomic<uint64_t> total_bytes(0);
  long long elapsed = 0;

  // sendRequest() in work(), will be called by multiple threads
  PERF_FUNCTION(elapsed, run_system.work, total_bytes);

  std::cout << "Replay finished, statistics: \n";
  std::cout << "total MBs: " << (double)total_bytes / (1024 * 1024) << std::endl;
  std::cout << "elapsed: " << elapsed << " us" << std::endl;
  std::cout << "Throughput: " << (double)total_bytes / elapsed << " MBytes/s" << std::endl;

  run_system.clear();
  return 0;
}