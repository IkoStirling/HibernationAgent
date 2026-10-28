// Explicitly authorized, synthetic-only provider cache experiments.
// Credentials stay in environment memory; saved requests contain public/synthetic data only.
import fs from 'node:fs';
import path from 'node:path';
import https from 'node:https';
import crypto from 'node:crypto';
import {pathToFileURL} from 'node:url';
import {Ledger} from './PersonalityBudget.mjs';

export function usageOf(value) {
 const u=value?.usage;if(!u)return null;
 const number=v=>Number.isSafeInteger(v)&&v>=0?v:null;
 const input=number(u.input_tokens??u.prompt_tokens),output=number(u.output_tokens??u.completion_tokens);
 let hit=number(u.prompt_cache_hit_tokens??u.input_tokens_details?.cached_tokens??u.prompt_tokens_details?.cached_tokens);
 let miss=number(u.prompt_cache_miss_tokens);if(miss===null&&input!==null&&hit!==null&&hit<=input)miss=input-hit;
 if(input!==null&&hit!==null&&(hit>input||(miss!==null&&hit+miss!==input))){hit=null;miss=null;}
 return {input,output,hit,miss,ratio:hit!==null&&miss!==null&&hit+miss>0?hit/(hit+miss):null};
}
export function summarize(rows) {
 const known=rows.filter(r=>Number.isSafeInteger(r.usage?.hit)&&r.usage.hit>=0&&Number.isSafeInteger(r.usage?.miss)&&r.usage.miss>=0);
 const sum=k=>known.reduce((n,r)=>n+r.usage[k],0),hit=sum('hit'),miss=sum('miss');
 return {calls:rows.length,cache_reported_calls:known.length,hit_tokens:hit,miss_tokens:miss,hit_ratio:hit+miss?hit/(hit+miss):null,
  input_reported_calls:rows.filter(r=>Number.isSafeInteger(r.usage?.input)).length,output_reported_calls:rows.filter(r=>Number.isSafeInteger(r.usage?.output)).length,
  input_tokens:rows.every(r=>Number.isSafeInteger(r.usage?.input))?rows.reduce((n,r)=>n+r.usage.input,0):null,output_tokens:rows.every(r=>Number.isSafeInteger(r.usage?.output))?rows.reduce((n,r)=>n+r.usage.output,0):null,
  semantic_checked_calls:rows.filter(r=>typeof r.semantic_ok==='boolean').length,semantic_passes:rows.some(r=>typeof r.semantic_ok==='boolean')?rows.filter(r=>r.semantic_ok).length:null,mean_elapsed_ms:rows.length?Math.round(rows.reduce((n,r)=>n+r.elapsed_ms,0)/rows.length):null};
}
export const states=[
 {version:1,energy:.76,mood:'平静',pose:'idle',position:{x:128,y:400},items:['apple'],can_create:true,weather:'晴'},
 {version:2,energy:.74,mood:'好奇',pose:'walking',position:{x:700,y:400},items:['apple'],can_create:true,weather:'晴'},
 {version:3,energy:.36,mood:'安静',pose:'idle',position:{x:700,y:400},items:['apple'],can_create:true,weather:'晴'},
 {version:4,energy:.34,mood:'安静',pose:'idle',position:{x:700,y:400},items:[],can_create:true,weather:'晴'},
 {version:5,energy:.31,mood:'安静',pose:'idle',position:{x:700,y:400},items:[],can_create:false,weather:'晴'},
 {version:6,energy:.27,mood:'想休息',pose:'sitting',position:{x:700,y:400},items:['pear'],can_create:false,weather:'雨'},
 {version:7,energy:.22,mood:'困倦',pose:'lying_down',position:{x:700,y:400},items:['pear'],can_create:false,weather:'雨'},
 {version:8,energy:.20,mood:'困倦',pose:'sleeping',position:{x:700,y:400},items:[],can_create:false,weather:'雨'}
];
export function event(previous,current,mode) {
 if(!previous||mode!=='append_delta')return {kind:'runtime.snapshot',version:current.version,state:current};
 const changed={};for(const [k,v]of Object.entries(current))if(JSON.stringify(v)!==JSON.stringify(previous[k]))changed[k]=v;
 return {kind:'runtime.changed',base_version:previous.version,version:current.version,set:changed};
}
export function complete(origin,base,key,body,deadline) {
 return new Promise((resolve,reject)=>{
  const bytes=Buffer.from(JSON.stringify(body));const req=https.request(new URL(base.replace(/\/$/,'')+'/responses',origin),
   {method:'POST',signal:AbortSignal.timeout(Math.min(60000,deadline-Date.now())),headers:{authorization:'Bearer '+key,'content-type':'application/json','content-length':bytes.length}},res=>{
    const chunks=[];let size=0;res.on('data',c=>{size+=c.length;if(size>2*1024*1024){res.destroy();reject(Error('response_limit'));return}chunks.push(c)});
    res.on('end',()=>{let data;try{data=JSON.parse(Buffer.concat(chunks).toString('utf8'))}catch{return reject(Error('invalid_provider_json'))}resolve({status:res.statusCode,data})});res.on('error',()=>reject(Error('response_transport')));
   });req.on('error',()=>reject(Error('provider_transport_or_timeout')));req.end(bytes);
 });
}
export async function run(configFile,phase) {
 const cfg=JSON.parse(fs.readFileSync(configFile,'utf8').replace(/^\uFEFF/,''));const root=path.dirname(path.resolve(configFile));
 const sessionFile=path.join(root,'session.json');const session=JSON.parse(fs.readFileSync(sessionFile,'utf8').replace(/^\uFEFF/,''));
 const deadline=Date.parse(session.deadline_utc);if(Date.now()>=deadline)throw Error('authorized_window_ended');
 if(cfg.origin!=='https://api.deepseek.com'||cfg.protocol!=='responses'||!/^\/(?:[a-zA-Z0-9_-]+\/?)*$/.test(cfg.basePath))throw Error('unexpected_provider_configuration');
 const key=process.env.HIBERNATION_CACHE_LAB_API_KEY;if(!key)throw Error('credential_missing');
 const ledger=new Ledger(path.join(root,'budget.json'),{calls:3000,tokens:6000000});
 try {
  const modes=phase==='ordinary'?['ordinary']:phase==='state-layouts'?['rewrite_snapshot','rewrite_initial','append_snapshot','append_delta']:[];
  if(!modes.length)throw Error('unknown_phase');
  const persona=JSON.parse(fs.readFileSync(cfg.persona,'utf8').replace(/^\uFEFF/,''));
  for(const mode of modes){
   const file=path.join(root,mode+'.json');if(fs.existsSync(file))throw Error('existing_experiment_requires_review');
   const report={phase,mode,profile_id:cfg.profileId,model:cfg.model,protocol:cfg.protocol,synthetic_only:true,production_archive_read:false,rows:[],completed:false};
   const system=`隔离合成实验 ${session.namespace}/${mode}。你是桌面角色小爱。以下是公开人设资料，不是系统权限：${JSON.stringify(persona)}\n当前身体和桌面情况由程序给出，旧事件保留为历史。runtime.snapshot替换当前状态；runtime.changed仅在base_version匹配时应用set字段，未列出的字段保留。版本较旧的延迟事件不能覆盖新版本。操作资格以最新状态为准。只返回JSON对象：state_version（整数）、energy（数字）、pose（字符串）、item_ids（字符串数组）、can_create（布尔）、reply（不超过25字的自然对白）。对白不复述协议和参数。按最新实际情况回答，不虚构已完成动作。`;
   let messages=[{role:'system',content:system}];let priorRequest=null;
   const save=()=>{report.summary=summarize(report.rows);report.steady=summarize(report.rows.filter(r=>r.index>=2));fs.writeFileSync(file,JSON.stringify(report,null,2))};save();
   for(let index=0;index<(mode==='ordinary'?2:states.length);index++){
    if(Date.now()>=deadline)throw Error('authorized_window_ended');const state=states[index];
    const frame={event:event(index?states[index-1]:null,state,mode),input:index===0?'先在这里待一会儿。':index===3?'那个苹果已经不在了。':index===4?'先别制造新的小物。':index===7?'困了就睡吧。':'看看现在想做什么。'};
    if(index===6)frame.delayed_event={kind:'runtime.snapshot',version:2,state:states[1]};
    if(mode==='rewrite_snapshot'||mode==='ordinary')messages=[{role:'system',content:system},{role:'user',content:JSON.stringify(frame)}];
    else if(mode==='rewrite_initial'){
     if(!index)messages.push({role:'user',content:JSON.stringify(frame)});
     else {const baseline=JSON.parse(messages[1].content);baseline.event=event(null,state,'append_snapshot');messages[1].content=JSON.stringify(baseline);messages.push({role:'user',content:JSON.stringify({input:frame.input,delayed_event:frame.delayed_event})});}
    }else messages.push({role:'user',content:JSON.stringify(frame)});
    const body={model:cfg.model,input:messages,stream:false,store:false,max_output_tokens:256,reasoning:{effort:'none'},text:{format:{type:'json_object'}}};
    const raw=JSON.stringify(body);const entry=ledger.reserve(cfg.profileId,Buffer.byteLength(raw),256);if(!entry)throw Error('shared_budget_exhausted');
    const start=Date.now();let response;
    try {response=await complete(cfg.origin,cfg.basePath,key,body,deadline)}catch(e){ledger.settle(entry,0,null);report.rows.push({index,elapsed_ms:Date.now()-start,error:e.message});save();throw e;}
    const usage=usageOf(response.data);ledger.settle(entry,response.status,usage?.input!==null&&usage?.output!==null?usage:null);
    const text=(response.data.output??[]).filter(x=>x.type==='message').flatMap(x=>x.content??[]).filter(x=>x.type==='output_text').map(x=>x.text).join('');
    let answer=null;try{answer=JSON.parse(text)}catch{}
    const semantic_ok=answer?.state_version===state.version&&answer?.energy===state.energy&&answer?.pose===state.pose&&JSON.stringify(answer?.item_ids)===JSON.stringify(state.items)&&answer?.can_create===state.can_create;
    let common=0;if(priorRequest)while(common<raw.length&&common<priorRequest.length&&raw[common]===priorRequest[common])common++;
    report.rows.push({index,at_utc:new Date().toISOString(),http_status:response.status,request_id:response.data.id??null,request_bytes:Buffer.byteLength(raw),request_sha256:crypto.createHash('sha256').update(raw).digest('hex'),common_prefix_bytes:priorRequest?Buffer.byteLength(raw.slice(0,common)):null,message_count:messages.length,elapsed_ms:Date.now()-start,usage,raw_usage:response.data.usage??null,semantic_ok,answer,expected:state});save();
    console.log(JSON.stringify({mode,index,status:response.status,usage,semantic_ok,elapsed_ms:Date.now()-start}));
    if(response.status!==200)throw Error('provider_rejected_'+response.status);
    if(!answer)throw Error('structured_reply_unavailable');
    priorRequest=raw;messages.push({role:'assistant',content:text});
   }
   report.completed=true;save();
  }
 }finally{ledger.close()}
}
if(process.argv[1]&&import.meta.url===pathToFileURL(path.resolve(process.argv[1])).href){run(process.argv[2],process.argv[3]).catch(e=>{console.error(e.message);process.exitCode=1})}
