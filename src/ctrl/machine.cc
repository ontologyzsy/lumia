// Copyright (c) 2015, Baidu.com, Inc. All Rights Reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ctrl/machine.h"

#include "rapidjson/writer.h"
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "baas-lib-c/baas.h"
#include "logging.h"

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
                       int32_t concurrency) {
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
        return true;
    }else{
        LOG(WARNING, "submit cmd fails for %s", doc["msg"].GetString());
    }
    return false;
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

}
}
