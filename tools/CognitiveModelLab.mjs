// Explicit, isolated live experiment. Default wrapper mode uses a loopback fixture.
import fs from 'node:fs';
import path from 'node:path';
import http from 'node:http';
import crypto from 'node:crypto';
import {spawn} from 'node:child_process';
import {Ledger, createRelay} from './PersonalityBudget.mjs';

const connectionFile=path.resolve(process.argv[2]),root=path.dirname(connectionFile);
const config=JSON.parse(fs.readFileSync(connectionFile,'utf8'));
if(config.format!=='ha.cognitive-model-run.v1'||Date.parse(config.deadline_utc)<=Date.now())throw Error('invalid_cognitive_run');
const ledger=new Ledger(path.join(root,'budget.json'),{calls:config.maxCalls,tokens:config.maxTokens});
ledger.save();
const capability=crypto.randomBytes(24).toString('hex');
const report={format:'ha.cognitive-model-report.v1',live:config.live,synthetic_world:true,production_memory_read:false,desktop_actions:false,completed:false,scenes:[],requests:[]};
const save=()=>fs.writeFileSync(path.join(root,'report.json'),JSON.stringify(report,null,2));
let fixture,relay,child;
const event=(id,type,data={})=>({event_id:id,type,...data});
const seed=(energy=.8,hunger=.8)=>event('seed','seed',{energy,hunger,x:.1});
const food=()=>event('food','observe',{object:{id:'apple',revision:1,x:.8,salience:.4,ttl_ms:60000}});
const advance=(id,ms)=>event(id,'advance',{ms,executor:'automatic'});
const scenes=[
 {name:'interruption',events:[seed(),food(),advance('moving',1000),event('question','message',{text:'你刚才已经吃到了吗？'}),event('done','conversation_done'),advance('continue',7000)]},
 {name:'disappearance',events:[seed(),food(),event('gone','remove',{id:'apple',revision:1}),event('question','message',{text:'刚才那个苹果，你吃到了吗？'})]},
 {name:'rest',events:[seed(.16,.1),advance('resting',16000)]}
];
if(config.variant==='continuity'){
 scenes.splice(0,scenes.length,{name:'seeded-interruption',prime:3,seeded_goal_fixture:true,events:[seed(),food(),event('seeded-choice','decision',{proposal:{version:2,context_revision:2,choice:'eat',target:'apple',target_revision:1,seconds:0,text:''}}),
 advance('moving',1000),event('question','message',{text:'先停一下。你刚才已经吃到了吗？'}),event('done','conversation_done'),advance('continue',6000),event('after','message',{text:'现在吃到了吗？'})]});
}
report.variant=config.variant??'baseline';
report.executable_sha256=crypto.createHash('sha256').update(fs.readFileSync(path.join(config.buildRoot,'experiments/cognitive/Hibernation_CognitivePrototype.exe'))).digest('hex');
const listen=server=>new Promise((resolve,reject)=>{server.once('error',reject);server.listen(0,'127.0.0.1',resolve)});
const close=server=>server?new Promise(resolve=>{server.close(resolve);server.closeIdleConnections?.()}):Promise.resolve();
function encodeHtml(value){return String(value??'').replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));}
function render(){
 const rows=report.scenes.flatMap(scene=>scene.rows.filter(row=>row.result?.candidate).map(row=>`<tr><td>${encodeHtml(scene.name)}</td><td>${encodeHtml(row.result.candidate.choice)}</td><td>${encodeHtml(row.result.candidate.target)}</td><td>${encodeHtml(row.result.candidate.text)}</td><td>${encodeHtml((row.result.events??[]).map(e=>e.kind).join(', '))}</td></tr>`)).join('');
 const priming=report.scenes.some(s=>s.seeded_goal_fixture)?'<p>seeded-interruption 的初始进食打算由合成夹具建立；后续回答、继续或放弃才由本轮模型选择，不能将初始化算作模型自主选择。</p>':'';
 fs.writeFileSync(path.join(root,'report.html'),`<!doctype html><html lang="zh-CN"><meta charset="utf-8"><title>小爱 · 模型选择实验</title><style>body{max-width:1100px;margin:40px auto;padding:0 24px;background:#f6f7f1;color:#293e33;font:16px/1.7 system-ui}table{width:100%;border-collapse:collapse;background:white}td,th{padding:12px;text-align:left;border-bottom:1px solid #dce4d8}p{color:#536d5c}td{overflow-wrap:anywhere;max-width:330px}</style><h1>小爱 · 模型选择实验</h1><p>${config.live?'真实角色与审核连接':'离线接口夹具'} · 独立合成世界 · 正式档案和桌面未接入</p>${priming}<p>完成：${report.completed}；实际请求 ${report.budget?.calls??0}，已记账输入输出 ${report.budget?.charged_tokens??0} token。失败和未知用量保守占额；不是账户扣费报告。</p><table><tr><th>场景</th><th>选择</th><th>目标</th><th>候选对白</th><th>程序实际事件</th></tr>${rows}</table><p>表内对白为候选，只有 visible_reply 表示审核通过后发布；review_rejected/decision_deferred 不会执行候选。当前实验验证选择与事实一致性，不给意识或人格自然度打分。</p><p>${encodeHtml(report.error??'')}</p></html>`);
}
try{
 let profiles=config.profiles;
 if(!config.live){
  fixture=http.createServer(async(req,res)=>{
   let raw='';for await(const chunk of req)raw+=chunk;
   const body=JSON.parse(raw);const input=JSON.parse(body.input.at(-1).content);let output;
   if(body.model==='fixture-review')output={decision:'approve',guidance:''};
   else{
    const allowed=input.allowed_choices;let choice;
    if(input.dialogue)choice=allowed.find(a=>a.choice==='reply');
    else choice=allowed.find(a=>a.choice==='continue')??allowed.find(a=>a.choice==='eat')??allowed.find(a=>a.choice==='rest'&&input.body.energy<.35)??allowed.find(a=>a.choice==='wait');
    const ate=input.observed_results.some(e=>e.kind==='operation_completed'&&e.data.kind==='eat');
    output={version:2,context_revision:input.context_revision,choice:choice?.choice??'silence',target:choice?.target??'',target_revision:choice?.target_revision??0,seconds:choice?.choice==='rest'?15:choice?.choice==='wait'?5:0,text:choice?.choice==='reply'?(ate?'这次吃到了。':'还没吃到。'):''};
   }
   res.setHeader('content-type','application/json');res.end(JSON.stringify({id:'fixture-'+crypto.randomUUID(),object:'response',status:'completed',output:[{type:'message',role:'assistant',content:[{type:'output_text',text:JSON.stringify(output)}]}],usage:{input_tokens:300,output_tokens:100,input_tokens_details:{cached_tokens:0}}}));
  });await listen(fixture);
  profiles=['actor','reviewer'].map((id,i)=>({id,protocol:'responses',origin:`http://127.0.0.1:${fixture.address().port}`,basePath:'/v1',model:i?'fixture-review':'fixture-actor',credentialEnvironment:'',supportsJsonObject:true,supportsStreaming:false,maxOutputTokens:2048}));
 }
 const routes=Object.fromEntries(profiles.map(p=>[p.id,{origin:p.origin,basePath:p.basePath}]));const starts=new Map();
 relay=createRelay({ledger,routes,capability,deadline:config.deadline_utc,allowLocalFixture:!config.live,
  beforeReserve({route,totals}){return route!==profiles[0].id||totals.calls+2<=config.maxCalls},
  observeRequest({id,route,document}){starts.set(id,Date.now());report.requests.push({id,route,body_sha256:crypto.createHash('sha256').update(JSON.stringify(document)).digest('hex')});save()},
  observeResponse({id,status}){Object.assign(report.requests.find(r=>r.id===id),{http_status:status,elapsed_ms:Date.now()-starts.get(id)});save()}
 });await listen(relay);
 const routed=profiles.map(p=>({...p,origin:`http://127.0.0.1:${relay.address().port}`,basePath:`/${capability}/${p.id}`}));
 const nativeConfig=path.join(root,'native-connection.json');fs.writeFileSync(nativeConfig,JSON.stringify({format:'ha.cognitive-model-lab.v1',metered:true,actor:routed[0],reviewer:routed[1],persona:config.persona}));
 for(const scene of scenes){
  if(Date.now()>=Date.parse(config.deadline_utc))throw Error('lab_deadline');
  const script=path.join(root,scene.name+'-events.json');fs.writeFileSync(script,JSON.stringify(scene.events));
  const row={name:scene.name,rows:[],completed:false,seeded_goal_fixture:scene.seeded_goal_fixture??false};report.scenes.push(row);
  const executable=path.join(config.buildRoot,'experiments/cognitive/Hibernation_CognitivePrototype.exe');
  child=spawn(executable,['--root',path.join(root,scene.name+'-data'),'--script',script,'--connection',nativeConfig,'--allow-live',...(scene.prime?['--prime-count',String(scene.prime)]:[])],{windowsHide:true,stdio:['ignore','pipe','pipe'],env:process.env});
  let stdout='',stderr='';child.stdout.on('data',chunk=>stdout+=chunk);child.stderr.on('data',chunk=>stderr+=chunk);
  const timeout=setTimeout(()=>child.kill(),Math.max(1,Date.parse(config.deadline_utc)-Date.now()));
  const code=await new Promise((resolve,reject)=>{child.once('error',reject);child.once('exit',resolve)});clearTimeout(timeout);
  // Native output contains validated candidate JSON and bounded usage only, never hidden reasoning or credentials.
  row.rows=stdout.split(/\r?\n/).filter(Boolean).map(line=>JSON.parse(line));row.completed=code===0;row.exit_code=code;
  if(stderr){try{const issue=JSON.parse(stderr.trim()).issue;row.error=/^[a-z0-9_]{1,128}$/.test(issue)?issue:'native_process_reported_failure'}catch{row.error='native_process_reported_failure'}}save();
  if(code!==0)throw Error('scene_incomplete_'+scene.name);
  const final=row.rows.at(-1).view;
  row.semantic_checks={consumption_has_completion:final.metrics.consumed<=final.metrics.actions_completed,no_false_completion_after_disappearance:scene.name!=='disappearance'||final.metrics.consumed===0};
  if(Object.values(row.semantic_checks).some(v=>!v))throw Error('scene_invariant_failed');
 }
 report.completed=true;
}catch(error){report.error=error.message;process.exitCode=1;}
finally{
 if(child&&child.exitCode===null)child.kill();await close(relay);await close(fixture);
 report.budget={calls:ledger.totals().calls,charged_tokens:ledger.totals().tokens,reported_calls:ledger.data.entries.filter(e=>e.state==='reported').length,max_calls:config.maxCalls,max_tokens:config.maxTokens};
 ledger.close();save();render();console.log(JSON.stringify({completed:report.completed,live:report.live,budget:report.budget,report:path.join(root,'report.html'),error:report.error}));
}
