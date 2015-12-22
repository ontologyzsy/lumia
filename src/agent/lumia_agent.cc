// Copyright (c) 2015, Baidu.com, Inc. All Rights Reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "agent/lumia_agent.h"

#include <sys/utsname.h>
#include "proto/lumia.pb.h"
#include <boost/lexical_cast.hpp>
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
DECLARE_int32(stat_check_period);
DECLARE_string(remove_galaxy_script);
DECLARE_double(max_cpu_usage);
DECLARE_double(max_mem_usage);
DECLARE_double(max_disk_r_bps);
DECLARE_double(max_disk_w_bps);
DECLARE_double(max_disk_r_rate);
DECLARE_double(max_disk_w_rate);
DECLARE_double(max_disk_util);
DECLARE_double(max_net_in_bps);
DECLARE_double(max_net_out_bps);
DECLARE_double(max_net_in_pps);
DECLARE_double(max_net_out_pps);
DECLARE_double(max_intr_rate);
DECLARE_double(max_soft_intr_rate);

namespace baidu {
namespace lumia {

static const uint64_t MIN_COLLECT_TIME = 4;

LumiaAgentImpl::LumiaAgentImpl():smartctl_(FLAGS_lumia_agent_smartctl_bin_path),
    pool_(4), lost_ping(4){
    rpc_client_ = new ::baidu::galaxy::RpcClient();
    stat_ = new SysStat();
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
        if (lines[i].find("/dev/sd", 0) != 0) {
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
   /* bool ok = ScanDevice(devices_);
    if (ok) {
        LOG(INFO, "scan devices successfully");
    } else {
        LOG(INFO, "fail to scan devices");
    }*/
    /*if (FLAGS_need_dev_check) {
        pool_.AddTask(boost::bind(&LumiaAgentImpl::DoCheck, this));
    }*/
    pool_.DelayTask(2000, boost::bind(&LumiaAgentImpl::KeepAlive, this)); 
    stat_pool_.AddTask(boost::bind(&LumiaAgentImpl::CollectSysStat, this));
    return true;
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
        if (++lost_ping > 10) {
            MinionStatus status;
            status.set_all_is_well(false);
            status.set_datetime(baidu::common::timer::get_micros());
            MutexLock lock(&mutex_);
            minion_status_.CopyFrom(status);
            //dorm
        }
        LOG(WARNING, "ping ctrl %s fails", ctrl_addr.c_str());
    } else {
        lost_ping = 0;
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

bool LumiaAgentImpl::GetGlobalMemStat(){
    mutex_.AssertHeld();
    FILE* fp = fopen("/proc/meminfo", "rb");
    if (fp == NULL) {
        return false;
    }
    std::string content;
 	char buf[1024];
 	int len = 0;
    while ((len = fread(buf, 1, sizeof(buf), fp)) > 0) {
        content.append(buf, len);
    }
    std::vector<std::string> lines;
    boost::split(lines, content, boost::is_any_of("\n"));
    int64_t total_mem = 0;
    int64_t free_mem = 0;
    int64_t buffer_mem = 0;
    int64_t cache_mem = 0;
    int64_t tmpfs_mem = 0;
    for (size_t i = 0; i < lines.size(); i++) {
        std::string line = lines[i];
        std::vector<std::string> parts;
        if (line.find("MemTotal:") == 0) {
            boost::split(parts, line, boost::is_any_of(" "));
            if (parts.size() < 2) {
                fclose(fp);
                return false;
            }
            total_mem = boost::lexical_cast<int64_t>(parts[parts.size() - 2]);
        }else if (line.find("MemFree:") == 0) {
            boost::split(parts, line, boost::is_any_of(" "));
            if (parts.size() < 2) {
                fclose(fp);
                return false;
            }
            free_mem = boost::lexical_cast<int64_t>(parts[parts.size() - 2]);
        }else if (line.find("Buffers:") == 0) {
            boost::split(parts, line, boost::is_any_of(" "));
            if (parts.size() < 2) {
                fclose(fp);
                return false;
            }
            buffer_mem = boost::lexical_cast<int64_t>(parts[parts.size() - 2]);
        }else if (line.find("Cached:") == 0) {
            boost::split(parts, line, boost::is_any_of(" "));
            if (parts.size() < 2) {
                fclose(fp);
                return false;
            }
            cache_mem = boost::lexical_cast<int64_t>(parts[parts.size() - 2]);
        }
    }
    fclose(fp);
    
    std::stringstream ss;
    int exit_code = -1;
    bool ok = SyncExec("df -h", ss, &exit_code);
    if (ok && exit_code == 0) {
        std::vector<std::string> lines;
        std::string content = ss.str();
        boost::split(lines, content, boost::is_any_of("\n"));
        for (size_t n = 0; n < lines.size(); n++) {
            std::string line = lines[n];
            if (line.find("tmpfs")) {
                std::vector<std::string> parts;
                boost::split(parts, line, boost::is_any_of(" "));
                tmpfs_mem = boost::lexical_cast<int64_t>(parts[1]);
                LOG(WARNING, "detect tmpfs %s %d", parts[1].c_str(), tmpfs_mem);
                tmpfs_mem = tmpfs_mem * 1024 * 1024 * 1024;
                break;
            } else {
                continue;
            }
        }
    } else {
        LOG(WARNING, "exec df fail err_code %d", exit_code);
    }
    stat_->mem_used_ = (total_mem - free_mem - buffer_mem - cache_mem + tmpfs_mem) / boost::lexical_cast<double>(total_mem);
 
    return true;
}

bool LumiaAgentImpl::GetGlobalIntrStat() {
    mutex_.AssertHeld();
    uint64_t intr_cnt = 0;
    uint64_t softintr_cnt = 0;
    std::string path = "/proc/stat";
    std::ifstream stat(path.c_str());
    if (!stat.is_open()) {
        LOG(WARNING, "open proc stat fail.");
        return false;
    }
    
    std::vector<std::string> lines;
    std::string content; 
    stat >> content;
    boost::split(lines, content, boost::is_any_of("\n"));
    for (size_t n = 0; n < lines.size(); n++) {
        std::string line = lines[n];
        if (line.find("intr")) {
            std::vector<std::string> parts;
            boost::split(parts, line, boost::is_any_of(" "));
            intr_cnt = boost::lexical_cast<int64_t>(parts[1]);
        } else if (line.find("softirq")) {
            std::vector<std::string> parts;
            boost::split(parts, line, boost::is_any_of(" "));
            softintr_cnt = boost::lexical_cast<int64_t>(parts[1]);
        }
        continue;
    }
    stat_->last_stat_ = stat_->cur_stat_;
    stat_->cur_stat_.interupt_times = intr_cnt;
    stat_->cur_stat_.soft_interupt_times = softintr_cnt;
    stat_->intr_rate_ = (stat_->cur_stat_.interupt_times - stat_->last_stat_.interupt_times) / FLAGS_stat_check_period * 1000; 
    stat_->soft_intr_rate_ = (stat_->cur_stat_.soft_interupt_times - stat_->last_stat_.soft_interupt_times) / FLAGS_stat_check_period * 1000;
    return true;
}

bool LumiaAgentImpl::GetGlobalIOStat() {
    mutex_.AssertHeld();
    std::string cmd = "iostat -x";
    std::stringstream ss;
    std::string content = ss.str();
    int exit_code = -1;
    bool ok = SyncExec(cmd, ss, &exit_code);
    if (ok && exit_code == 0) {
        std::vector<std::string> lines;
        boost::split(lines, content, boost::is_any_of("\n"));
        for (size_t n = 0; n < lines.size(); n++) {
            std::string line = lines[n];
            if (line.find("sda")) {
                std::vector<std::string> parts;
                boost::split(parts, line, boost::is_any_of(" "));
                stat_->disk_read_times_ = boost::lexical_cast<double>(parts[3]);
                stat_->disk_write_times_ = boost::lexical_cast<double>(parts[4]);
                stat_->disk_read_Bps_ = boost::lexical_cast<double>(parts[5]);
                stat_->disk_write_Bps_ = boost::lexical_cast<double>(parts[6]);
                stat_->disk_io_util_ = boost::lexical_cast<double>(parts[lines.size() - 1]);
                break;
            } else {
                continue;
            }
        }
    }else {
        LOG(WARNING, "exec df fail err_code %d", exit_code);
    }
    return true;
}

bool LumiaAgentImpl::GetGlobalNetStat() {
    mutex_.AssertHeld();
    std::string path = "/proc/net/dev";
    std::ifstream stat(path.c_str());
    if (!stat.is_open()) {
        LOG(WARNING, "open dev stat fail.");
        return false;
    }
    std::string content;
    stat >> content;
    stat_->last_stat_ = stat_->cur_stat_;
    std::vector<std::string> lines;
    boost::split(lines, content, boost::is_any_of("\n"));
    for (size_t n = 0; n < lines.size(); n++) {
        std::string line = lines[n];
        if (line.find("eth0") || line.find("xgbe0")) {
            std::vector<std::string> parts;
            boost::split(parts, line, boost::is_any_of(" "));
            std::vector<std::string> tokens;
            boost::split(tokens, parts[0], boost::is_any_of(":"));
            stat_->cur_stat_.net_in_bits = boost::lexical_cast<int64_t>(tokens[1]);
            stat_->cur_stat_.net_in_packets = boost::lexical_cast<int64_t>(parts[1]);
            stat_->cur_stat_.net_out_bits = boost::lexical_cast<int64_t>(tokens[8]);
            stat_->cur_stat_.net_out_packets = boost::lexical_cast<int64_t>(parts[9]);
        }
        continue;
    }
    stat_->net_in_bps_ = (stat_->cur_stat_.net_in_bits - stat_->last_stat_.net_in_bits) / FLAGS_stat_check_period * 1000;
    stat_->net_out_bps_ = (stat_->cur_stat_.net_out_bits - stat_->last_stat_.net_out_bits) / FLAGS_stat_check_period * 1000;
    stat_->net_in_pps_ = (stat_->cur_stat_.net_in_packets - stat_->last_stat_.net_in_packets) / FLAGS_stat_check_period * 1000;
    stat_->net_out_pps_ = (stat_->cur_stat_.net_out_packets - stat_->last_stat_.net_out_packets) / FLAGS_stat_check_period * 1000;
    return true;
}

bool LumiaAgentImpl::GetGlobalCpuStat() {
    mutex_.AssertHeld();
    ResourceStatistics statistics;
    std::string path = "/proc/stat";
    FILE* fin = fopen(path.c_str(), "r");
    if (fin == NULL) {
        LOG(WARNING, "open %s failed", path.c_str());
        return false; 
    }

    ssize_t read;
    size_t len = 0;
    char* line = NULL;
    if ((read = getline(&line, &len, fin)) == -1) {
        LOG(WARNING, "read line failed err[%d: %s]", 
                errno, strerror(errno)); 
        fclose(fin);
        return false;
    }
    fclose(fin);

    char cpu[5];
    int item_size = sscanf(line, 
                           "%s %ld %ld %ld %ld %ld %ld %ld %ld %ld", 
                           cpu,
                           &(statistics.cpu_user_time),
                           &(statistics.cpu_nice_time),
                           &(statistics.cpu_system_time),
                           &(statistics.cpu_idle_time),
                           &(statistics.cpu_iowait_time),
                           &(statistics.cpu_irq_time),
                           &(statistics.cpu_softirq_time),
                           &(statistics.cpu_stealstolen),
                           &(statistics.cpu_guest)); 

    free(line); 
    line = NULL;
    if (item_size != 10) {
        LOG(WARNING, "read from /proc/stat format err"); 
        return false;
    }
    stat_->last_stat_ = stat_->cur_stat_;
    stat_->cur_stat_ = statistics;
    long total_cpu_time_last = 
    stat_->last_stat_.cpu_user_time
    + stat_->last_stat_.cpu_nice_time
    + stat_->last_stat_.cpu_system_time
    + stat_->last_stat_.cpu_idle_time
    + stat_->last_stat_.cpu_iowait_time
    + stat_->last_stat_.cpu_irq_time
    + stat_->last_stat_.cpu_softirq_time
    + stat_->last_stat_.cpu_stealstolen
    + stat_->last_stat_.cpu_guest;
    long total_cpu_time_cur =
    stat_->cur_stat_.cpu_user_time
    + stat_->cur_stat_.cpu_nice_time
    + stat_->cur_stat_.cpu_system_time
    + stat_->cur_stat_.cpu_idle_time
    + stat_->cur_stat_.cpu_iowait_time
    + stat_->cur_stat_.cpu_irq_time
    + stat_->cur_stat_.cpu_softirq_time
    + stat_->cur_stat_.cpu_stealstolen
    + stat_->cur_stat_.cpu_guest;
    long total_cpu_time = total_cpu_time_cur - total_cpu_time_last;
    if (total_cpu_time < 0) {
        LOG(WARNING, "invalide total cpu time cur %ld last %ld", total_cpu_time_cur, total_cpu_time_last);
        return false;
    }     

    long total_used_time_last = 
    stat_->last_stat_.cpu_user_time 
    + stat_->last_stat_.cpu_system_time
    + stat_->last_stat_.cpu_nice_time
    + stat_->last_stat_.cpu_irq_time
    + stat_->last_stat_.cpu_softirq_time
    + stat_->last_stat_.cpu_stealstolen
    + stat_->last_stat_.cpu_guest;

    long total_used_time_cur =
    stat_->cur_stat_.cpu_user_time
    + stat_->cur_stat_.cpu_nice_time
    + stat_->cur_stat_.cpu_system_time
    + stat_->cur_stat_.cpu_irq_time
    + stat_->cur_stat_.cpu_softirq_time
    + stat_->cur_stat_.cpu_stealstolen
    + stat_->cur_stat_.cpu_guest;
    long total_cpu_used_time = total_used_time_cur - total_used_time_last;
    if (total_cpu_used_time < 0)  {
        LOG(WARNING, "invalude total cpu used time cur %ld last %ld", total_used_time_cur, total_used_time_last);
        return false;
    }
    double rs = total_cpu_used_time / static_cast<double>(total_cpu_time);
    stat_->cpu_used_ = rs;
    return true;
}

void LumiaAgentImpl::CollectSysStat() {
    MutexLock lock(&mutex_);
    do {
        LOG(INFO, "start collect sys stat");
        ResourceStatistics tmp_statistics;
        bool ok = GetGlobalCpuStat();
        if (!ok) {
            LOG(WARNING, "fail to get cpu usage");
            break;
        }
        ok = GetGlobalMemStat();
        if (!ok) {
            LOG(WARNING, "fail to get mem usage");
            break;
        }
        ok = GetGlobalIntrStat();
        if (!ok) {
            LOG(WARNING, "fail to get interupt usage");
            break;
        }
        ok = GetGlobalIOStat();
        if (!ok) {
            LOG(WARNING, "fail to get IO usage");
            break;
        }
        ok = GetGlobalNetStat();
        if (!ok) {
            LOG(WARNING, "fail to get Net usage");
            break;
        }
        stat_->collect_times_++;
        if (stat_->collect_times_ < MIN_COLLECT_TIME) {
            LOG(WARNING, "collect times not reach %d", MIN_COLLECT_TIME);
            break;
        }
        if (CheckSysHealth()) {
            break;
        } else {
            LOG(WARNING, "sys too busy, prepare to remove galaxy ");
            std::string cmd = FLAGS_remove_galaxy_script;
            std::stringstream ss;
            int exit_code = -1;
            SyncExec(cmd, ss, &exit_code);
            LOG(WARNING, "remove galaxy with exit code %d", exit_code);
        }
    } while(0); 
    stat_pool_.DelayTask(FLAGS_stat_check_period, boost::bind(&LumiaAgentImpl::CollectSysStat, this));
    return;
}

bool LumiaAgentImpl::CheckSysHealth() {
    if (fabs(FLAGS_max_cpu_usage) <= 1e-6 && stat_->cpu_used_ > FLAGS_max_cpu_usage) {
        LOG(WARNING, "cpu uage %f reach threshold %f", stat_->cpu_used_, FLAGS_max_cpu_usage);
        return false;
    }
    if (fabs(FLAGS_max_mem_usage) <= 1e-6 && stat_->mem_used_ > FLAGS_max_mem_usage) {
        LOG(WARNING, "mem usage %f reach threshold %f", stat_->mem_used_, FLAGS_max_mem_usage);
        return false;
    }
    if (fabs(FLAGS_max_disk_r_bps) <= 1e-6 && stat_->disk_read_Bps_ > FLAGS_max_disk_r_bps) {
        LOG(WARNING, "disk read Bps %f reach threshold %f", stat_->disk_read_Bps_, FLAGS_max_disk_r_bps);
        return false;
    }
    if (fabs(FLAGS_max_disk_w_bps) <= 1e-6 && stat_->disk_write_Bps_ > FLAGS_max_disk_w_bps) {
        LOG(WARNING, "disk write Bps %f reach threshold %f", stat_->disk_write_Bps_, FLAGS_max_disk_w_bps);
        return false;
    }
    if (fabs(FLAGS_max_disk_r_rate) <= 1e-6 && stat_->disk_read_times_ > FLAGS_max_disk_r_rate) {
        LOG(WARNING, "disk write rate %f reach threshold %f", stat_->disk_read_times_, FLAGS_max_disk_r_rate);
        return false;
    }
    if (fabs(FLAGS_max_disk_w_rate) <= 1e-6 && stat_->disk_write_times_ > FLAGS_max_disk_w_rate) {
        LOG(WARNING, "disk write rate %f reach threshold %f", stat_->disk_write_times_, FLAGS_max_disk_w_rate);
        return false;
    }
    if (fabs(FLAGS_max_disk_util) <= 1e-6 && stat_->disk_io_util_ > FLAGS_max_disk_util) {
        LOG(WARNING, "disk io util %f reach threshold %f", stat_->disk_io_util_, FLAGS_max_disk_util);
        return false;
    }
    if (fabs(FLAGS_max_net_in_bps) <= 1e-6 != 0 && stat_->net_in_bps_ > FLAGS_max_net_in_bps) {
        LOG(WARNING, "net in bps %f reach threshold %f", stat_->net_in_bps_, FLAGS_max_net_in_bps);
        return false;
    }
    if (fabs(FLAGS_max_net_out_bps) <= 1e-6 && stat_->net_out_bps_ > FLAGS_max_net_out_bps) {
        LOG(WARNING, "net out bps %f reach threshold %f", stat_->net_out_bps_, FLAGS_max_net_out_bps);
        return false;
    }
    if (fabs(FLAGS_max_net_in_pps) <= 1e-6 && stat_->net_in_pps_ > FLAGS_max_net_in_pps) {
        LOG(WARNING, "net in pps %f reach threshold %f", stat_->net_in_bps_, FLAGS_max_net_in_pps);
        return false;
    }
    if (fabs(FLAGS_max_net_out_pps) <= 1e-6 && stat_->net_out_pps_ > FLAGS_max_net_out_pps) {
        LOG(WARNING, "net out pps %f reach threshold %f", stat_->net_out_pps_, FLAGS_max_net_out_pps);
        return false;
    }
    if (fabs(FLAGS_max_intr_rate) <= 1e-6  && stat_->intr_rate_ > FLAGS_max_intr_rate) {
        LOG(WARNING, "interupt rate %f reach threshold %f", stat_->intr_rate_, FLAGS_max_intr_rate);
        return false;
    }
    if (fabs(FLAGS_max_soft_intr_rate) <= 1e-6 && stat_->soft_intr_rate_ > FLAGS_max_soft_intr_rate) {
        LOG(WARNING, "soft interupt rate %f reach threshold %f", stat_->soft_intr_rate_, FLAGS_max_soft_intr_rate);
        return false;
    }
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
        if (exit_code == 0 && (index != std::string::npos 
                    || ss.str().find("OK") != std::string::npos)) {
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
        if (lines[i].find("/dev/sd", 0) != 0) {
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

void LumiaAgentImpl::Exec(::google::protobuf::RpcController* /*controller*/,
                          const ::baidu::lumia::ExecRequest* request,
                          ::baidu::lumia::ExecResponse* response,
                          ::google::protobuf::Closure* done) {
    std::string cmd = request->cmd();
    std::vector<std::string> lines;
    boost::split(lines, request->cmd(), boost::is_any_of(" "));
    std::string script_dir = "./" + lines[0];
    std::ofstream script(script_dir.c_str(), std::ios::ate);
    if (!script.is_open()) {
        LOG(WARNING, "create script %s fail", script_dir.c_str());
        response->set_status(-1);
        response->set_ip(FLAGS_lumia_agent_ip);
        done->Run();
        return;
    }
    script << request->script();
    script.close();
    if (0 != chmod(script_dir.c_str(), S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH)) {
        LOG(WARNING, "chmod fail");
        response->set_status(-2);
        response->set_ip(FLAGS_lumia_agent_ip);
        done->Run();
        return;
    }
    std::stringstream ss;
    int exit_code = -1;
    bool ok = SyncExec(cmd, ss, &exit_code);
    if (ok && exit_code == 0) {
        response->set_status(0);
    } else {
        response->set_status(exit_code);
        LOG(WARNING, "exec %s fail err_code %d", cmd.c_str(), exit_code);
    }
    response->set_ip(FLAGS_lumia_agent_ip);
    done->Run();
}   
#if 0
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
#endif

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

