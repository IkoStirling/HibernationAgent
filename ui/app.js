'use strict';
const $=id=>document.getElementById(id);
const sessionKey='hibernation.local.session';
const fragment=new URLSearchParams(location.hash.slice(1));
let token=fragment.get('session')||sessionStorage.getItem(sessionKey)||'';
if(fragment.has('session')){sessionStorage.setItem(sessionKey,token);history.replaceState(null,'',location.pathname);}
let current=null,page='chat',messageSignature='',eventCursor=0,events=[],workspacePath='',polling=false,stopped=false;
const notifiedFailures=new Set();
let eventGeneration=0;const eventInFlight=new Set();
const kindNames={conversation:'交谈记忆',story:'故事内经历',outer:'外层事件',inference:'推测',summary:'摘要'};
const failures={busy:'角色正在处理上一项工作，请稍后再试。',paused:'后台任务已暂停，请先恢复。',unauthorized:'本次界面连接已失效，请重新运行启动脚本。',workspace_path:'路径超出工作区范围。',workspace_read:'这份文件暂不可读。',output_schema:'模型返回结构不符合要求。',request_fields:'输入内容不完整。',operation_failed:'操作未完成，请查看后台记录。'};
function el(tag,className,text){const node=document.createElement(tag);if(className)node.className=className;if(text!==undefined)node.textContent=String(text);return node;}
function toast(text){$('toast').textContent=text;$('toast').hidden=false;clearTimeout(toast.timer);toast.timer=setTimeout(()=>$('toast').hidden=true,4500);}
async function api(path,data){
  const response=await fetch(path,{method:data===undefined?'GET':'POST',headers:{Authorization:`Bearer ${token}`,'Content-Type':'application/json'},body:data===undefined?undefined:JSON.stringify(data),cache:'no-store'});
  const value=await response.json();if(!response.ok){const err=new Error(failures[value.error]||`操作未完成（${value.error||response.status}）`);err.code=value.error;throw err;}return value;
}
function detail(title,value){$('detail-title').textContent=title;$('detail-content').textContent=typeof value==='string'?value:JSON.stringify(value,null,2);$('details').showModal();}
function setPage(name){page=name;document.querySelectorAll('.nav').forEach(n=>n.classList.toggle('active',n.dataset.page===name));document.querySelectorAll('.page').forEach(n=>n.classList.toggle('active',n.id===`page-${name}`));const titles={chat:['CONVERSATION','把未完成的想法留在这里。'],memories:['MEMORY','经历有来源，访问有边界。'],activity:['CONTINUITY','让变化留下可以追溯的痕迹。'],workspace:['WORKSPACE','给尚未成形的想法一页草稿。']};$('page-eyebrow').textContent=titles[name][0];$('page-title').textContent=titles[name][1];if(name==='memories')loadMemories();if(name==='activity')loadEvents();if(name==='workspace')loadWorkspace();}
document.querySelectorAll('.nav').forEach(button=>button.addEventListener('click',()=>setPage(button.dataset.page)));
$('close-details').addEventListener('click',()=>$('details').close());
document.querySelectorAll('[data-prompt]').forEach(button=>button.addEventListener('click',()=>{$('message').value=button.dataset.prompt;$('message').focus();}));
function renderState(value){
  current=value;$('connection').textContent='后台已连接';$('connection-dot').classList.remove('offline');
  const profile=value.profiles.find(p=>p.id===value.model.id);$('mode').textContent=profile?.protocol===0?'离线模拟':'模型连接';
  const signature=JSON.stringify(value.profiles);if($('model').dataset.signature!==signature){$('model').replaceChildren();for(const p of value.profiles){const option=el('option','',p.label);option.value=p.id;option.disabled=!p.enabled;$('model').append(option);}$('model').dataset.signature=signature;}
  $('model').value=value.model.id;$('model').disabled=value.busy;
  $('stage-name').textContent=value.stage_definition.name;$('stage-description').textContent=value.stage_definition.description;
  $('attention').textContent=value.state.attention;$('questions').replaceChildren();
  if(value.state.open_questions.length)value.state.open_questions.forEach(q=>$('questions').append(el('p','',q)));else $('questions').append(el('p','muted','允许暂时没有问题。'));
  $('busy-label').textContent=value.control.paused?'已暂停':value.busy?'工作中':'静候';$('send').disabled=value.busy||value.control.paused;
  $('solitude').disabled=value.busy||value.control.paused;$('pause').textContent=value.control.paused?'恢复后台任务':'暂停后台任务';
  $('health').textContent=value.health==='ok'?'状态连续保存':`后台提示：${value.health}`;
  const active=value.jobs.find(j=>j.state==='running')||value.jobs.find(j=>j.state==='pending'&&j.id!=='solitude.timer');
  $('work-status').textContent=value.control.paused?'后台已暂停；正在取消的调用会先完成安全收尾。':active?(active.id.startsWith('solitude')?'正在独处，结果会留给下一次交谈。':'正在装配记忆并处理这次输入……'):'';
  for(const job of value.jobs){const key=`${job.id}:${job.run_token}`;if(notifiedFailures.has(key))continue;let rejected=false;try{rejected=JSON.parse(job.result).status==='rejected';}catch{}if(rejected||job.state==='failed'){notifiedFailures.add(key);toast(rejected?'模型提案未通过验证，未更新状态或工作区。可在经历与记录查看原因。':'调用未完成，原始输入已保留。请查看经历与记录。');}}
  const messageKey=value.messages.map(m=>m.id).join('|');if(messageSignature!==messageKey){
    messageSignature=messageKey;if(value.messages.length){$('messages').replaceChildren();for(const message of value.messages){
      const type=message.kind==='user_message'?'user':message.kind==='assistant_message'?'assistant':'system';
      const row=el('article',`message ${type}`);const sender=el('div','sender');sender.append(el('span','',type==='user'?'你':type==='assistant'?'小爱':({story_end:'故事结束',outer_event:'外层事件',task_complete:'任务完成'}[message.kind]||'经历')));
      if(message.trace_id){const trace=el('button','trace-button','查看来源');trace.addEventListener('click',async()=>{try{detail('这次装配与调用',await api('/api/trace',{id:message.trace_id}));}catch(e){toast(e.message);}});sender.append(trace);}
      row.append(sender,el('div','bubble',message.text));$('messages').append(row);
    }$('messages').scrollTop=$('messages').scrollHeight;}
  }
  renderReviews(value.reviews);
}
function renderReviews(reviews){$('review-list').replaceChildren();for(const review of reviews.filter(r=>r.status==='pending')){
  const card=el('article','card review-card');card.append(el('span','badge','待审核 · 阶段建议'),el('p','',`${review.from_stage} → ${review.stage_id}`),el('p','',review.reason));
  const evidence=el('button','source-link',`查看 ${review.evidence.length} 条经历依据`);evidence.addEventListener('click',()=>detail('阶段变化依据',review));card.append(evidence);
  for(const approve of [true,false]){const button=el('button','source-link',approve?'批准变化':'保留当前阶段');button.disabled=current?.busy;button.addEventListener('click',async()=>{try{await api('/api/review',{id:review.id,approve});toast(approve?'阶段变化已保存。':'已保留当前阶段。');await poll();}catch(e){toast(e.message);}});card.append(button);}$('review-list').append(card);
}}
async function poll(){if(polling||stopped||!token)return;polling=true;try{renderState(await api('/api/state'));if(page==='activity')await loadEvents();}catch(e){$('connection').textContent='连接暂不可用';$('connection-dot').classList.add('offline');if(e.code==='unauthorized'){stopped=true;toast(e.message);}}finally{polling=false;}}
$('composer').addEventListener('submit',async event=>{event.preventDefault();const text=$('message').value.trim();if(!text)return;$('send').disabled=true;try{await api('/api/message',{request_id:crypto.randomUUID(),text});$('message').value='';await poll();}catch(e){toast(e.message);$('send').disabled=false;}});
$('message').addEventListener('keydown',event=>{if(event.key==='Enter'&&!event.shiftKey&&!event.isComposing){event.preventDefault();if(!$('send').disabled)$('composer').requestSubmit();}});
$('solitude').addEventListener('click',async()=>{try{await api('/api/solitude',{});toast('已安排独处，界面可以关闭，后台会继续运行。');await poll();}catch(e){toast(e.message);}});
$('pause').addEventListener('click',async()=>{if(!current)return;try{await api('/api/control',{paused:!current.control.paused});await poll();}catch(e){toast(e.message);}});
$('model').addEventListener('change',async()=>{try{await api('/api/model',{id:$('model').value});toast('已切换模型，角色状态和记忆继续保留。');await poll();}catch(e){toast(e.message);if(current)$('model').value=current.model.id;}});
$('storage-info').addEventListener('click',()=>{if(current)detail('档案与运行边界',{档案位置:current.data_root,人格版本:current.persona_version,上下文字节预算:current.context_bytes,说明:'原始档案持久保留，可访问记忆另行管理。模型密钥只从环境变量读取。关闭页面不会结束后台；使用停止脚本结束进程。'});});
async function loadMemories(){try{const value=await api('/api/memories',{search:$('search-text').value.trim()});$('memory-list').replaceChildren();if(!value.memories.length){$('memory-list').append(el('div','empty','还没有匹配的记忆。完成一次交谈后，合规的记忆提案会出现在这里。'));return;}
  value.memories.reverse().forEach(memory=>{const card=el('article',`card ${memory.accessible?'':'hidden-memory'}`);const top=el('div','card-top');top.append(el('span','badge',kindNames[memory.kind]||memory.kind));
    const toggle=el('button','toggle',memory.accessible?'设为暂不可访问':'恢复访问');toggle.disabled=current?.busy;toggle.addEventListener('click',async()=>{try{await api('/api/access',{id:memory.id,accessible:!memory.accessible});toast('访问范围已更新，原始记录完整保留。');await loadMemories();}catch(e){toast(e.message);}});top.append(toggle);card.append(top,el('p','',memory.text),el('small','',`${memory.origin==='solitude'?'来自独处 · ':''}${new Date(memory.created_at).toLocaleString()} · v${memory.revision}`));
    const sources=el('div','');for(const source of memory.sources){const button=el('button','source-link',`${source.id.slice(0,28)} · v${source.revision}`);button.addEventListener('click',async()=>{try{detail('记忆来源',source.scope==='ha.archive'?await api('/api/archive',{id:source.id}):source);}catch(e){toast(e.message);}});sources.append(button);}card.append(sources);$('memory-list').append(card);
  });
}catch(e){toast(e.message);}}
$('memory-search').addEventListener('submit',event=>{event.preventDefault();loadMemories();});
async function loadEvents(){const generation=eventGeneration;if(eventInFlight.has(generation))return;eventInFlight.add(generation);try{const value=await api('/api/events',{after_sequence:eventCursor,channel:$('event-filter').value});if(generation!==eventGeneration)return;eventCursor=value.next_sequence;events.push(...value.events);events=events.slice(-200);$('event-list').replaceChildren();if(!events.length)$('event-list').append(el('div','empty','还没有这类事件。交谈、独处和草稿操作完成后会留下记录。'));
  for(const record of [...events].reverse()){const row=el('article','timeline-row');const time=record.occurred_at?new Date(record.occurred_at).toLocaleTimeString([], {hour:'2-digit',minute:'2-digit'}):'未指定';row.append(el('span','time',time));const body=el('div','');body.append(el('strong','',{'agent.turn.completed':'一次交谈或经历处理完成','agent.solitude.completed':'一个独处时段结束','agent.action.completed':'工作区操作已处理'}[record.channel]||record.channel),el('p','',`#${record.sequence} · ${record.data.body.status||'已记录'}`));const button=el('button','', '查看记录');button.addEventListener('click',async()=>{const trace=record.data.body.trace_id;try{detail('可追溯记录',trace?{event:record,trace:await api('/api/trace',{id:trace})}:record);}catch(e){toast(e.message);}});body.append(button);row.append(body);$('event-list').append(row);}
}catch(e){toast(e.message);}finally{eventInFlight.delete(generation);}}
$('event-filter').addEventListener('change',()=>{eventGeneration++;eventCursor=0;events=[];loadEvents();});
$('add-event').addEventListener('click',()=>$('event-dialog').showModal());$('cancel-event').addEventListener('click',()=>$('event-dialog').close());
$('event-form').addEventListener('submit',async event=>{event.preventDefault();try{await api('/api/event',{request_id:crypto.randomUUID(),kind:$('event-kind').value,text:$('event-text').value.trim()});$('event-dialog').close();$('event-text').value='';toast('经历原文已保存，正在处理。');await poll();}catch(e){toast(e.message);}});
async function loadWorkspace(){try{const value=await api('/api/workspace',{mount:$('workspace-mount').value,path:workspacePath});$('workspace-path').textContent=workspacePath||'/';$('file-list').replaceChildren();if(!value.entries.length)$('file-list').append(el('p','empty','这里暂时没有文件。'));
  for(const name of value.entries){const button=el('button','',name);const directory=value.directories.includes(name);button.addEventListener('click',async()=>{const next=workspacePath?`${workspacePath}/${name}`:name;try{if(directory){workspacePath=next;await loadWorkspace();}else{const file=await api('/api/workspace',{mount:$('workspace-mount').value,path:next,read:true});$('file-content').textContent=file.content;}}catch(e){toast(e.message);}});$('file-list').append(button);}
}catch(e){toast(e.message);}}
$('workspace-mount').addEventListener('change',()=>{workspacePath='';$('file-content').textContent='选择一份草稿或素材。';loadWorkspace();});$('workspace-up').addEventListener('click',()=>{workspacePath=workspacePath.split('/').slice(0,-1).join('/');loadWorkspace();});
if(!token){$('connection').textContent='等待本机连接';toast('请运行项目中的启动脚本，它会为当前界面建立本机连接。');}else poll();setInterval(poll,1000);
