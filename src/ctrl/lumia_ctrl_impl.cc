// Copyright (c) 2015, Baidu.com, Inc. All Rights Reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "ctrl/lumia_ctrl_impl.h"

#include "ctrl/lumia_ctrl_util.h"
#include "galaxy/galaxy.h"
#include "proto/agent.pb.h"
#include "proto/galaxy.pb.h"
#include <boost/algorithm/string/join.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/lexical_cast.hpp>
#include <unistd.h>
#include <gflags/gflags.h>
#include <boost/function.hpp>
#include <boost/bind.hpp>
#include <boost/scoped_ptr.hpp>
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
DECLARE_string(nexus_root_path);
DECLARE_string(data_center);
DECLARE_string(master_path);
DECLARE_string(galaxy_ftp_path);
DECLARE_int32(galaxy_mem_max_percent);
DECLARE_int32(galaxy_cpu_max_percent);
DECLARE_string(lumia_agent_port);

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
    rpc_client_ = new ::baidu::galaxy::RpcClient();
    query_node_count_ = 0;
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
    ScheduleNextQuery();
    ScheduleNextQueryGalaxy();
    return;
}

void LumiaCtrlImpl::GetOverview(::google::protobuf::RpcController* /*controller*/,
                    const ::baidu::lumia::GetOverviewRequest* /*request*/,
                    ::baidu::lumia::GetOverviewResponse* response,
                    ::google::protobuf::Closure* done) {
    MutexLock lock(&mutex_);
    const minion_set_id_index_t& id_index = boost::multi_index::get<id_tag>(minion_set_);
    minion_set_id_index_t::const_iterator it = id_index.begin();
    int32_t count = 0;
    for (; it != id_index.end(); ++it) {
        const MinionStatus& status = it->minion_->status();
        if (status.devices_size() <= 0) {
            continue;
        }
        MinionOverview* view = response->add_minions();
        view->set_hostname(it->minion_->hostname());
        view->set_ip(it->minion_->ip());
        bool mount_ok = true;
        bool device_ok = true;
        for (int i = 0; i < status.devices_size(); i++) {
            if (status.devices(i).healthy()) {
                continue;
            }
            device_ok = false;
            break;
        }
        for (int i = 0;  i < status.mounts_size(); i++) {
            if (status.mounts(i).mounted()) {
                continue;
            }
            if (status.mounts(i).mount_point().find("/noah") != std::string::npos) {
                continue;
            }
            mount_ok = false;
            break;
        }
        view->set_mount_ok(mount_ok);
        view->set_device_ok(device_ok);
        view->set_datetime(status.datetime());
        count++;
    }
    done->Run();
    LOG(INFO, "get overview  count %d", count);
}

void LumiaCtrlImpl::ScheduleNextQuery() {
    workers_.DelayTask(30000, boost::bind(&LumiaCtrlImpl::LaunchQuery, this));
}

void LumiaCtrlImpl::LaunchQuery() {
    MutexLock lock(&mutex_);
    if (query_node_count_ != 0) {
        LOG(WARNING, "invalide query node count number %d", query_node_count_);
        return;
    }
    std::set<std::string>::iterator it = nodes_.begin();
    for (; it != nodes_.end(); ++it) {
        QueryNode(*it);
    }
    if (query_node_count_ == 0) {
        ScheduleNextQuery();
    }
}

void LumiaCtrlImpl::ScheduleNextQueryGalaxy() {
    workers_.DelayTask(30000, boost::bind(&LumiaCtrlImpl::LaunchQueryGalaxy, this));
}

void LumiaCtrlImpl::LaunchQueryGalaxy() {
    ScheduleNextQueryGalaxy();
    ::galaxy::ins::sdk::SDKError err;
    std::string master_path_key = FLAGS_nexus_root_path + FLAGS_master_path;
    std::string master_addr;
    bool ok = nexus_->Get(master_path_key, &master_addr, &err);
    if (!ok) {
        LOG(WARNING, "get master addr err");
        return;
    }
    baidu::galaxy::Galaxy* galaxy = baidu::galaxy::Galaxy::ConnectGalaxy(master_addr);
    std::vector<baidu::galaxy::NodeDescription> agents;
    if (galaxy->ListAgents(&agents)) {
        MutexLock lock(&mutex_);
        const minion_set_ip_index_t& ip_index = boost::multi_index::get<ip_tag>(minion_set_);
        for (uint32_t i = 0; i < agents.size(); i++) {
            if (agents[i].state == AgentState_Name(::baidu::galaxy::kDead)) {
                minion_set_ip_index_t::const_iterator it = ip_index.find(agents[i].addr);
                if (it == ip_index.end()) {
                    LOG(WARNING, "no lumia on host %s ", agents[i].addr.c_str());
                    continue;
                }
                if (it->minion_->state() != kMinionAlive) {
                    LOG(WARNING, "lumia agent on %s state : %u", agents[i].addr.c_str(), it->minion_->state());
                    continue;
                }
                it->minion_->set_state(kMinionReinstalling);
                workers_.AddTask(boost::bind(&LumiaCtrlImpl::HandleRemoveGalaxy, this, it->minion_->ip()));
                LOG(INFO, "rebuild galaxy agent env on host %s", agents[i].addr.c_str());
            }
        }
    } else {
        LOG(WARNING, "quert galaxy for agents failed");
    }
    delete galaxy;
    return;
}

void LumiaCtrlImpl::HandleInitGalaxy(const std::string& node_addr) {
    MutexLock lock(&mutex_);
    const minion_set_ip_index_t& ip_index = boost::multi_index::get<ip_tag>(minion_set_);
    minion_set_ip_index_t::const_iterator it = ip_index.find(node_addr);
    if (it == ip_index.end()) {
        LOG(WARNING, "node lumia on host %s ", node_addr.c_str());
        return;
    }
    ExecRequest* request = new ExecRequest();
    ExecResponse* response = new ExecResponse();
    std::string cmd = "deploy_galaxy_agent.sh";
    if (!FLAGS_data_center.empty()) {
        cmd = cmd +  " -data_center" + " " + FLAGS_data_center;
    }
    cmd = cmd + " -ftp_path" + " " + FLAGS_galaxy_ftp_path;
    if (it->minion_->has_disk() && it->minion_->disk().has_size()) {
        std::stringstream ss;
        ss << it->minion_->disk().size();
        cmd = cmd + " -disk_size" + " " + ss.str();
    }
    if(it->minion_->has_bandwidth()) {
        std::stringstream ss;
        ss << it->minion_->bandwidth();
        cmd = cmd + " -bandwidth" + " " + ss.str();
    }
    std::map<std::string, std::string>::iterator sc_it = scripts_.find("deploy_galaxy_agent.sh");
    if (sc_it == scripts_.end()) {
        LOG(WARNING, "deploy_galaxy_agent.sh does not exist in lumia");
        return;
    }
    request->set_script(sc_it->second);
    request->set_cmd(cmd);
    LumiaAgent_Stub* agent = NULL;
    std::string minion_addr = node_addr + ":" + FLAGS_lumia_agent_port;
    rpc_client_->GetStub(minion_addr, &agent);
    boost::function<void (const ExecRequest*, ExecResponse*, bool, int)> callback;
    callback = boost::bind(&LumiaCtrlImpl::ExecCallback, this, _1, _2, _3, _4, node_addr, "init");
    rpc_client_->AsyncRequest(agent, &LumiaAgent_Stub::Exec, request, response, callback, 5 ,0);
    it->minion_->set_state(kMinionIniting);
    delete agent;
}

void LumiaCtrlImpl::QueryNode(const std::string& node_addr) {
    mutex_.AssertHeld();
    LOG(INFO, "start a query on node %s", node_addr.c_str());
    QueryAgentRequest* request = new QueryAgentRequest();
    QueryAgentResponse* response = new QueryAgentResponse();
    LumiaAgent_Stub* agent = NULL;
    rpc_client_->GetStub(node_addr, &agent);
    boost::function<void (const QueryAgentRequest*, QueryAgentResponse*, bool, int)> query_callback; 
    query_callback = boost::bind(&LumiaCtrlImpl::QueryCallBack, this, _1, _2, _3, _4, node_addr);
    rpc_client_->AsyncRequest(agent, &LumiaAgent_Stub::Query,
                              request, response, query_callback, 5, 0);
    query_node_count_++;
    delete agent;
}

void LumiaCtrlImpl::QueryCallBack(const QueryAgentRequest* request,
                                  QueryAgentResponse* response,
                                  bool fails, int /*error*/, 
                                  const std::string& node_addr) {
    boost::scoped_ptr<const QueryAgentRequest> request_ptr(request);
    boost::scoped_ptr<QueryAgentResponse> response_ptr(response);
   
    MutexLock lock(&mutex_);
    if (--query_node_count_ == 0) {
        ScheduleNextQuery();
    }
    if (fails || response->status() != 0) {
        LOG(WARNING, "fail to query node %s", node_addr.c_str());
        return;
    }
    const minion_set_ip_index_t& ip_index = boost::multi_index::get<ip_tag>(minion_set_);
    minion_set_ip_index_t::const_iterator it = ip_index.find(response->ip());
    if (it == ip_index.end()) {
        LOG(WARNING, "host %s meta does not in lumia", response->ip().c_str());
        return;
    }
    it->minion_->mutable_status()->CopyFrom(response->minion_status());
    LOG(INFO, "update minion %s status successfully status %d", response->ip().c_str(),
       response->minion_status().all_is_well());
    if (response->minion_status().cpu_used() * 100 > FLAGS_galaxy_cpu_max_percent
        || response->minion_status().mem_used() * 100 > FLAGS_galaxy_mem_max_percent) {
        it->minion_->set_state(kMinionBusy);
        workers_.AddTask(boost::bind(&LumiaCtrlImpl::HandleRemoveGalaxy, this, node_addr));
    }
}

void LumiaCtrlImpl::HandleRemoveGalaxy(const std::string& node_addr) {
    MutexLock lock(&mutex_);
    const minion_set_ip_index_t& ip_index = boost::multi_index::get<ip_tag>(minion_set_);
    minion_set_ip_index_t::const_iterator it = ip_index.find(node_addr);
    if (it == ip_index.end()) {
        LOG(WARNING, "no Lumia on host %s", node_addr.c_str());
        return;
    }
    std::string cmd = "remove_galaxy_agent.sh ";
    ExecRequest* request = new ExecRequest();
    ExecResponse* response = new ExecResponse();
    std::map<std::string, std::string>::iterator sc_it = scripts_.find("remove_galaxy_agent.sh");
    if (sc_it == scripts_.end()) {
        LOG(WARNING, "deploy_galaxy_agent.sh does not exist in lumia");
        return;
    }
    request->set_script(sc_it->second);
    request->set_cmd(cmd);
    LumiaAgent_Stub* agent = NULL;
    std::string minion_addr = node_addr + ":" + FLAGS_lumia_agent_port;
    rpc_client_->GetStub(minion_addr, &agent);
    boost::function<void (const ExecRequest*, ExecResponse*,
                          bool, int)> callback;
    callback = boost::bind(&LumiaCtrlImpl::ExecCallback, this, _1, _2, _3, _4, node_addr, "rm");
    rpc_client_->AsyncRequest(agent, &LumiaAgent_Stub::Exec,
                              request, response, callback, 5, 0);
    delete agent;
}

void LumiaCtrlImpl::HandleExecGalaxy(const std::string& node_addr, const std::string& cmd, const std::string& script) {
    MutexLock lock(&mutex_);
    const minion_set_ip_index_t& ip_index = boost::multi_index::get<ip_tag>(minion_set_);
    minion_set_ip_index_t::const_iterator it = ip_index.find(node_addr);
    if (it == ip_index.end()) {
        LOG(WARNING, "no Lumia on host %s", node_addr.c_str());
        return;
    }
    ExecRequest* request = new ExecRequest();
    ExecResponse* response = new ExecResponse();
    std::map<std::string, std::string>::iterator sc_it = scripts_.find(script);
    if (sc_it == scripts_.end()) {
        LOG(WARNING, "script does not exist in lumia");
        return;
    }
    request->set_script(sc_it->second);
    request->set_cmd(cmd);
    LumiaAgent_Stub* agent = NULL;
    std::string minion_addr = node_addr + ":" + FLAGS_lumia_agent_port;
    rpc_client_->GetStub(minion_addr, &agent);
    boost::function<void (const ExecRequest*, ExecResponse*,
                          bool, int)> callback;
    callback = boost::bind(&LumiaCtrlImpl::ExecCallback, this, _1, _2, _3, _4, node_addr, "exec");
    rpc_client_->AsyncRequest(agent, &LumiaAgent_Stub::Exec,
                              request, response, callback, 5, 0);
    delete agent;
}

void LumiaCtrlImpl::ExecMinion(::google::protobuf::RpcController* /*controller*/,
                               const ::baidu::lumia::ExecMinionRequest* request,
                               ::baidu::lumia::ExecMinionResponse* response,
                               ::google::protobuf::Closure* done) {
    MutexLock lock(&mutex_);
    const minion_set_hostname_index_t& index = boost::multi_index::get<hostname_tag>(minion_set_);
    for (int i = 0; i < request->minions_size(); i++) {
        minion_set_hostname_index_t::const_iterator it = index.find(request->minions(i));
        if (it == index.end()) {
            LOG(WARNING, "minion %s is not exist.", it->minion_->hostname().c_str());
        }
        std::vector<std::string> lines;
        boost::split(lines, request->cmd(), boost::is_any_of(" "));
        std::map<std::string, std::string>::iterator sc_it = scripts_.find(lines[0]);
        if (sc_it == scripts_.end()) {
            LOG(WARNING, "remove_galaxy_hybrid.sh is not found");
            response->set_status(kLumiaScriptNotFound);
            done->Run();
            return;
        }
        workers_.AddTask(boost::bind(&LumiaCtrlImpl::HandleExecGalaxy, this, it->minion_->ip(),
                                     request->cmd(), lines[0]));
    }
}
void LumiaCtrlImpl::ExecCallback(const ExecRequest* request,
                                 ExecResponse* response,
                                 bool fails, int /*error*/,
                                 const std::string& node_addr,
                                 const std::string& cmd) {
    boost::scoped_ptr<const ExecRequest> request_ptr(request);
    boost::scoped_ptr<ExecResponse> response_ptr(response);
    MutexLock lock(&mutex_);
    if (cmd == "init") {
        const minion_set_ip_index_t& ip_index = boost::multi_index::get<ip_tag>(minion_set_);
        minion_set_ip_index_t::const_iterator it = ip_index.find(response->ip());
        if (it == ip_index.end()) {
            LOG(WARNING, "host %s meta does not in lumia", response->ip().c_str());
            return;
        }
        if (fails || response->status() != 0) {
            LOG(WARNING, "fail to init galaxy env on host %s", node_addr.c_str());
            it->minion_->set_state(kMinionError);
            return;
        }
        it->minion_->set_state(kMinionAlive);
        return;
    } else if (cmd == "rm") {
        const minion_set_ip_index_t& ip_index = boost::multi_index::get<ip_tag>(minion_set_);
        minion_set_ip_index_t::const_iterator it = ip_index.find(response->ip());
        if (it == ip_index.end()) {
            LOG(WARNING, "host %s meta does not in lumia", response->ip().c_str());
            return;
        }
        if (fails || response->status() != 0) {
            LOG(WARNING, "fail to remove galaxy env on host %s", node_addr.c_str());
            it->minion_->set_state(kMinionError);
            return;
        } else if (it->minion_->state() == kMinionReinstalling) {
            workers_.AddTask(boost::bind(&LumiaCtrlImpl::HandleInitGalaxy, this, node_addr));
        }
        return;
    } else {
        if (fails || response->status() != 0) {
            LOG(WARNING, "fail to exec %s on host %s", cmd.c_str(), node_addr.c_str());
            return;
        }
        LOG(INFO, "success to exec %s on host %s", cmd.c_str(), node_addr.c_str());
        return;
    }
}

#if 0
void LumiaCtrlImpl::InitGalaxy(::google::protobuf::RpcController*,
                               const ::baidu::lumia::InitGalaxyRequest* request,
                               ::baidu::lumia::InitGalaxyResponse* response,
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
            LOG(WARNING, "fail tp put minon to nexus %s", minion.hostname().c_str());
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
        } else {
            MinionState state = it->minion_->state();
            it->minion_->CopyFrom(minion);
            it->minion_->set_state(state);
            LOG(INFO, "update minion %s", minion.hostname().c_str());               
        }
        InitGalaxyEnv(it->minion_->ip());
        LOG(INFO, "Init galaxy env %s", it->minion_->ip().c_str());
    }
    response->set_status(kLumiaOk);
    done->Run();
}
#endif

void LumiaCtrlImpl::ImportData(::google::protobuf::RpcController* /*controller*/,
                    const ::baidu::lumia::ImportDataRequest* request,
                    ::baidu::lumia::ImportDataResponse* response,
                    ::google::protobuf::Closure* done) {
    MutexLock lock(&mutex_);
    ::galaxy::ins::sdk::SDKError err;
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
            workers_.AddTask(boost::bind(&LumiaCtrlImpl::HandleInitGalaxy, this, m->ip()));     
            LOG(INFO, "insert new minion %s", m->hostname().c_str());
        } else {
            MinionState state = it->minion_->state();
            it->minion_->CopyFrom(minion);
            if (state == kMinionDel || state == kMinionError || state == kMinionBusy) {
                it->minion_->set_state(kMinionAlive);
                workers_.AddTask(boost::bind(&LumiaCtrlImpl::HandleInitGalaxy, this, it->minion_->ip()));
            } else {
                it->minion_->set_state(state);
            }
            LOG(INFO, "update minion %s", minion.hostname().c_str());
        }
    }

    response->set_status(kLumiaOk);
    done->Run();
}

void LumiaCtrlImpl::ExportData(::google::protobuf::RpcController*,
                                 const ::baidu::lumia::ExportDataRequest* request,
                                 ::baidu::lumia::ExportDataResponse* response,
                                 ::google::protobuf::Closure* done) {
    MutexLock lock(&mutex_);   
    std::map<std::string, std::string>::iterator sc_it = scripts_.find("remove_galaxy_agent.sh");
    if (sc_it == scripts_.end()) {
        LOG(WARNING, "remove_galaxy_agent.sh is not found");
        response->set_status(kLumiaScriptNotFound);
        done->Run();
        return;
    }
    ::galaxy::ins::sdk::SDKError err;
    const minion_set_hostname_index_t& index = boost::multi_index::get<hostname_tag>(minion_set_);
    for (int i = 0; i < request->hostnames_size(); i++) {
        minion_set_hostname_index_t::const_iterator it = index.find(request->hostnames(i));
        if (it == index.end()) {
            LOG(WARNING, "minion %s is not exist.", request->hostnames(i).c_str());
        } else {
            it->minion_->set_state(kMinionDel);
        }
        std::string minion_key = FLAGS_lumia_root_path + FLAGS_lumia_minion + "/" + it->minion_->id();
        int ok = nexus_->Delete(minion_key, &err);
        if (!ok || err != ::galaxy::ins::sdk::kOK) {
            LOG(WARNING, "fail to del minion %s", it->minion_->hostname().c_str());
            continue;
        }
        workers_.AddTask(boost::bind(&LumiaCtrlImpl::HandleRemoveGalaxy, this, it->minion_->ip()));
        LOG(INFO, "remove galaxy env %s", it->minion_->ip().c_str());
    }
    return;
}

void LumiaCtrlImpl::Ping(::google::protobuf::RpcController* /*controller*/,
                         const ::baidu::lumia::PingRequest* request,
                         ::baidu::lumia::PingResponse* /*response*/,
                         ::google::protobuf::Closure* done) {
    {
        MutexLock lock(&mutex_);
        nodes_.insert(request->node_addr());
    }
    MutexLock lock(&timer_mutex_);
    std::map<std::string, int64_t>::iterator it = node_timers_.find(request->node_addr());
    if (it != node_timers_.end()) {
        bool ok = dead_checkers_.CancelTask(it->second);
        if (!ok) {
            LOG(WARNING, "some stranges happend to %s ", request->node_addr().c_str());
        }
    }else {
        LOG(INFO, "new node %s join", request->node_addr().c_str());
    }
    int64_t id = dead_checkers_.DelayTask(10000, boost::bind(&LumiaCtrlImpl::HandleNodeOffline, this, request->node_addr()));
    node_timers_[request->node_addr()] = id;
    done->Run();
}

void LumiaCtrlImpl::HandleNodeOffline(const std::string& node_addr) {
    MutexLock lock(&mutex_);
    nodes_.erase(node_addr);
}

#if 0
void LumiaCtrlImpl::ReportDeadMinion(::google::protobuf::RpcController* /*controller*/,
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

bool LumiaCtrlImpl::DoDelAgent(const std::vector<std::string> hosts,
                               const std::string scripts) {
    std::string sessionid;
    bool ok = minion_ctrl_->Exec(scripts,
                                hosts,
                                "root",
                                10,
                                &sessionid,
                                boost::bind(&LumiaCtrlImpl::DelAgentCallBack, this, _1, _2, _3));
    if (!ok) {
        LOG(WARNING, "fail to del agents %s", boost::algorithm::join(hosts, ",").c_str());
        return false;
    }
    LOG(INFO, "submit del cmd to agents %s successfully", boost::algorithm::join(hosts, ",").c_str());
    return true;
}

void LumiaCtrlImpl::DelAgentCallBack(const std::string /*sessionid*/,
                                     const std::vector<std::string> success,
                                     const std::vector<std::string> fails) {
    MutexLock lock(&mutex_);
    LOG(INFO, "del agent call back succ %s, fails %s", boost::algorithm::join(success, ",").c_str(),
        boost::algorithm::join(fails, ",").c_str());
    minion_set_hostname_index_t& index = boost::multi_index::get<hostname_tag>(minion_set_);
    for (size_t i = 0; i < success.size(); i++) {
        minion_set_hostname_index_t::const_iterator it = index.find(success[i]);
        if (it == index.end()) {
            LOG(WARNING, "del agent with hostname %s does not exist in lumia", success[i].c_str());
            continue;
        }
        //MinionIndex m_index(it->minion_->id(), it->minion_->hostname(), it->minion_->ip(), it->minion_); 
        index.erase(it);
        delete it->minion_;
    }
    return;
}

void LumiaCtrlImpl::InitAgentCallBack(const std::string /*sessionid*/,
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

void LumiaCtrlImpl::RebootCallBack(const std::string /*sessionid*/,
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
#endif

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
#if 0
void LumiaCtrlImpl::DelMinion(::google::protobuf::RpcController* /*controller*/,
                                const ::baidu::lumia::DelMinionRequest* request,
                                ::baidu::lumia::DelMinionResponse* response,
                                ::google::protobuf::Closure* done){
    MutexLock lock(&mutex_);
    std::vector<Minion> minion_vec;
    std::vector<std::string> hosts;
    const minion_set_ip_index_t& ip_index = boost::multi_index::get<ip_tag>(minion_set_);
    for (int i = 0; i < request->ips_size(); i++) {
        minion_set_ip_index_t::const_iterator it = ip_index.find(request->ips(i));
        if (it != ip_index.end()) {
            minion_vec.push_back(*(it->minion_));
            hosts.push_back(it->minion_->hostname());
        }
    }   
    const minion_set_hostname_index_t& ht_index = boost::multi_index::get<hostname_tag>(minion_set_);
    for (int i = 0; i < request->hostnames_size(); i++) {
        minion_set_hostname_index_t::const_iterator it = ht_index.find(request->hostnames(i));
        if (it != ht_index.end()) {
            minion_vec.push_back(*(it->minion_));
            hosts.push_back(it->minion_->hostname());
        }
    }   
    const minion_set_id_index_t& id_index = boost::multi_index::get<id_tag>(minion_set_);
    for (int i = 0; i < request->ids_size(); i++) {
        minion_set_id_index_t::const_iterator it  = id_index.find(request->ids(i));
        if (it != id_index.end()) {
            minion_vec.push_back(*(it->minion_));
            hosts.push_back(it->minion_->hostname());
        }
    }
    for (uint32_t i = 0; i < minion_vec.size(); i++) {
        std::string minion_key = FLAGS_lumia_root_path + FLAGS_lumia_minion + "/" + minion_vec[i].id();
        ::galaxy::ins::sdk::SDKError err;
        bool ok = nexus_->Delete(minion_key, &err);
        if (!ok || err != ::galaxy::ins::sdk::kOK) {
            LOG(WARNING, "fail to del minion to nexus %s", minion_key.c_str());
            continue;
        }
    }
    std::map<std::string, std::string>::iterator sc_it = scripts_.find("remove-galaxy-agent.sh");
    if (sc_it == scripts_.end()) {
        LOG(WARNING, "deploy-galaxy-agent-60.sh does not exist in lumia");
        return;
    }
    DoDelAgent(hosts, sc_it->second);    
    response->set_status(kLumiaOk);
    done->Run();
}
#endif

}
}
