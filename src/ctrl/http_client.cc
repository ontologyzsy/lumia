// Copyright (c) 2015, Baidu.com, Inc. All Rights Reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ctrl/http_client.h"

#include <ostream>
#include <iostream>
#include <pthread.h>
#include <unistd.h>
#include "logging.h"

static pthread_once_t once_control = PTHREAD_ONCE_INIT;

static void GlobalDestroy() {
    curl_global_cleanup();    
}

static void GlobalInit() {
    curl_global_init(CURL_GLOBAL_ALL);
    ::atexit(GlobalDestroy);
}

static int writer(char *data, size_t size, size_t nmemb,
                  std::string *writerData){
  if (writerData == NULL)
    return 0;
  writerData->append(data, size*nmemb);
  return size * nmemb;
}


namespace baidu {
namespace lumia {

HttpClient::HttpClient() {
    pthread_once(&once_control, GlobalInit);
}

bool HttpClient::Post(const HttpPostRequest* request,
                      HttpResponse* response) {
    CURL *curl = curl_easy_init();
    if (!curl) {
        return false;
    }
    bool ok = false;
    struct curl_slist *chunk = NULL; 
    std::vector<std::string>::const_iterator it = request->headers.begin();
    for (; it != request->headers.end(); ++it) {
         chunk = curl_slist_append(chunk, it->c_str());
    }
    do {
        if (chunk != NULL) {
            int status  = curl_easy_setopt(curl, CURLOPT_HTTPHEADER, chunk);
            if (status != CURLE_OK) {
                LOG(WARNING, "fail add header url %s to curl", request->url.c_str());
                break;
            
            }
        }
        LOG(INFO, "url %s", request->url.c_str());
        int status = curl_easy_setopt(curl, CURLOPT_URL, request->url.c_str());
        if (status != CURLE_OK) {
            LOG(WARNING, "fail add url %s to curl", request->url.c_str());
            break;
        }
        std::stringstream ss;
        BuildPostForm(request, ss, curl);
        std::string data = ss.str();
        status = curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data.c_str());
        if (status != CURLE_OK) {
            LOG(WARNING, "fail set post field %s", ss.str().c_str());
            break;
        }
        status = curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writer);
        if (status != CURLE_OK) {
            break;
        }
        std::string content;
        status = curl_easy_setopt(curl, CURLOPT_WRITEDATA, &content);
        if (status != CURLE_OK) {
            LOG(WARNING, "fail set write data ");
            break;
        }
        status = curl_easy_perform(curl);
        if (status != CURLE_OK) {
            LOG(WARNING, "fail to post data to %s", request->url.c_str());
            break;
        }
        response->body = content;
        ok = true;
    } while (0);
    curl_easy_cleanup(curl);
    curl_slist_free_all(chunk);
    return ok;
}

bool HttpClient::Get(const HttpGetRequest* request,
                     HttpResponse* response) {

    CURL *curl = curl_easy_init();
    if (!curl) {
        return false;
    }
    bool ok = false;
    struct curl_slist *chunk = NULL; 
    std::vector<std::string>::const_iterator it = request->headers.begin();
    for (; it != request->headers.end(); ++it) {
         chunk = curl_slist_append(chunk, it->c_str());
    }
    do {
        if (chunk != NULL) {
            int status  = curl_easy_setopt(curl, CURLOPT_HTTPHEADER, chunk);
            if (status != CURLE_OK) {
                LOG(WARNING, "fail add header url %s to curl", request->url.c_str());
                break; 
            }
        }
        int status = curl_easy_setopt(curl, CURLOPT_URL, request->url.c_str());
        if (status != CURLE_OK) {
            LOG(WARNING, "fail add url %s to curl", request->url.c_str());
            break;
        }
        status = curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writer);
        if (status != CURLE_OK) {
            break;
        }
        std::string content;
        status = curl_easy_setopt(curl, CURLOPT_WRITEDATA, &content);
        if (status != CURLE_OK) {
            LOG(WARNING, "fail set write data ");
            break;
        }
        status =curl_easy_perform(curl);
        if (status != CURLE_OK) {
            LOG(WARNING, "fail to get data to %s", request->url.c_str());
            break;
        }
        response->body = content;
        ok = true;
    } while (0);
    curl_easy_cleanup(curl);
    curl_slist_free_all(chunk);
    return ok;
}

void HttpClient::BuildPostForm(const HttpPostRequest* request,
                               std::stringstream& ss,
                               CURL* curl) {
    std::vector<std::pair<std::string, std::string> >::const_iterator it = request->data.begin();
    int index = 0;
    for (; it != request->data.end(); ++it) {
        if (index > 0) {
            ss << "&";
        }
        char *output = curl_easy_escape(curl, it->second.c_str(), 0);
        if (output) {
            ss << it->first << "=" << output ;
            curl_free(output);
        }
        ++index;
    }
}

}
}
