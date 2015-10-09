// Copyright (c) 2015, Baidu.com, Inc. All Rights Reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef BAIDU_LUMIA_SDK_LUMIA_H
#define BAIDU_LUMIA_SDK_LUMIA_H

#include <string>

namespace baidu {
namespace lumia {
class LumiaSdk {
public:
    LumiaSdk(){}
    virtual ~LumiaSdk(){}
    static LumiaSdk* ConnectLumia(const std::string& lumia_addr);
    virtual bool ReportDeadMinion(const std::string& ip, const std::string& reason) = 0;
};

}
}
#endif


