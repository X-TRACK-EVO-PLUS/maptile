# X-MAP

Research on rendering of vector maps on embedded devices.

## 代码及环境

### 代码下载

``` shell
git clone git@github.com:X-TRACK-OVERFLOW/X-MAP.git --recursive
```

### 环境

``` shell
# install protobuf compiler
apt-get install protobuf-compiler # for Ubuntu/Debian
brew install protobuf # for MacOS

# verify
protoc --version
```

### 编译

``` shell
mkdir build
cd build
cmake ..
make -j
```

### 运行

``` shell
./build/Simulator
```

## Proto

### compile vector-tile-proto

``` shell
echo "syntax = \"proto2\";" > MapTileRender/vector_tile.proto
cat external/vector-tile-spec/2.1/vector_tile.proto >> MapTileRender/vector_tile.proto
```
