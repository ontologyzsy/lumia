// Copyright (c) 2015, Galaxy Authors. All Rights Reserved
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Author: wangtaize@baidu.com
#include "fsm/fsm.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include "logging.h"
#include "rapidjson/document.h"
#include "rapidjson/filereadstream.h"

namespace baidu {
namespace lumia {
FSM::FSM(const std::string& conf_path):conf_path_(conf_path){}

FSM::~FSM(){}

bool FSM::Init() {
    LOG(INFO, "load conf from path %s", conf_path_.c_str());
    FILE * fd = fopen (conf_path_.c_str(), "r");
    if (fd == NULL) {
        LOG(WARNING, "fail to open conf %s for %s", conf_path_.c_str(), strerror(errno));
        return false;
    }
    char readBuffer[65536];
    rapidjson::FileReadStream is(fd, readBuffer, sizeof(readBuffer));
    rapidjson::Document document;
    document.ParseStream<0>(is);
    if (!document.IsObject()) {
        LOG(WARNING, "conf file is not json object");
        return false;
    }
    std::string initial_str;
    bool ok = GetString(document, "initial", &initial_str);
    if (!ok) {
        LOG(WARNING, "fail to parse initial");
        return false;
    }
    ok = MinionState_Parse(initial_str, &initial_);
    if (!ok) {
        LOG(WARNING, "fail parse minon state %s", initial_str.c_str());
        return false;
    }
    if (!document.HasMember("events")) {
        LOG(WARNING, "events field is required ");
        return false;
    }
    const rapidjson::Value& events = document["events"];
    for (rapidjson::SizeType i = 0; i < events.Size(); i++) {
        ok = LoadEvent(events[i]);
        if (!ok) {
            return false;
        }
    }
    if (document.HasMember("callbacks")) {
        const rapidjson::Value& callbacks = document["callbacks"];
        for (rapidjson::SizeType i = 0; i< callbacks.Size(); i++) {
            ok = LoadCallBack(callbacks[i]);
            if (!ok) {
                return false;
            }
        }
    }
    return true;
}

void FSM::RegisterFunc(const std::string& event, const Func func) {
    funcs_.insert(std::make_pair(event, func));
    LOG(INFO, "register event %s process function", event.c_str());
}

bool FSM::Dispatch(const std::string& event, Minion* minion){
    if (minion == NULL) {
        return false;
    }
    EventMap::iterator e_it = events_.find(event);
    if (e_it == events_.end()) {
        LOG(WARNING, "event %s is not configured on fsm", event.c_str());
        return false;
    }
    FuncMap::iterator f_it = funcs_.find(event);
    if (f_it == funcs_.end()) {
        LOG(WARNING, "event %s has no handle func", event.c_str());
        return false;
    }
    LOG(INFO, "dispatch event %s on minon %s", event.c_str(), minion->hostname().c_str());
    bool ok = false;
    CallBack::iterator b_e_it = before_event_.find(event);
    if (b_e_it != before_event_.end()) {
        std::set<std::string>::iterator i_b_e_it = b_e_it->second.begin();
        for (; i_b_e_it != b_e_it->second.end(); ++i_b_e_it) {
            ok = Dispatch(*i_b_e_it, minion);
            if (!ok) {
                LOG(WARNING, "exec event %s before event %s fails", (*i_b_e_it).c_str(), event.c_str());
                return false;
            }
        }
    }
    ok = f_it->second(minion);
    if (!ok) {
        LOG(WARNING, "exec event %s handle func fails", event.c_str());
        return false;
    }
    CallBack::iterator a_e_it = after_event_.find(event);
    if (a_e_it != after_event_.end()) {
        std::set<std::string>::iterator i_a_e_it = a_e_it->second.begin();
        for (; i_a_e_it != a_e_it->second.end(); ++i_a_e_it) {
            ok = Dispatch(*i_a_e_it, minion);
            if (!ok) {
                LOG(WARNING, "exec event %s after event %s fails", (*i_a_e_it).c_str(), event.c_str());
                return false;
            }
        }
    }
    return true;
}

bool FSM::GetString(const rapidjson::Value& doc,
                    const std::string& key, 
                    std::string* value) {
    if (value == NULL) {
        return false;
    }
    if (!doc.HasMember(key.c_str())) {
        return false;
    }   
    *value = doc[key.c_str()].GetString();
    if (value->empty()) {
        return false;
    }
    return true;
}

bool FSM::LoadEvent(const rapidjson::Value& event) {
    Event e;
    bool ok = GetString(event, "name", &e.name_);
    if (!ok) {
        LOG(WARNING, "fail to parse event name ");
        return false;
    }
    std::string from_str;
    ok = GetString(event, "from", &from_str);
    if (!ok) {
        LOG(WARNING, "fail to parse event from state");
        return false;
    }
    MinionState from;
    ok = MinionState_Parse(from_str, &from);
    if (!ok) {
        LOG(WARNING, "fail to parse event from state %s", from_str.c_str());
        return false;
    }
    e.from_ = from;
    std::string to_str;
    ok = GetString(event, "to", &to_str);
    if (!ok) {
        LOG(WARNING, "fail to parse event to state");
        return false;
    }
    MinionState to;
    ok = MinionState_Parse(to_str, &to);
    if (!ok) {
        LOG(WARNING, "fail to parse event to state %s", to_str.c_str());
        return false;
    }
    e.to_ = to;
    events_.insert(std::make_pair(e.name_, e));
    LOG(INFO, "load event %s successfully", e.name_.c_str());
    return true;
}

bool FSM::LoadCallBack(const rapidjson::Value& call_back) {
    std::string name;
    bool ok = GetString(call_back, "name", &name);
    if (!ok) {
        LOG(WARNING, "fail to get call back name");
        return false;
    }
    if (call_back.HasMember("after")) {
        const rapidjson::Value& after_events = call_back["after"];
        std::set<std::string> after_events_set;
        for (rapidjson::SizeType i = 0; i < after_events.Size(); i ++) {
            std::string event_name = after_events[i].GetString();
            if (event_name.empty()) {
                continue;
            }
            after_events_set.insert(after_events[i].GetString());
        }
        if (after_events_set.size() > 0) {
            after_event_.insert(std::make_pair(name, after_events_set));
        }
    }

    if (call_back.HasMember("before")) {
        const rapidjson::Value& before_events = call_back["before"];
        std::set<std::string> before_events_set;
        for (rapidjson::SizeType i = 0; i < before_events.Size(); i ++) {
            std::string event_name = before_events[i].GetString();
            if (event_name.empty()) {
                continue;
            }
            before_events_set.insert(before_events[i].GetString());
        }
        if (before_events_set.size() > 0) {
            before_event_.insert(std::make_pair(name, before_events_set));
        }
    }
    return true;
}


}
} // galaxy
