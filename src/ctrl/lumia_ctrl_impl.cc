// Copyright (c) 2015, Baidu.com, Inc. All Rights Reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "ctrl/lumia_ctrl_impl.h"

#include <unistd.h>
#include <gflags/gflags.h>
#include <boost/function.hpp>
#include <boost/bind.hpp>
#include "logging.h"

DECLARE_string(fsm_conf_path);

namespace baidu {
namespace lumia {

LumiaCtrlImpl::LumiaCtrlImpl():checker_(4){
}

LumiaCtrlImpl::~LumiaCtrlImpl(){
}

void LumiaCtrlImpl::ReportDeadMinion(::google::protobuf::RpcController* controller,
                          const ::baidu::lumia::ReportDeadMinionRequest* request,
                          ::baidu::lumia::ReportDeadMinionResponse* response,
                          ::google::protobuf::Closure* done) {}


bool LumiaCtrlImpl::StartInitMinion(Minion* minion) {
    if (minion == NULL) {
        return false;
    }
}

}
}
