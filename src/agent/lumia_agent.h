// Copyright (c) 2015, Baidu.com, Inc. All Rights Reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "proto/agent.pb.h"
#include <string>
#include <vector>
#include <map>
#include "mutex.h"
#include "thread_pool.h"
#include "rpc/rpc_client.h"

namespace baidu {
namespace lumia {

struct MountInfo {
    std::string device;
    std::string mount_point;
    std::string type;
};

typedef std::map<std::string, MountInfo> MountContainer;

class LumiaAgentImpl : public LumiaAgent {

public:
    LumiaAgentImpl();
    ~LumiaAgentImpl();
    void Query(::google::protobuf::RpcController* controller,
               const ::baidu::lumia::QueryAgentRequest* request,
               ::baidu::lumia::QueryAgentResponse* response,
               ::google::protobuf::Closure* done);
    bool Init();
private:
    void DoCheck();
    bool CheckDevice(const std::string& device, bool* ok);
    bool ScanDevice(std::vector<std::string>& devices);
    bool CheckMounts(bool* all_mounted, MinionStatus& status);
    bool ParseScanDevice(const std::string& output,
                         std::vector<std::string>& devices);
    bool SyncExec(const std::string& cmd, 
                  std::stringstream& output,
                  int* exit_code);
    bool ReadFile(const std::string& path,
                  std::stringstream& content);
    bool ParseTab(const std::string& content,
                  MountContainer& container);
    void KeepAlive();
    std::string GetHostName();
private:
    baidu::common::Mutex mutex_;
    std::string smartctl_;
    std::vector<std::string > devices_;
    MinionStatus minion_status_;
    baidu::common::ThreadPool pool_;
    std::string ctrl_addr_;
    ::baidu::galaxy::RpcClient* rpc_client_;
};

}
}
