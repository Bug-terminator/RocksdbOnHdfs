#include "begin_dump.h"
#include "clientImpl.h"
#include <cassert>
#include <gflags/gflags.h>
#include <gtest/gtest.h>
#include <string>
#include "cpputil/pool/thread_pool.h"
extern const char kSvnInfo[];
extern const char kBuildTime[];
DEFINE_string(index_service_psm, "data.index->aweme_music_invert", "");
DEFINE_string(psm, "data.index.rocksdb_on_hdfs.service.lf", "固定值，不用修改");
DEFINE_string(host, "hdfs://haruna", "已废弃");
DEFINE_string(path, "/data/arch/index/hl/2021-07-25/"
                    "data.index.service.blacklist-1627200000-1627200106_"
                    "Uniform_0/",
              "");
DEFINE_string(dumpPath, "0", "数据保存的文件名");
DEFINE_string(dc, "", "");
DEFINE_int32(cf, 12, "服务column family个数");
DEFINE_int32(shard_num, "1", "服务shard数");
DEFINE_bool(do_open, false, "");
DEFINE_bool(do_close, false, "");
DEFINE_bool(do_scan, false, "");
DEFINE_bool(do_get, false, "");
DEFINE_int64(offset, 0, "从哪里开始scan，默认从头开始");
std::string get_region();

int main(int argc, char *argv[]) {
  gflags::SetVersionString(std::string(kSvnInfo) + "  " +
                           std::string(kBuildTime));
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  // 此处必须初始化, 如果没有配置文件, 则传空字符串
  cpputil::log::init("conf/log4j.properties");
  cpputil::program::Conf conf("conf/rocksdb_hdfs.conf");
  //   cpputil::metrics2::Metrics::init(conf);
  // ArchonContext的初始化是必须的
  archon::common::ArchonContext::init(conf);

  auto region = get_region();
  std::cerr << region << std::endl;
  std::cerr << "Dumping" << std::endl;
  std::vector<std::string> dump_paths;
  int ret = dump_index_service(FLAGS_index_service_psm, FLAGS_path, FLAGS_dc,
                               region, dump_paths);
  if (ret != 0) {
    std::cerr << "fail " << std::endl;
    return -1;
  }
  std::string hdfs_host = "hdfs://haruna";
  if (region == "cn") {
    hdfs_host = "hdfs://haruna";
  } else if (region == "sg") {
    hdfs_host = "hdfs://harunasg";
  } else if (region == "va") {
    hdfs_host = "hdfs://harunava";
  }
  cpputil::pool::ThreadPool tp(20);
  std::vector<std::function<void()>> func_list;
  for(const auto &dpp: dump_paths)
    std::cerr << dpp << std::endl;
  // std::vector<std::future<void()>> future_list;
  for (int i = 0; i < dump_paths.size(); ++i) {
    auto func = [=]() {
      auto cli =
          std::make_shared<Client>(dump_paths[i], hdfs_host, i, FLAGS_cf,
                                   FLAGS_psm, FLAGS_dumpPath, FLAGS_offset);

      if (FLAGS_do_open)
        cli->open();

      if (FLAGS_do_scan)
        cli->scan();

      if (FLAGS_do_close)
        cli->close();
    };
    func_list.emplace_back(std::move(func));
  }
  tp.concurrent_run(func_list);

  // if (FLAGS_do_get)
  //   cli->get();
  return 0;
}

std::string get_region() {
  FILE *pp =
      popen("/bin/bash /opt/tiger/consul_deploy/bin/determine_dc.sh", "r");
  if (!pp)
    throw std::runtime_error("error");
  char dc_local[1024];
  if (fgets(dc_local, sizeof(dc_local), pp) != NULL)
    std::cout << dc_local << std::endl;
  pclose(pp);
  std::string dc_local_str(dc_local);
  if (dc_local_str == "lf" || dc_local_str == "hl" || dc_local_str == "lq") {
    return "cn";
  } else if (dc_local == "alisg" || dc_local == "sg1") {
    return "sg";
  } else if (dc_local == "va" || dc_local == "maliva") {
    return "va";
  }
  return "cn";
}
