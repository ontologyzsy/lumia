// Copyright (c) 2015, Baidu.com, Inc. All Rights Reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef BAIDU_HTTP_CLIENT_H
#define BAIDU_HTTP_CLIENT_H

#include <sstream>
#include <vector>
#include <map>
#include <boost/function.hpp>
extern "C" {
#include <curl/curl.h>
}

namespace baidu {
namespace lumia {

struct HttpPostRequest {
    std::vector<std::pair<std::string, std::string> > data;
    std::string url;
};

struct HttpResponse {
    std::string body;
    std::string error;
};

class HttpClient {

public:
    HttpClient();
    ~HttpClient(){}
    bool Post(const HttpPostRequest* request,
              HttpResponse* response);
private:
    void BuildPostForm(const HttpPostRequest* request,
                       std::stringstream& ss,
                       CURL* curl);
};


}
}
#endif
