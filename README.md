# RocksDB on HDFS踩坑&&优化

### 官网https://github.com/facebook/rocksdb/tree/master/hdfs

## 踩过的坑

1. setup.sh需要替换：

```
#!/usr/bin/env bash
# shellcheck disable=SC2148
# Copyright (c) Facebook, Inc. and its affiliates. All Rights Reserved.
ulimit -c unlimited
export LIBHDFS_OPTS="-Xmx5g -Xms1g"
export USE_HDFS=1
if [[ -z "$JAVA_HOME" ]] ;then
export JAVA_HOME="/opt/tiger/jdk/jdk1.8"
fi
if [[ -z "$HADOOP_HOME" ]] ;then
export HADOOP_HOME="/opt/tiger/yarn_deploy/hadoop"
fi
export PATH="$PATH:$HADOOP_HOME/bin/"
export LD_LIBRARY_PATH="$JAVA_HOME/jre/lib/amd64/server:$JAVA_HOME/jre/lib/amd64:$HADOOP_HOME/lib/native"
export INFSEC_HADDOP_ENABLED=1
export CLASSPATH=`hadoop classpath --glob`
doas $SHELL
```

1. include文件夹中并没有env_hdfs.h，需要把它和它include的某些头文件都加进include文件夹中；
2. MakeFIle中没有HDFS的头文件路径，手动添加：

```
CXXFLAGS += -I/opt/tiger/yarn_deploy/hadoop/include
```

1. 公司HDFS的连接方式和默认的不同，修改enf_hdfs.h:

```
hdfsFS connectToPath(const std::string& uri) {
    if (uri.empty()) {
      return nullptr;
    }
    hdfsFS fs = hdfsConnectNewInstance(uri.c_str(), 0);
    return fs;
  }
```

1. 编译指令

```
source setup.sh
make static_lib -j100
```

1. Core dump，原因是添加了两个mmap的配置项，HDFS不支持。
2. open速度慢，原因是配置项max_open_files=-1，改为2之后open速度快了十几倍。

## 本次scan对象：

kv数量在2.5亿左右。

### Scan工具优化过程：

1. 比较朴素的想法是用iter从头开始遍历整个db，速度大概是12500keys/min.
2. 以column family为粒度，并发读进同一个文件，有锁。速度提升了num_of(column family)倍。这里达到了50000keys/min。
3. Lock free，把每个column family读到一个对应的文件，性能没有明显提升。由于临界区不大，如果iter迭代很快的话，应该会有性能提升；然而没有，所以这里我猜测瓶颈在迭代器上。
4. 官网wiki中提到迭代器有readahead功能https://github.com/facebook/rocksdb/wiki/Iterator#read-ahead，涉及到两个配置项：DBOptions.advise_random_on_open = false和ReadOptions.readahead_size，虽然第一段提到只适用于BlockBase格式的SST，我依然尝试了一下。经尝试无效。
5. 在看PlainTable配置项的时候无意间发现ptoptions.full_scan_mode可以关闭索引，关闭后性能提升了一倍左右，以现在的速度要读完2.5亿个kv大概要20~25h。[为什么关闭索引之后性能提升这么大？](https://bytedance.feishu.cn/docs/doccnlZzpX8pKL0f7YxPZk2SGMe) 

![img](https://bytedance.feishu.cn/space/api/box/stream/download/asynccode/?code=MGYzODM0NmIwMDY4NjMzMDk3OWNiMDYzMjI2ZTAxOTdfS2NWVXhwMnE2ZEZlYzlzOGVWR2s3RFoyaFJHc1JUN29fVG9rZW46Ym94Y25JSDhVZlc1WGtLdW1pWG01RFdEcmhjXzE2Mjg1OTU3MzI6MTYyODU5OTMzMl9WNA)

1. 这个服务数据量400G，应该分为20个Column family，现在只有4个。如果提到20个，速度能提升五倍。
2. max_open_file在open之后可以通过setDBOptions接口动态调整为-1，效率提升10%左右。

# Rocksdb on HDFS设计文档

# server侧总体设计

由于scan耗时过长，必须分段，采取类似TCP的ACK问答模式（offset），保证传输有序（不重不丢），有cache用于保存上一次传输的内容，解决了rocksdb::Iterator无法回滚的痛点。openObj是对rocksdb，offset，cache，iters的封装：

```
struct openObj {
  const int step = 5e3; //cache容量，去掉value之后可以加大。
  rocksdb::DBWithTTL *db = nullptr;
  std::vector<rocksdb::Iterator *> iters;
  std::vector<rocksdb::ColumnFamilyHandle *> cfhs;
  std::unordered_map<std::string, rocksdb_hdfs::Value> cache;
  std::mutex cache_lock;
  time_t ts = 0; // 用于关闭过期DB
  int offset = 0;
  std::mutex scan_lock; // 锁定整个scan过程，避免重复scan。
};
```

### 状态机

db有四种状态Opening，Opened，Closed，NeverOpened，用三个集合进行表征：

```
 std::unordered_set<std::string> opening, closed;
  std::unordered_map<std::string, openObj *> opened;
  std::mutex opening_lock, opened_lock, closed_lock;
```

open接口只能在非Opening和非Opened状态打开：

```
if (is_opening(req.hdfs_path)) {
      rsp.status.message = "Opening";
      return -1;
    }
    if (is_opened(req.hdfs_path)) {
      rsp.status.message = "Opened";
      return -1;
    }
    {
      std::lock_guard<std::mutex> lg(opening_lock);
      opening.insert(req.hdfs_path);
    }
```

状态切换发生在open成功后：

```
   if (status.ok()) {
      ...
      { 
        std::lock_guard<std::mutex> lg_(opening_lock);
        std::lock_guard<std::mutex> lg(opened_lock);
        opening.erase(req.hdfs_path);
        opened[req.hdfs_path] = obj;
      }
      ...
      return 0;
    } 
```

scan有两种状态，undergoing和free，用一个mutex保证一时间只能有一个scan在进行：

```
 if (!obj->scan_lock.try_lock()) {
      rsp.status.message =
          "One scan routine is undergoing, please wait until it finish.";
      return -1;

    }
```

### Scan不重不丢

当且仅当client侧offset = server侧offset时，才进行下一段scan；若client侧offset == server侧offset - 步长step，重传cache； 否则什么也不干。server侧offset仅在scan完之后才会增加，client侧offset仅在收到server的rsp后才会增加（有轮询重传机制）。

```
 if (req.begin_offset < obj->offset - obj->step ||
        req.begin_offset > obj->offset) { //超出cache范围
      if (req.begin_offset < obj->offset - obj->step)
        rsp.status.message = "Outdated data, if you want that data, close DB "
                             "and read from start.";
      else
        rsp.status.message = "Offset exceed limit.";
      obj->scan_lock.unlock();
      return -1;
    }
    if (req.begin_offset < obj->offset) {                  //重传cache
      assert(req.begin_offset == obj->offset - obj->step); //这个值是确定的
      rsp.kvs = std::map<std::string, rocksdb_hdfs::Value>(obj->cache.begin(),
                                                           obj->cache.end());
      rsp.status.message = "ok";
      rsp.end_offset = obj->offset;
      obj->scan_lock.unlock();
      return 0;
    }
    assert(req.begin_offset == obj->offset);         //进行下一段scan...
```

### Scan保证读完（检查每个cf的iter的validation和status）

```
    if (!obj->iters_status()) {
      rsp.status.message = "Something wrong with iter (maybe HDFS has "
                           "been closed), close DB and reopen.";
      obj->scan_lock.unlock();
      return -1;
    }
    rsp.status.message = obj->eof() ? "eof, ok" : "ok";
```

### 资源释放（close接口）

```
  ~openObj() {
    if (!iters.empty()) {
      for (auto &iter : iters)
        if (iter)
          delete iter;
      iters.clear();
    }
    if (!cfhs.empty()) {
      for (auto &cfh : cfhs)
        if (cfh)
          delete cfh;
      cfhs.clear();
    }
    if (!cache.empty())
      cache.clear();
    if (db)
      delete db;
  }
delete opened[req.hdfs_path];
opened.erase(req.hdfs_path);
rsp.status.message = "Successfully close DB and release related resourses";
```

## 点查get接口(暂时弃用）

```
//关闭index后scan效率得到提升，但是get会出core
  int get(::rocksdb_hdfs::GetRsp &rsp, const ::rocksdb_hdfs::GetReq &req,
.   ...
    rocksdb::Status s;
    for (const auto &key : req.key) {
      rocksdb_hdfs::Value val;
      for (const auto &cfh : obj->cfhs) {
        s = obj->db->Get(rocksdb::ReadOptions(), cfh, key, &(val.value));
        if (s.ok()) {
          rsp.kvs.emplace(key, val);
          break;
        }
      }
    }
    return 0;
  }
```

server端异步化

# client端总体设计

### 持久化offset

```
    // 持久化kv和offset
    for(const auto & kv: rsp->kvs)
        dumpFile << kv.first << /* '\t' << kv.second.value <<  */std::endl;
    offset = rsp->end_offset;
    OFFSET << offset << std::endl;
```

### [RocksdbOnHdfs Client使用说明](https://bytedance.feishu.cn/docs/doccnkz8Ls0vJ8vNt3RvnivDnWe) 

# 测试

- client侧恶意while(true) open/scan/close，server侧自动屏蔽；
- scan正确性测试（https://byterec.bytedance.net/byterec/index_service/service/839/overview数据量太大，预计读完需要20-25h)

![img](https://bytedance.feishu.cn/space/api/box/stream/download/asynccode/?code=MTI2YjExMDJjZjRiMGM5OTc2MTg1YjYxMjg0YzRmNjFfS09ETG51anVDTUV1Qk52VUM5ZjRLZkhPRFplWFkwZzJfVG9rZW46Ym94Y25yTlBFRnhyRHVlZ1NsNVQ2dDY5ZGpoXzE2Mjg1OTU1NzU6MTYyODU5OTE3NV9WNA)

- scan断点续传

![img](https://bytedance.feishu.cn/space/api/box/stream/download/asynccode/?code=NzBjZDk2M2UxOTE0MWM5ZTRmNjU2ZjhhY2Q4NzJlMjVfajJHSzlSZVRLOFpPRXpaemtTZHhJZVM5TWtHN0N0eGNfVG9rZW46Ym94Y241WUFNQ1poVGFFUlNhQ1c5aUNKakVjXzE2Mjg1OTU1NzU6MTYyODU5OTE3NV9WNA)

- 多client并行测试

![img](https://bytedance.feishu.cn/space/api/box/stream/download/asynccode/?code=ZGJiMDZlZGYwM2NjNzc4ZTk4Y2MxODE4OTU4N2FjMGJfOTREbW93N3dpbFJMTzJHd1VobmJReVRHNVJaQ2cwYVJfVG9rZW46Ym94Y25jNWU2SXREek5NMVpQOWhVWW54WUZoXzE2Mjg1OTU1NzU6MTYyODU5OTE3NV9WNA)

### 