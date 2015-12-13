// Copyright (c) 2015, Baidu.com, Inc. All Rights Reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "sdk/lumia.h"

#include <gflags/gflags.h>
#include <fstream>
#include <sstream>
#include <dirent.h>
#include "rpc/rpc_client.h"
#include "proto/lumia.pb.h"
#include "meta_query.h"

DECLARE_int32(galaxy_cpu_use_percent);
DECLARE_int32(galaxy_mem_use_percent);
DECLARE_int64(galaxy_disk_usage);
DECLARE_int64(galaxy_bandwidth_usage);

namespace baidu {
namespace lumia {

class LumiaSdkImpl : public LumiaSdk {
public:
    LumiaSdkImpl(){}
    ~LumiaSdkImpl(){}
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
    bool ImportData(const std::string& dict_path,
                    const std::string& init_scripts_dir,
                    const std::string& rm_scripts_dir);
    bool ExportData(const std::string& dict_path);
    bool ExecMinion(const std::string& dict_path,
                    const std::string& scripts_dir);
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
        bool mount_ok = true;
        bool device_ok = true;
        const MinionStatus& pb_minion = response.minions(i).status();
        for (int i = 0; i < pb_minion.devices_size(); i++) {
            if (pb_minion.devices(i).healthy()) {
                continue;
            }
            device_ok = false;
            break;
        }
        for (int i = 0;  i < pb_minion.devices_size(); i++) {
            if (pb_minion.mounts(i).mounted()) {
                continue;
            }
            mount_ok = false;
            break;
        }
        minion.mount_ok = mount_ok;
        minion.device_ok = device_ok;
        minions->push_back(minion);
    }
    return true;
}

bool LumiaSdkImpl::DelMinion(const std::string& dict_path) {
    DelMinionRequest request;
    if (!dict_path.empty()) {
        std::ifstream minion_list;
        minion_list.open(dict_path.c_str());
        std::string minion_name;
        while (getline(minion_list, minion_name)) {
            request.add_hostnames(minion_name);
        }
    }
    DelMinionResponse response;
    bool ok = rpc_client_->SendRequest(lumia_, &LumiaCtrl_Stub::DelMinion, &request, &response, 5, 1);
    if (!ok || response.status() != kLumiaOk) {
        return false;
    }
    return true;
}
#if 0
bool LumiaSdkImpl::ImportData(const std::string& dict_path,
                              const std::string& scripts_dir) {
    ImportDataRequest request;
    if (!dict_path.empty()) {
        std::ifstream ip_minions_is;
        ip_minions_is.open(dict_path.c_str(), std::ifstream::binary);
        char buffer[1024];
        std::stringstream ss;
        while(ip_minions_is.good()) {
            ip_minions_is.read(buffer, 1024);
            int32_t read_count = ip_minions_is.gcount();
            ss.write(buffer, read_count);
        }
        request.mutable_minions()->ParseFromString(ss.str());
    }
    if (!scripts_dir.empty()) {
        DIR *dir = opendir(scripts_dir.c_str());
        if (dir == NULL) {
            LOG(WARNING, "fail to open folder %s", scripts_dir.c_str());
            return false;
        }
        struct dirent *dirp;
        char buffer[1024];
        while ((dirp = readdir(dir)) != NULL) {
            std::string filename(dirp->d_name);
            if (filename.compare(".") == 0 || filename.compare("..") == 0) {
                continue;
            }
            std::string full_path = scripts_dir + "/" + filename;
            std::ifstream script;
            script.open(full_path.c_str(), std::ifstream::in);
            std::stringstream ss;
            while (script.good()) {
                script.read(buffer, 1024);
                int32_t count = script.gcount();
                ss.write(buffer, count);
            }
            SystemScript* sc = request.mutable_scripts()->Add();
            sc->set_name(filename);
            sc->set_content(ss.str());
        } 
    }
   
    ImportDataResponse response;
    bool ok = rpc_client_->SendRequest(lumia_, &LumiaCtrl_Stub::ImportData,
                                       &request, &response, 100, 1);
    if (!ok || response.status() != kLumiaOk) {
        return false;
    }
    return true;
}
#endif

LumiaSdk::LumiaSdk(){}
LumiaSdk::~LumiaSdk(){}

LumiaSdk* LumiaSdk::ConnectLumia(const std::string& lumia_addr){
    return new LumiaSdkImpl(lumia_addr);
}

bool LumiaSdkImpl::ImportData(const std::string& dict_path,
                              const std::string& init_scripts_dir,
                              const std::string& rm_scripts_dir) {
    if (dict_path.empty()) {
        return false;
    }
    std::ifstream hostnames_is;
    hostnames_is.open(dict_path.c_str());
    std::string minion_name;
    metaquery::EntityRequest req;
    req.type =  metaquery::EntityType::HOST;
    std::vector<std::string> hosts;
    while (getline(hostnames_is, minion_name)) {
        hosts.push_back(minion_name);
    }
    req.__set_insts(hosts);
    
    std::vector<std::string> fields;
    fields.push_back("cpuFrequency");
    fields.push_back("cpuLogicalCores");
    fields.push_back("diskTotal");
    fields.push_back("id");
    fields.push_back("memTotal");
    fields.push_back("netIdc");
    fields.push_back("netIpIn1");
    req.__set_fields(fields);
    req.__set_limit(5);
    req.__set_offset(1);

    uint32_t timeout = 300000;
    metaquery::EntityResponse res;
    int ret = get_entity(req, timeout, &res);
    if (!ret == metaquery::ReturnCode::SUCCESS) {
        LOG(WARNING, "metaquery return fail code : %d", ret);
        return ret;
    }
    std::vector<metaquery::InstType> inst = res.data;
    ImportDataRequest init_req;
    Minions* minions = init_req.mutable_minions();
    for (size_t i = 0; i < inst.size(); i++) {
        Minion* minion = minions->add_minions();
        minion->set_hostname(inst[i].name);
        std::map<std::string, std::string> values = inst[i].values;
        for (std::map<std::string, std::string>::iterator value_it = values.begin();
                value_it != values.end(); value_it++) {
            if (value_it->first == "cpuFrequency") {
                minion->mutable_cpu()->set_clock(value_it->second);
            } else if (value_it->first == "cpuLogicalCores") {
                std::stringstream cores;
                int64_t count;
                cores << value_it->second;
                cores >> count;
                minion->mutable_cpu()->set_count(count*FLAGS_galaxy_cpu_use_percent/100);
            } else if (value_it->first == "diskTotal") {
                int size = 0;
                if (FLAGS_galaxy_disk_usage == 0) {
                    std::stringstream disksize;
                    disksize << value_it->second;
                    disksize >> size;
                } else {
                    size = FLAGS_galaxy_disk_usage;
                }
                minion->mutable_disk()->set_size(size);
            } else if (value_it->first == "id") {
                minion->set_id(value_it->second);
            } else if (value_it->first == "memTotal") {
                std::stringstream mem_size;
                int64_t size;
                mem_size << value_it->second;
                mem_size >> size;
                minion->mutable_mem()->set_size(size*FLAGS_galaxy_mem_use_percent/100);
            } else if (value_it->first == "netIdc") {
                minion->set_rock_ip(value_it->second);
            } else if (value_it->first == "netIpIn1") {
                minion->set_ip(value_it->second);
            }
            if (FLAGS_galaxy_bandwidth_usage != 0) {
                minion->set_bandwidth(FLAGS_galaxy_bandwidth_usage);
            }
        }
    }
    
    if (!scripts_dir.empty()) {
        DIR *dir = opendir(scripts_dir.c_str());
        if (dir == NULL) {
            LOG(WARNING, "fail to open folder %s", scripts_dir.c_str());
            return false;
        }
        struct dirent *dirp;
        char buffer[1024];
        while ((dirp = readdir(dir)) != NULL) {
            std::string filename(dirp->d_name);
            if (filename.compare(".") == 0 || filename.compare("..") == 0) {
                continue;
            }
            std::string full_path = scripts_dir + "/" + filename;
            std::ifstream script;
            script.open(full_path.c_str(), std::ifstream::in);
            std::stringstream ss;
            while (script.good()) {
                script.read(buffer, 1024);
                int32_t count = script.gcount();
                ss.write(buffer, count);
            }
            SystemScript* sc = init_req.mutable_scripts()->Add();
            sc->set_name(filename);
            sc->set_content(ss.str());
        }
    }

    ImportDataResponse init_resp;
    bool ok = rpc_client_->SendRequest(lumia_, &LumiaCtrl_Stub::ImportData,
                                       &init_req, &init_resp, 100, 1);
    if (!ok || init_resp.status() != kLumiaOk) {
        return false;
    }
    return true;
}

bool LumiaSdkImpl::ExportData(const std::string& dict_path) {
    ExportDataRequest request;
    if (dict_path.empty()) {
        return false;
    }
    std::ifstream ip_minions_is;
    ip_minions_is.open(dict_path.c_str());
    std::string minion_name;
    while (getline(ip_minions_is, minion_name)) {
        request.add_hostnames(minion_name);
    }
    ExportDataResponse response;
    bool ok = rpc_client_->SendRequest(lumia_, &LumiaCtrl_Stub::ExportData,
                                       &request, &response, 100, 1);
    if (!ok || response.status() != kLumiaOk) {
        return false;
    }
    return true;
}

bool ExecMinion(const std::string& dict_path,
                const std::string& script_name) {
    ExecMinionRequest request;
    if (dict_path.empty() || scripts_dir.empty) {
        return false;
    }
    std::ifstream minions_is;
    minions_is.open(dict_path.c_str());
    std::string host;
    while (getline(minions_is, host)) {
        request.add_hostnames(host);
    }
    request.set_script_name(script_name);

    ExecMinionResponse response;
    bool ok = rpc_client_->SendRequest(lumia_, &LumiaCtrl_Stub::ExecMinion,
                                       &request, &response, 100, 1);
    if (!ok || response.status() != kLumiaOk) {
        return false;
    }
    return true;
}

}
}
