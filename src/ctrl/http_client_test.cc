#include "ctrl/http_client.h"
#include <map>
#include <vector>
#include <iostream>

int main() {
    ::baidu::lumia::HttpClient client;
    ::baidu::lumia::HttpPostRequest request;
    request.url = "http://jc.noah.baidu.com/job/simplyAdd";
    ::baidu::lumia::HttpResponse response;
    std::vector<std::pair<std::string, std::string> > data;
    data.push_back(std::make_pair("licenseID","string"));
    bool ok = client.Post(&request, &response);
    std::cout << "ret " <<  ok <<  " status" << response.body;
    return 0;
}
