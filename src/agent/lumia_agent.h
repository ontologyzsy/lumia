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

struct ResourceStatistics {
    // cpu
    long cpu_user_time; 
    long cpu_nice_time;
    long cpu_system_time;
    long cpu_idle_time;
    long cpu_iowait_time;
    long cpu_irq_time;
    long cpu_softirq_time;
    long cpu_stealstolen;
    long cpu_guest;

    long cpu_cores;

    long memory_rss_in_bytes;
    ResourceStatistics() :
        cpu_user_time(0),
        cpu_nice_time(0),
        cpu_system_time(0),
        cpu_idle_time(0),
        cpu_iowait_time(0),
        cpu_irq_time(0),
        cpu_softirq_time(0),
        cpu_stealstolen(0),
        cpu_guest(0),
        cpu_cores(0),
        memory_rss_in_bytes(0) {
    }
};

struct SysStat {
    ResourceStatistics last_stat_;
    ResourceStatistics cur_stat_;
    double cpu_used_;
    double mem_used_;
    uint64_t collect_times_;
    SysStat():last_stat_(),
              cur_stat_(),
              cpu_used_(0.0),
              mem_used_(0.0){}
    ~SysStat(){
    }
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
#if 0
    void InitGalaxyEnv(::google::protobuf::RpcController* controller,
                       const ::baidu::lumia::InitGalaxyEnvRequest* request,
                       ::baidu::lumia::InitGalaxyEnvResponse* response,
                       ::google::protobuf::Closure* done);
    void RemoveGalaxyEnv(::google::protobuf::RpcController* controller,
                         const ::baidu::lumia::RemoveGalaxyEnvRequest* request,
                         ::baidu::lumia::RemoveGalaxyEnvResponse* response,
                         ::google::protobuf::Closure* done);
#endif
    void Exec(::google::protobuf::RpcController* controller,
              const ::baidu::lumia::ExecRequest* request,
              ::baidu::lumia::ExecResponse* response,
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

    void CollectSysStat();
    bool GetGlobalCpuStat(ResourceStatistics* statistics);
    std::string GetHostName();
private:
    baidu::common::Mutex mutex_;
    std::string smartctl_;
    std::vector<std::string > devices_;
    MinionStatus minion_status_;
    baidu::common::ThreadPool pool_;
    baidu::common::ThreadPool stat_pool_;
    std::string ctrl_addr_;
    ::baidu::galaxy::RpcClient* rpc_client_;
    SysStat* stat_;
};

}
}
