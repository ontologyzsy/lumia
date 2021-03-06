// Copyright (c) 2015, Baidu.com, Inc. All Rights Reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include <gflags/gflags.h>

DEFINE_string(nexus_root_path, "/baidu/galaxy", "root path of nexus");
DEFINE_string(master_path, "/master", "master path on nexus");
DEFINE_string(data_center, "", "cluster data center");
DEFINE_string(lumia_ctrl_port, "8080", "lumia controller port");
DEFINE_string(lumia_ctrl_host, "0.0.0.0", "lumia controller host");
DEFINE_string(minion_dict, "dict/minion.pb.dict", "minion dict");
DEFINE_string(scripts_dir, "scripts", "scripts dir");

DEFINE_string(rms_api_http_host, "xxx", "rms api http host");
DEFINE_string(ccs_api_http_host, "xxx", "ccs api http host");

DEFINE_string(rms_api_check_job, "xxx", "rms check job status api");
DEFINE_string(rms_token, "b4631c7cb697d9ee7080126b18dd09abcfad79eb", "rms token");
DEFINE_string(rms_app_key, "112", "rms app key");
DEFINE_string(rms_auth_user, "wangtaize", "rms auth user");
DEFINE_int32(exec_job_check_interval, 2000, "exec job check interval");

DEFINE_string(nexus_servers, "", "server list of nexus, e.g abc.com:1234,def.com:5342");

DEFINE_string(lumia_root_path, "/baidu/lumia", "root path of lumia cluster on nexus, e.g /baidu/lumia");

DEFINE_string(lumia_lock, "/lock", "root path of lumia lock on nexus, e.g /lock");
DEFINE_string(lumia_main, "/main", "the path of lumia main ");
DEFINE_string(lumia_minion, "/minion", "the path of lumia minion ");
DEFINE_string(lumia_script, "/script", "the path of lumia script ");

DEFINE_string(lumia_agent_smartctl_bin_path, "./bin/smartctl", "the path of smartctl path");
DEFINE_string(lumia_agent_port, "8123", "the port of lumia agent");
DEFINE_string(lumia_agent_ip, "127.0.0.1", "the ip of lumia agent");
DEFINE_string(galaxy_script_hybrid, "./scripts/deploy_galaxy_agent_hunbu.sh", "deploy hybrid galaxy script");
DEFINE_string(galaxy_script, "./scripts/deploy_galaxy_agent.sh", "deploy galaxy script");
DEFINE_string(galaxy_remove_script, "./scripts/remove_galaxy.sh", "remove galaxy script");
DEFINE_string(galaxy_ftp_path, "ftp://yq01-tera1.yq01.baidu.com/tmp/galaxy.tar.gz", "galaxy ftp path");
DEFINE_int32(galaxy_cpu_use_percent, 100, "galaxy use cpu percent");
DEFINE_int32(galaxy_mem_use_percent, 100, "galaxy use mem percent");
DEFINE_int64(galaxy_disk_usage, 0, "galaxy disk usage (G)");
DEFINE_int64(galaxy_bandwidth_usage, 0, "galaxy bandwidth usage (Bps)");
//DEFINE_int32(galaxy_cpu_max_percent, 80, "galaxy cpu max percent");
//DEFINE_int32(galaxy_mem_max_percent, 80, "galaxy mem max percent");
DEFINE_int32(stat_check_period, 1000, "agent check health period(ms)");
DEFINE_string(remove_galaxy_script, "scripts/remove_galaxy.sh", "remove galaxy script");
DEFINE_double(max_cpu_usage, 0.8, "max cpu usage");
DEFINE_double(max_mem_usage, 0.8, "max mem usage");
DEFINE_double(max_disk_r_bps, 0.0, "max disk read bytes per second");
DEFINE_double(max_disk_w_bps, 0.0, "max disk write bytes per second");
DEFINE_double(max_disk_r_rate, 0.0, "max disk read times per second");
DEFINE_double(max_disk_w_rate, 0.0, "max disk read times per second");
DEFINE_double(max_disk_util, 0.0, "max disk io util");
DEFINE_double(max_net_in_bps, 0.0, "max net in bits per second");
DEFINE_double(max_net_out_bps, 0.0, "max net out bits per second");
DEFINE_double(max_net_in_pps, 0.0, "max net in packets per second");
DEFINE_double(max_net_out_pps, 0.0, "max net out packets per second");
DEFINE_double(max_intr_rate, 0.0, "max interupt times per second");
DEFINE_double(max_soft_intr_rate, 0.0, "max soft interupt times per second");
