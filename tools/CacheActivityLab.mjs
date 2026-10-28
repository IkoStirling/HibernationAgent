import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {spawn} from 'node:child_process';
import {Ledger,createRelay} from './PersonalityBudget.mjs';
import {usageOf,summarize} from './CacheEventLab.mjs';
const [file,buildRoot,phase]=process.argv.slice(2),root=path.dirname(path.resolve(file));
const connection=JSON.parse(fs.readFileSync(file,'utf8')),session=JSON.parse(fs.readFileSync(path.join(root,'session.json'),'utf8'));
if(!['activity-baseline','activity-events','activity-complete'].includes(phase)||connection.profiles.some(p=>p.origin!=='https://api.deepseek.com')||Date.now()>=Date.parse(session.deadline_utc))throw Error('invalid_activity_lab');
const work=path.join(root,phase);if(fs.existsSync(work))throw Error('existing_phase_requires_review');fs.mkdirSync(work);
const ledger=new Ledger(path.join(root,'budget.json'),{calls:3000,tokens:6000000}),capability=crypto.randomBytes(24).toString('hex'),routes={};
for(const p of connection.profiles)routes[p.id]={origin:p.origin,basePath:p.basePath};
const report={phase,synthetic_only:true,production_archive_read:false,actions_executed:false,requests:[],completed:false},starts=new Map();
const save=()=>fs.writeFileSync(path.join(work,'report.json'),JSON.stringify(report,null,2));
let relay;try{relay=createRelay({ledger,routes,capability,deadline:session.deadline_utc,
 observeRequest({id,route,document}){
  const raw=JSON.stringify(document);let latest=null;for(const m of document.input||[]){try{const v=JSON.parse(m.content);if(v.planning_snapshot)latest=v.planning_snapshot.data;else if(v.review_snapshot)latest=v.review_snapshot.data.context;else if(v.time_context)latest=v;else if(v.context)latest=v.context}catch{}}
  if(latest?.time_context?.now_ms>=1003&&raw.includes('CACHE_PRIVATE_OLD_NOTE'))throw Error('revoked_source_in_wire');
  starts.set(id,Date.now());fs.writeFileSync(path.join(work,id+'-request.json'),raw);report.requests.push({id,profile:route,index:report.requests.length,body_sha256:crypto.createHash('sha256').update(raw).digest('hex')});save();
 },
 observeResponse({id,status,body,type}){let value=null;try{if(type.includes('text/event-stream')){for(const line of body.split(/\r?\n/))if(line.startsWith('data:'))try{const v=JSON.parse(line.slice(5));if(v.response?.usage)value=v.response;else if(v.usage)value=v}catch{}}else value=JSON.parse(body)}catch{}const r=report.requests.find(r=>r.id===id);Object.assign(r,{status,usage:usageOf(value),elapsed_ms:Date.now()-starts.get(id)});save()}
});}catch(e){ledger.close();throw e}
let child, timeout;
try{
 await new Promise(r=>relay.listen(0,'127.0.0.1',r));const profiles=connection.profiles.map(p=>({...p,origin:'http://127.0.0.1:'+relay.address().port,basePath:'/'+capability+'/'+p.id}));
 const config=path.join(work,'config.json'),output=path.join(work,'plans.json');fs.writeFileSync(config,JSON.stringify({profiles,persona:path.join(connection.project,'character/persona.json'),enabled:phase!=='activity-baseline',layout:phase==='activity-complete'?'complete':'references'}));
 const log=fs.openSync(path.join(work,'probe.log'),'w');child=spawn(path.join(buildRoot,'Hibernation_CacheActivityProbe.exe'),[config,output],{windowsHide:true,stdio:['ignore',log,log],env:{...process.env,TEMP:work,TMP:work}});
 const status=await Promise.race([new Promise(r=>child.once('exit',r)),new Promise(r=>timeout=setTimeout(()=>r(-1),Math.min(330000,Date.parse(session.deadline_utc)-Date.now())))]);
 if(status!==0)throw Error('activity_probe_failed');const plans=JSON.parse(fs.readFileSync(output,'utf8'));report.completed=plans.completed;report.semantic_passes=plans.rows.filter(r=>r.status==='ok').length;report.semantic_checked=plans.rows.length;report.summary=summarize(report.requests);for(const p of profiles)report[p.id]=summarize(report.requests.filter(r=>r.profile===p.id));save();if(report.semantic_passes!==report.semantic_checked)throw Error('activity_scene_validation_failed');console.log(JSON.stringify({phase,semantic_passes:report.semantic_passes,summary:report.summary}));
}catch(e){report.error=e.message;save();console.error(e.message);process.exitCode=1}
finally{clearTimeout(timeout);if(child&&child.exitCode===null)child.kill();await new Promise(r=>relay.close(r));ledger.close()}
