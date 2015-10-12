// Copyright (c) 2015, Baidu.com, Inc. All Rights Reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef BAIDU_LUMIA_ENGINE_LUMIA_IMPL_H
#define BAIDU_LUMIA_ENGINE_LUMIA_IMPL_H
#include <set>
#include "proto/lumia.pb.h"
#include "mutex.h"
#include "thread_pool.h"
#include "ctrl/minion_ctrl.h"
#include <boost/multi_index_container.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/hashed_index.hpp>

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

    bool LoadMinion(const std::string& path);
    bool LoadScripts(const std::string& folder);
    // galaxy log checker report dead agent
    void ReportDeadMinion(::google::protobuf::RpcController* controller,
                          const ::baidu::lumia::ReportDeadMinionRequest* request,
                          ::baidu::lumia::ReportDeadMinionResponse* response,
                          ::google::protobuf::Closure* done);


private:
    void HandleDeadReport(const std::string& ip);
    void CheckDeadCallBack(const std::string sessionid, 
                           const std::vector<std::string> success,
                           const std::vector<std::string> fails);
    void RebootCallBack(const std::string sessionid,
                        const std::vector<std::string> sucesss,
                        const std::vector<std::string> fails);
private:
    MinionSet minion_set_;
    ::baidu::common::Mutex mutex_;
    ::baidu::common::ThreadPool checker_;
    MinionCtrl* minion_ctrl_;

    // system check script config
    std::map<std::string, std::string> scripts_;

    // minion under process
    std::set<std::string> under_process_;

};

}
}
#endif
