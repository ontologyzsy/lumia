// Copyright (c) 2015, Baidu.com, Inc. All Rights Reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "ctrl/lumia_ctrl_impl.h"

#include <boost/algorithm/string/join.hpp>
#include <unistd.h>
#include <gflags/gflags.h>
#include <boost/function.hpp>
#include <boost/bind.hpp>
#include "logging.h"
#include <fstream>
#include <sstream>
#include <dirent.h>

DECLARE_string(rms_api_http_host);
DECLARE_string(ccs_api_http_host);

namespace baidu {
namespace lumia {

const static int64_t MEM_128G = 128 * 1024 * 1024 * 1024;
const static int64_t MEM_64G = 64 * 1024 * 1024 * 1024;

LumiaCtrlImpl::LumiaCtrlImpl():workers_(4){
    minion_ctrl_ = new MinionCtrl(FLAGS_ccs_api_http_host,
                                  FLAGS_rms_api_http_host);
}

LumiaCtrlImpl::~LumiaCtrlImpl(){
    delete minion_ctrl_;
}

void LumiaCtrlImpl::ReportDeadMinion(::google::protobuf::RpcController* controller,
                          const ::baidu::lumia::ReportDeadMinionRequest* request,
                          ::baidu::lumia::ReportDeadMinionResponse* response,
                          ::google::protobuf::Closure* done) {
    MutexLock lock(&mutex_);
    LOG(INFO, "report dead minion %s for %s", request->ip().c_str(), request->reason().c_str());
    const minion_set_ip_index_t& index = boost::multi_index::get<ip_tag>(minion_set_);
    minion_set_ip_index_t::const_iterator i_it = index.find(request->ip());

    if (i_it == index.end()) {
        LOG(WARNING, "minion with ip %s is not found", request->ip().c_str());
        response->set_status(kLumiaMinionNotFound);
        done->Run();
        return;
    }

    if (i_it->minion_->state() != kMinionAlive) {
        LOG(WARNING, "minion with state %s does not accept dead check", MinionState_Name(i_it->minion_->state()).c_str());
        response->set_status(kLumiaMinionInProcess);
        done->Run();
        return;
    }
    std::map<std::string, std::string>::iterator sc_it = scripts_.find("minion-dead-check.sh");
    if (sc_it == scripts_.end()) {
        LOG(WARNING, "minion-dead-check.sh is not found");
        response->set_status(kLumiaScriptNotFound);
        done->Run();
        return;
    }
    i_it->minion_->set_state(kMinionDeadChecking);
    workers_.AddTask(boost::bind(&LumiaCtrlImpl::HandleDeadReport, this, request->ip()));
    response->set_status(kLumiaOk);
    done->Run();
}

void LumiaCtrlImpl::HandleDeadReport(const std::string& ip) {
    MutexLock lock(&mutex_);
    const minion_set_ip_index_t& index = boost::multi_index::get<ip_tag>(minion_set_);
    minion_set_ip_index_t::const_iterator i_it = index.find(ip);
    std::map<std::string, std::string>::iterator sc_it = scripts_.find("minion-dead-check.sh");
    std::vector<std::string> hosts;
    hosts.push_back(i_it->hostname_);
    std::string sessionid;
    bool ok = minion_ctrl_->Exec(sc_it->second, 
                                hosts,
                                "root",
                                1,
                                &sessionid,
                                boost::bind(&LumiaCtrlImpl::CheckDeadCallBack, this, _1, _2, _3));
    if (!ok) {
        LOG(WARNING, "fail to run dead check script on minion %s", i_it->hostname_.c_str());
        return;
    }
}


void LumiaCtrlImpl::CheckDeadCallBack(const std::string sessionid,
                                      const std::vector<std::string> success,
                                      const std::vector<std::string> fails) {
    LOG(INFO, "dead check with session %s  callback success host %s, fails %s",
             sessionid.c_str(),
             boost::algorithm::join(success, ",").c_str(),
             boost::algorithm::join(fails, ",").c_str());
    const minion_set_hostname_index_t& h_index = boost::multi_index::get<hostname_tag>(minion_set_);
    std::vector<std::string> fail_ids;
    for (size_t i = 0; i < fails.size(); i++) {
        minion_set_hostname_index_t::const_iterator it = h_index.find(fails[i]);
        if (it == h_index.end()) {
            LOG(WARNING, "%s has no rms id", fails[i].c_str());
            continue;
        }
        fail_ids.push_back(it->id_);
        it->minion_->set_state(kMinionRestarting);
    }
    std::string reboot_sessionid;
    if (fail_ids.size() > 0) {
        bool ok = minion_ctrl_->Reboot(fail_ids,
                                   boost::bind(&LumiaCtrlImpl::RebootCallBack, this, _1, _2, _3), &reboot_sessionid);
        if (!ok) {
            LOG(WARNING, "fail to submit reboot to rms with dead check session %s", sessionid.c_str());
            return;
        } 
    }  
    if (success.size() > 0) {
        workers_.AddTask(boost::bind(&LumiaCtrlImpl::HandleInitAgent, this, success));
    }
    
}

void LumiaCtrlImpl::HandleInitAgent(const std::vector<std::string> hosts) {
    MutexLock lock(&mutex_);
    std::vector<std::string> host_64;
    std::vector<std::string> host_128;
    const minion_set_hostname_index_t& index = boost::multi_index::get<hostname_tag>(minion_set_);
    for (size_t i = 0; i < hosts.size(); i++) {
        minion_set_hostname_index_t::const_iterator it = index.find(hosts[i]);
        if (it == index.end()) {
            LOG(INFO, "host %s does not exit in lumia", it->hostname_.c_str());
            continue;
        }
        // TODO need better strategy 
        int64_t total_mem = it->minion_->mem().count() * it->minion_->mem().size();
        it->minion_->set_state(kMinionIniting);
        if (total_mem > MEM_64G) {
            host_128.push_back(it->hostname_);
        }else {
            host_64.push_back(it->hostname_);
        }
    }
    if (host_64.size() > 0) {
        std::map<std::string, std::string>::iterator sc_it = scripts_.find("deploy-galaxy-agent-60.sh");
        if (sc_it == scripts_.end()) {
            LOG(WARNING, "deploy-galaxy-agent-60.sh does not exist in lumia");
            return;
        }
        DoInitAgent(host_64, sc_it->second);
    }

    if (host_128.size() > 0) { 
         std::map<std::string, std::string>::iterator sc_it = scripts_.find("deploy-galaxy-agent-120.sh");
        if (sc_it == scripts_.end()) {
            LOG(WARNING, "deploy-galaxy-agent-120.sh does not exist in lumia");
            return;
        }
        DoInitAgent(host_128, sc_it->second);
    }
}

bool LumiaCtrlImpl::DoInitAgent(const std::vector<std::string> hosts,
                                const std::string scripts) {
    std::string sessionid;
    bool ok = minion_ctrl_->Exec(scripts, 
                                hosts,
                                "root",
                                10,
                                &sessionid,
                                boost::bind(&LumiaCtrlImpl::InitAgentCallBack, this, _1, _2, _3));
    if (!ok) {
        LOG(WARNING, "fail to init agents %s", boost::algorithm::join(hosts, ",").c_str());
        return false;
    }
    LOG(INFO, "submit init cmd to agents %s successfully", boost::algorithm::join(hosts, ",").c_str());
    return true;
}

void LumiaCtrlImpl::InitAgentCallBack(const std::string sessionid,
                                      const std::vector<std::string> success,
                                      const std::vector<std::string> fails) {
    MutexLock lock(&mutex_);
    LOG(INFO, "init agent call back succ %s, fails %s", boost::algorithm::join(success, ",").c_str(),
      boost::algorithm::join(fails, ",").c_str()); 
    const minion_set_hostname_index_t& index = boost::multi_index::get<hostname_tag>(minion_set_);
    for (size_t i = 0; i < success.size(); i++) {
        minion_set_hostname_index_t::const_iterator it = index.find(success[i]);
        if (it == index.end()) {
            LOG(WARNING, "agent with hostname %s does not exist in lumia", success[i].c_str());
            continue;
        }
        // galaxy agent alive means agent alive
        it->minion_->set_state(kMinionAlive);
    }
}


void LumiaCtrlImpl::RebootCallBack(const std::string sessionid,
                                   const std::vector<std::string> success,
                                   const std::vector<std::string> fails){ 

    MutexLock lock(&mutex_);
    std::vector<std::string> hosts_ok;
    const minion_set_id_index_t& index = boost::multi_index::get<id_tag>(minion_set_);
    for (size_t i = 0; i < success.size(); i++) {
        minion_set_id_index_t::const_iterator it = index.find(success[i]);
        if (it == index.end()) {
            LOG(WARNING, "host with id %s does not exist in lumia", success[i].c_str());
            continue;
        }
        hosts_ok.push_back(it->hostname_);
    } 
    std::vector<std::string> hosts_err;
    for (size_t i = 0; i < fails.size(); i++) {
        minion_set_id_index_t::const_iterator it = index.find(success[i]);
        if (it == index.end()) {
            LOG(WARNING, "host with id %s does not exist in lumia", success[i].c_str());
            continue;
        }
        hosts_err.push_back(it->hostname_);
    }
    LOG(INFO, "reboot call back succ hosts %s, fails hosts %s",
        boost::algorithm::join(hosts_ok, ",").c_str(),
        boost::algorithm::join(hosts_err, ",").c_str());
    if (success.size() > 0) {
        workers_.DelayTask(10000, boost::bind(&LumiaCtrlImpl::HandleInitAgent, this, hosts_ok));
    }

}

bool LumiaCtrlImpl::LoadMinion(const std::string& path) {
    MutexLock lock(&mutex_);
    LOG(INFO, "load minion dict %s", path.c_str());
    std::ifstream ip_minions_is;
    ip_minions_is.open(path.c_str(), std::ifstream::binary);
    char buffer[1024];
    std::stringstream ss;
    while(ip_minions_is.good()) {
        ip_minions_is.read(buffer, 1024);
        int32_t read_count = ip_minions_is.gcount();
        ss.write(buffer, read_count);
    }
    LumiaMinions minions;
    minions.ParseFromString(ss.str());
    for (int i = 0; i < minions.minions_size(); i++) {
        Minion* m = new Minion();
        m->CopyFrom(minions.minions(i));
        m->set_state(kMinionAlive);
        MinionIndex m_index(m->id(), m->hostname(), m->ip(), m);
        minion_set_.insert(m_index);
    }
    LOG(INFO, "load %d minions", minions.minions_size());
    return true;
}

bool LumiaCtrlImpl::LoadScripts(const std::string& folder) {
    LOG(INFO, "load scripts from %s", folder.c_str());
    DIR *dir = opendir(folder.c_str());
    if (dir == NULL) {
        LOG(WARNING, "fail to open folder %s", folder.c_str());
        return false;
    }
    struct dirent *dirp;
    char buffer[1024];
    while ((dirp = readdir(dir)) != NULL) {
        std::string filename(dirp->d_name);
        if (filename.compare(".") == 0 || filename.compare("..") == 0) {
            continue;
        }
        LOG(INFO, "load file %s", dirp->d_name);
        std::string full_path = folder + "/" + filename;
        std::ifstream script;
        script.open(full_path.c_str(), std::ifstream::in);
        std::stringstream ss;
        while (script.good()) {
            script.read(buffer, 1024);
            int32_t count = script.gcount();
            ss.write(buffer, count);
        }
        scripts_.insert(std::make_pair(filename, ss.str()));
    }
    return true;
}

void LumiaCtrlImpl::GetMinion(::google::protobuf::RpcController* /*controller*/,
                   const ::baidu::lumia::GetMinionRequest* request,
                   ::baidu::lumia::GetMinionResponse* response,
                   ::google::protobuf::Closure* done) {
    MutexLock lock(&mutex_);
    const minion_set_ip_index_t& ip_index = boost::multi_index::get<ip_tag>(minion_set_);
    for (int i = 0; i < request->ips_size(); i++) {
        minion_set_ip_index_t::const_iterator it = ip_index.find(request->ips(i));
        if (it != ip_index.end()) {
            Minion* minion = response->add_minions();
            minion->CopyFrom(*(it->minion_));
        }    
    }
    const minion_set_hostname_index_t& ht_index = boost::multi_index::get<hostname_tag>(minion_set_);
    for (int i = 0; i < request->hostnames_size(); i++) {
        minion_set_hostname_index_t::const_iterator it = ht_index.find(request->hostnames(i));
        if (it != ht_index.end()) {
            Minion* minion = response->add_minions();
            minion->CopyFrom(*(it->minion_));
        }
    }
    const minion_set_id_index_t& id_index = boost::multi_index::get<id_tag>(minion_set_);
    for (int i = 0; i < request->ids_size(); i++) {
        minion_set_id_index_t::const_iterator it  = id_index.find(request->ids(i));
        if (it != id_index.end()) {
            Minion* minion = response->add_minions();
            minion->CopyFrom(*(it->minion_));
        }
    }
    done->Run();

}

}
}
