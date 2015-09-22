// Copyright (c) 2015, Baidu.com, Inc. All Rights Reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "ctrl/lumia_ctrl_impl.h"

#include <gflags/gflags.h>
#include "fsm/fsm.h"

DECLARE_string(fsm_conf_path);

namespace baidu {
namespace lumia {

LumiaCtrlImpl::LumiaCtrlImpl(){
    fsm_ = new FSM(FLAGS_fsm_conf_path);
    fsm_->Init();
}
LumiaCtrlImpl::~LumiaCtrlImpl(){
    delete fsm_;
}
void LumiaCtrlImpl::ReportDeadMinion(::google::protobuf::RpcController* controller,
                          const ::baidu::lumia::ReportDeadMinionRequest* request,
                          ::baidu::lumia::ReportDeadMinionResponse* response,
                          ::google::protobuf::Closure* done) {}


}
}