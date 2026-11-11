#include <Hibernation/NetworkRouting.h>
#include <iostream>
int main(){
#ifdef _WIN32
 _putenv_s("HTTPS_PROXY","http://127.0.0.1:9");
 _putenv_s("WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS","--proxy-server=http://127.0.0.1:9 --disable-gpu");
 hibernation::directNetworkEnvironment();
 const auto proxy=std::getenv("HTTPS_PROXY");if(proxy&&*proxy)return 1;
 const auto args=std::string(std::getenv("WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS"));
 if(args.find("--no-proxy-server")==std::string::npos||args.find("--disable-gpu")==std::string::npos)return 2;
 hibernation::directNetworkEnvironment();if(std::string(std::getenv("WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS"))!=args)return 3;
#endif
 std::cout<<"Direct child environment and WebView flags passed\n";return 0;
}
