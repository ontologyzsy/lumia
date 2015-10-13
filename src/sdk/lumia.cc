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
    bool GetMinion(const std::vector<std::string>& ips,
                   const std::vector<std::string>& hostnames,
                   const std::vector<std::string>& ids,
                   std::vector<MinionDesc>* minions);
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

bool LumiaSdkImpl::GetMinion(const std::vector<std::string>& ips,
                             const std::vector<std::string>& hostnames,
                             const std::vector<std::string>& ids,
                             std::vector<MinionDesc>* minions) {
    GetMinionRequest request;
    for (size_t i = 0; i < ips.size(); i++) {
        request.add_ips(ips[i]);
    }
    for (size_t i = 0; i < hostnames.size(); i++) {
        request.add_hostnames(hostnames[i]);
    }
    for (size_t i = 0; i < ids.size(); i++) {
        request.add_ids(ids[i]);
    }
    GetMinionResponse response;
    bool ok = rpc_client_->SendRequest(lumia_, &LumiaCtrl_Stub::GetMinion,
                                       &request, &response, 5, 1);
    if (!ok) {
        return false;
    }
    
    for (int i = 0;  i < response.minions_size(); i++) {
        MinionDesc minion;
        minion.id = response.minions(i).id();
        minion.ip = response.minions(i).ip();
        minion.hostname = response.minions(i).hostname();
        minion.rock_ip = response.minions(i).rock_ip();
        minion.state = MinionState_Name(response.minions(i).state());
        minion.datacenter = response.minions(i).datacenter();
        minion.bandwidth = response.minions(i).bandwidth();

        minion.cpu.count = response.minions(i).cpu().count();
        minion.cpu.clock = response.minions(i).cpu().clock();

        minion.mem.size = response.minions(i).mem().size();
        minion.mem.count = response.minions(i).mem().count();

        minion.disk.size = response.minions(i).disk().size();
        minion.disk.count = response.minions(i).disk().count();
        minion.disk.speed = response.minions(i).disk().speed();

        minion.flash.size = response.minions(i).flash().size();
        minion.flash.count = response.minions(i).flash().count();
        minions->push_back(minion);
    }
    return true;
}

LumiaSdk* LumiaSdk::ConnectLumia(const std::string& lumia_addr){
    return new LumiaSdkImpl(lumia_addr);
}

}
}
