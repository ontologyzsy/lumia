// Copyright (c) 2015, Galaxy Authors. All Rights Reserved
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Author: wangtaize@baidu.com

#ifndef LUMIA_FSM_H
#define LUMIA_FSM_H
#include <string>
#include <boost/function.hpp>
#include <map>
#include <vector>
#include <set>
#include "proto/lumia.pb.h"
#include "rapidjson/document.h"

namespace baidu {
namespace lumia {

typedef boost::function<bool (Minion* minion)> Func;


struct Event {
    std::string name_;
    MinionState from_;
    MinionState to_;
};

struct EventCompare{
    bool operator()(const Event& e1,const Event& e2)const {
        return e1.name_.compare(e2.name_) == 0;
    }
};


// NOTE run fsm in single thread
class FSM {
public:
    FSM(const std::string& conf_path);
    ~FSM();
    bool Init();
    void RegisterFunc(const std::string& event, const Func func);
    bool Dispatch(const std::string& event, Minion* minion);
private:
    bool GetString(const rapidjson::Value& doc,
                   const std::string& key, 
                   std::string* value);
    bool LoadEvent(const rapidjson::Value& event);
    bool LoadCallBack(const rapidjson::Value& call_back);
private:
    std::string conf_path_;
    typedef std::map<std::string, Event> EventMap;
    EventMap events_;
    typedef std::map<std::string, std::set<std::string> > CallBack;
    CallBack before_event_;
    CallBack after_event_;
    MinionState initial_;
    typedef std::map<std::string, Func> FuncMap;
    FuncMap funcs_;
};

} // lumia
} // baidu
#endif
