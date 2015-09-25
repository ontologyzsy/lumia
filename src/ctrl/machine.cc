// Copyright (c) 2015, Baidu.com, Inc. All Rights Reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ctrl/machine.h"

#include "rapidjson/writer.h"
#include <gflags/gflags.h>
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "baas-lib-c/baas.h"
#include <boost/uuid/uuid.hpp>
#include <boost/bind.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/lexical_cast.hpp>
#include "logging.h"

DECLARE_int32(exec_job_check_interval);
DECLARE_string(rms_token);
DECLARE_string(rms_app_key);
DECLARE_string(rms_auth_user);
namespace baidu {
namespace lumia {

MachineCtrl::MachineCtrl(const std::string& ccs_http_server,
                         const std::string& rms_http_server):ccs_http_server_(ccs_http_server),
    rms_http_server_(rms_http_server),
    http_client_(){
    
}

MachineCtrl::~MachineCtrl(){}

bool MachineCtrl::Exec(const std::string& script,
                       const std::vector<std::string>& hosts,
                       const std::string& account,
                       int32_t concurrency,
                       std::string* sessionid,
                       const CallBack& callback) {
    MutexLock lock(&mutex_);
    LOG(INFO, "exec %s with account %s", script.c_str(), account.c_str());
    std::string ticket;
    std::string service;
    bool ok = GenerateTicket(&ticket, &service);
    if (!ok) {
        return false;
    }
    std::string job;
    ok = BuildJob(script, hosts, account, service, concurrency, &job);
    if (!ok) {
        return false;
    }
    std::vector<std::pair<std::string, std::string> > data;
        data.push_back(std::make_pair("job", job));
    data.push_back(std::make_pair("credential", ticket));
    HttpPostRequest request;
    request.data = data;
    request.url = ccs_http_server_ + "/job/simplyAdd";
    HttpResponse response;
    ok = http_client_.Post(&request, &response);
    if (!ok) {
        LOG(WARNING, "post request to %s fails %s",ccs_http_server_.c_str(), response.error.c_str());
        return false;
    }
    rapidjson::Document doc;
    doc.Parse(response.body.c_str());
    if (!doc.IsObject()) {
        LOG(WARNING, "fail to parse response %s", response.body.c_str());
        return false;
    }
    if (doc.HasMember("code")
       && doc["code"].GetInt() == 0) {
        LOG(INFO, "submit cmd with job %s",doc["data"]["jobId"].GetString());
        boost::uuids::uuid uuid = boost::uuids::random_generator()();
        std::string id = boost::lexical_cast<std::string>(uuid); 
        *sessionid = id;
        ExecContext& context = exec_sessions_[id];
        context.hosts = hosts;
        context.jobid = doc["data"]["jobId"].GetString();
        context.sessionid = id;
        context.script = script;
        context.callback = callback;
        context.concurrency = concurrency;
        checker_.DelayTask(FLAGS_exec_job_check_interval, boost::bind(&MachineCtrl::CheckExecJob, this, id));
        return true;
    }else{
        LOG(WARNING, "submit cmd fails for %s", doc["msg"].GetString());
    }
    return false;
}

void MachineCtrl::CheckExecJob(const std::string& sessionid) {
    MutexLock lock(&mutex_);
    std::map<std::string, ExecContext>::iterator it = exec_sessions_.find(sessionid);
    if (it == exec_sessions_.end()) {
        LOG(WARNING, "sessionid %s does not exist ", sessionid.c_str());
        return;
    }
    ExecContext& context = it->second;
    LOG(INFO, "check job %s", context.jobid.c_str());
    HttpGetRequest request;
    request.url = ccs_http_server_ + "/job/query?jobId=" + context.jobid.c_str();
    HttpResponse response;
    bool ok = http_client_.Get(&request, &response);
    if (!ok) {
        LOG(WARNING, "fail get job %s task status from %s",it->second.jobid.c_str(), request.url.c_str());
        checker_.DelayTask(FLAGS_exec_job_check_interval, boost::bind(&MachineCtrl::CheckExecJob, this, sessionid));
        return;
    }
    rapidjson::Document doc;
    doc.Parse(response.body.c_str());
    if (!doc.IsObject()) {
        LOG(WARNING, "fail to parse response %s for request %s jobid %s", 
            response.body.c_str(),
            request.url.c_str(),
            context.jobid.c_str());
        // TODO error count
        checker_.DelayTask(FLAGS_exec_job_check_interval, boost::bind(&MachineCtrl::CheckExecJob, this, sessionid));
        return;
    }
    if (!doc.HasMember("code")
        || doc["code"].GetInt() != 0) {
        LOG(WARNING, "fail get job %s task status %s", context.jobid.c_str(), response.body.c_str());
        checker_.DelayTask(FLAGS_exec_job_check_interval, boost::bind(&MachineCtrl::CheckExecJob, this, sessionid));
        return;
    }
    if (!doc.HasMember("data")) {
        std::vector<std::string> place_holder;
        context.callback(sessionid, place_holder, place_holder);
        exec_sessions_.erase(sessionid);
    }
    if (doc["data"]["jobStatus"] == "FINISH"
       || doc["data"]["jobStatus"] == "CANCELED") {
        LOG(INFO, "job %s is completed with status %s", context.jobid.c_str(), doc["data"]["jobStatus"].GetString());
        std::vector<std::string> place_holder;
        context.callback(sessionid, context.hosts, place_holder);
        exec_sessions_.erase(sessionid);
    } else {
        LOG(INFO, "job %s is running with status %s", context.jobid.c_str(), doc["data"]["jobStatus"].GetString());
        checker_.DelayTask(FLAGS_exec_job_check_interval, boost::bind(&MachineCtrl::CheckExecJob, this, sessionid));
    }

}

bool MachineCtrl::GenerateTicket(std::string* ticket, 
                                 std::string* service) {
    if (ticket == NULL || service == NULL) {
        return false;
    }
    baas::CredentialGenerator generator = baas::EmptyCredentialGenerator();
    generator = baas::ClientUtility::Login();
    if (!generator.IsOK()) {
        LOG(WARNING, "fail to login error code %d, error msg %s", generator.ErrorCode(), generator.ErrorMsg().c_str());
        return false;
    }
    int ret = generator.GenerateCredential(ticket);
    if (ret != baas::sdk::BAAS_OK) {
        LOG(WARNING, "fail to GenerateTicket error code %d, error msg %d",
          ret,
          baas::sdk::GetReturnCodeMessage(ret).c_str());
        return false;
    }
    *service = generator.my_user();
    return true;
}

bool MachineCtrl::BuildJob(const std::string& script,
                           const std::vector<std::string>& hosts,
                           const std::string& account,
                           const std::string& service,
                           int32_t concurrency,
                           std::string* job) {
    if (job == NULL) {
        LOG(WARNING, "fail build job for job is NULL");
        return false;
    }
    rapidjson::StringBuffer sb;
    rapidjson::Writer<rapidjson::StringBuffer> writer(sb);
    writer.StartObject();
    writer.String("serviceType");
    writer.String(service.c_str());
    writer.String("creator");
    writer.String(service.c_str());
    writer.String("account");
    writer.String(account.c_str());
    writer.String("command");
    writer.String(script.c_str());
    writer.String("concurrency");
    writer.Int(concurrency);
    writer.String("hosts");
    writer.StartArray();
    for (size_t i = 0; i < hosts.size(); i++) {
        writer.String(hosts[i].c_str());
    }
    writer.EndArray();
    writer.EndObject();
    *job = sb.GetString();
    return true;
}

bool MachineCtrl::Reboot(const std::vector<std::string>& hosts,
                         const CallBack& callback) {
    MutexLock lock(&mutex_);
    std::string access_token;
    bool ok = GetRmsAccessToken(&access_token);
    if (!ok) {
        LOG(WARNING, "fail to get rms access token");
        return false;
    }
    LOG(INFO, "get access_token %s", access_token.c_str());
    HttpGetRequest request;
    request.headers.push_back("Authorization: RopAuth " + access_token);
    std::string query_str;
    ok = BuildRebootJob(hosts, &query_str);
    if (!ok) {
        LOG(WARNING, "fail build job form");
        return false;
    }
    LOG(INFO, "reboot query_str  %s", query_str.c_str());
    request.url = rms_http_server_ + "/v1/unified_list/selfReboot?" + query_str;
    HttpResponse response;
    ok = http_client_.Get(&request, &response);
    if (!ok) {
        LOG(WARNING, "fail to reboot for query_str %s", query_str.c_str());
        return false;
    }
    rapidjson::Document doc;
    doc.Parse(response.body.c_str());
    LOG(INFO, "reboot response %s", response.body.c_str());
    if (!doc.IsObject()) {
        LOG(WARNING, "fail to parse response %s", response.body.c_str());
        return false;
    }
    if (doc.HasMember("status") && doc["status"].GetInt() ==0) {
        LOG(INFO, "reboot %s successfully with id %s ", query_str.c_str(), doc["data"]["list_id"].GetString());
        return true;
    }
    return false;
}

bool MachineCtrl::BuildRebootJob(const std::vector<std::string>& hosts,
                                 std::string* query_str) {
    std::string machines;
    for (size_t i = 0; i < hosts.size(); i++) {
        if (i > 0) {
            machines.append(",");
        }
        machines.append(hosts[i]);
    }
    query_str->append("machines=");
    query_str->append(machines); 
    query_str->append("&skip_audit=1");
    return true;
}

bool MachineCtrl::GetRmsAccessToken(std::string* access_token) {
    HttpPostRequest request;
    request.url = rms_http_server_ + "/auth/accessToken";
    request.data.push_back(std::make_pair("app_key", FLAGS_rms_app_key));
    request.data.push_back(std::make_pair("secret_token", FLAGS_rms_token));
    request.data.push_back(std::make_pair("generate_type", "special_auth"));
    request.data.push_back(std::make_pair("user", FLAGS_rms_auth_user));
    HttpResponse response;
    bool ok = http_client_.Post(&request, &response);
    if (!ok) {
        LOG(WARNING, "fail to get rms access token");
        return false;
    }
    rapidjson::Document doc;
    doc.Parse(response.body.c_str());
    if (!doc.IsObject()) {
        LOG(WARNING, "fail to parse response %s", response.body.c_str());
        return false;
    }
    if (!doc.HasMember("access_token")) {
        LOG(WARNING, "fail get access token for response %s", response.body.c_str());
        return false;
    }
    *access_token = doc["access_token"].GetString();
    return true;
}

} // lumia
} // galaxy
