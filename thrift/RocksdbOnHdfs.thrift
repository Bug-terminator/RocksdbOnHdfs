include "idl/base/base.thrift"
namespace cpp rocksdb_hdfs

enum ERRCode{
    OK = 0,
    OPENING = 1,
    FAIL = 2,
}

struct Status{
    1: string message,
    2: ERRCode err_code,
}

struct OpenReq{
    1: string hdfs_path,
    2: string hdfs_host,
    3: i32 cf_num,
    255: optional base.Base Base, // 必须写
}

struct OpenRsp{
    1: Status status,
    255: optional base.BaseResp BaseResp, // 必须写
}

struct OpenStatusReq{
    1: string hdfs_path,
    255: optional base.Base Base, // 必须写
}

struct OpenStatusRsp{
    1: Status status,
    255: optional base.BaseResp BaseResp, // 必须写
}

struct CloseReq{
    1: string hdfs_path,
    255: optional base.Base Base, // 必须写
}

struct CloseRsp{
    1: Status status,
    255: optional base.BaseResp BaseResp, // 必须写
}

struct Value{
    1:binary value,
    2:i32 ts,
}
struct  ScanReq{
   1: string hdfs_path,
   2: i64 begin_offset,
   3: optional i32 size,
   255: optional base.Base Base, // 必须写
}

struct ScanRsp{
    1: map<string, Value> kvs,
    2: Status status,
    3: i64 end_offset,
    255: optional base.BaseResp BaseResp, // 必须写
}

struct GetReq{
    1: list<string> key,
    2: string hdfs_path,
    255: optional base.Base Base, // 必须写
}

struct GetRsp{
    1: map<string,Value> kvs,
    2: Status status,
    255: optional base.BaseResp BaseResp, // 必须写
}

service RocksdbOnHdfs{
    //Open相关
    OpenRsp     open(1:OpenReq req),
    OpenStatusRsp     openStatus(1:OpenStatusReq req),

    //scan相关
    ScanRsp        scan(1:ScanReq req),
    
    //close相关
    CloseRsp close(1:CloseReq req),
    
    //get相关
    GetRsp              get(1:GetReq req),
}
