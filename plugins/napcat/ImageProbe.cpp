// Private diagnostics: URL arrives on stdin, never command line or output.
// Uses the same HTTP and WIC path as incoming QQ images, without model calls.
#include "IncomingMedia.h"
#include <iostream>
int main(){
    using namespace hibernation;using namespace hibernation::napcat;
    try{
        std::string url;std::getline(std::cin,url);if(url.size()>8192)return 2;const auto address=imageAddress(url,"https://unused.invalid");if(!address)return 3;
        ayt::net::http::Request request;request.origin=address.origin;request.path=address.path;request.method="GET";request.timeout=std::chrono::seconds(10);request.maxResponseBytes=8*1024*1024;request.useSystemProxy=false;
        const auto response=ayt::net::http::perform(request);Json result={{"download_ok",bool(response)&&response.httpStatus==200},{"http_status",response.httpStatus},{"transport_status",int(response.status)}};
        if(response&&response.httpStatus==200){auto image=prepareImageObservation(response.body);result["decoded"]=image.value("ok",false);if(image.value("ok",false)){image["result"].erase("image");result["metadata"]=image["result"];}else result["error"]=image.at("error");}
        std::cout<<result.dump()<<'\n';return 0;
    }catch(...){std::cout<<"{\"error\":\"probe_failed\"}\n";return 1;}
}
