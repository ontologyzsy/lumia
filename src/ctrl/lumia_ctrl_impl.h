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

namespace baidu {
namespace lumia {

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
    void CheckDeadCallBack(const std::string& sessionid, 
                           const std::vector<std::string>& success,
                           const std::vector<std::string>& fails);
private:
    // readonly ip -> minion pairs
    std::map<std::string, Minion> minions_;
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
