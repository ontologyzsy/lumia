#include "ctrl/machine.h"
#include "baas-lib-c/baas.h"

int main(int argc, char* argv[]) {
    int ret = baas::BAAS_Init(&argc, &argv);
    if (ret != baas::sdk::BAAS_OK){
        return -1;
    }
    ::baidu::lumia::MachineCtrl machine("jc.noah.baidu.com",
       "api.rms.baidu.com");
    std::string script="#!/usr/bin/env bash\n"
                       "touch lumia\n";
    std::vector<std::string> hosts;
    hosts.push_back("yq01-tera1.yq01");
    machine.Exec(script, hosts, "root", 1);
    return 0;
}
