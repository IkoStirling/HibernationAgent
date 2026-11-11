'use strict';
const $=id=>document.getElementById(id),token=new URLSearchParams(location.hash.slice(1)).get('session')||sessionStorage.getItem('hibernation-chat-session')||'';
// The IPC session credential stays in this tab; it never goes into media URLs.
if(token)sessionStorage.setItem('hibernation-chat-session',token);history.replaceState(null,'',location.pathname+location.search);
let before='',page=null,back=[],loading=false,generation=0,activeUrl='',activeAbort=null,thumbnailObserver=null;
const node=(tag,cls,text)=>{const e=document.createElement(tag);if(cls)e.className=cls;if(text!==undefined)e.textContent=text;return e;};
async function api(path,data,signal){
 const response=await fetch(path,{method:data===undefined?'GET':'POST',headers:{Authorization:'Bearer '+token,'Content-Type':'application/json'},body:JSON.stringify(data),cache:'no-store',signal});
 if(!response.ok){
  const error=new Error(response.status===401?'会话已过期，请从对话窗口重新打开历史记录。':'记录暂时无法读取（'+response.status+'）。');
  error.status=response.status;
  throw error;
 }
 return response.json();
}
function remoteMedia(text){const found=[];for(const match of text.matchAll(/https?:\/\/[^\s<>"'`）)\]，。]+/g)){try{const url=new URL(match[0]);if(url.username||url.password||url.href.length>2000)continue;const extension=url.pathname.split('.').pop().toLowerCase(),video=['mp4','webm','m4v'].includes(extension),image=['png','jpg','jpeg','gif','webp'].includes(extension);if(!video&&!image||found.some(x=>x.url===url.href))continue;found.push({url:url.href,name:decodeURIComponent(url.pathname.split('/').pop()),mime_type:video?'video/'+(extension==='webm'?'webm':'mp4'):'image/'+extension});if(found.length===4)break;}catch{}}return found;}
function closePreview(){activeAbort?.abort();activeAbort=null;if(activeUrl){URL.revokeObjectURL(activeUrl);activeUrl='';}for(const video of $('content').querySelectorAll('video')){video.pause();video.removeAttribute('src');video.load();}$('content').replaceChildren();}
$('preview').addEventListener('close',closePreview);$('close').onclick=()=>$('preview').close();
async function preview(message,file){if($('preview').open)$('preview').close();closePreview();$('filename').textContent=file.name||'媒体';$('progress').textContent=file.url?'此媒体来自远程链接，点击播放会连接该网站。':'正在读取加密附件……';$('preview').showModal();const abort=new AbortController();activeAbort=abort;
 try{let url=file.url;if(!url){const metadata=await api('/api/chat/media',{message_id:message.id,id:file.id},abort.signal);const parts=[];for(let part=0;part<metadata.chunks;part++){const chunk=await api('/api/chat/media',{message_id:message.id,id:file.id,part},abort.signal);parts.push(Uint8Array.from(atob(chunk.base64),c=>c.charCodeAt(0)));$('progress').textContent='正在准备预览 '+Math.round((part+1)*100/metadata.chunks)+'%';}if(abort.signal.aborted)return;const blob=new Blob(parts,{type:metadata.mime_type});if(blob.size!==metadata.bytes)throw new Error('附件内容不完整。');url=URL.createObjectURL(blob);activeUrl=url;}
 if(abort.signal.aborted)return;const media=node(file.mime_type.startsWith('video/')?'video':'img');media.referrerPolicy='no-referrer';if(media.tagName==='VIDEO'){media.controls=true;media.preload='metadata';media.playsInline=true;}else media.alt=file.name||'图片';media.onload=()=>{$('progress').textContent=file.mime_type==='image/gif'?'动图预览中':'图片预览';};media.onerror=()=>{$('progress').textContent='无法解码或读取该媒体。视频需使用当前浏览器支持的编码。';};media.onloadedmetadata=()=>{$('progress').textContent='点击播放开始观看';};media.src=url;$('content').replaceChildren(media);
 }catch(error){if(!abort.signal.aborted)$('progress').textContent=error.message;}}
function card(message,file){const button=node('button','card');button.type='button';const kind=file.mime_type.startsWith('video/')?'视频':file.mime_type==='image/gif'?'动图':'图片';button.append(node('span','symbol',kind==='视频'?'▷':'▧'),node('span','name',file.name||'媒体'),node('span','kind',kind+(file.url?' · 远程链接 · 点击加载':' · 点击预览')));button.onclick=()=>preview(message,file);
 if(!file.url&&!file.mime_type.startsWith('video/')){button._loadThumbnail=async()=>{const view=generation;try{const data=await api('/api/chat/media',{message_id:message.id,id:file.id});if(view!==generation||!button.isConnected)return;if(data.thumbnail?.mime_type==='image/jpeg'){const image=node('img');image.alt=file.name||'图片';image.src='data:image/jpeg;base64,'+data.thumbnail.base64;button.firstChild.replaceWith(image);}}catch{if(button.isConnected)button.append(node('span','retry','缩略图暂不可用，仍可点击重试'));}};thumbnailObserver.observe(button);}return button;}
async function load(cursor='',stack=back){if(loading)return;loading=true;$('older').disabled=$('newer').disabled=true;$('issue').textContent='';try{const result=await api('/api/chat/page',{limit:24,before:cursor});before=cursor;back=stack;page=result;++generation;thumbnailObserver?.disconnect();thumbnailObserver=new IntersectionObserver(entries=>{for(const entry of entries)if(entry.isIntersecting){thumbnailObserver.unobserve(entry.target);entry.target._loadThumbnail?.();}},{rootMargin:'100px'});const fragment=document.createDocumentFragment();for(const message of result.messages){const bubble=node('article','message '+(message.role==='user'?'user':'assistant')),label=node('div','label'),time=node('time','',message.created_at?new Date(message.created_at).toLocaleString('zh-CN'):'');label.append(node('span','',message.role==='user'?'你':'小爱'),time);bubble.append(label,node('p','text',message.text));const media=node('div','media');for(const file of [...(message.attachments||[]),...remoteMedia(message.text)])media.append(card(message,file));if(media.childNodes.length)bubble.append(media);fragment.append(bubble);}$('messages').replaceChildren(fragment);$('empty').hidden=result.messages.length!==0;$('range').textContent=(cursor?'较早记录':'最近记录')+' · 本页 '+result.messages.length+' 条';window.scrollTo(0,cursor||new URLSearchParams(location.search).get('view')==='tasks'?0:document.documentElement.scrollHeight);
 }catch(error){$('issue').textContent=error.message;}finally{loading=false;$('older').disabled=!page?.has_more;$('newer').disabled=back.length===0;}}
$('older').onclick=()=>page?.has_more&&load(page.next_before,[...back,before]);$('newer').onclick=()=>{if(back.length)load(back.at(-1),back.slice(0,-1));};$('recent').onclick=()=>load('',[]);load('',[]);
// History browsing stays fixed while new conversation continues elsewhere.

// This view consumes the public task projection. Checkpoints and traces stay internal.
const taskLabels = {
  ready:'待继续', running:'执行中', waiting_input:'等你补充', paused:'已暂放',
  blocked:'受阻', completed:'已完成', abandoned:'已取消',
  runtime_paused:'后台已暂停', offline:'离线等待', resting:'休息后继续', budget_wait:'等待调用额度'
};
const taskPanel = $('task-panel');
let taskSnapshot = '', taskLoading = false, taskControlPending = false, draftRequest;

function taskButton(label, action) {
  const button = node('button', '', label);
  button.type = 'button';
  button.onclick = action;
  return button;
}

async function previewTaskDraft(task, artifact) {
  draftRequest?.abort();
  const request = new AbortController();
  draftRequest = request;
  $('draft-name').textContent = artifact.path;
  $('draft-text').textContent = '正在读取当前草稿…';
  if (!$('draft-preview').open) $('draft-preview').showModal();
  try {
    const draft = await api('/api/chat/tasks/detail', {id:task.id, artifact_id:artifact.id}, request.signal);
    if (!request.signal.aborted) $('draft-text').textContent = draft.content;
  } catch {
    if (!request.signal.aborted) $('draft-text').textContent = '草稿暂不可读，可能已移动、移除或失去访问权限。';
  }
}

async function loadTaskArtifacts(task, target) {
  target.textContent = '正在读取产物…';
  try {
    const {artifacts} = await api('/api/chat/tasks/detail', {id:task.id});
    if (!target.isConnected) return;
    target.replaceChildren();
    if (!artifacts.length) target.textContent = '目前没有可预览的工作区草稿。';
    for (const artifact of artifacts) {
      const button = taskButton(artifact.path, () => previewTaskDraft(task, artifact));
      button.className = 'task-artifact';
      target.append(button);
    }
  } catch {
    if (target.isConnected) target.textContent = '暂时无法读取产物，任务的资料或工作区可能已变化。';
  }
}

async function controlChatTask(task, action) {
  if (taskControlPending) return;
  if (action === 'cancel' && !confirm('取消这项目标？已保存的草稿会保留。')) return;
  taskControlPending = true;
  const controls = [...$('task-list').querySelectorAll('button[data-action]:not(:disabled)')];
  for (const control of controls) control.disabled = true;
  $('task-issue').textContent = '';
  try {
    await api('/api/chat/tasks/control', {id:task.id, expected_revision:task.revision, action});
  } catch (error) {
    $('task-issue').textContent = error.status === 409
      ? '任务状态已变化，或当前条件不允许继续。请查看刷新后的状态。'
      : '操作未完成，请重试。';
  } finally {
    taskControlPending = false;
    for (const control of controls) control.disabled = false;
    await refreshChatTasks();
  }
}

function renderChatTasks(data) {
  const list = $('task-list');
  const opened = new Set([...list.querySelectorAll('details[open]')].map(card => card.dataset.taskId));
  const activeControl = list.contains(document.activeElement) ? document.activeElement : null;
  const activeId = activeControl?.closest('[data-task-id]')?.dataset.taskId;
  const activeAction = activeControl?.dataset.action;
  const fragment = document.createDocumentFragment();
  $('task-count').textContent = data.active_count ? ' · ' + data.active_count + ' 项未结束' : '';
  if (!data.tasks.length) {
    fragment.append(node('p','task-note',data.enabled ? '目前没有持续任务。' : '持续任务已关闭。'));
  }
  for (const task of data.tasks) {
    const card = node('details','task-card');
    card.dataset.taskId = task.id;
    const summary = node('summary','task-summary');
    summary.append(node('span','task-title',task.title), node('span','task-badge',taskLabels[task.phase] || '状态未知'));
    const meta = node('p','task-note', '已启动 ' + task.steps_started + ' 个工作段 · 草稿预览显示当前文件');
    const actions = node('div','task-actions');
    if (!['completed','abandoned'].includes(task.status)) {
      const active = ['ready','running'].includes(task.status);
      for (const [action,label] of [[active?'pause':'resume',active?'暂放':'继续'],['cancel','取消目标']]) {
        const button = taskButton(label, () => controlChatTask(task, action));
        button.dataset.action = action;
        button.disabled = action === 'resume' && (data.paused || !data.enabled);
        actions.append(button);
      }
    }
    const artifacts = node('div','task-artifacts');
    card.append(summary,meta);
    if (task.notice) card.append(node('p','task-note',task.notice));
    card.append(actions,artifacts);
    card.ontoggle = () => { if (card.open) loadTaskArtifacts(task, artifacts); };
    card.open = opened.has(task.id);
    fragment.append(card);
  }
  list.replaceChildren(fragment);
  if (activeId && activeAction) {
    const card = [...list.children].find(child => child.dataset.taskId === activeId);
    const control = [...(card?.querySelectorAll('button[data-action]') || [])].find(button => button.dataset.action === activeAction);
    (control && !control.disabled ? control : card?.querySelector('summary'))?.focus({preventScroll:true});
  }
}

async function refreshChatTasks() {
  if (!taskPanel.open || document.hidden || taskLoading || taskControlPending) return;
  taskLoading = true;
  try {
    const data = await api('/api/chat/tasks');
    if (taskControlPending) return;
    $('task-fetch-error').textContent = '';
    const snapshot = JSON.stringify(data);
    if (snapshot !== taskSnapshot) {
      renderChatTasks(data);
      taskSnapshot = snapshot;
    }
  } catch {
    $('task-fetch-error').textContent = '暂时无法刷新任务状态。';
  } finally {
    taskLoading = false;
  }
}

$('draft-close').onclick = () => $('draft-preview').close();
$('draft-preview').addEventListener('close', () => {
  draftRequest?.abort();
  $('draft-text').textContent = '';
});
taskPanel.open = new URLSearchParams(location.search).get('view') === 'tasks';
taskPanel.ontoggle = refreshChatTasks;
document.addEventListener('visibilitychange', refreshChatTasks);
setInterval(refreshChatTasks, 2500);
refreshChatTasks();
