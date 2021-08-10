#include "idl/RocksdbOnHdfs/RocksdbOnHdfs.h"
#include "idl/RocksdbOnHdfs/gen-archon/RocksdbOnHdfsClient.h"
#include "idl/RocksdbOnHdfs/gen-archon/details/RocksdbOnHdfsClient_async_impl.h"
#include "idl/RocksdbOnHdfs/gen-archon/details/RocksdbOnHdfsClient_async_opt_framed_impl.h"
#include "idl/RocksdbOnHdfs/gen-archon/details/RocksdbOnHdfsClient_mesh_impl.h"
#include <atomic>
#include <cassert>
#include <fstream>
#include <gflags/gflags.h>
#include <gtest/gtest.h>
#include <string>
class Client {
public:
  int open();
  int scan(); //全量scan，直到eof或者出错为止，或者手动中断（关闭client）。
  int close();
  int get(const std::vector<std::string> &keys,
          std::map<std::string, rocksdb_hdfs::Value> &kvs);
  int async_open();
  int openStatus(std::string &msg);
  Client(std::string hdfs_path, std::string hdfs_host, int shard, int cf_num,
         std::string psm, std::string dumpPath = "dumpFile",
         int64_t ofst = 0) // offset默认初始化为0，支持断点续传
      : path(hdfs_path), host(hdfs_host), cf(cf_num), dump_path(dumpPath),
        offset(ofst) {
    archon::common::ServiceMeta meta(psm);
    client =
        rocksdb_hdfs::archon::RocksdbOnHdfsClient::Builder(
            "rocksdb_on_hdfs_service", meta, archon::client::ClientMode::Async)
            .transport_type(archon::transport::TransportType::Framed)
            .loadbalance_type(archon::lb::LoadBalanceType::ConsistentHash)
            .build();
    //因为scan耗时过长，必须调大archon的timeout
    opt.set_timeout_options(
        archon::common::RequestOptions::TimeoutOptions(10000, 10000, 30));
    opt.set_seed_options(archon::common::RequestOptions::SeedOptions(path));
    OFFSET.open(dump_path+"OFFSET_shard" +std::to_string(shard) , std::ios::out | std::ios::app);
    dumpFile.open(dump_path+"shard"+std::to_string(shard), std::ios::binary | std::ios::out | std::ios::app);
    OFFSET << path << '\t' << host << '\t' << psm << '\t' << time(nullptr)
           << std::endl;
  }
  ~Client() {
    OFFSET.close();
    dumpFile.close();
  }

private:
  std::shared_ptr<rocksdb_hdfs::archon::RocksdbOnHdfsClient> client;
  std::string path, host, dump_path;
  int cf;
  int64_t offset;
  archon::common::RequestContext ctx;
  archon::common::RequestOptions opt;
  std::fstream OFFSET, dumpFile;
};
