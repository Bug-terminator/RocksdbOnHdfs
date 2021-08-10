#include "begin_dump.h"
#include <idl/index_service/gen-archon/IndexServiceClient.h>
#include <random>
#include <stdio.h>
#include <sys/time.h>
int get_shard_num(const std::string &consul_psm, int &shard_count,
                  const std::string &dc) {
  std::set<std::string> shard_set;
  std::vector<cpputil::consul::ServiceEndpoint> rawEndpoints =
      cpputil::consul::lookup_name(
          dc.empty() ? consul_psm : (consul_psm + ".service." + dc), 300, 300);
  if (rawEndpoints.size() == 0) {
    ERROR("no ip found or exception");
    return -1;
  }
  shard_count = 0;
  for (auto &rawEndpoint : rawEndpoints) {
    auto iter = rawEndpoint.tags.find("cluster");
    if (iter != rawEndpoint.tags.end()) {
      shard_set.insert(iter->second);
    }
  }
  shard_count = shard_set.size();
  for (auto &rawEndpoint : rawEndpoints) {
    auto iter = rawEndpoint.tags.find("cluster_total_num");
    if (iter != rawEndpoint.tags.end()) {
      shard_count = (std::stoi(iter->second));
    }
  }
  for (auto &rawEndpoint : rawEndpoints) {
    auto iter = rawEndpoint.tags.find("shard_num");
    if (iter != rawEndpoint.tags.end()) {
      shard_count = (std::stoi(iter->second));
    }
  }
  DEBUG("index_service shard_num = %d", shard_count);
  return 0;
}

//可以跨机房，不能跨region
int dump_index_service(const std::string &psm, const std::string &hdfs_path,
                       const std::string &dc, const std::string &region,
                       std::vector<std::string> &dump_paths) {
  std::string haruna_;
  if (hdfs_path.back() != '/')
    hdfs_path.push_back("/");
  int shard_num = 0;
  if (get_shard_num(psm, shard_num, dc))
    return -1;
  else
    std::cerr << "shard_num " << shard_num << std::endl;

  std::vector<std::function<int()>> func_list;
  cpputil::pool::ThreadPool tp(20);
  for (int i = 0; i < shard_num; i++) {
    auto func = [=, &dump_paths]() -> int {
      std::string hdfs_path_inner =
          "hdfs:" + hdfs_path + "shard" + std::to_string(i) + "/";
      dump_paths.emplace_back(hdfs_path + "shard" + std::to_string(i) + "/");
      if (region == "cn") {
        haruna_ = "";
      } else if (region == "sg") {
        haruna_ = ":harunasg";
      } else if (region == "va") {
        haruna_ = ":harunava";
      }
      thread_local std::mt19937 gen((std::random_device())());
      struct timeval tv = {0, 0};
      gettimeofday(&tv, nullptr);
      cpputil::program::Conf conf("");
      archon::common::ArchonContext::init(conf);

      ::archon::common::ServiceMeta service_meta(psm, "default", dc,
                                                 std::to_string(i));
      auto client =
          index_service::archon::IndexServiceClient::Builder(
              "test", service_meta, ::archon::client::ClientMode::Async)
              .transport_type(::archon::transport::TransportType::Framed)
              .loadbalance_type(::archon::lb::LoadBalanceType::ConsistentHash)
              .client_type(::archon::client::ClientType::MultiDC)
              .build(); // dc
      auto req = std::make_shared<index_service::OpReq>();
      req->command = 11;
      req->set_kargs(std::vector<std::string>{
          hdfs_path_inner, std::to_string(tv.tv_sec) + "version" + haruna_});
      ::archon::common::RequestContext ctx;
      ::archon::common::RequestOptions options;
      options.set_seed_options(
          archon::common::RequestOptions::SeedOptions(gen(), true));
      auto res_state = std::make_shared<
          ::archon::client::ResponseState<index_service::OpRsp>>();
      std::cerr << "Dumping shard " << i << std::endl;
      auto ret = client->op(req, res_state, ctx, options);
      int result_code = std::move(ret).get();
      if (result_code == 0) {
        auto rsp = res_state->get_response();
        std::cerr << "status = " << rsp->status << std::endl;
      }
      req->command = 12;
      std::string res_status;
      while (true) {
        auto ret = client->op(req, res_state, ctx, options);
        int result_code = std::move(ret).get();
        if (result_code == 0) {
          auto rsp = res_state->get_response();
          res_status = rsp->status;
        //   std::cerr << " status = " << res_status << std::endl;
          if (res_status != "not ready") {
            break;
          }
        } else {
          // rpc通信失败
        }

        std::this_thread::sleep_for(std::chrono::seconds(1));
      }

      std::string ok{"ok"};
      auto res = std::mismatch(ok.begin(), ok.end(), res_status.begin());
      if (res.first != ok.end()) // ok 开头
        return -1;
      return 0;
    };
    func_list.emplace_back(std::move(func));
  }
  tp.concurrent_run(func_list);
  return 0;
}
