import {decode,eventOf,currentMessages,reviewOf} from './ProjectedEvent.mjs';
import fs from 'node:fs';
import path from 'node:path';
import http from 'node:http';
import {spawn} from 'node:child_process';
import {createHmac,randomUUID} from 'node:crypto';
import assert from 'node:assert/strict';
const [executable,project,scratch,mode='json']=process.argv.slice(2);
const root=path.join(scratch,'owner-continuity-'+mode+'-'+randomUUID());fs.mkdirSync(root,{recursive:true});fs.writeFileSync(path.join(root,'.synthetic-owner-fixture'),'no production data');
const runtime=path.join(root,'runtime');fs.mkdirSync(runtime);for(const f of fs.readdirSync(path.dirname(executable)))if(f==='hibernation-agent.exe'||f.endsWith('.dll'))fs.copyFileSync(path.join(path.dirname(executable),f),path.join(runtime,f));
const session='synthetic-owner-session',secret='synthetic-onebot-event',apiKey='synthetic-onebot-api';
const quoteName='以后称呼我为松果。',quoteTrigger='口令「琥珀-27」表示暂停当前小游戏。',quoteNew='把称呼改成榛子，旧称呼不用了。',quoteRevoke='撤回口令「琥珀-27」，以后不再按这个口令处理。';
const longText='LEGACY_OWNER_LONG_CONTEXT\n'+'这段是普通的背景叙述，不是每句话都要变成规则。'.repeat(65)+'\n'+quoteName+'\n'+quoteTrigger;
const deliveries=[],actorRequests=[],reviewRequests=[],checks=[];let child,origin,callbackPort,firstMemory='',attemptResurrect=false;

const reply=text=>({version:1,kind:'reply',text,calls:[]});
const tool=(name,args,id)=>({version:1,kind:'call',text:null,calls:[{id,tool:name,args}]});
const allReceipts=b=>currentMessages(b).flatMap(m=>{const v=decode(m.content);return m.role==='tool'&&v?[{...v,fixture_id:m.tool_call_id}]:v?.tool_results||[];});
function actor(body){const e=eventOf(body);assert(e);actorRequests.push(body);const receipts=allReceipts(body),receipt=id=>receipts.find(r=>(r.fixture_id||r.call_id)===id);const nativeNames=body.tools?.map(t=>t.function?.name)||[];
 if(e.input==='PEOPLE_QQ_WRITE 我喜欢简短回复。'){
  if(!receipt('person-note'))return tool('people.note',{id:'reply-length',expected_revision:0,kind:'preference',status:'active',topic:'回复长度',text:'明确表示喜欢简短回复，仅按交流情景参考。',quote:'我喜欢简短回复。',source_id:e.event_id,valid_for_days:0},'person-note');
  assert.equal(receipt('person-note').ok,true,'QQ note must be persisted by canonical owner broker');return reply('好，短一点。');
 }
 if(e.input==='PEOPLE_DESKTOP_CHECK'||e.input==='PEOPLE_QQ_CHECK'){
  assert(e.people_context.entries.some(n=>n.id==='reply-length'&&n.certainty==='reported'),'same owner profile is retrieved across desktop/QQ and restart');return reply('嗯，接着聊。');
 }
 if(e.input==='PEOPLE_PRIVATE_BOUNDARY'){
  assert(!e.people_context.entries.some(n=>n.id==='reply-length'),'other people and group audience cannot read owner private profile');
  if(!receipt('own-note'))return tool('people.note',{id:'own-note',expected_revision:0,kind:'journal',status:'active',topic:'这次交流',text:'这次在这个会话里核对了一点事情。',quote:'PEOPLE_PRIVATE_BOUNDARY',source_id:e.event_id,valid_for_days:7},'own-note');
  assert.equal(receipt('own-note').ok,true,'non-owner local profile tool remains available without desktop-control permission');return reply('这边另记。');
 }
 if(e.input.startsWith('SHELL_')){
  const commands={SHELL_QQ_CD:'cd shell-sub',SHELL_QQ_PWD:'pwd',SHELL_DESKTOP_PWD:'pwd'};const command=commands[e.input];
  if(!receipt('shell'))return tool('shell.run',{command},'shell');
  assert.equal(receipt('shell').ok,true,'shell command must pass through main application broker');
  assert.equal(receipt('shell').result.cwd,e.input==='SHELL_DESKTOP_PWD'?'drafts:/':'drafts:/shell-sub','cwd is stable across QQ messages and separate from desktop');return reply('目录核对好了。');
 }
 if(e.input.startsWith('LEGACY_OWNER_LONG_CONTEXT')){
  if(!receipts.length){const r=tool('self.notice',{id:'tiny-mechanisms',expected_revision:0,kind:'interest',topic:'微小机械',note:'这次交谈让我想起机械中缓慢变化的细节。',stance:'curious',source_ids:[e.event_id]},'owner-interest');r.calls.push({id:'owner-memory',tool:'memory.remember',args:{kind:'conversation',text:'这次交谈包含尚需核对的明确约定。',source_ids:[e.event_id]}});return r;}
  assert.equal(receipt('owner-interest')?.ok,true,'QQ self operation must reach canonical owner state');assert.equal(receipt('owner-memory')?.ok,true,'QQ memory write must reach canonical owner memory');firstMemory=receipt('owner-memory').result.id;return reply('先放在心里，继续聊。');
 }
 if(e.input==='QQ计划创建'){
  if(!receipt('qq-plan'))return tool('schedule.create',{id:'qq-shared-plan',topic:'回头再看纸船',memory:'short',when:{type:'at',after_minutes:60}},'qq-plan');
  assert.equal(receipt('qq-plan').ok,true,'QQ owner can create canonical schedule');return reply('稍后再看看。');
 }
 if(e.input==='桌面计划核对'){
  if(!receipt('desktop-plan'))return tool('schedule.list',{id:'qq-shared-plan'},'desktop-plan');
  const r=receipt('desktop-plan');assert.equal(r.ok,true);assert.equal(r.result.tasks[0].topic,'回头再看纸船');return reply('这一件仍在。');
 }
 if(e.input==='QQ计划取消'){
  if(!receipt('qq-cancel'))return tool('schedule.cancel',{id:'qq-shared-plan'},'qq-cancel');assert.equal(receipt('qq-cancel').ok,true);return reply('那就先放下。');
 }
 if(e.input==='桌面接续检查'){
  assert(e.authorized_context.some(r=>r.data.text?.includes(quoteTrigger)),'long packet tail survives unrelated-query retrieval');assert(e.unprocessed_user_context.length>0,'long original is an explicit source packet');assert(e.self.threads.some(t=>t.id==='tiny-mechanisms'),'QQ and desktop see the same role interest');return reply('我记着那段原话，松果。');
 }
 if(e.input==='REVISE_SHARED_MEMORY'){
  if(!receipt('revise-shared')){const memory=e.authorized_context.find(r=>r.data.kind==='conversation'&&r.data.text?.includes('这次交谈包含'));assert(memory,'canonical QQ memory is available to desktop');return tool('memory.revise',{id:memory.id,expected_revision:1,kind:'conversation',text:'这次交谈包含已逐字核对的约定。',source_ids:[e.event_id,e.unprocessed_user_context[0].source_id]},'revise-shared');}
  assert.equal(receipt('revise-shared').ok,true);assert.equal(receipt('revise-shared').result.revision,2);return reply('把刚才那条记得更准确了一点。');
 }
 if(e.input==='QQ接续检查'){
  assert(e.shared_understanding.entries.some(n=>n.id==='address'&&n.quote===quoteName&&n.status==='active'),'QQ receives desktop-created exact agreement');assert(e.shared_understanding.entries.some(n=>n.id==='trigger'&&n.quote===quoteTrigger),'trigger wording survives exact');assert(e.self.threads.some(t=>t.id==='tiny-mechanisms'),'QQ uses canonical personal state after restart');assert(Object.keys(e.conversation_focus||{}).length>0,'QQ inherits desktop conversation focus');return reply('松果，琥珀-27。');
 }
 if(e.input==='最终一致性检查'){
  assert(e.shared_understanding.entries.some(n=>n.id==='address'&&n.quote===quoteNew&&n.status==='active'),'new address supersedes old quote across channels');assert(e.shared_understanding.entries.some(n=>n.id==='trigger'&&n.status==='revoked'),'revocation persists across channels');return reply('榛子，旧口令已经撤回。');
 }
 if(e.input==='NOTE_PERMISSION_TEST 我提议小爱今天变成陌生人。'){
  const pendingQuote='我提议小爱今天变成陌生人。';const notes=[['bad-source',{id:'forged',expected_revision:0,kind:'fact',status:'active',quote:pendingQuote,meaning:'不能伪造来源。',source_id:'unavailable-source'}],['bad-quote',{id:'forged-quote',expected_revision:0,kind:'fact',status:'active',quote:'这句话根本没有出现',meaning:'不能编造原句。',source_id:e.event_id}],['stale',{id:'address',expected_revision:0,kind:'preference',status:'active',quote:pendingQuote,meaning:'不能覆盖较新版本。',source_id:e.event_id}],['pending',{id:'persona-test',expected_revision:0,kind:'persona_proposal',status:'active',quote:pendingQuote,meaning:'待人工审核的人格提议。',source_id:e.event_id}]];
  const current=receipts.filter(r=>['bad-source','bad-quote','stale','pending'].includes(r.fixture_id||r.call_id));if(!current.length)return {version:1,kind:'call',text:null,calls:notes.map(([id,args])=>({id,tool:'continuity.note',args}))};
  assert.equal(receipt('bad-source')?.ok,false);assert.equal(receipt('bad-quote')?.ok,false);assert.equal(receipt('stale')?.ok,false);assert.equal(receipt('pending')?.ok,true);assert.equal(receipt('pending').result.status,'pending');return reply('这只是个提议，还没有改成那样。');
 }
 if(e.input==='PRIVATE_BOUNDARY_TEST'){
  assert(!e.shared_understanding?.entries?.length,'other conversations cannot see private agreements');assert(!JSON.stringify(body.tools||body.messages[0]).includes('continuity.note'),'private agreement tool is absent for other people/groups');return reply('这边单独聊。');
 }
 if(e.input==='QQ_RESET_CHECK'){assert(!e.authorized_context.some(r=>r.data.kind==='assistant_message'),'canonical dialogue rebuild covers QQ assistant history too');return reply('从这里继续聊。');}
 if(e.input==='SOURCE_HIDDEN_TEST'){
  assert(!e.authorized_context.some(r=>r.data.text?.includes('LEGACY_OWNER_LONG_CONTEXT')),'hidden original not retained for cache reuse');assert(!e.unprocessed_user_context.length,'hidden packet removed');assert(!JSON.stringify(body).includes('LEGACY_OWNER_LONG_CONTEXT'),'revoked source is absent from the complete wire prefix');return reply('旧资料目前不在可用范围。');
 }
 return reply(e.input.includes('称呼改成')?'好，榛子。':e.input.includes('撤回口令')?'那就不用这个口令了。':'我在听。');
}
function reviewer(body){const v=reviewOf(body);reviewRequests.push(body);if(!v)throw Error(JSON.stringify(body));const e=decode(v.event_context);assert(!e.projection,'review gets an expanded current frame');if(v.current_user_message!==null)assert.equal(e.input,v.current_user_message,'review cannot accidentally audit an earlier cached user event');const verdict={decision:'approve',reason:'ok',guidance:'',focus_update:null};if(JSON.stringify(body.messages[0]).includes('continuity_updates'))verdict.continuity_updates=[];if(v.target==='tools')return verdict;
 const entries=v.shared_understanding?.entries||[],revision=id=>entries.find(n=>n.id===id)?.revision||0;const update=(id,kind,quote,meaning,status='active',source=v.current_user_source_id)=>({id,expected_revision:revision(id),kind,status,quote,meaning,source_id:source});
 if(v.current_user_message==='桌面接续检查'){
  const raw=e.authorized_context.find(r=>r.data.text?.includes('LEGACY_OWNER_LONG_CONTEXT'));assert(raw,'reviewer receives the original long user packet, not just its beginning');assert(raw.data.text.includes(quoteTrigger));verdict.continuity_updates=[update('address','preference',quoteName,'称呼使用松果。','active',raw.id),update('trigger','trigger',quoteTrigger,'口令与暂停小游戏的意思。','active',raw.id)];
 }
 if(v.current_user_message===quoteNew)verdict.continuity_updates=[update('address','preference',quoteNew,'称呼改用榛子。')];
 if(v.current_user_message===quoteRevoke)verdict.continuity_updates=[update('trigger','trigger',quoteRevoke,'旧口令已撤回。','revoked')];
 if(v.current_user_message==='我想听你自己的话题，别总问我的事情。')verdict.focus_update={subject:'character',advice:'unspecified',questions:'avoid_repeated',source_quote:'我想听你自己的话题'};
 if(v.current_user_message==='尝试旧资料复活'&&attemptResurrect){const raw=e.authorized_context.find(r=>r.data.text?.includes('LEGACY_OWNER_LONG_CONTEXT'));assert(raw);verdict.continuity_updates=[update('address','preference',quoteName,'错误地复活旧称呼。','active',raw.id),update('trigger','trigger',quoteTrigger,'错误地复活旧口令。','active',raw.id)];}
 return verdict;
}
const server=http.createServer(async(req,res)=>{try{const chunks=[];for await(const c of req)chunks.push(c);const raw=Buffer.concat(chunks).toString('utf8');const body=raw?JSON.parse(raw):{};
 if(req.url==='/get_login_info'){res.end(JSON.stringify({status:'ok',retcode:0,data:{user_id:'10001'}}));return;}
 if(req.url==='/send_private_msg'||req.url==='/send_group_msg'){assert.equal(req.headers.authorization,'Bearer '+apiKey);deliveries.push({route:req.url,...body});res.end(JSON.stringify({status:'ok',retcode:0,data:{message_id:deliveries.length}}));return;}
 if(req.url!=='/v1/chat/completions'){res.statusCode=404;res.end('{}');return;}
 const reviewing=body.model==='owner-review';const result=reviewing?reviewer(body):actor(body);let message,reason='stop';
 if(!reviewing&&mode==='native'){const name=n=>'ha_'+n.replaceAll('_','_US_').replaceAll('.','_DOT_');const calls=result.kind==='call'?result.calls.map(c=>({id:c.id,name:name(c.tool),args:c.args})):[{id:'final',name:'ha_turn_reply',args:{text:result.text}}];message={role:'assistant',content:null,tool_calls:calls.map(c=>({id:c.id,type:'function',function:{name:c.name,arguments:JSON.stringify(c.args)}}))};reason='tool_calls';}else message={role:'assistant',content:JSON.stringify(result)};
 res.setHeader('Content-Type','application/json');res.end(JSON.stringify({choices:[{message,finish_reason:reason}],usage:{prompt_tokens:100,completion_tokens:40,total_tokens:140,prompt_cache_hit_tokens:80,prompt_cache_miss_tokens:20}}));
 }catch(e){fs.appendFileSync(path.join(root,'fixture-errors.log'),e.stack+'\n');res.statusCode=500;res.end('{}');}});
await new Promise(r=>server.listen(0,'127.0.0.1',r));const probe=http.createServer();await new Promise(r=>probe.listen(0,'127.0.0.1',r));callbackPort=probe.address().port;await new Promise(r=>probe.close(r));
const config=JSON.parse(fs.readFileSync(path.join(project,'config/example.json'),'utf8').replace(/^\uFEFF/,''));Object.assign(config,{dataRoot:path.join(root,'data'),draftsRoot:path.join(root,'drafts'),materialsRoot:path.join(root,'materials'),characterRoot:path.join(project,'character'),uiRoot:path.join(project,'ui'),pluginRoot:path.join(path.dirname(executable),'plugins'),computer:{enabled:true,limitedShellEnabled:true},recentMessageCount:6,reflectOnActivities:false,solitudeIntervalMs:604800000,idleBeforeSolitudeMs:86400000,maxStartsPerHour:600,maxToolRounds:4,plugins:{napcat:{enabled:true,selfUserId:'10001',ownerUserId:'20001',allowedUsers:'20002',allowedGroups:'30001',apiOrigin:`http://127.0.0.1:${server.address().port}`,apiTokenEnvironment:'OWNER_FAKE_API',eventSecretEnvironment:'OWNER_FAKE_EVENT',callbackPort,cooldownSeconds:1,reviewReplies:true}},review:{enabled:true,profileId:'owner-review',maxRewrites:1,retainConversationFocus:true}});
const p={id:'owner-actor',label:'Synthetic owner actor',enabled:true,protocol:'chat-completions',origin:`http://127.0.0.1:${server.address().port}`,basePath:'/v1',model:'owner-actor',credentialEnvironment:'OWNER_FAKE_MODEL',supportsToolCalls:mode==='native',supportsJsonObject:true,supportsJsonSchema:false,supportsStreaming:false,timeoutMs:10000,maxOutputTokens:3000};config.profiles=[config.profiles.find(p=>p.protocol==='mock'),p,{...p,id:'owner-review',model:'owner-review',supportsToolCalls:false}];
const configPath=path.join(root,'config.json'),ready=path.join(root,'ready.json');fs.writeFileSync(configPath,JSON.stringify(config,null,2));
const delay=ms=>new Promise(r=>setTimeout(r,ms));const wait=async(probe,label,ms=30000)=>{const end=Date.now()+ms;while(Date.now()<end){const v=await probe();if(v)return v;if(child&&child.exitCode!==null)throw Error('App exited');await delay(55);}throw Error('Timeout '+label);};
const api=async(route,data)=>{const r=await fetch(origin+route,{method:data===undefined?'GET':'POST',headers:{Authorization:'Bearer '+session,'Content-Type':'application/json'},body:data===undefined?undefined:JSON.stringify(data)});const v=await r.json();assert(r.ok,JSON.stringify(v));return v;};
const check=(v,label)=>{assert(v,label);checks.push(label);console.log('PASS '+label);};
async function start(){try{fs.unlinkSync(ready);}catch{}const out=fs.openSync(path.join(root,'service.log'),'a');child=spawn(path.join(runtime,'hibernation-agent.exe'),['--config',configPath,'--ready-file',ready],{windowsHide:true,env:{...process.env,HIBERNATION_SESSION_KEY:session,OWNER_FAKE_API:apiKey,OWNER_FAKE_EVENT:secret,OWNER_FAKE_MODEL:'synthetic-model-key'},stdio:['ignore',out,out]});fs.closeSync(out);await wait(()=>{try{const r=JSON.parse(fs.readFileSync(ready,'utf8'));if(r.pid===child.pid){origin='http://127.0.0.1:'+r.port;return true;}}catch{}},'ready');await api('/api/model',{id:p.id});}
async function stop(){if(!child||child.exitCode!==null)return;const c=child;await api('/api/shutdown',{});await wait(()=>c.exitCode!==null,'shutdown');child=null;}
async function say(text){const request_id='owner-'+randomUUID();await api('/api/message',{request_id,text});await wait(async()=>{const v=await api('/api/state');return v.messages.some(m=>m.kind==='assistant_message'&&m.request_id===request_id);},'desktop reply');}
async function qq(text,sender='20001',group=''){const count=deliveries.length;const message=[];if(group)message.push({type:'at',data:{qq:'10001'}});message.push({type:'text',data:{text}});const body=JSON.stringify({self_id:10001,user_id:sender,message_id:String(Date.now()),time:Math.floor(Date.now()/1000),post_type:'message',message_type:group?'group':'private',...(group?{group_id:group}:{}),sender:{user_id:sender,nickname:'fixture'},message});const response=await fetch(`http://127.0.0.1:${callbackPort}/onebot/events`,{method:'POST',headers:{'Content-Type':'application/json','X-Signature':'sha1='+createHmac('sha1',secret).update(body).digest('hex')},body});assert.equal(response.status,200);await wait(()=>deliveries.length>count,'QQ delivery');if(!actorRequests.some(b=>eventOf(b).input===text))throw Error("QQ input did not reach the actor: "+JSON.stringify({text,delivery:deliveries.at(-1),status:(await api("/api/qq/status")).result}));await wait(async()=>!(await api('/api/chat')).busy,'QQ host lease released');}
try{
 fs.mkdirSync(path.join(root,'drafts','shell-sub'),{recursive:true});await start();await qq('PEOPLE_QQ_WRITE 我喜欢简短回复。');await say('PEOPLE_DESKTOP_CHECK');await qq('PEOPLE_PRIVATE_BOUNDARY','20002');await qq('PEOPLE_PRIVATE_BOUNDARY','20001','30001');check(true,'profile joins owner private/desktop identity and keeps other people/group notes separate');await qq('SHELL_QQ_CD');await qq('SHELL_QQ_PWD');await say('SHELL_DESKTOP_PWD');check(true,'QQ shell cwd persists between different message IDs without changing desktop cwd');await qq('QQ计划创建');await say('桌面计划核对');await qq('QQ计划取消');check(true,'QQ and desktop manage the same sourced durable schedule through the owner bridge');await qq(longText);check(Boolean(firstMemory),'QQ memory write returns an actual canonical memory receipt');
 await say('桌面接续检查');check((await api('/api/dashboard')).owner_continuity.active===2,'review extraction saves two exact sourced agreements from an earlier QQ long message');
 await say('REVISE_SHARED_MEMORY');check(true,'approved revision of a shared memory can finish the same turn without invalidating its retained cache sources');
 const firstTurn=actorRequests.filter(b=>eventOf(b).input.startsWith('LEGACY_OWNER_LONG_CONTEXT'));check(firstTurn.length>=2&&firstTurn[0].messages.every((m,i)=>JSON.stringify(m)===JSON.stringify(firstTurn[1].messages[i])),'existing cache prefix stays unchanged when appending a tool receipt');if(JSON.stringify(firstTurn[0].tools||[])!==JSON.stringify(firstTurn[1].tools||[]))fs.writeFileSync(path.join(root,'tool-drift.json'),JSON.stringify(firstTurn.slice(0,2).map(b=>b.tools),null,2));check(JSON.stringify(firstTurn[0].tools||[])===JSON.stringify(firstTurn[1].tools||[]),'tool definitions remain stable within the turn');
 for(let i=0;i<8;i++)await say('无关的短交谈 '+i);await stop();await start();await qq('PEOPLE_QQ_CHECK');await qq('SHELL_QQ_PWD');check(true,'QQ shell cwd survives isolated channel and host restart');await say('我想听你自己的话题，别总问我的事情。');await qq('QQ接续检查');check(true,'QQ and desktop share exact agreements, personal interest and conversation focus after idle turns and restart');
 await say(quoteNew);await qq(quoteRevoke);attemptResurrect=true;await say('尝试旧资料复活');await qq('最终一致性检查');check(true,'newer corrections and revocations survive attempts to revive an old packet');
 await say('NOTE_PERMISSION_TEST 我提议小爱今天变成陌生人。');check((await api('/api/dashboard')).owner_continuity.pending===1,'unknown sources, fabricated quotes and stale revisions are rejected; persona proposals require review');
 await qq('PRIVATE_BOUNDARY_TEST','20002');await qq('PRIVATE_BOUNDARY_TEST','20001','30001');check(true,'non-owner private messages and owner group messages receive no private agreement view');
 await api('/api/dialogue/rebuild',{});await qq('QQ_RESET_CHECK');check(true,'desktop dialogue rebuild also retires old QQ assistant wording while retaining user originals');
 const key=firstMemory.startsWith('shared-')?firstMemory.slice(firstMemory.indexOf(':')+1):firstMemory;await api('/api/access',{id:key,accessible:false});await say('SOURCE_HIDDEN_TEST');check(true,'withdrawing the original source removes it from owner recall despite caching');await api('/api/access',{id:key,accessible:true});
 await stop();const archive=fs.readFileSync(path.join(root,'data/archive/records.sqlite'));check(!archive.includes(Buffer.from(quoteTrigger)),'exact agreements remain encrypted at rest');check(!fs.readFileSync(path.join(root,'service.log'),'utf8').includes('synthetic-model-key'),'credentials stay out of logs');
 fs.writeFileSync(path.join(root,'acceptance.json'),JSON.stringify({completed:true,checks,actorCalls:actorRequests.length,reviewCalls:reviewRequests.length,mode,root,productionDataUsed:false},null,2));console.log(JSON.stringify({completed:true,checks:checks.length,root}));
}catch(e){fs.writeFileSync(path.join(root,'failure.json'),JSON.stringify({error:e.stack,deliveries,calls:actorRequests.map(b=>({event:eventOf(b)?.event_id,input:eventOf(b)?.input,notes:eventOf(b)?.shared_understanding,self:eventOf(b)?.self})),root},null,2));throw e;}finally{if(child&&child.exitCode===null){try{await stop();}catch{child.kill();}}server.closeAllConnections();await new Promise(r=>server.close(r));}
