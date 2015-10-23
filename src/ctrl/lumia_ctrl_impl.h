// Copyright (c) 2015, Baidu.com, Inc. All Rights Reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef BAIDU_LUMIA_ENGINE_LUMIA_IMPL_H
#define BAIDU_LUMIA_ENGINE_LUMIA_IMPL_H
#include <set>
#include "proto/lumia.pb.h"
#include "mutex.h"
#include "thread_pool.h"
#include "ins_sdk.h"
#include "ctrl/minion_ctrl.h"
#include "rpc/rpc_client.h"
#include <boost/multi_index_container.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/hashed_index.hpp>

using ::galaxy::ins::sdk::InsSDK;

namespace baidu {
namespace lumia {

struct MinionIndex {
    std::string id_;
    std::string hostname_;
    std::string ip_;
    Minion* minion_;
    MinionIndex(std::string id, const std::string& hostname, const std::string ip, Minion* minion):id_(id), hostname_(hostname), ip_(ip), minion_(minion){}
};

struct id_tag{};
struct hostname_tag{};
struct ip_tag{};

typedef boost::multi_index_container<
    MinionIndex,
    boost::multi_index::indexed_by<
        boost::multi_index::hashed_unique<boost::multi_index::tag<id_tag>,  BOOST_MULTI_INDEX_MEMBER(MinionIndex , std::string ,id_)>,
        boost::multi_index::hashed_unique<boost::multi_index::tag<hostname_tag>, BOOST_MULTI_INDEX_MEMBER(MinionIndex, std::string, hostname_)>,
        boost::multi_index::hashed_unique<boost::multi_index::tag<ip_tag>, BOOST_MULTI_INDEX_MEMBER(MinionIndex, std::string, ip_)>
    >
> MinionSet;

typedef boost::multi_index::index<MinionSet, id_tag>::type minion_set_id_index_t;

typedef boost::multi_index::index<MinionSet, hostname_tag>::type minion_set_hostname_index_t;

typedef boost::multi_index::index<MinionSet, ip_tag>::type minion_set_ip_index_t;

class LumiaCtrlImpl : public LumiaCtrl {

public:
    LumiaCtrlImpl();
    ~LumiaCtrlImpl();
    void Init();
    // galaxy log checker report dead agent
    void ReportDeadMinion(::google::protobuf::RpcController* controller,
                          const ::baidu::lumia::ReportDeadMinionRequest* request,
                          ::baidu::lumia::ReportDeadMinionResponse* response,
                          ::google::protobuf::Closure* done);

    void GetMinion(::google::protobuf::RpcController* controller,
                   const ::baidu::lumia::GetMinionRequest* request,
                   ::baidu::lumia::GetMinionResponse* response,
                   ::google::protobuf::Closure* done);

    void ImportData(::google::protobuf::RpcController* controller,
                    const ::baidu::lumia::ImportDataRequest* request,
                    ::baidu::lumia::ImportDataResponse* response,
                    ::google::protobuf::Closure* done);
    void Ping(::google::protobuf::RpcController* controller,
              const ::baidu::lumia::PingRequest* request,
              ::baidu::lumia::PingResponse* response,
              ::google::protobuf::Closure* done);

    void OnSessionTimeout();

    void OnLockChange(const std::string& sessionid);
private:
    void HandleDeadReport(const std::string& ip);
    void CheckDeadCallBack(const std::string sessionid, 
                           const std::vector<std::string> success,
                           const std::vector<std::string> fails);
    void RebootCallBack(const std::string sessionid,
                        const std::vector<std::string> success,
                        const std::vector<std::string> fails);

    void HandleInitAgent(const std::vector<std::string> hosts);
    void InitAgentCallBack(const std::string sessionid,
                           const std::vector<std::string> success,
                           const std::vector<std::string> fails);

    bool DoInitAgent(const std::vector<std::string> hosts,
                     const std::string scripts);

    void AcquireLumiaLock();

    void HandleNodeOffline(const std::string& node_addr);

    void ScheduleNextQuery();
    void LaunchQuery();
    void QueryNode(const std::string& node_addr);
    void QueryCallBack(const QueryAgentRequest* request,
                       QueryAgentResponse* response,
                       bool fails,
                       int error,
                       const std::string& node_addr);
private:
    MinionSet minion_set_;
    ::baidu::common::Mutex mutex_;
    ::baidu::common::Mutex timer_mutex_;
    ::baidu::common::ThreadPool workers_;
    ::baidu::common::ThreadPool dead_checkers_;
    MinionCtrl* minion_ctrl_;

    // system check script config
    std::map<std::string, std::string> scripts_;

    // minion under process
    std::set<std::string> under_process_;

    InsSDK* nexus_;

    // 
    std::set<std::string> nodes_;
    std::map<std::string, int64_t>  node_timers_;

    // 
    int64_t query_node_count_;
    ::baidu::galaxy::RpcClient* rpc_client_;
};

}
}
#endif
