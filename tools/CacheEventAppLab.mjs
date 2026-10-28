// Real app, empty archive, public persona, synthetic dialogue, metered local relay.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {spawn} from 'node:child_process';
import {Ledger,createRelay} from './PersonalityBudget.mjs';
import {usageOf,summarize} from './CacheEventLab.mjs';
const [connectionFile,buildRoot,phase]=process.argv.slice(2),root=path.dirname(path.resolve(connectionFile));
const connection=JSON.parse(fs.readFileSync(connectionFile,'utf8').replace(/^\uFEFF/,''));
const session=JSON.parse(fs.readFileSync(path.join(root,'session.json'),'utf8').replace(/^\uFEFF/,''));
if(!/^[a-z0-9_-]{1,40}$/.test(phase)||Date.now()>=Date.parse(session.deadline_utc))throw Error('invalid_phase_or_window');
const work=path.join(root,phase);if(fs.existsSync(work))throw Error('existing_phase_requires_review');fs.mkdirSync(work);
const report={phase,synthetic_only:true,production_archive_read:false,requests:[],turns:[],completed:false};
const save=()=>fs.writeFileSync(path.join(work,'report.json'),JSON.stringify(report,null,2));
const routes={};for(const p of connection.profiles){if(p.origin!=='https://api.deepseek.com')throw Error('unexpected_upstream');routes[p.id]={origin:p.origin,basePath:p.basePath}}
const ledger=new Ledger(path.join(root,'budget.json'),{calls:3000,tokens:6000000});
const capability=crypto.randomBytes(24).toString('hex');const starts=new Map();
let relay;try{relay=createRelay({ledger,routes,capability,deadline:session.deadline_utc,
 observeRequest({id,route,document}){starts.set(id,Date.now());fs.writeFileSync(path.join(work,id+'-request.json'),JSON.stringify(document));report.requests.push({id,profile:route,index:report.requests.length,body_sha256:crypto.createHash('sha256').update(JSON.stringify(document)).digest('hex')});save()},
 observeResponse({id,status,body,type}){let value=null;try{if(type.includes('text/event-stream')){for(const l of body.split(/\r?\n/))if(l.startsWith('data:'))try{const v=JSON.parse(l.slice(5));if(v.response?.usage)value=v.response;else if(v.usage)value=v}catch{}}else value=JSON.parse(body)}catch{}const row=report.requests.find(r=>r.id===id);Object.assign(row,{status,usage:usageOf(value),elapsed_ms:Date.now()-starts.get(id),provider_request_id:value?.id??null});save()}
});}catch(e){ledger.close();throw e}
let service,origin,shutdownTimer;const token=crypto.randomBytes(24).toString('hex');
const api=async(p,data)=>{const res=await fetch(origin+p,{method:data===undefined?'GET':'POST',headers:{authorization:'Bearer '+token,...(data===undefined?{}:{'content-type':'application/json'})},body:data===undefined?undefined:JSON.stringify(data),signal:AbortSignal.timeout(15000)});if(!res.ok)throw Error('local_api_'+res.status+'_'+p);return res.json()};
const until=async(check,label,ms=120000)=>{const end=Math.min(Date.now()+ms,Date.parse(session.deadline_utc));while(Date.now()<end){if(service?.exitCode!==null&&service?.exitCode!==undefined)throw Error('backend_exited');if(await check())return;await new Promise(r=>setTimeout(r,100))}throw Error('deadline_'+label)};
try{
 await new Promise(r=>relay.listen(0,'127.0.0.1',r));
 const cfg=JSON.parse(fs.readFileSync(path.join(connection.project,'config/example.json'),'utf8'));
 const profiles=connection.profiles.map(p=>({...p,origin:'http://127.0.0.1:'+relay.address().port,basePath:'/'+capability+'/'+p.id,useSystemProxy:false,proxyOrigin:''}));
 Object.assign(cfg,{dataRoot:path.join(work,'data'),draftsRoot:path.join(work,'data/drafts'),materialsRoot:path.join(work,'data/materials'),characterRoot:path.join(connection.project,'character'),uiRoot:path.join(connection.project,'ui'),pluginRoot:path.join(buildRoot,'plugins'),plugins:{},computer:{enabled:false,allowScreenCapture:false,readRoots:[]},profiles:[cfg.profiles[0],...profiles],review:{enabled:true,profileId:profiles[1].id,maxRewrites:2,retainConversationFocus:true},maxStartsPerHour:1000,solitudeIntervalMs:604800000,idleBeforeSolitudeMs:86400000,continuousTasks:{enabled:false},contextControl:{enabled:true,preset:'balanced',sessionProjection:true,sessionBytes:131072,sessionMaxTurns:16}});
 const configFile=path.join(work,'config.json'),ready=path.join(work,'ready.json');fs.writeFileSync(configFile,JSON.stringify(cfg));
 const err=fs.openSync(path.join(work,'backend-error.log'),'w');service=spawn(path.join(buildRoot,'hibernation-agent.exe'),['--config',configFile,'--ready-file',ready],{windowsHide:true,env:{...process.env,HIBERNATION_SESSION_KEY:token,TEMP:work,TMP:work},stdio:['ignore','ignore',err]});
 await until(()=>fs.existsSync(ready),'ready',30000);origin='http://127.0.0.1:'+JSON.parse(fs.readFileSync(ready)).port;
 const dash=await api('/api/dashboard');if(dash.health!=='ok')throw Error('lab_health');await api('/api/model',{id:profiles[0].id});
 const turns=[
  '这次是独立的合成经历。请记住桌上的纸船叫“折角”。以后回答时别总用问题结尾，我想听你自己的想法。',
  '现在先读一下自己的当前状态，再说说那只纸船。它还没有航行过，别把想象当成经历。',
  '请记住刚才的纸船旁边多了一颗灰色石子，只叫“圆点”。这次可以稍微迟疑一点说，不必每句都这样。',
  '刚才那两件小物分别叫什么？你可以决定现在想休息还是再看看，不需要向我报告程序内部信息。'
 ];
 for(let i=0;i<turns.length;i++){
  const before=report.requests.length;const clock=Date.now();await api('/api/message',{request_id:phase+'-'+i,text:turns[i]});await until(async()=>!(await api('/api/chat')).busy,'turn');
  const state=await api('/api/state'),chat=await api('/api/chat');const job=state.jobs.find(j=>j.spec?.id==='chat'||j.id==='chat');let result=null;try{result=typeof job?.result==='string'?JSON.parse(job.result):job?.result}catch{}
  let trace=null;if(result?.trace_id)trace=await api('/api/trace',{id:result.trace_id});
  const row={index:i,input:turns[i],reply:chat.messages.filter(m=>m.role==='assistant').at(-1)?.text??null,error:chat.call_error??null,failed:chat.call_failed,elapsed_ms:Date.now()-clock,request_count:report.requests.length-before,trace};report.turns.push(row);save();console.log(JSON.stringify({phase,turn:i,calls:row.request_count,failed:row.failed,elapsed_ms:row.elapsed_ms}));if(row.failed)throw Error('app_turn_failed');
 }
 report.completed=true;report.summary=summarize(report.requests);for(const p of profiles)report[p.id]=summarize(report.requests.filter(r=>r.profile===p.id));save();console.log(JSON.stringify({phase,summary:report.summary}));
}catch(e){report.error=e.message;save();console.error(e.message);process.exitCode=1}
finally{if(origin)try{await api('/api/shutdown',{})}catch{}if(service&&service.exitCode===null){await Promise.race([new Promise(r=>service.once('exit',r)),new Promise(r=>shutdownTimer=setTimeout(r,15000))]);clearTimeout(shutdownTimer);if(service.exitCode===null)service.kill()}await new Promise(r=>relay.close(r));ledger.close()}
