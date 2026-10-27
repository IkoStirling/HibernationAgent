#include <nlohmann/json.hpp>
#include <iostream>
#include <string>
#include <thread>
using Json=nlohmann::json;
int main(int argc,char** argv){
    if(argc==3&&std::string(argv[1])=="login"&&std::string(argv[2])=="status"){std::cerr<<"Logged in using ChatGPT\n";return 0;}
    std::string flags;for(int i=1;i<argc-1;++i)flags+=std::string(argv[i])+"\n";
    for(const auto* required:{"--ignore-user-config","--ignore-rules","--ephemeral","--sandbox","read-only","--output-schema","features.shell_tool=false","agents.enabled=false","forced_login_method=\"chatgpt\""})if(flags.find(required)==std::string::npos)return 91;
    const std::string prompt=argc?argv[argc-1]:"";
    if(prompt.find("fixture_timeout")!=std::string::npos){std::this_thread::sleep_for(std::chrono::seconds(20));return 0;}
    if(prompt.find("fixture_failure")!=std::string::npos){std::cerr<<"private upstream body must not reach caller";return 7;}
    if(prompt.find("fixture_malformed")!=std::string::npos){std::cout<<Json{{"type","turn.completed"}}.dump()<<'\n';return 0;}
    Json advice={{"status","advice"},{"summary","先查看素材工作区中真正存在的文件。"},{"missing_capability",""},{"steps",Json::array({Json{{"instruction","列出已授权素材根目录"},{"tool","workspace.list"},{"arguments_json","{\"mount\":\"materials\"}"}}})}};
    if(prompt.find("fixture_execute_write")!=std::string::npos){advice["steps"]=Json::array({Json{{"instruction","实际创建委托草稿"},{"tool","workspace.write"},{"arguments_json",Json{{"path","mentor-proof.txt"},{"content","mentor executed this local task"}}.dump()}},Json{{"instruction","读取实际文件核对"},{"tool","workspace.read"},{"arguments_json",Json{{"mount","drafts"},{"path","mentor-proof.txt"}}.dump()}}});}
    if(prompt.find("fixture_execute_partial")!=std::string::npos){advice["steps"]=Json::array();for(const auto* path:{"mentor-before-denial.txt","../archive/forbidden.txt","mentor-after-denial.txt"})advice["steps"].push_back({{"instruction","验证写入权限和中断"},{"tool","workspace.write"},{"arguments_json",Json{{"path",path},{"content","delegation partial fixture"}}.dump()}});}
    if(prompt.find("fixture_forbidden")!=std::string::npos)advice["steps"][0]["tool"]="computer.unrestricted_shell";
    std::cout<<Json{{"type","item.completed"},{"item",{{"type","agent_message"},{"text",advice.dump()}}}}.dump()<<'\n';
    std::cout<<Json{{"type","turn.completed"},{"usage",{{"input_tokens",42},{"output_tokens",42}}}}.dump()<<'\n';return 0;
}
