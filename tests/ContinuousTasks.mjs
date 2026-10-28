import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import http from 'node:http';
import {spawn} from 'node:child_process';
import assert from 'node:assert/strict';
import {randomUUID} from 'node:crypto';
import {pathToFileURL} from 'node:url';
const [executable,project,scratch,mode='json']=process.argv.slice(2);
if(!executable||!project||!scratch)throw Error('Expected executable, project root, scratch root, optional json/native mode');
const root=path.join(scratch,`continuous-${mode}-${randomUUID()}`);fs.mkdirSync(root,{recursive:true});
fs.writeFileSync(path.join(root,'.continuous-task-fixture'),'synthetic only');
const runtime=path.join(root,'runtime');fs.mkdirSync(runtime);for(const file of fs.readdirSync(path.dirname(executable))){if(file==='hibernation-agent.exe'||file.endsWith('.dll'))fs.copyFileSync(path.join(path.dirname(executable),file),path.join(runtime,file));}const runtimeExecutable=path.join(runtime,'hibernation-agent.exe');
const checks=[],calls=[],session='synthetic-continuous-task-session';let child,origin='',rejectCompletions=0,rememberedId="",browser=null,page=null,heldFinal=false;
const delay=ms=>new Promise(resolve=>setTimeout(resolve,ms));
const check=(condition,label)=>{assert.ok(condition,label);checks.push(label);console.log('PASS '+label);};
const decode=value=>{try{if(Array.isArray(value))value=value.filter(v=>v.type==='text'||v.type==='input_text').map(v=>v.text).join('');return JSON.parse(value);}catch{return null;}};
const api=async(route,data)=>{const response=await fetch(origin+route,{method:data===undefined?'GET':'POST',headers:{Authorization:'Bearer '+session,'Content-Type':'application/json'},body:data===undefined?undefined:JSON.stringify(data)});const body=await response.json();if(!response.ok){const error=Error(`${route}: ${response.status} ${JSON.stringify(body)}`);error.status=response.status;throw error;}return body;};
const awaitValue=async(probe,label,timeout=20000)=>{const end=Date.now()+timeout;while(Date.now()<end){const value=await probe();if(value)return value;if(child&&child.exitCode!==null)throw Error('Backend exited: '+fs.readFileSync(path.join(root,'service-error.log'),'utf8'));await delay(65);}throw Error('Timed out: '+label);};
const tool=(name,args,id)=>({version:1,kind:'call',text:null,calls:[{id,tool:name,args}]});
const reply=text=>({version:1,kind:'reply',text,calls:[]});
const silence=()=>({version:1,kind:'silence',text:null,calls:[]});
function actor(body){
 const parsed=body.messages.map(m=>({role:m.role,value:decode(m.content),nativeId:m.tool_call_id}));
 const event=parsed.find(m=>m.role==='user'&&m.value?.event_kind)?.value;
 if(!event)throw Error('Missing actual event context');
 const receipts=parsed.flatMap(m=>m.role==='tool'&&m.value?[{...m.value,fixture_call_id:m.nativeId}]:m.value?.tool_results||[]);
 const receipt=id=>receipts.find(r=>(r.fixture_call_id||r.call_id)===id);
 const task=event.current_task;
 calls.push({event:event.event_id||null,task:task?.id||null,step:task?.steps||0});
 if(task){
  const checkpoint=(status,summary,next,sourceIds=[],wait=2)=>tool('task.checkpoint',{id:task.id,expected_revision:task.revision,status,summary,next_step:next,source_ids:sourceIds,wait_seconds:wait},'checkpoint');
  if(receipt('checkpoint')?.ok&&task.goal==='checkpoint-recovery')return {fixture_hold:true};
  if(receipt('checkpoint')?.ok)return task.goal==='two-notes'&&task.steps>1?reply('两段便签已经写好。'):silence();
  if(task.goal==='two-notes'){
   if(task.steps===1){
    if(!receipt('write-one'))return tool('workspace.write',{path:'continuous/月光便签.md',content:'第一部分：实际写入。'},'write-one');
    assert.equal(receipt('write-one').ok,true);
    return checkpoint('continue','第一段便签已实际保存。','读取第一段，再写第二段。',[receipt('write-one').source_id],5);
   }
   if(!receipt('read-one'))return tool('workspace.read',{mount:'drafts',path:'continuous/月光便签.md'},'read-one');
   if(!receipt('write-two'))return tool('workspace.write',{path:'continuous/小窗台便签.md',content:'第二部分：依据已读取的第一段继续。'},'write-two');
   assert.equal(receipt('write-two').ok,true);
   return checkpoint('completed','两段便签均已实际写入。','',[receipt('write-two').source_id]);
  }
  if(task.goal==='unproved'){
   if(!receipt('false-complete'))return tool('task.checkpoint',{id:task.id,expected_revision:task.revision,status:'completed',summary:'声称完成但没有实际结果',next_step:'',source_ids:[event.task_context.references[0].id]},'false-complete');
   assert.equal(receipt('false-complete').ok,false);assert.equal(receipt('false-complete').error.code,'task_completion_evidence_required');rejectCompletions++;
   return checkpoint('waiting_input','没有可确认的完成依据，等待补充。','等待资料。',[event.task_context.references[0].id]);
  }
  if(task.goal==='needs-details'){
   if(task.steps===1)return checkpoint('waiting_input','需要用户提供标题。','等待标题。',[event.task_context.references[0].id]);
   if(!receipt('write-details'))return tool('workspace.write',{path:'continuous/details.txt',content:'用户同意继续后的实际成果。'},'write-details');
   return checkpoint('completed','补充后已实际保存成果。','',[receipt('write-details').source_id]);
  }
  if(task.goal==='no-progress')return checkpoint('continue','暂时还没有实际结果。','再考虑一次。');
  if(task.goal==='checkpoint-recovery'){
   if(!receipt('write-recovery'))return tool('workspace.write',{path:'continuous/recovery.txt',content:'恢复前已经实际保存。'},'write-recovery');
   return checkpoint('completed','成果已经保存，检查点也已提交。','',[receipt('write-recovery').source_id]);
  }
  if(task.goal==='hold'){
   if(!receipt('write-held'))return tool('workspace.write',{path:'continuous/held.txt',content:'这一步已实际写入。'},'write-held');
   return checkpoint('continue','已保存第一步。','等待后再处理。',[receipt('write-held').source_id],3600);
  }
  throw Error('Unknown fixture task '+task.goal);
 }
 if(event.input==='稍后继续的任务'){
  if(!receipt('remember-hold'))return tool('memory.remember',{kind:'conversation',text:'用户希望这项任务稍后继续。',source_ids:[event.event_id]},'remember-hold');
  const memory=receipt('remember-hold');assert.equal(memory.ok,true);rememberedId=memory.result.id||memory.result.memory_id||memory.result.memory?.id;if(!rememberedId)throw Error('Fixture expected memory id, received keys '+Object.keys(memory.result));
  if(!receipt('accept-task'))return tool('task.start',{goal:'hold',completion_criterion:'实际写入一份便签。',source_id:event.event_id},'accept-task');
 }
 if(receipts.length)return reply('已经按你的意思处理。');
 const input=event.input;
 const goals={'分两段做便签':'two-notes','没有证据的任务':'unproved','需要补充的任务':'needs-details','没有进展的任务':'no-progress','稍后继续的任务':'hold','检查点恢复任务':'checkpoint-recovery'};
 if(goals[input]){const result=tool('task.start',{goal:goals[input],completion_criterion:'实际保存指定成果，只有真实结果才算完成。',source_id:event.event_id},'accept-task');if(input==='分两段做便签')result.calls.push({id:'premature-write',tool:'workspace.write',args:{path:'continuous/should-not-write.txt',content:'acceptance must yield'}});return result;}
 if(input==='继续补充任务'){
  const task=event.ongoing_tasks.tasks.find(t=>t.goal==='needs-details');assert.ok(task);
  return tool('task.control',{id:task.id,expected_revision:task.revision,action:'resume',source_id:event.event_id},'resume-task');
 }
 return reply('好，聊这个。');
}
const provider=http.createServer(async(req,res)=>{try{
 let raw='';for await(const chunk of req)raw+=chunk;const body=JSON.parse(raw);
 const reviewing=(body.messages[0]?.content||'').includes('独立的角色表现');
 const result=reviewing?{decision:'approve',reason:'ok',guidance:''}:actor(body);
 if(result.fixture_hold){heldFinal=true;await new Promise(resolve=>{const timer=setTimeout(resolve,11000);res.once('close',()=>{clearTimeout(timer);resolve();});});if(res.destroyed)return;Object.assign(result,silence());delete result.fixture_hold;}
 let message,reason='stop';
 if(!reviewing&&mode==='native'){
  const names=n=> 'ha_'+n.replaceAll('_','_US_').replaceAll('.','_DOT_');
  const planned=result.kind==='call'?result.calls.map(c=>({name:names(c.tool),args:c.args,id:c.id})):[{name:result.kind==='reply'?'ha_turn_reply':'ha_turn_silence',args:result.kind==='reply'?{text:result.text}:{},id:'final'}];
  message={role:'assistant',content:null,tool_calls:planned.map(c=>({id:c.id,type:'function',function:{name:c.name,arguments:JSON.stringify(c.args)}}))};reason='tool_calls';
 }else message={role:'assistant',content:JSON.stringify(result)};
 res.writeHead(200,{'Content-Type':'application/json'});res.end(JSON.stringify({id:'synthetic-task-response',model:'fixture',choices:[{index:0,message,finish_reason:reason}],usage:{prompt_tokens:80,completion_tokens:25,total_tokens:105}}));
 }catch(error){console.error('PROVIDER FIXTURE ERROR',error.stack);res.writeHead(500);res.end('{}');}});
await new Promise(resolve=>provider.listen(0,'127.0.0.1',resolve));
const config=JSON.parse(fs.readFileSync(path.join(project,'config/example.json'),'utf8').replace(/^\uFEFF/,''));
Object.assign(config,{dataRoot:path.join(root,'data'),draftsRoot:path.join(root,'drafts'),materialsRoot:path.join(root,'materials'),characterRoot:path.join(project,'character'),uiRoot:path.join(project,'ui'),pluginRoot:path.join(root,'no-plugins'),plugins:{},computer:{enabled:false},solitudeIntervalMs:604800000,idleBeforeSolitudeMs:86400000,reflectOnActivities:false,maxToolRounds:4,continuousTasks:{enabled:true,maxActive:3,maxSteps:8}});
config.profiles=[config.profiles.find(p=>p.protocol==='mock'),{id:'fixture',label:'Synthetic task actor',enabled:true,protocol:'chat-completions',origin:`http://127.0.0.1:${provider.address().port}`,basePath:'/v1',model:'fixture',credentialEnvironment:'HIBERNATION_CONTINUOUS_FIXTURE',supportsToolCalls:mode==='native',supportsJsonObject:true,supportsJsonSchema:false,supportsStreaming:false,timeoutMs:10000,maxOutputTokens:2048}];
config.review={enabled:true,profileId:'fixture',maxRewrites:1};
const configPath=path.join(root,'config.json'),readyPath=path.join(root,'ready.json');fs.writeFileSync(configPath,JSON.stringify(config,null,2));
async function start(){
 try{fs.unlinkSync(readyPath);}catch{}
 const out=fs.openSync(path.join(root,'service.log'),'a'),err=fs.openSync(path.join(root,'service-error.log'),'a');
 child=spawn(runtimeExecutable,['--config',configPath,'--ready-file',readyPath],{windowsHide:true,env:{...process.env,HIBERNATION_SESSION_KEY:session,HIBERNATION_CONTINUOUS_FIXTURE:'synthetic-provider-key'},stdio:['ignore',out,err]});fs.closeSync(out);fs.closeSync(err);
 await awaitValue(()=>{try{const r=JSON.parse(fs.readFileSync(readyPath,'utf8'));if(r.pid===child.pid){origin='http://127.0.0.1:'+r.port;return true;}}catch{}},'backend readiness');
 await api('/api/model',{id:'fixture'});
}
async function stop(){if(!child||child.exitCode!==null)return;const saved=child;await api('/api/shutdown',{});await awaitValue(()=>saved.exitCode!==null,'graceful exit');child=null;}
async function say(text){const request_id='task-fixture-'+randomUUID();const accepted=await api('/api/message',{request_id,text});await awaitValue(async()=>{const state=await api('/api/state');return state.messages.some(m=>m.kind==='assistant_message'&&m.request_id===request_id);},'approved foreground reply');return accepted;}
async function taskWith(goal,predicate=()=>true){return await awaitValue(async()=>{const list=await api('/api/tasks');return list.tasks.find(t=>t.goal===goal&&predicate(t));},'task '+goal);}
async function operator(task,action){return api('/api/tasks/control',{id:task.id,expected_revision:task.revision,action});}
async function openDashboard(){
 if(browser)return;const entry=process.env.PLAYWRIGHT_PACKAGE_PATH;if(!entry)throw Error('Playwright package entry is required for forms mode');
 const {chromium}=await import(pathToFileURL(entry).href);browser=await chromium.launch({executablePath:process.env.HIBERNATION_TEST_BROWSER,headless:true,args:['--disable-gpu']});page=await browser.newPage({viewport:{width:1100,height:900}});await page.goto(origin+'/#session='+session);await page.locator('#continuous-tasks').waitFor({state:'visible'});const fold=page.locator('#continuous-tasks > .fold-head > .fold-toggle');await fold.waitFor({state:'visible'});if(await fold.getAttribute('aria-expanded')!=='true')await fold.click();await page.locator('#continuous-tasks h3').waitFor({state:'visible'});check(await page.locator('#continuous-tasks').getByRole('heading',{name:'说完之后，事情仍可以继续。'}).count()===1,'dashboard serves and renders the real continuous-task module');
}
async function button(task,action){const target=page.locator(`#continuous-tasks button[data-task-id="${task.id}"][data-task-action="${action}"]`);await target.waitFor({state:'visible'});await page.waitForFunction(({id,action,revision})=>{const b=document.querySelector(`#continuous-tasks button[data-task-id="${id}"][data-task-action="${action}"]`);return b&&!b.disabled&&Number(b.dataset.taskRevision)>=revision;},{id:task.id,action,revision:task.revision});await target.click();}

try{
 await start();const before=calls.length;await say('你好');check((await api('/api/tasks')).tasks.length===0,'ordinary conversation does not create a task');check(calls.length===before+1,'conversation routing uses the same actor call without an extra classifier');
 await say('分两段做便签');let task=await taskWith('two-notes');check(!fs.existsSync(path.join(root,'drafts/continuous/should-not-write.txt')),'accepting a goal prevents remaining calls in the same batch from doing its work twice');check(task.status!=='completed','acceptance and the initial reply do not claim goal completion');
 task=await taskWith('two-notes',t=>t.status==='ready'&&t.steps===1);check(fs.existsSync(path.join(root,'drafts/continuous/月光便签.md')),'first background slice actually writes its result after the initial reply');
 const firstBytes=fs.readFileSync(path.join(root,'drafts/continuous/月光便签.md'),'utf8'),firstModified=fs.statSync(path.join(root,'drafts/continuous/月光便签.md')).mtimeMs;
 await say('先聊句别的');check((await api('/api/tasks')).tasks.some(t=>t.id===task.id&&!['abandoned','completed'].includes(t.status)),'unrelated new conversation preserves the accepted goal');
 await stop();await start();task=await taskWith('two-notes',t=>t.status==='completed');
 check(task.steps===2,'a saved checkpoint resumes automatically after a real backend restart');check(fs.readFileSync(path.join(root,'drafts/continuous/月光便签.md'),'utf8')===firstBytes&&fs.statSync(path.join(root,'drafts/continuous/月光便签.md')).mtimeMs===firstModified,'restart does not repeat the already completed file write');check(fs.readFileSync(path.join(root,'drafts/continuous/小窗台便签.md'),'utf8').includes('第二部分'),'later slice performs the dependent second operation');
 await say('检查点恢复任务');await awaitValue(()=>heldFinal,'persisted checkpoint before final response');const callsBeforeRecovery=calls.length;const recoveryModified=fs.statSync(path.join(root,'drafts/continuous/recovery.txt')).mtimeMs;await stop();await start();await taskWith('checkpoint-recovery',t=>t.status==='completed');check(calls.length===callsBeforeRecovery,'restart settles an approved checkpoint without regenerating the interrupted final response');check(fs.statSync(path.join(root,'drafts/continuous/recovery.txt')).mtimeMs===recoveryModified,'checkpoint recovery does not repeat external file effects');
 await say('没有证据的任务');task=await taskWith('unproved',t=>t.status==='waiting_input');check(rejectCompletions===1,'program rejects completion based only on the original request even when the reviewer approves');check(!fs.existsSync(path.join(root,'drafts/continuous/unproved.txt')),'rejected completion does not fabricate a result');await operator(task,'cancel');
 await say('需要补充的任务');task=await taskWith('needs-details',t=>t.status==='waiting_input');const pausedCalls=calls.filter(c=>c.task===task.id).length;await delay(400);check(calls.filter(c=>c.task===task.id).length===pausedCalls,'waiting for input stops autonomous model calls');if(mode==='forms'){await openDashboard();await button(task,'resume');}else await say('继续补充任务');task=await taskWith('needs-details',t=>t.status==='completed');check(fs.existsSync(path.join(root,'drafts/continuous/details.txt')),'a later user instruction resumes the waiting task through the same conversation');
 await say('没有进展的任务');task=await taskWith('no-progress',t=>t.status==='blocked');check(task.last_issue==='task_no_progress'&&task.steps===3,'three empty continuations stop instead of consuming calls indefinitely');await operator(task,'cancel');
 const accepted=await say('稍后继续的任务');task=await taskWith('hold',t=>t.status==='ready'&&t.steps===1);let stale=false;try{await api('/api/tasks/control',{id:task.id,expected_revision:task.revision-1,action:'cancel'});}catch(e){stale=e.status===409;}check(stale,'operator task control refuses stale revisions');
 const access=await api('/api/access',{id:rememberedId,accessible:false});let denied=false;try{await operator(task,'resume');}catch(e){denied=e.status===409;}check(denied,'revoked instruction access prevents resuming the task');
 await api('/api/access',{id:rememberedId,accessible:true});task=await taskWith('hold');if(mode==='forms')await button(task,'pause');else await operator(task,'pause');task=await taskWith('hold',t=>t.status==='paused');check(task.status==='paused','operator pause retains the goal and actual progress');if(mode==='forms'){await page.screenshot({path:path.join(root,'dashboard.png'),fullPage:true});await button(task,'cancel');await taskWith('hold',t=>t.status==='abandoned');check(true,'dashboard cancel uses the actual versioned task endpoint');}else await operator(task,'cancel');
 await say('稍后继续的任务');task=await taskWith('hold',t=>t.status==='ready'&&t.steps===1);await stop();config.draftsRoot=path.join(root,'different-drafts');fs.writeFileSync(configPath,JSON.stringify(config,null,2));await start();let workspaceDenied=false;try{await operator(task,'resume');}catch(e){workspaceDenied=e.status===409;}check(workspaceDenied&&!fs.existsSync(path.join(root,'different-drafts/continuous/held.txt')),'old goals cannot migrate effects into a newly configured workspace');await operator(task,'cancel');
 await stop();config.maxToolRounds=1;fs.writeFileSync(configPath,JSON.stringify(config,null,2));await start();await say('需要补充的任务');task=await taskWith('needs-details',t=>t.status==='blocked');check(task.last_issue==='task_requires_two_tool_rounds'&&!calls.some(c=>c.task===task.id),'insufficient operation rounds stop a task before starting a paid background request');await operator(task,'cancel');
 await stop();check(!fs.readFileSync(path.join(root,'data/archive/records.sqlite')).includes(Buffer.from('two-notes')),'goal and checkpoint storage remains encrypted on disk');const log=fs.readFileSync(path.join(root,'service.log'),'utf8');check(!log.includes(session)&&!log.includes('synthetic-provider-key'),'startup logs do not expose session or model credentials');
 fs.writeFileSync(path.join(root,'acceptance.json'),JSON.stringify({completed:true,checks,actorCalls:calls.length,mode,root,productionDataUsed:false},null,2));console.log(JSON.stringify({completed:true,checks:checks.length,actorCalls:calls.length,root}));
}catch(error){if(page)await page.screenshot({path:path.join(root,'failure.png'),fullPage:true}).catch(()=>{});try{fs.writeFileSync(path.join(root,'failure.json'),JSON.stringify({error:error.message,tasks:await api('/api/tasks'),calls},null,2));}catch{}throw error;}finally{if(browser)await browser.close();if(child&&child.exitCode===null){try{await stop();}catch{child.kill();}}provider.closeAllConnections();await new Promise(resolve=>provider.close(resolve));}
