#include "clientImpl.h"
int Client::open() {
  auto req = std::make_shared<rocksdb_hdfs::OpenReq>();
  auto res_state =
      std::make_shared<archon::client::ResponseState<rocksdb_hdfs::OpenRsp>>();
  req->hdfs_path = path;
  req->hdfs_host = host;
  req->cf_num = cf;
  auto ret = client->open(req, res_state, ctx, opt);
  auto res = std::move(ret).get();
  std::cerr << "open ret " << res << std::endl;
  auto rsp = res_state->get_response();
  std::cerr << rsp->status.message << std::endl;
}

int Client::scan() {
  while (true) {
    auto req = std::make_shared<rocksdb_hdfs::ScanReq>();
    auto res_state = std::make_shared<
        archon::client::ResponseState<rocksdb_hdfs::ScanRsp>>();
    req->hdfs_path = path;
    req->begin_offset = offset;
    auto ret = client->scan(req, res_state, ctx, opt);
    auto res = std::move(ret).get();
    std::cerr << "scan ret " << res << std::endl;
    auto rsp = res_state->get_response();
    std::cerr << rsp->status.message << std::endl;
    if (res == -1)
      return res;
    if (res == -80103) // timeout,server侧可能在open sst
    {
      sleep(60);
      continue;
    }
    if(res){ //network error
      sleep(3);
      continue;
    }

    std::cerr << "scan kvs " << rsp->kvs.size() << std::endl;
    // 持久化kv和offset
    for (const auto &kv : rsp->kvs)
      dumpFile << kv.first << /* '\t' << kv.second.value <<  */ std::endl;
    offset = rsp->end_offset;
    OFFSET << offset << std::endl;
    if (!res && rsp->status.message == std::string("eof, ok"))
      return 0;
  }
  return 0;
}

int Client::close() {
  auto req = std::make_shared<rocksdb_hdfs::CloseReq>();
  auto res_state =
      std::make_shared<archon::client::ResponseState<rocksdb_hdfs::CloseRsp>>();
  req->hdfs_path = path;
  auto ret = client->close(req, res_state, ctx, opt);
  auto res = std::move(ret).get();
  std::cerr << "close ret " << res << std::endl;
  auto rsp = res_state->get_response();
  std::cerr << rsp->status.message << std::endl;
  return res;
}

int Client::get(const std::vector<std::string> &keys,
                std::map<std::string, rocksdb_hdfs::Value> &kvs) {
  auto get_req = std::make_shared<rocksdb_hdfs::GetReq>();
  auto get_res_state =
      std::make_shared<archon::client::ResponseState<rocksdb_hdfs::GetRsp>>();
  get_req->hdfs_path = path;
  auto ret = client->get(get_req, get_res_state, ctx, opt);
  auto res = std::move(ret).get();
  std::cerr << "get ret " << res << std::endl;
  auto get_rsp = get_res_state->get_response();
  std::cerr << get_rsp->status.message << std::endl;
  std::cerr << "get kvs " << get_rsp->kvs.size() << std::endl;
  return res;
}

int Client::async_open() {
  auto req = std::make_shared<rocksdb_hdfs::OpenReq>();
  auto res_state =
      std::make_shared<archon::client::ResponseState<rocksdb_hdfs::OpenRsp>>();
  req->hdfs_path = path;
  req->hdfs_host = host;
  req->cf_num = cf;
  auto ret = client->open(req, res_state, ctx, opt);
  auto res = std::move(ret).get();
  std::cerr << "open ret " << res <<'\n' << res_state->get_response()->status.message << std::endl;
  return res;
}

int Client::openStatus(std::string &msg) {
  auto req = std::make_shared<rocksdb_hdfs::OpenStatusReq>();
  auto res_state = std::make_shared<
      archon::client::ResponseState<rocksdb_hdfs::OpenStatusRsp>>();
  req->hdfs_path = path;
  auto ret = client->openStatus(req, res_state, ctx, opt);
  auto res = std::move(ret).get();
  std::cerr << "Status ret " << res << std::endl;
  auto rsp = res_state->get_response();
  msg = rsp->status.message;
  sleep(1);
  std::cerr << msg << std::endl;
  return res;
}
