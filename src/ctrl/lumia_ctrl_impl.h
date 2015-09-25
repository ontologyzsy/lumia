// Copyright (c) 2015, Baidu.com, Inc. All Rights Reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef BAIDU_LUMIA_ENGINE_LUMIA_IMPL_H
#define BAIDU_LUMIA_ENGINE_LUMIA_IMPL_H
#include "proto/lumia.pb.h"
#include "fsm/fsm.h"
#include "mutex.h"
#include "thread_pool.h"

namespace baidu {
namespace lumia {

class LumiaCtrlImpl : public LumiaCtrl {

public:
    LumiaCtrlImpl();
    ~LumiaCtrlImpl();

    // galaxy log checker report dead agent
    void ReportDeadMinion(::google::protobuf::RpcController* controller,
                          const ::baidu::lumia::ReportDeadMinionRequest* request,
                          ::baidu::lumia::ReportDeadMinionResponse* response,
                          ::google::protobuf::Closure* done);


private:
    bool StartInitMinion(Minion* minion);
private:
    // ip -> minion pairs
    std::map<std::string, Minion> minions_;
    ::baidu::common::Mutex mutex_;
    ::baidu::common::ThreadPool checker_;
};

}
}
#endif
