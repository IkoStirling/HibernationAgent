#include <Hibernation/NapcatProcess.h>
#include <Windows.h>
#include <shellapi.h>
int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR,int){
    int count=0;auto args=CommandLineToArgvW(GetCommandLineW(),&count);if(!args)return 2;
    int result=2;if(count==3&&std::wstring(args[1])==L"--config")result=hibernation::runNapcatHost(args[2]);LocalFree(args);return result;
}
