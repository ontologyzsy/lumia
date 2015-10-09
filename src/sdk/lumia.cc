// Copyright (c) 2015, Baidu.com, Inc. All Rights Reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "sdk/lumia.h"

#include "rpc/rpc_client.h"
#include "proto/lumia.pb.h"

namespace baidu {
namespace lumia {

class LumiaSdkImpl : public LumiaSdk {
public:
    virtual ~LumiaSdkImpl(){}
    LumiaSdkImpl(const std::string& lumia_addr) {
        rpc_client_ = new ::baidu::galaxy::RpcClient();
        rpc_client_->GetStub(lumia_addr, &lumia_);
    }
    bool ReportDeadMinion(const std::string& ip,
                          const std::string& reason);
private:
    ::baidu::galaxy::RpcClient* rpc_client_;
    LumiaCtrl_Stub* lumia_;
};

bool LumiaSdkImpl::ReportDeadMinion(const std::string& ip,
                                    const std::string& reason) {
    ReportDeadMinionRequest request;
    request.set_ip(ip);
    request.set_reason(reason);
    ReportDeadMinionResponse response;
    bool ok = rpc_client_->SendRequest(lumia_, &LumiaCtrl_Stub::ReportDeadMinion,
                                       &request, &response, 5, 1);
    if (!ok || response.status() != kLumiaOk) {
        return false;
    }
    return true;
}

LumiaSdk* LumiaSdk::ConnectLumia(const std::string& lumia_addr){
    return new LumiaSdkImpl(lumia_addr);
}

}
}
