// Copyright (c) 2015, Baidu.com, Inc. All Rights Reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "agent/lumia_agent.h"

#include <sys/utsname.h>
#include "proto/lumia.pb.h"
#include <boost/algorithm/string.hpp>
#include <boost/bind.hpp>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include "logging.h"
#include "timer.h"
#include <gflags/gflags.h>
#include <sstream>
#include <fstream>

DECLARE_string(lumia_agent_smartctl_bin_path);
DECLARE_string(lumia_ctrl_host);
DECLARE_string(lumia_ctrl_port);

DECLARE_string(lumia_agent_ip);
DECLARE_string(lumia_agent_port);
DECLARE_string(galaxy_script);
DECLARE_string(galaxy_script_hybrid);
DECLARE_string(galaxy_remove_script);
DECLARE_string(galaxy_ftp_path);

namespace baidu {
namespace lumia {

LumiaAgentImpl::LumiaAgentImpl():smartctl_(FLAGS_lumia_agent_smartctl_bin_path),
    pool_(4){
    rpc_client_ = new ::baidu::galaxy::RpcClient();
}

LumiaAgentImpl::~LumiaAgentImpl(){}

//TODO sync exec
bool LumiaAgentImpl::ScanDevice(std::vector<std::string>& devices) {
    std::string cmd = smartctl_ + " --scan";
    std::stringstream ss;
    int exit_code = -1;
    bool ok = SyncExec(cmd, ss, &exit_code);
    if (ok && exit_code == 0) {
        return ParseScanDevice(ss.str(), devices);
    }
    return false;
}

bool LumiaAgentImpl::ParseScanDevice(const std::string& output,
                                 std::vector<std::string>& devices) {
    LOG(INFO, "parse scan result %s ", output.c_str());
    std::vector<std::string> lines;
    boost::split(lines, output, boost::is_any_of("\n"));
    for (size_t i = 0; i < lines.size(); i++) {
        if (lines[i].find_first_of("/dev/sd", 0) != 0) {
            continue;
        }
        std::vector<std::string> parts;
        boost::split(parts, lines[i], boost::is_any_of(" "));
        devices.push_back(parts[0]);
        LOG(INFO, "add device %s", parts[0].c_str());
    }
    return true;
}

bool LumiaAgentImpl::Init() {
    bool ok = ScanDevice(devices_);
    if (ok) {
        LOG(INFO, "scan devices successfully");
    } else {
        LOG(INFO, "fail to scan devices");
    }
    pool_.AddTask(boost::bind(&LumiaAgentImpl::DoCheck, this));
    pool_.DelayTask(2000, boost::bind(&LumiaAgentImpl::KeepAlive, this));
    return ok;
}

void LumiaAgentImpl::DoCheck() {
    MinionStatus status;
    status.set_all_is_well(true);
    std::vector<std::string>::iterator it = devices_.begin();
    for (; it != devices_.end(); ++it) {
        bool ok = false;
        bool ret = CheckDevice(*it, &ok);
        DeviceStatus* dev_status = status.add_devices();
        dev_status->set_name(*it);
        if (ret && ok) {
            LOG(INFO ,"device %s is ok", (*it).c_str());
            dev_status->set_healthy(true);
        }else {
            LOG(INFO, "device %s is error", (*it).c_str());
            dev_status->set_healthy(false);
            status.set_all_is_well(false);
        }
    }
    bool all_mounted = false;
    bool ret = CheckMounts(&all_mounted, status);
    if (ret && all_mounted) {
        LOG(INFO, "all devices are mounted");
    }else {
        LOG(INFO, "some devices are unmounted");
        status.set_all_is_well(false);
    }
    status.set_datetime(baidu::common::timer::get_micros());
    MutexLock lock(&mutex_);
    minion_status_.CopyFrom(status);
    pool_.DelayTask(10000, boost::bind(&LumiaAgentImpl::DoCheck, this));
}

void LumiaAgentImpl::KeepAlive() {
    LumiaCtrl_Stub* lumia_;
    std::string ctrl_addr = FLAGS_lumia_ctrl_host + ":" + FLAGS_lumia_ctrl_port;
    rpc_client_->GetStub(ctrl_addr, &lumia_);
    PingRequest request;
    request.set_node_addr(FLAGS_lumia_agent_ip + ":" + FLAGS_lumia_agent_port);
    PingResponse response;
    bool ok = rpc_client_->SendRequest(lumia_, &LumiaCtrl_Stub::Ping, 
                        &request,
                        &response,
                        5, 1);
    if (!ok) {
        LOG(WARNING, "ping ctrl %s fails", ctrl_addr.c_str());
    }
    pool_.DelayTask(2000, boost::bind(&LumiaAgentImpl::KeepAlive, this));
}

bool LumiaAgentImpl::SyncExec(const std::string& cmd,
                          std::stringstream& output,
                          int* exit_code) {
    int pipe_fd[2];
    int ok = pipe(pipe_fd);
    if (ok != 0) { 
        LOG(WARNING, "fail to create pipe exec");
        return false;
    }
    bool ret = false;
    int pid = fork();
    if (pid == 0) {
        dup2(pipe_fd[1], STDOUT_FILENO);
        close(pipe_fd[0]);
        char* argv[] = {
            const_cast<char*>("sh"),
            const_cast<char*>("-c"),
            const_cast<char*>(cmd.c_str()),
            NULL};
        char* env[] = {NULL};
        ::execve("/bin/sh", argv, env);
        assert(0);
    } else {
        int status = 0;
        close(pipe_fd[1]);
        //TODO handle hang problem
        int ok = waitpid(-1, &status, 0);
        if (ok == pid && WIFEXITED(status)) {
            if (WEXITSTATUS(status) != 0) { 
                ret = true;
                *exit_code = WEXITSTATUS(status);
            }else{
                LOG(INFO, "exec cmd %s successfully", cmd.c_str());
                char buffer[1024];
                while (true) {
                    int count = read(pipe_fd[0], buffer, 1024);
                    if (count == 0) {
                        break;
                    }
                    if (count <= -1) {
                        LOG(WARNING, "fail to read data");
                        break;
                    }
                    output.write(buffer, count);
                }
                ret = true;
                *exit_code = 0;
            }
        } else {
            ret = false;
            LOG(WARNING, "fail to exec cmd %s", cmd.c_str());
        }
    }
    close(pipe_fd[0]);
    return ret;
}

bool LumiaAgentImpl::ReadFile(const std::string& path,
                          std::stringstream& content) {
    std::ifstream is;
    is.open(path.c_str(), std::ifstream::binary);
    char buffer[1024];
    while (is.good()) {
        is.read(buffer, 1024);
        content.write(buffer, is.gcount());
    }
    is.close();
    return true;
}

bool LumiaAgentImpl::CheckDevice(const std::string& devices, bool* ok) {
    std::string cmd = smartctl_ + " -H " + devices;
    std::stringstream ss;
    int exit_code = -1;
    bool ret = SyncExec(cmd, ss, &exit_code);
    LOG(INFO, "check device %s output %s with exit code %d", 
         devices.c_str(),
         ss.str().c_str(),
         exit_code);
    if (ret) {
        // self check passed
        std::size_t index = ss.str().find("PASSED");
        if (exit_code == 0 && index != std::string::npos) {
            *ok = true;
        }else {
            *ok = false;
        }
        return true;
    }
    return false;
}

bool LumiaAgentImpl::CheckMounts(bool* all_mounted, MinionStatus& status) {
    std::stringstream ss;
    bool ok = ReadFile("/etc/fstab", ss);
    if (!ok) {
        LOG(WARNING, "read etc/fstab fails");
        return false;
    }
    MountContainer fstab;
    ParseTab(ss.str(), fstab);
    std::stringstream mss;
    ok = ReadFile("/etc/mtab", mss);
    if (!ok) {
        LOG(WARNING, "read etc/mtab fails");
        return false;
    }
    MountContainer mtab;
    ParseTab(mss.str(), mtab);
    MountContainer::iterator it = fstab.begin();
    *all_mounted = true;
    for (; it !=  fstab.end(); ++it) {
        MountStatus* m_status = status.add_mounts();
        m_status->set_mounted(true);
        m_status->set_dev(it->second.device);
        m_status->set_mount_point(it->second.mount_point);
        if (mtab.find(it->first) == mtab.end()) {
            *all_mounted = false;
            m_status->set_mounted(false);
            LOG(WARNING, "device %s umounted", it->first.c_str());
            continue;
        }
        LOG(INFO, "device %s mounted to %s", it->first.c_str(), it->second.mount_point.c_str());
    }
    return true;
}

bool LumiaAgentImpl::ParseTab(const std::string& content,
                            MountContainer& container) {
    std::vector<std::string> lines;
    boost::split(lines, content, boost::is_any_of("\n"));
    for (size_t i = 0; i < lines.size(); i++) {
        if (lines[i].find_first_of("/dev/sd", 0) != 0) {
            continue;
        }
        std::vector<std::string> parts;
        boost::split(parts, lines[i], boost::is_any_of(" "));
        if (parts.size() < 3) {
            LOG(INFO, "invalide mount point %s", lines[i].c_str());
            return false;
        }
        LOG(INFO, "%s", lines[i].c_str());
        MountInfo info;
        info.device = parts[0];
        for (size_t j = 1; j < parts.size(); j++) {
            if (parts[j].empty()) {
                continue;
            }
            info.mount_point = parts[j];
            if ((j + 1) < parts.size()) {
                info.type = parts[j+1];
            }
            break;
        }
        container.insert(std::make_pair(info.device, info));
    }
    return true;
}

void LumiaAgentImpl::Query(::google::protobuf::RpcController* /*controller*/,
                           const ::baidu::lumia::QueryAgentRequest* /*request*/,
                           ::baidu::lumia::QueryAgentResponse* response,
                           ::google::protobuf::Closure* done) {
    MutexLock lock(&mutex_);
    response->mutable_minion_status()->CopyFrom(minion_status_);
    response->set_ip(FLAGS_lumia_agent_ip);
    response->set_status(0);
    done->Run();
}

void LumiaAgentImpl::InitGalaxyEnv(::google::protobuf::RpcController* /*controller*/,
                   const ::baidu::lumia::InitGalaxyEnvRequest* request,
                   ::baidu::lumia::InitGalaxyEnvResponse* response,
                   ::google::protobuf::Closure* done) {
    std::string cmd;
    int check_err = 0;
    if (request->hybrid()) {
        cmd = FLAGS_galaxy_script_hybrid;
    } else {
        cmd = FLAGS_galaxy_script;
    }
    if (request->has_data_center()) {
        cmd = cmd +  " -data_center" + " " + request->data_center();
    } 
    if (request->has_disk_size()) {
        std::stringstream ss;
        ss << request->disk_size();
        cmd = cmd + " -disk_size" + " " + ss.str();
    } else {
        check_err = 255;
    } 
    if (request->has_bandwidth()) {
        std::stringstream ss;
        ss << request->bandwidth();
        cmd = cmd + " -bandwidth" + " " + ss.str();
    } else {
        check_err = 255;
    }
    cmd = cmd + " -ftp_path" + " " + FLAGS_galaxy_ftp_path;
    if (check_err != 0) {
        response->set_status(check_err);
    } else {
        std::stringstream ss;
        int exit_code = -1;
        bool ok = SyncExec(cmd, ss, &exit_code);
        if (ok && exit_code == 0) {
            response->set_status(0);
        } else {
            response->set_status(exit_code);
        }
    }
    response->set_ip(FLAGS_lumia_agent_ip);
    done->Run();
    return;
}

void LumiaAgentImpl::RemoveGalaxyEnv(::google::protobuf::RpcController* /*controller*/,
                                     const ::baidu::lumia::RemoveGalaxyEnvRequest* request,
                                     ::baidu::lumia::RemoveGalaxyEnvResponse* response,
                                     ::google::protobuf::Closure* done)  {

    std::string cmd;
#if 0
    if (request->hybrid()) {
        cmd += "remove_galaxy_agent_hybrid.sh";
    } else {
        cmd += "remove_galaxy_agent.sh";
    }
#endif
    cmd = FLAGS_galaxy_remove_script;
    std::stringstream ss;
    int exit_code = -1;
    bool ok = SyncExec(cmd, ss, &exit_code);
    if (ok && exit_code == 0) {
        response->set_status(0);
    } else {
        response->set_status(exit_code);
    }
    response->set_ip(FLAGS_lumia_agent_ip);
    done->Run();
    return;
}

std::string LumiaAgentImpl::GetHostName(){
    std::string hostname = "";
    struct utsname buf;
    if (0 != uname(&buf)) {
        *buf.nodename = '\0';
    }
    hostname = buf.nodename;
    return hostname; 
}

}
}

