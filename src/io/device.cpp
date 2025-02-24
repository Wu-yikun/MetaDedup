#include "device.h"

#include <assert.h>
#include <fcntl.h>
#include <libpmemobj.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <cassert>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <unordered_map>

namespace cache {
Device::~Device() { close(_fd); }

BlockDevice::BlockDevice() {}

BlockDevice::~BlockDevice() { close(_fd); }

void BlockDevice::sync() { ::syncfs(_fd); }

int BlockDevice::open(char *filename, uint64_t size) {
  struct stat stat_buf;
  int handle = ::stat(filename, &stat_buf);
  if (handle == -1) {
    if (errno == ENOENT) {
      return open_new_device(filename, size);
    } else {
      return -1;
    }
  } else {
    return open_existing_device(filename, size, &stat_buf);
  }
}

int BlockDevice::open_existing_device(char *filename, uint64_t size, struct stat *statbuf) {
  int fd = 0;
  std::cout << "BlockDevice::Open existing device!" << std::endl;
  if (Config::getInstance().isDirectIOEnabled())
    fd = ::open(filename, O_RDWR | O_DIRECT);
  else
    fd = ::open(filename, O_RDWR);

  if (fd < 0) {
    std::cout << "No O_DIRECT!" << std::endl;
    fd = ::open(filename, O_RDWR);
    if (fd < 0) {
      return -1;
    }
  }

  if (size == 0) {
    if (S_ISREG(statbuf->st_mode))
      size = statbuf->st_size;
    else
      size = get_size(fd);
  }
  _size = size;
  _fd = fd;
  return 0;
}

int BlockDevice::open_new_device(char *filename, uint64_t size) {
  std::cout << "BlockDevice::Open new device!" << std::endl;
  int fd = 0;
  if (size == 0) {
    return -1;
  }
  if (Config::getInstance().isDirectIOEnabled())
    fd = ::open(filename, O_RDWR | O_DIRECT | O_CREAT, 0666);
  else
    fd = ::open(filename, O_RDWR | O_CREAT, 0666);
  if (fd < 0) {
    fd = ::open(filename, O_RDWR | O_CREAT, 0666);
    if (fd < 0) {
      // cannot open the file
      return -1;
    }
  }
  if (::ftruncate(fd, size) < 0) {
    return -1;
  }
  _fd = fd;
  _size = size;
  return 0;
}

int BlockDevice::write(uint64_t addr, uint8_t *buf, uint32_t len) {
  assert(addr % 512 == 0);
  assert(len % 512 == 0);

  if (addr % _size + len > _size) {
    len -= (addr % _size + len - _size);
  }

  int n_written_bytes = 0;
  while (1) {
    int n = ::pwrite(_fd, buf, len, addr);
    if (n < 0) {
      std::cout << "addr: " << addr << " len: " << len << std::endl;
      std::cout << "BlockDevice::write errno: " << errno << " - " << std::strerror(errno) << std::endl;
      throw std::runtime_error(std::strerror(errno));
    }
    n_written_bytes += n;
    if (n == len) {
      break;
    } else {
      buf += n;
      len -= n;
    }
  }
  return n_written_bytes;
}

int BlockDevice::read(uint64_t addr, uint8_t *buf, uint32_t len) {
  assert(addr % 512 == 0);
  assert(len % 512 == 0);

  if (addr % _size + len > _size) {
    len -= (addr % _size + len - _size);
  }

  int n_read_bytes = 0;
  while (1) {
    int n = ::pread(_fd, buf, len, addr);
    if (n < 0) {
      std::cout << (long)buf << " " << addr << " " << len << std::endl;
      std::cout << "BlockDevice::read " << std::strerror(errno) << std::endl;
      assert(0);
    }
    n_read_bytes += n;
    if (n == len) {
      break;
    } else {
      buf += n;
      len -= n;
    }
  }

  return n_read_bytes;
}

int BlockDevice::get_size(int fd) {
  uint32_t offset = 1024;
  uint32_t prev_offset = 0;
  char buffer[1024];
  char *read_buffer = (char *)(((long)buffer + 511) & ~511);

  while (1) {
    if (::lseek(fd, offset, SEEK_SET) == -1) {
      offset -= (offset - prev_offset) / 2;
      continue;
    }
    int nr = 0;
    if ((nr = ::read(fd, read_buffer, 512)) != 512) {
      if (nr >= 0) {
        offset += nr;
        return offset;
      } else {
        if (prev_offset == offset)
          return offset;
        else
          offset -= (offset - prev_offset) / 2;
      }
    } else {
      prev_offset = offset;
      offset *= 2;
    }
  }
}

// ----------------------------------PMEM pool----------------------------------
ByteDevice::ByteDevice() : pmem_pool(nullptr), pool_size(0) {}

ByteDevice::~ByteDevice() {
  if (pmem_pool != nullptr) {
    pmemobj_close(pmem_pool);
  }
}

int ByteDevice::open(char *filename, uint64_t size) {
  const char *layout_name = "pmem_layout";
  size_t pool_size = size;

  if (access(filename, 0)) {
    pmem_pool = pmemobj_create(filename, layout_name, pool_size, 0666);
    if (pmem_pool == nullptr) {
      std::cout << "Create fail\n";
      exit(1);
    }
    std::cout << "Create PMEM pool\n";
  } else {
    pmem_pool = pmemobj_open(filename, layout_name);
    if (pmem_pool == nullptr) {
      std::cout << "Open fail\n";
      exit(1);
    }
    std::cout << "Open PMEM pool\n";
  }
  return 0;
}

int ByteDevice::write(uint64_t lba, uint8_t *buf, uint32_t len) {
  // 检查是否需要为新的 LBA 分配持久化内存
  if (lba_to_pmem.find(lba) == lba_to_pmem.end()) {
    // 首次写入，为该 LBA 分配 512 字节持久化内存
    void *addr = allocate_pmem(len);
    lba_to_pmem[lba] = addr;
  }

  memcpy(lba_to_pmem[lba], buf, len);
  pmemobj_persist(pmem_pool, lba_to_pmem[lba], len);

  return len;
}

int ByteDevice::read(uint64_t lba, uint8_t *buf, uint32_t len) {
  auto it = lba_to_pmem.find(lba);
  if (it == lba_to_pmem.end()) {
    std::cout << "Error: LBA " << lba << " not found in PM.\n";
    return -1;
  }
  memcpy(buf, it->second, len);  // 从持久化内存中读取数据
  // std::cout << "Read from LBA: " << lba << "\n";

  return len;
}

void ByteDevice::sync() {}

}  // namespace cache