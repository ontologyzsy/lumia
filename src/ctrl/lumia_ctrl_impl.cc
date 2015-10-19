// Copyright (c) 2015, Baidu.com, Inc. All Rights Reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "ctrl/lumia_ctrl_impl.h"

#include "ctrl/lumia_ctrl_util.h"
#include <boost/algorithm/string/join.hpp>
#include <boost/lexical_cast.hpp>
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
DECLARE_string(nexus_servers);
DECLARE_string(lumia_lock);
DECLARE_string(lumia_ctrl_port);
DECLARE_string(lumia_main);
DECLARE_string(lumia_minion);
DECLARE_string(lumia_script);
DECLARE_string(minion_dict);
DECLARE_string(scripts_dir);
DECLARE_string(lumia_root_path);

namespace baidu {
namespace lumia {

const static int64_t MEM_RESERVED = 6442450944;
const static int32_t CPU_IDLE = 700;

static void OnLumiaSessionTimeout(void* ctx) {
    LumiaCtrlImpl* lumia = static_cast<LumiaCtrlImpl*>(ctx);
    lumia->OnSessionTimeout();
}

static void OnLumiaLockChange(const ::galaxy::ins::sdk::WatchParam& param,
  ::galaxy::ins::sdk::SDKError) {
    LumiaCtrlImpl* lumia = static_cast<LumiaCtrlImpl*>(param.context);
    lumia->OnLockChange(param.value);
}

LumiaCtrlImpl::LumiaCtrlImpl():workers_(4){
    minion_ctrl_ = new MinionCtrl(FLAGS_ccs_api_http_host,
                                  FLAGS_rms_api_http_host);
    nexus_ = new ::galaxy::ins::sdk::InsSDK(FLAGS_nexus_servers);
}

void LumiaCtrlImpl::OnLockChange(const std::string& sessionid) {
    std::string self_sessionid = nexus_->GetSessionID();
    if (self_sessionid != sessionid) {
        LOG(WARNING, "lumia lost lock");
        abort();
    }
}


LumiaCtrlImpl::~LumiaCtrlImpl() {
    delete minion_ctrl_;
    delete nexus_;
}

void LumiaCtrlImpl::Init() {
    AcquireLumiaLock(); 
}
void LumiaCtrlImpl::OnSessionTimeout() {
    LOG(WARNING, "time out with nexus");
    abort();
}

void LumiaCtrlImpl::AcquireLumiaLock() {
    std::string lock = FLAGS_lumia_root_path + FLAGS_lumia_lock;
    ::galaxy::ins::sdk::SDKError err;
    nexus_->RegisterSessionTimeout(&OnLumiaSessionTimeout, this);
    bool ret = nexus_->Lock(lock, &err); //whould block until accquired
    assert(ret && err == ::galaxy::ins::sdk::kOK);
    std::string lumia_endpoint = LumiaUtil::GetHostName() + ":" + FLAGS_lumia_ctrl_port;
    std::string lumia_key = FLAGS_lumia_root_path + FLAGS_lumia_main;
    ret = nexus_->Put(lumia_key, lumia_endpoint, &err);
    assert(ret && err == ::galaxy::ins::sdk::kOK);
    ret = nexus_->Watch(lock, &OnLumiaLockChange, this, &err);
    assert(ret && err == ::galaxy::ins::sdk::kOK);
    LOG(INFO, "master lock [ok].  %s -> %s", 
        lumia_key.c_str(), lumia_endpoint.c_str());

    std::string minion_start_key = FLAGS_lumia_root_path + FLAGS_lumia_minion +"/0";
    std::string minion_end_key = FLAGS_lumia_root_path + FLAGS_lumia_minion +"/~~~~~~~~~";
    // TODO delete minions
    ::galaxy::ins::sdk::ScanResult* minions = nexus_->Scan(minion_start_key, minion_end_key);
    if (minions->Error() != ::galaxy::ins::sdk::kOK) {
        LOG(FATAL, "fail to load minions from nexus");
    }
    while (!minions->Done()) {
        Minion* m = new Minion();
        ret = m->ParseFromString(minions->Value());
        if (!ret) {
            LOG(WARNING, "fail to parse %s", minions->Key().c_str());
            minions->Next();
        }
        MinionIndex m_index(m->id(), m->hostname(), m->ip(), m);
        minion_set_.insert(m_index);
        LOG(INFO, "load minion %s", m->hostname().c_str());
        minions->Next();
    }
    std::string script_start_key = FLAGS_lumia_root_path + FLAGS_lumia_script  + "/";
    std::string script_end_key = FLAGS_lumia_root_path + FLAGS_lumia_script + "/~~~~~~~~~";
    ::galaxy::ins::sdk::ScanResult* scripts = nexus_->Scan(script_start_key, script_end_key);
    if (scripts->Error() != ::galaxy::ins::sdk::kOK) {
        LOG(FATAL, "fail to load scripts from nexus");
    }
    while (!scripts->Done()) {
        SystemScript script;
        script.ParseFromString(scripts->Value());
        scripts_[script.name()] = script.content();
        LOG(INFO, "load script %s", script.name().c_str());
        scripts->Next();
    }

}

void LumiaCtrlImpl::ImportData(::google::protobuf::RpcController* controller,
                    const ::baidu::lumia::ImportDataRequest* request,
                    ::baidu::lumia::ImportDataResponse* response,
                    ::google::protobuf::Closure* done) {
    MutexLock lock(&mutex_);
    ::galaxy::ins::sdk::SDKError err;
    const minion_set_id_index_t& index = boost::multi_index::get<id_tag>(minion_set_);
    for (int i = 0; i < request->minions().minions_size(); i++) {
        const Minion& minion = request->minions().minions(i);
        std::string minion_value;
        bool ok = minion.SerializeToString(&minion_value);
        if (!ok) {
           LOG(WARNING, "fail to serialize minion %s to string", minion.hostname().c_str());
           continue;
        }
        std::string minion_key = FLAGS_lumia_root_path + FLAGS_lumia_minion + "/" + minion.id();
        ok = nexus_->Put(minion_key, minion_value, &err);
        if (!ok || err != ::galaxy::ins::sdk::kOK) {
           LOG(WARNING, "fail to put minion to nexus %s", minion.hostname().c_str());
           continue;
        }
        minion_set_id_index_t::const_iterator it = index.find(minion.id());
        if (it == index.end()) {
            Minion* m = new Minion();
            m->CopyFrom(minion);
            m->set_state(kMinionAlive);
            MinionIndex m_index(m->id(), m->hostname(), m->ip(), m);
            minion_set_.insert(m_index);
            LOG(INFO, "insert new minion %s", m->hostname().c_str());
        }else {
            MinionState state = it->minion_->state();
            it->minion_->CopyFrom(minion);
            it->minion_->set_state(state);
            LOG(INFO, "update minion %s", minion.hostname().c_str());
        }
    }

    for (int i = 0; i < request->scripts_size(); i++) {
        LOG(INFO, "load script with name %s", request->scripts(i).name().c_str());
        std::string script_key = FLAGS_lumia_root_path + FLAGS_lumia_script + "/" + request->scripts(i).name();
        std::string script_value;
        bool ok = request->scripts(i).SerializeToString(&script_value);
        if (!ok) {
            LOG(WARNING, "fail to serialize script %s", script_key.c_str());
            continue;
        }
        ok = nexus_->Put(script_key, script_value, &err);
        if (!ok || err != ::galaxy::ins::sdk::kOK ) {
           LOG(WARNING, "fail to save script %s to nexus", request->scripts(i).name().c_str());
           continue;
        }
        scripts_[request->scripts(i).name()] = request->scripts(i).content();
    }
    response->set_status(kLumiaOk);
    done->Run();
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
    std::map<std::string, std::vector<std::string> > hosts_map;
    const minion_set_hostname_index_t& index = boost::multi_index::get<hostname_tag>(minion_set_);
    std::map<std::string, std::string>::iterator sc_it = scripts_.find("deploy-galaxy-agent.tpl.sh");
    if (sc_it == scripts_.end()) {
        LOG(WARNING, "deploy-galaxy-agent.tpl.sh does not exist in lumia");
        return;
    }
    for (size_t i = 0; i < hosts.size(); i++) {
        minion_set_hostname_index_t::const_iterator it = index.find(hosts[i]);
        if (it == index.end()) {
            LOG(INFO, "host %s does not exit in lumia", it->hostname_.c_str());
            continue;
        }
        // TODO need better strategy 
        int64_t mem_share = it->minion_->mem().count() * it->minion_->mem().size() - MEM_RESERVED;
        int32_t cpu_share = it->minion_->cpu().count() * CPU_IDLE; 
        it->minion_->set_state(kMinionIniting);
        LOG(INFO, "start init minion with cpu share %d, mem share %ld", cpu_share, mem_share);
        std::string deploy_script = sc_it->second;
        std::string cpu_share_sc = "sed -i 's/--agent_millicores_share=.*/--agent_millicores_share=" + boost::lexical_cast<std::string>(cpu_share) + "/' conf/galaxy.flag\n";
        std::string mem_share_sc = "sed -i 's/--agent_mem_share=.*/--agent_mem_share=" + boost::lexical_cast<std::string>(mem_share) + "/' conf/galaxy.flag\n";
        deploy_script.append(cpu_share_sc);
        deploy_script.append(mem_share_sc);
        deploy_script.append("babysitter bin/galaxy-agent.conf stop >/dev/null 2>&1\n");
        deploy_script.append("sleep 2\n");
        deploy_script.append("babysitter bin/galaxy-agent.conf start\n");
        std::vector<std::string>& hosts = hosts_map[deploy_script]; 
        hosts.push_back(it->hostname_);
    }
    std::map<std::string, std::vector<std::string> >::iterator it = hosts_map.begin();
    for (; it != hosts_map.end(); it++) {
        if (it->second.size() <= 0) {
            continue;
        }
        DoInitAgent(it->second, it->first);
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
