#ifndef __DEVICE_H__
#define __DEVICE_H__

#include <fcntl.h>
#include <libpmemobj.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <cstdint>
#include <iostream>
#include <unordered_map>

#include "metadata/index.h"

// #include "metadata/index.h"

#define TOID_TYPE_NUM_CHAR 1

namespace cache {

class Device {
 public:
  virtual ~Device();
  virtual int read(uint64_t addr, uint8_t *buf, uint32_t len) = 0;
  virtual int write(uint64_t addr, uint8_t *buf, uint32_t len) = 0;
  bool _direct_io;

 protected:
  int _fd;
  void *_device_buffer;
  uint64_t _size;
};

class BlockDevice : public Device {
 public:
  BlockDevice();
  ~BlockDevice();
  int read(uint64_t addr, uint8_t *buf, uint32_t len);
  int write(uint64_t addr, uint8_t *buf, uint32_t len);
  int open(char *filename, uint64_t size);
  void sync();

 private:
  int open_new_device(char *filename, uint64_t size);
  int open_existing_device(char *filename, uint64_t size, struct stat *statbuf);
  int get_size(int fd);
};

class ByteDevice : public Device {
 public:
  ByteDevice();
  ~ByteDevice();
  int read(uint64_t addr, uint8_t *buf, uint32_t len);
  int write(uint64_t addr, uint8_t *buf, uint32_t len);
  int open(char *filename, uint64_t size);
  void sync();

 private:
  PMEMobjpool *pmem_pool;
  size_t pool_size;

  struct PmemEntry {
    void *addr;
    size_t size;
  };

  // 用于存储 LBA 到 PMEM 地址的映射
  std::unordered_map<uint64_t, void *> lba_to_pmem;

  // 分配持久化内存
  void *allocate_pmem(size_t size) {
    void *addr;
    PMEMoid ptr;
    int ret = pmemobj_zalloc(pmem_pool, &ptr, size, TOID_TYPE_NUM_CHAR);
    if (ret) {
      std::cout << "Allocate PMEM fail\n";
      exit(1);
    }
    addr = pmemobj_direct(ptr);
    return addr;
  }
};

}  // namespace cache

#endif
