#pragma once
#include <AYPlatform/ChildProcess.h>
#include <fstream>
#include <wincrypt.h>
namespace hibernation::plugins {
inline std::filesystem::path speechDirectory(){
    HMODULE module=nullptr;GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(&speechDirectory),&module);
    std::wstring name(32768,L'\0');const auto n=GetModuleFileNameW(module,name.data(),static_cast<DWORD>(name.size()));if(!n||n>=name.size())throw std::runtime_error("speech_directory");name.resize(n);return std::filesystem::path(name).parent_path();
}
inline PluginJson kanaRuntime(){
    const auto dir=speechDirectory();std::ifstream in(dir/"runtime.json",std::ios::binary|std::ios::ate);if(!in||in.tellg()<0||in.tellg()>8192)throw std::runtime_error("speech_runtime_missing");in.seekg(0);auto cfg=PluginJson::parse(in);
    for(const auto* key:{"pythonExecutable","modules"}){const auto value=std::filesystem::u8path(cfg.at(key).get<std::string>());if(!value.is_absolute()||!std::filesystem::exists(value))throw std::runtime_error("speech_runtime_missing");}
    if(!std::filesystem::is_regular_file(dir/"kana_worker.py")||!std::filesystem::is_regular_file(dir/"kana_reading.py"))throw std::runtime_error("speech_runtime_missing");return cfg;
}
inline std::string renderKana(const PluginJson& request,const std::filesystem::path& dataRoot,const std::function<bool()>& cancelled){
    try{
        const auto runtime=kanaRuntime();const auto serialized=request.dump();DATA_BLOB source{static_cast<DWORD>(serialized.size()),reinterpret_cast<BYTE*>(const_cast<char*>(serialized.data()))},sealed{};
        if(!CryptProtectData(&source,L"Transient speech request",nullptr,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&sealed))return {};
        struct Sealed {BYTE* value;~Sealed(){LocalFree(value);}} sealedOwner{sealed.pbData};
        const auto root=dataRoot/"runtime"/"speech";std::filesystem::create_directories(root);GUID id{};if(FAILED(CoCreateGuid(&id)))return {};wchar_t name[40]{};StringFromGUID2(id,name,40);const auto file=root/(std::wstring(name)+L".dpapi");
        struct RequestFile {std::filesystem::path path;~RequestFile(){std::error_code ec;std::filesystem::remove(path,ec);}} owner{file};
        {std::ofstream out(file,std::ios::binary);out.write(reinterpret_cast<const char*>(sealed.pbData),sealed.cbData);if(!out)return {};}
        ayt::platform::ProcessOptions options;options.executable=std::filesystem::u8path(runtime.at("pythonExecutable").get<std::string>());options.arguments={"-B",pathUtf8(speechDirectory()/"kana_worker.py"),pathUtf8(file)};options.timeout=std::chrono::seconds(15);options.outputLimit=8*1024*1024;options.cancellationRequested=cancelled;
        const auto result=ayt::platform::runProcess(options);if(!result.launched||result.exitCode!=0||result.timedOut||result.cancelled||result.outputTruncated||!result.error.empty())return {};return result.output;
    }catch(...){return {};}
}
}
