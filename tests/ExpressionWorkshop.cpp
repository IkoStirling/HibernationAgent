#include "DatasetWorkshop.h"
#include <fstream>
#include <thread>
#include <iostream>
#include <atomic>
using namespace hibernation::expression;
using namespace ayt::inference;
namespace {
int checks=0;
void check(bool value,const char* label){if(!value)throw std::runtime_error(label);++checks;std::cout<<"PASS "<<label<<'\n';}
Json fixture(){
    Json records=Json::array(),votes=Json::array();
    for(const auto& task:std::vector<std::string>{"conversation_sft","rewrite_sft","behavior_reference"}){
        auto id="seed-"+task;Json r={{"id",id},{"task",task},{"prompt",{{"scene_is_synthetic",true},{"scene_facts",Json::array({"SYNTHETIC_DATASET_SENTINEL"})},{"dialogue_context",Json::array()},{"runtime_observations",Json::array()},{"user_message","你在做什么？"}}},
            {"target",{{"kind",task=="behavior_reference"?"no_visible_reply":"assistant_text"},{"text",task=="behavior_reference"?Json(nullptr):Json("嗯，正在看看。")}}}};
        if(task=="rewrite_sft")r["rewrite"]={{"original","我目前正在观察。"}};
        r["target_sha256"]=datasetDigest({{"task",r["task"]},{"prompt",r["prompt"]},{"target",r["target"]},{"rewrite",r.value("rewrite",Json(nullptr))}});
        records.push_back(r);votes.push_back({{"id",id},{"decision","keep"},{"target_sha256",r["target_sha256"]},{"note",""},{"suggested_text",""}});
    }
    Json pack={{"schema","xiaoa.training-demonstrations/1"},{"version","synthetic-direction-1"},{"records",records}};
    pack["sha256"]=datasetDigest(pack);Json review={{"schema","xiaoa.demonstration-direction-review/1"},{"version",pack["version"]},{"pack_sha256",pack["sha256"]},
        {"reviewer","user"},{"approval_scope","direction_only_not_training_consent"},{"complete",true},{"decisions",votes}};
    return {{"pack",pack},{"review",review}};
}
Json profiles(const std::string& dir,int count=3,int budget=32,const std::string& task="conversation_sft"){
    return {{"direction_id",dir},{"generator",{{"id","fixture-generator"},{"protocol","mock"},{"model","generator"}}},{"reviewer",{{"id","fixture-reviewer"},{"protocol","mock"},{"model","reviewer"}}},
        {"task",task},{"count",count},{"max_calls",budget}};
}
Json status(DatasetWorkshop& w){return w.invoke("status",Json::object()).at("result").at("job");}
Json await(DatasetWorkshop& w){for(int i=0;i<2000;++i){auto j=status(w);auto s=j.value("state",std::string{});if(s=="completed"||s=="failed"||s=="cancelled")return j;std::this_thread::sleep_for(std::chrono::milliseconds(5));}throw std::runtime_error("dataset test timeout");}
Json get(DatasetWorkshop& w,const Json& j){return w.invoke("get",{{"job_id",j.at("id")}}).at("result");}
std::string import(DatasetWorkshop& w){auto result=w.invoke("import",fixture());check(result.value("ok",false),"direction import");return result["result"]["id"];}
}
int main(int argc,char** argv){
 try{
  auto root=std::filesystem::path(argc>1?argv[1]:"expression-workshop-tests")/("run-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));std::filesystem::create_directories(root);
  std::string finishedId;
  {
   DatasetWorkshop w(root/"normal");auto dir=import(w);
   auto duplicate=w.invoke("import",fixture());check(duplicate.value("ok",false)&&w.summary()["directions"].size()==1,"identical imports are idempotent");
   auto bad=fixture();bad["review"]["pack_sha256"]="wrong";check(!w.invoke("import",bad).value("ok",true),"mismatched review rejected");
   bad=fixture();bad["review"]["decisions"][1]=bad["review"]["decisions"][0];check(!w.invoke("import",bad).value("ok",true),"duplicate user IDs rejected");
   bad=fixture();bad["review"]["decisions"][0]["suggested_text"]="new target";check(!w.invoke("import",bad).value("ok",true),"unreviewed proposed edit is not an approved seed");
   auto real=profiles(dir);real["generator"]={{"id","real"},{"protocol","responses"},{"origin","https://example.invalid"},{"model","fake"}};
   check(w.invoke("start",real)["error"]["code"]=="dataset_send_consent_required","real route requires this-job send confirmation before any call");
   auto same=profiles(dir);same["reviewer"]["id"]=same["generator"]["id"];check(w.invoke("start",same)["error"]["code"]=="dataset_distinct_profiles_required","reviewer uses a distinct profile");
   check(w.invoke("start",profiles(dir,6)).value("ok",false),"mock job starts");auto j=await(w);auto full=get(w,j);finishedId=j["id"];
   check(j["state"]=="completed"&&j["calls"]==4&&j["real_model_calls"]==0&&full["candidates"].size()==6,"engine mock completes generation and separate review batches");
   check(std::all_of(full["candidates"].begin(),full["candidates"].end(),[](const auto& c){return c.at("mock_only")==true&&c.at("training_eligible")==false&&c.at("human_review").at("decision")=="pending";}),"mock and reviewer keep never become human or training approval");
   const auto& c=full["candidates"][0];auto vote=Json{{"job_id",finishedId},{"candidate_id",c["id"]},{"candidate_sha256",c["candidate_sha256"]},{"decision","keep"}};
   auto changed=vote;changed["candidate_sha256"]="old";check(w.invoke("decide",changed)["error"]["code"]=="dataset_candidate_changed","stale human vote rejected");
   changed=vote;changed["suggested_text"]="edited";check(w.invoke("decide",changed)["error"]["code"]=="dataset_edit_requires_re_review","edited text is not silently approved");
   check(w.invoke("decide",vote).value("ok",false),"human selection saved independently");
   auto bundle=w.invoke("export",{{"job_id",finishedId}});check(bundle.value("ok",false)&&bundle["result"]["job"]["exchanges"].size()==4&&bundle["result"]["training_approved"]==false,"export preserves prompts and exact visible exchanges without training approval");
   for(const auto& file:std::filesystem::directory_iterator(root/"normal")){std::ifstream in(file.path(),std::ios::binary);std::string data((std::istreambuf_iterator<char>(in)),{});check(data.find("SYNTHETIC_DATASET_SENTINEL")==data.npos&&data.find("模拟候选")==data.npos,"stored files are encrypted, not cleartext corpus");}
  }
  {DatasetWorkshop w(root/"normal");check(status(w)["id"]==finishedId&&status(w)["state"]=="completed","completed batch survives reconstruction without new calls");}
  {DatasetWorkshop w(root/"budget");auto dir=import(w);w.invoke("start",profiles(dir,6,1));auto j=await(w);check(j["state"]=="failed"&&j["error"]=="dataset_call_budget"&&j["calls"]==1&&get(w,j)["candidates"].size()==3,"call budget preserves unreviewed partial results and stops");}
  {DatasetWorkshop w(root/"silence");auto dir=import(w);w.invoke("start",profiles(dir,2,8,"behavior_reference"));auto j=await(w);auto f=get(w,j);check(j["state"]=="completed"&&f["candidates"][0]["response"].is_null(),"behavior reference remains null and separate from text targets");}
  {DatasetWorkshop w(root/"rewrite");auto dir=import(w);w.invoke("start",profiles(dir,1,8,"rewrite_sft"));auto j=await(w);check(j["state"]=="completed"&&get(w,j)["candidates"][0]["original"].is_string(),"rewriter keeps its own original and target format");}
  {
   std::atomic<int> count=0;
   DatasetWorkshop w(root/"repair",[&](const ModelProfile& p,const Request& q){auto r=ayt::inference::invoke(p,q);if(count++==0)r.text="not-json";return r;});auto dir=import(w);w.invoke("start",profiles(dir,1));auto j=await(w);
   check(j["state"]=="completed"&&j["calls"]==3,"invalid structure receives one bounded repair counted in the budget");
  }
  {
   DatasetWorkshop w(root/"bad-source",[](const ModelProfile& p,const Request& q){auto r=ayt::inference::invoke(p,q);if(p.model=="generator"){auto v=Json::parse(r.text);v["samples"][0]["source_ids"]=Json::array({"unknown-source"});r.text=v.dump();}return r;});auto dir=import(w);w.invoke("start",profiles(dir,1));auto j=await(w);
   check(j["state"]=="failed"&&j["calls"]==2&&get(w,j)["candidates"].empty(),"forged source lineage is rejected even after repair");
  }
  {
   DatasetWorkshop w(root/"disagreement",[](const ModelProfile& p,const Request& q){auto r=ayt::inference::invoke(p,q);if(p.model=="reviewer"){auto v=Json::parse(r.text);v["reviews"][0]["decision"]="hold";r.text=v.dump();}return r;});auto dir=import(w);w.invoke("start",profiles(dir,1));auto j=await(w);auto c=get(w,j)["candidates"][0];
   Json v={{"job_id",j["id"]},{"candidate_id",c["id"]},{"candidate_sha256",c["candidate_sha256"]},{"decision","keep"}};
   check(w.invoke("decide",v)["error"]["code"]=="dataset_disagreement_confirmation","review disagreement cannot be silently discarded");v["accept_disagreement"]=true;v["note"]="人工仍想保留";check(w.invoke("decide",v).value("ok",false),"explicit human override is recorded without changing model vote");
  }
  {
   std::atomic<bool> entered=false;
   DatasetWorkshop w(root/"cancel",[&](const ModelProfile&,const Request& q){entered=true;while(!q.cancelled())std::this_thread::sleep_for(std::chrono::milliseconds(2));Result r;r.status=Status::Cancelled;return r;});auto dir=import(w);w.invoke("start",profiles(dir,3));while(!entered)std::this_thread::sleep_for(std::chrono::milliseconds(2));w.cancel();check(await(w)["state"]=="cancelled","cancel reaches the actual in-flight engine request");
  }
  {
   std::atomic<bool> entered=false,release=false;
   DatasetWorkshop w(root/"pause",[&](const ModelProfile& p,const Request& q){if(!entered.exchange(true))while(!release&&!q.cancelled())std::this_thread::sleep_for(std::chrono::milliseconds(2));return ayt::inference::invoke(p,q);});auto dir=import(w);w.invoke("start",profiles(dir,3));while(!entered)std::this_thread::sleep_for(std::chrono::milliseconds(2));w.invoke("pause",Json::object());release=true;
   for(int i=0;i<1000&&status(w)["state"]!="paused";++i)std::this_thread::sleep_for(std::chrono::milliseconds(2));check(status(w)["state"]=="paused"&&status(w)["calls"]==1,"pause finishes the current call then prevents the next one");w.invoke("resume",Json::object());check(await(w)["state"]=="completed","resume continues the same batch");
  }
  {
   std::vector<std::uint32_t> outputCaps;
   DatasetWorkshop w(root/"profile-output",[&](const ModelProfile& p,const Request& q){outputCaps.push_back(q.maxOutputTokens);return ayt::inference::invoke(p,q);});
   auto dir=import(w);auto args=profiles(dir,1);args["generator"]["maxOutputTokens"]=128;args["reviewer"]["maxOutputTokens"]=32768;
   check(w.invoke("start",args).value("ok",false),"existing large connection output settings are accepted with workshop cap");auto j=await(w);
   check(j["state"]=="completed"&&outputCaps==std::vector<std::uint32_t>{128,2048},"per-call output respects both a smaller profile and workshop budget");
  }
  {
   DatasetWorkshop w(root/"input-budget");auto dir=import(w);auto args=profiles(dir,3);args["max_input_bytes"]=1024;w.invoke("start",args);auto j=await(w);
   check(j["state"]=="failed"&&j["calls"]==0&&j["error"]=="dataset_input_budget","input budget stops before dispatch without fabricating a used call");
  }
  std::cout<<"Expression workshop passed "<<checks<<" checks. Synthetic fixtures, real API calls: 0\n";return 0;
 }catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
}
