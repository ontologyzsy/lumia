// Copyright (c) 2015, Baidu.com, Inc. All Rights Reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef BAIDU_CTRL_MACHINE_H
#define BAIDU_CTRL_MACHINE_H

#include <boost/function.hpp>
#include "ctrl/http_client.h"
#include "thread_pool.h"

namespace baidu {
namespace lumia {

class MachineCtrl {
public:
    MachineCtrl(const std::string& ccs_http_server,
                const std::string& rms_http_server);
    bool Exec(const std::string& script,
              const std::vector<std::string>& hosts,
              const std::string& account,
              int32_t concurrency);

    ~MachineCtrl();
private:
    bool BuildJob(const std::string& script,
                  const std::vector<std::string>& hosts,
                  const std::string& account,
                  const std::string& service,
                  int32_t concurrency,
                  std::string* job);
    bool GenerateTicket(std::string* ticket, std::string* service);

private:
    std::string ccs_http_server_;
    std::string rms_http_server_;
    HttpClient http_client_;
    ::baidu::common::ThreadPool checker_;
};

}
}
#endif

