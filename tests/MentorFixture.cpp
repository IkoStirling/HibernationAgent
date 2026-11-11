#include <nlohmann/json.hpp>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <string>
#include <thread>
#define NOMINMAX
#include <Windows.h>
using Json=nlohmann::json;
int wmain(int argc,wchar_t** argv){
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    if(argc>=3&&std::wstring(argv[1])==L"login"){std::cerr<<"Logged in using ChatGPT\n";return 0;}
    if(argc<2||std::wstring(argv[1])!=L"app-server")return 91;
    const auto send=[](Json value){std::cout<<value.dump()<<std::endl;};
    int turns=0;std::string pending;bool malformed=false;std::filesystem::path cwd;std::string line;
    const auto finish=[&](const std::string& summary){if(!malformed)send({{"method","item/completed"},{"params",{{"threadId","fixture-thread"},{"turnId","fixture-turn"},{"item",{{"type","agentMessage"},{"id","answer"},{"phase","final_answer"},{"text",summary}}}}}});send({{"method","turn/completed"},{"params",{{"threadId","fixture-thread"},{"turn",{{"id","fixture-turn"},{"status","completed"}}}}}});};
    while(std::getline(std::cin,line))try{
        const auto request=Json::parse(line);if(!request.contains("method")){if(request.value("id",0)==9001){finish(pending);pending.clear();}continue;}
        const auto method=request.at("method").get<std::string>();const auto id=request.value("id",0);const auto params=request.value("params",Json::object());
        if(method=="initialized")continue;
        if(method=="initialize")send({{"id",id},{"result",Json::object()}});
        else if(method=="account/read")send({{"id",id},{"result",{{"account",{{"type","chatgpt"}}}}}});
        else if(method=="account/rateLimits/read"){const bool exhausted=std::filesystem::path(argv[0]).filename().native().find(L"quota")!=std::wstring::npos;send({{"id",id},{"result",{{"rateLimits",{{"limitId","codex"},{"primary",{{"usedPercent",exhausted?100:10},{"resetsAt",2000000000},{"windowDurationMins",300}}}}}}}});}
        else if(method=="thread/start"){
            if(!params.value("ephemeral",false)||params.contains("approvalPolicy")||params.contains("sandbox")||!params.contains("dynamicTools"))return 92;
            cwd=std::filesystem::u8path(params.at("cwd").get<std::string>());send({{"id",id},{"result",{{"thread",{{"id","fixture-thread"},{"ephemeral",true}}}}}});
        }else if(method=="turn/interrupt"){send({{"id",id},{"result",Json::object()}});send({{"method","turn/completed"},{"params",{{"turn",{{"id","fixture-turn"},{"status","interrupted"}}}}}});}
        else if(method=="turn/start"){
            if(!params.contains("model")||!params.contains("effort"))return 94;
            ++turns;const auto prompt=Json::parse(params.at("input")[0].at("text").get<std::string>());const auto task=prompt.at("current_request");const auto goal=task.at("goal").get<std::string>();
            send({{"id",id},{"result",{{"turn",{{"id","fixture-turn"},{"status","inProgress"}}}}}});
            if(goal.find("fixture_timeout")!=goal.npos)continue;
            if(goal.find("fixture_failure")!=goal.npos){std::cerr<<"private upstream body";return 7;}
            if(goal.find("fixture_approval")!=goal.npos){send({{"id",9002},{"method","item/commandExecution/requestApproval"},{"params",{{"threadId","fixture-thread"},{"turnId","fixture-turn"},{"itemId","command"}}}});continue;}
            malformed=goal.find("fixture_malformed")!=goal.npos;
            std::string summary="可以聊一会儿。你刚才想到什么有趣的事？";
            if(goal.find("fixture_routing")!=goal.npos)summary=params.at("model").get<std::string>()+"|"+params.at("effort").get<std::string>()+"|"+std::to_string(turns);
            if(goal.find("fixture_continuity")!=goal.npos)summary="native-turn-"+std::to_string(turns);
            if(goal.find("fixture_recovered")!=goal.npos){bool found=false;for(const auto& record:prompt.at("historical_records_not_new_instructions"))if(record.at("data").dump().find("fixture_chat")!=std::string::npos)found=true;summary=found?"recovered-history":"missing-history";}
            if(goal.find("fixture_execute_write")!=goal.npos){std::ofstream(cwd/"mentor-proof.txt")<<"mentor executed this local task";send({{"method","item/completed"},{"params",{{"item",{{"type","commandExecution"},{"id","native-command"},{"status","completed"},{"exitCode",0}}}}}});summary="已在导师工作区写入并核对。";}
            if(goal.find("fixture_memory")!=goal.npos){pending=summary;send({{"id",9001},{"method","item/tool/call"},{"params",{{"namespace","mentor_memory"},{"tool","write"},{"arguments",{{"text","fixture-private-memory"},{"category","summary"},{"sources",Json::array({prompt.at("current_record_id")})}}}}}});continue;}
            finish(summary);
        }else send({{"id",id},{"result",Json::object()}});
    }catch(...){return 93;}
    return 0;
}
