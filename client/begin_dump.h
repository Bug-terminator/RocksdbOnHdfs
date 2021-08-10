#pragma once
#include "cpputil/consul/bridge.h"
#include "cpputil/log/log.h"
#include <set>
#include <string>
#include <vector>
#include "cpputil/pool/thread_pool.h"
int dump_index_service(const std::string &psm, const std::string &hdfs_path,
                       const std::string &dc, const std::string &region,
                       std::vector<std::string> &dump_paths);
std::string get_region();
int get_shard_num(const std::string &consul_psm, int &shard_count,
                  const std::string &dc);
