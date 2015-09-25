#include "ctrl/machine.h"
#include "baas-lib-c/baas.h"
#include <iostream>
#include <boost/bind.hpp>
#include <gflags/gflags.h>
#include <unistd.h>
void call_back(const std::string sessionid, const std::vector<std::string> succ, const std::vector<std::string> fails) {
    std::cout << sessionid << std::endl;
}

int main(int argc, char* argv[]) {
    int ret = baas::BAAS_Init(&argc, &argv);
    if (ret != baas::sdk::BAAS_OK){
        return -1;
    }

    ::google::ParseCommandLineFlags(&argc, &argv, true);
    ::baidu::lumia::MachineCtrl machine("jc.noah.baidu.com",
             "api.rms.baidu.com");
    std::string script="#!/usr/bin/env bash\n"
                                 "touch lumia\n";
    std::vector<std::string> hosts;
    hosts.push_back("yq01-tera1.yq01");
    std::string session;
    bool ok = machine.Exec(script, hosts, "root", 0, &session, 
      boost::bind(call_back, _1, _2, _3));
    if (ok) {
        std::cout << "submit job success" << std::endl;
    }
    while(1) {
        sleep(3);
    }
    return 0;
}
