// One eight-hour session owns one durable admission ledger. All native lab
// controllers share it; credentials remain in clients' process environments.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {Ledger,createRelay} from './PersonalityBudget.mjs';
const file=path.resolve(process.argv[2]),config=JSON.parse(fs.readFileSync(file,'utf8'));
if(config.version!==1||config.maxCalls!==1000||!config.routes||Date.parse(config.deadline)<=Date.now())throw Error('invalid_session');
const root=path.dirname(file),ledger=new Ledger(path.join(root,'budget.json'),{calls:1000,tokens:config.maxTokens??16000000});
ledger.save();const capability=crypto.randomBytes(24).toString('hex'),last=new Map();let closing=false;
function digest(value){return crypto.createHash('sha256').update(value).digest('hex');}
function metrics(){
 const entries=ledger.data.entries,reported=entries.filter(e=>e.state==='reported'),cache=entries.filter(e=>e.cache_reported);
 const sum=(list,key)=>list.reduce((n,e)=>n+(e[key]??0),0),input=sum(reported,'input_tokens'),hit=sum(cache,'cached_input_tokens'),miss=sum(cache,'uncached_input_tokens');
 return {version:1,session_id:config.sessionId,updated_utc:new Date().toISOString(),deadline_utc:config.deadline,max_calls:1000,calls:entries.length,remaining_calls:1000-entries.length,reported_calls:reported.length,unknown_usage_calls:entries.length-reported.length,input_tokens:input,output_tokens:sum(reported,'output_tokens'),cache_reported_calls:cache.length,cached_input_tokens:hit,uncached_input_tokens:miss,reported_cache_ratio:hit+miss?hit/(hit+miss):null,closed:closing};
}
function save(){fs.writeFileSync(path.join(root,'meter-status.json'),JSON.stringify(metrics(),null,2));}
const relay=createRelay({ledger,routes:config.routes,capability,deadline:config.deadline,
 beforeReserve({route,totals,document}){
  const expected=config.routes[route];if(document.model!==expected.model)return false;
  return expected.role!=='actor'||totals.calls+2<=1000;
 },
 observeRequest({id,route,document}){
  const row=ledger.data.entries.find(e=>e.id===id),messages=document.messages??document.input??[];
  const system=messages.filter?.(m=>m.role==='system'||m.role==='developer')??[];
  const body=Buffer.from(JSON.stringify({instructions:document.instructions??'',tools:document.tools??[],messages}));
  const previous=last.get(route);let common=0;if(previous)while(common<previous.length&&common<body.length&&previous[common]===body[common])++common;
  row.system_sha256=digest(JSON.stringify(system));row.tools_sha256=digest(JSON.stringify(document.tools??[]));row.local_serialized_message_common_bytes=common;
  row.request_sha256=digest(JSON.stringify(document));row.model=document.model;row.batch=config.routes[route].batch;
  last.set(route,body);ledger.save();save();
 },
 observeResponse(){save();}
});
relay.listen(0,'127.0.0.1',()=>{
 fs.writeFileSync(path.join(root,'relay-ready.json'),JSON.stringify({version:1,pid:process.pid,origin:`http://127.0.0.1:${relay.address().port}`,capability,deadline:config.deadline,routes:Object.fromEntries(Object.entries(config.routes).map(([id,r])=>[id,{role:r.role,batch:r.batch,basePath:`/${capability}/${id}`}]))},null,2));save();
 console.log(JSON.stringify({session:config.sessionId,ready_file:path.join(root,'relay-ready.json'),max_calls:1000}));
});
const timer=setTimeout(stop,Math.max(1,Date.parse(config.deadline)-Date.now()));
function stop(){if(closing)return;closing=true;clearTimeout(timer);save();relay.close(()=>{ledger.close();save();process.exit(0);});relay.closeIdleConnections?.();}
process.on('SIGTERM',stop);process.on('SIGINT',stop);
