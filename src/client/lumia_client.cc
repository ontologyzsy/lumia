// Copyright (c) 2015, Baidu.com, Inc. All Rights Reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gflags/gflags.h>
#include <stdio.h>
#include "sdk/lumia.h"
#include <tprinter.h>
#include <string_util.h>

DEFINE_string(i, "", "Report dead minion with specify ip address");
DEFINE_string(lumia_ctrl_host, "", "Lumia server host");
DEFINE_string(lumia_ctrl_port, "", "Lumia server port");
DECLARE_string(flagfile);

const std::string kLumiaUsage = "lumia client.\n"
                                 "Usage:\n"
                                 "    lumia report -i <minion ip>\n"
                                 "    lumia show -i <minion ip>\n"
                                 "Options:\n"
                                 "    -i ip    Report dead minion with specify ip address\n";

int ReportDeadMinion() {
    if (FLAGS_i.empty()) {
        fprintf(stderr, "-i is required\n");
        return -1;
    }
    ::baidu::lumia::LumiaSdk* lumia = ::baidu::lumia::LumiaSdk::ConnectLumia(FLAGS_lumia_ctrl_host + ":" + FLAGS_lumia_ctrl_port);
    bool ok = lumia->ReportDeadMinion(FLAGS_i, "client report");
    if (ok) {
        fprintf(stdout, "report dead minion %s successfully\n", FLAGS_i.c_str());
        return 0;
    }
    fprintf(stderr, "fail to report dead minion %s\n", FLAGS_i.c_str());
    return -1;
}

int ShowMinion() {
    if (FLAGS_i.empty()) {
        fprintf(stderr, "-i is required\n");
        return -1;
    } 
    ::baidu::lumia::LumiaSdk* lumia = ::baidu::lumia::LumiaSdk::ConnectLumia(FLAGS_lumia_ctrl_host + ":" + FLAGS_lumia_ctrl_port);
    std::vector<std::string> ips;
    ips.push_back(FLAGS_i);
    std::vector<std::string> place_holder;
    std::vector< ::baidu::lumia::MinionDesc> minions;
    bool ok = lumia->GetMinion(ips, place_holder, place_holder, & minions);
    if (!ok) {
        fprintf(stderr, "fail to get minions \n");
        return -1;
    }
    baidu::common::TPrinter tp(11);
    tp.AddRow(11, "", "id", "ip", "hostname", "cpu", "mem", "disk", "flash", "state", "rock ip", "datacenter");
    for (uint32_t i = 0; i < minions.size(); i++) {
        std::vector<std::string> vs;
        vs.push_back(baidu::common::NumToString(i + 1));
        vs.push_back(minions[i].id);
        vs.push_back(minions[i].ip);
        vs.push_back(minions[i].hostname);

        vs.push_back(minions[i].cpu.clock + " * " + baidu::common::NumToString(minions[i].cpu.count));

        vs.push_back(baidu::common::HumanReadableString(minions[i].mem.size)+" * " + baidu::common::NumToString(minions[i].mem.count));

        vs.push_back(baidu::common::HumanReadableString(minions[i].disk.size) + " * " + baidu::common::NumToString(minions[i].disk.count));
        
        vs.push_back(baidu::common::HumanReadableString(minions[i].flash.size) + " * " + baidu::common::NumToString(minions[i].flash.count));

        vs.push_back(minions[i].state);
        vs.push_back(minions[i].rock_ip);
        vs.push_back(minions[i].datacenter);
        tp.AddRow(vs);
    }
    printf("%s\n", tp.ToString().c_str());
    return 0;
}

int main(int argc, char* argv[]) {
    FLAGS_flagfile = "./lumia.flag";
    ::google::SetUsageMessage(kLumiaUsage);
    ::google::ParseCommandLineFlags(&argc, &argv, true);
    if (argc < 2) {
        fprintf(stderr, "%s", kLumiaUsage.c_str());
        return -1;
    }
    if (strcmp(argv[1], "report") == 0) {
        return ReportDeadMinion();
    } else if (strcmp(argv[1], "show") == 0) {
        return ShowMinion();
    } else {
        fprintf(stderr, "%s", kLumiaUsage.c_str());
        return -1;
    }
}

