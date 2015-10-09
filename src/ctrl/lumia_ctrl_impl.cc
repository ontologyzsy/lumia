// Copyright (c) 2015, Baidu.com, Inc. All Rights Reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "ctrl/lumia_ctrl_impl.h"

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

LumiaCtrlImpl::LumiaCtrlImpl():checker_(4){
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
    LOG(INFO, "report dead minion %s for %s", request->ip().c_str(), request->reason().c_str());
    std::map<std::string, Minion>::iterator m_it = minions_.find(request->ip());
    if (m_it == minions_.end()) {
        LOG(WARNING, "minion with ip %s is not found", request->ip().c_str());
        response->set_status(kLumiaMinionNotFound);
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
    checker_.AddTask(boost::bind(&LumiaCtrlImpl::HandleDeadReport, this, request->ip()));
    response->set_status(kLumiaOk);
    done->Run();
}

void LumiaCtrlImpl::HandleDeadReport(const std::string& ip) {
    std::map<std::string, Minion>::iterator m_it = minions_.find(ip);
    std::map<std::string, std::string>::iterator sc_it = scripts_.find("minion-dead-check.sh");
    std::vector<std::string> hosts;
    hosts.push_back(m_it->second.hostname());
    std::string sessionid;
    bool ok = minion_ctrl_->Exec(sc_it->second, 
                                hosts,
                                "root",
                                1,
                                &sessionid,
                                boost::bind(&LumiaCtrlImpl::CheckDeadCallBack, this, _1, _2, _3));
    if (!ok) {
        LOG(WARNING, "fail to run dead check script on minion %s", m_it->second.hostname().c_str());
        return;
    }
}

void LumiaCtrlImpl::CheckDeadCallBack(const std::string& sessionid,
                                      const std::vector<std::string>& success,
                                      const std::vector<std::string>& fails) {

}

bool LumiaCtrlImpl::LoadMinion(const std::string& path) {
    LOG(INFO, "load minion dict %s", path.c_str());
    std::ifstream minions_is;
    minions_is.open(path.c_str(), std::ifstream::binary);
    char buffer[1024];
    std::stringstream ss;
    while(minions_is.good()) {
        minions_is.read(buffer, 1024);
        int32_t read_count = minions_is.gcount();
        ss.write(buffer, read_count);
    }
    LumiaMinions minions;
    minions.ParseFromString(ss.str());
    for (int i = 0; i < minions.minions_size(); i++) {
        minions_.insert(std::make_pair(minions.minions(i).ip(), minions.minions(i)));
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

}
}
