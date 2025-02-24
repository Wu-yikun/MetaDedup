## Build
### Testbed Environment
1. Third party libraries: LZ4, ISA-L_crypto
2. OS: Ubuntu 16.04 with Linux kernel 4.4.0-170-generic
3. Compiler tools: cmake 3.15.2, gcc 5.4.0

### Build third_party libraries
```
mkdir third_party && cd third_party
```

#### LZ4 compression algorithms
```
wget https://github.com/lz4/lz4/archive/v1.9.1.zip
unzip v1.9.1.zip
cd lz4-1.9.1
make
cd ..
```

#### ISA-L Crypto
```
git clone https://github.com/intel/isa-l_crypto.git
cd isa-l_crypto
./autogen.sh
./configure --prefix=$(pwd)
make && make install
cd ..
```