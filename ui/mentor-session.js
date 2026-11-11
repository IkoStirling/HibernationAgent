'use strict';
// This self-contained panel reads metadata only. It never renders memory text,
// provider conversation identifiers, worker output, credentials or tool traces.
const mentorSessionUI={panel:null,snapshot:null,refreshing:null,controlling:false,epoch:0,lastRead:0,readError:false};
function mentorSessionNumber(value){return typeof value==='number'&&Number.isFinite(value)&&value>=0?new Intl.NumberFormat('zh-CN').format(value):'未报告';}
function mentorSessionTime(value){return typeof value==='number'&&Number.isFinite(value)&&value>0&&Number.isFinite(new Date(value).getTime())?new Date(value).toLocaleString('zh-CN',{hour12:false}):'未报告';}
function mentorSessionQuota(value){
 if(!value||value.known!==true)return '工作额度未报告，暂时无法判断可用余量。';
 const lines=[value.exhausted===true?'工作额度已用尽。':'已收到工作额度报告。'];
 for(const [index,bucket]of (Array.isArray(value.buckets)?value.buckets:[]).slice(0,8).entries())for(const [key,label]of [['primary','主窗口'],['secondary','次窗口']]){
  const window=bucket?.[key];if(!window||typeof window!=='object')continue;
  const percent=typeof window.used_percent==='number'&&Number.isFinite(window.used_percent)&&window.used_percent>=0&&window.used_percent<=100?`${window.used_percent.toFixed(1)}%`:'未报告';
  const period=typeof window.window_minutes==='number'&&Number.isFinite(window.window_minutes)&&window.window_minutes>0?`（${mentorSessionNumber(window.window_minutes)} 分钟）`:'';
  lines.push(`额度 ${index+1} · ${label}${period}：已用 ${percent}；重置时间 ${mentorSessionTime(typeof window.reset_at==='number'?window.reset_at*1000:null)}。`);
 }
 if(lines.length===1)lines.push('后台未提供窗口用量，不能据此推算剩余额度。');
 if(typeof value.checked_ms==='number'&&value.checked_ms>0)lines.push(`更新于 ${mentorSessionTime(value.checked_ms)}。`);return lines.join('\n');
}
function mentorSessionError(error){
 const code=String(error?.message||error||'');
 const known={mentor_work_quota_exhausted:'工作额度已用尽；可使用普通 Chat 回退，或等待额度恢复。',mentor_chat_login_required:'请点击“打开普通 Chat 并登录”，在专用窗口完成登录。',mentor_chat_identity_unverified:'暂时无法确认网页登录身份，请打开专用窗口检查登录。',mentor_chat_mode_unverified:'请在专用网页选择普通 Chat 并关闭 Work；无法确认模式时不会发送。',mentor_chat_work_mode:'专用网页仍处于 Work，请先切换到普通 Chat。',mentor_chat_not_ready:'普通 Chat 页面仍未就绪，请打开专用窗口等待页面加载。',mentor_chat_markup_changed:'网页布局与桥接不匹配，本次没有继续；请检查专用窗口。',mentor_chat_context_lost:'网页会话已变化，本次请求停止，未自动重新发送。',mentor_chat_send_uncertain:'无法确认网页发送结果；请查看专用窗口，避免重复发送。',mentor_chat_timeout:'等待网页完整回复超时；请查看专用窗口，本次不会自动重发。',mentor_chat_not_available:'网页桥接未能启动，请检查 WebView2 组件。',mentor_chat_desktop_required:'网页回退需要桌面后台，请以桌面模式启动程序。',mentor_python_required:'没有找到可用的 Python，请填写导师插件中的 Python 程序路径并保存。',mentor_worker_missing:'导师会话组件缺失，请重新部署插件。',mentor_service_not_ready:'导师会话服务未能启动，请检查 Python 与独立存储目录。',mentor_unavailable:'导师插件尚未启用或未能加载，请检查插件设置。',mentor_approval_required:'导师正在等待其原生权限审批；本页面暂不代为批准。',mentor_busy:'导师正在处理请求，请先取消当前咨询，再重新建立上下文。',mentor_not_configured:'请先启用导师并选择 ChatGPT 连接，然后保存插件设置。',mentor_disabled:'导师尚未启用，请先保存插件设置。',plugin_tool_missing:'导师插件尚未就绪，请先启用并保存插件设置。',plugins_unavailable:'导师插件尚未就绪。',tool_not_available:'当前导师版本暂不支持此操作，请更新后台。',mentor_chatgpt_login_required:'请先在导师使用的 Codex 中登录 ChatGPT。',mentor_codex_not_found:'没有找到导师使用的 Codex，请检查程序路径。',mentor_session_unavailable:'导师会话状态暂时不可用，请稍后刷新。',mentor_storage_unavailable:'导师独立存储暂时不可用，请检查存储目录。'};
 for(const [key,message]of Object.entries(known))if(code===key||code.endsWith('：'+key)||code.endsWith(': '+key))return message;
 return '这次操作未完成，请稍后重试或检查导师设置。';
}
function mentorSessionPayload(value){if(value?.ok===false)throw new Error(typeof value.error?.code==='string'?value.error.code:'mentor_session_unavailable');return value?.ok===true?value.result:value;}
function mentorSessionConfigured(){const configuration=typeof current!=='undefined'?current?.settings?.plugins?.mentor:null;return Boolean(configuration?.enabled&&configuration.backend!=='none');}
function mentorSessionMessage(text,error=false){const target=mentorSessionUI.panel?.querySelector('[data-mentor="message"]');if(target){target.textContent=text;target.classList.toggle('runtime-error',error);}}
function renderMentorSessionStatus(value){
 const panel=mentorSessionUI.panel;if(!panel)return;const set=(key,text)=>{panel.querySelector(`[data-mentor="${key}"]`).textContent=text;};
 const connected=mentorSessionConfigured();panel.querySelector('[data-mentor="refresh"]').disabled=mentorSessionUI.controlling||!connected;
 panel.querySelector('[data-mentor="chat_open"]').disabled=mentorSessionUI.controlling||!connected;
 if(!connected){set('state','导师连接尚未启用');set('continuity','启用并保存后读取现有会话。');panel.querySelector('[data-mentor="cancel"]').disabled=true;panel.querySelector('[data-mentor="rebuild"]').disabled=true;return;}
 if(!value){set('state','正在读取导师会话');set('continuity','尚未收到后台会话信息。');set('web','普通 Chat 网页桥接未连接，等待后台报告。');panel.querySelector('[data-mentor="cancel"]').disabled=true;panel.querySelector('[data-mentor="rebuild"]').disabled=true;return;}
 set('state',value.busy===true?'正在与导师交流':value.service_running===true?'导师会话服务已就绪':value.service_running===false?'等待下次启动导师':'导师服务状态未报告');
 set('continuity',value.session_id?'沿用同一个逻辑会话；关闭界面或重启后继续使用本地保存的记录。':'逻辑会话尚未建立或后台尚未报告。');
 set('native',value.provider_thread_id?`已有原生上下文；代次 ${mentorSessionNumber(value.generation)}。`:`尚无可续接的原生上下文；代次 ${mentorSessionNumber(value.generation)}。`);
 set('records',mentorSessionNumber(value.record_count));set('memories',mentorSessionNumber(value.memory_count));
 set('encryption',value.encrypted===true?'导师独立记忆已加密':value.encrypted===false?'后台未确认记忆加密':'记忆加密状态未报告');
 const workspace=panel.querySelector('[data-mentor="workspace"]');workspace.value=typeof value.workspace==='string'?value.workspace:'';workspace.placeholder='导师工作区尚未报告';
 set('permission',value.permission_policy==='codex_user_configuration'?'继承导师自己的 Codex 用户权限配置；具体可用能力由该配置和当前授权决定。':'导师权限策略尚未报告，以后台和 Codex 当前配置为准。');
 set('routing',value.routing_mode==='fixed'?'当前路由：固定模型与推理强度。':value.routing_mode==='character'?'当前路由：由小爱为每次请求选择闲聊 / 普通 / 复杂档。':'当前路由：后台尚未报告。');
 const model=typeof value.last_selected_model==='string'&&value.last_selected_model.trim()?value.last_selected_model.slice(0,120):'未报告';const effort=new Map([['low','Low'],['medium','Medium'],['high','High'],['xhigh','XHigh']]).get(value.last_selected_effort)||'未报告';const level=new Map([['casual','闲聊'],['normal','普通'],['complex','复杂']]).get(value.last_selected_level)||'未报告';
 set('selected',`最近实际选择：${level}档 · 模型 ${model} · 推理强度 ${effort}。`);set('quota',mentorSessionQuota(value.work_quota));
 const web=value.web_fallback;
 set('web',web?.enabled===false?'普通 Chat 网页回退已关闭；额度不足时等待工作额度恢复。':web?.ready===true?'实验性普通 Chat 网页桥接已就绪。网页回退仅用于交流与建议，不执行导师原生操作。':web?.login_required===true?'专用普通 Chat 窗口等待你登录。点击下方按钮打开窗口并手动登录。':web?.enabled===true?'实验性普通 Chat 网页桥接尚未连接，请先打开专用窗口并登录。':'普通 Chat 网页桥接状态未报告，当前按未连接显示。');
 if(web?.last_issue)set('web',panel.querySelector('[data-mentor="web"]').textContent+' '+mentorSessionError(web.last_issue));
 set('issue',value.last_issue?mentorSessionError(value.last_issue):'当前没有报告会话错误。');
 panel.querySelector('[data-mentor="cancel"]').disabled=mentorSessionUI.controlling||value.busy!==true;
 panel.querySelector('[data-mentor="rebuild"]').disabled=mentorSessionUI.controlling||value.busy!==false;
}
async function refreshMentorSession(force=false){
 if(!mentorSessionUI.panel?.isConnected||!mentorSessionConfigured()){renderMentorSessionStatus(mentorSessionUI.snapshot);return;}
 if(mentorSessionUI.refreshing){if(force){await mentorSessionUI.refreshing;return refreshMentorSession(false);}return mentorSessionUI.refreshing;}
 const epoch=mentorSessionUI.epoch;
 mentorSessionUI.refreshing=(async()=>{try{const value=mentorSessionPayload(await api('/api/mentor/session'));if(epoch!==mentorSessionUI.epoch)return;mentorSessionUI.snapshot=value&&typeof value==='object'?value:null;mentorSessionUI.lastRead=Date.now();if(mentorSessionUI.readError){mentorSessionUI.readError=false;mentorSessionMessage('导师状态已更新。');}renderMentorSessionStatus(mentorSessionUI.snapshot);}catch(error){if(epoch!==mentorSessionUI.epoch)return;mentorSessionUI.readError=true;mentorSessionMessage(mentorSessionError(error),true);const label=mentorSessionUI.panel?.querySelector('[data-mentor="state"]');if(label)label.textContent='导师会话状态暂时不可用';}finally{mentorSessionUI.refreshing=null;}})();return mentorSessionUI.refreshing;
}
async function controlMentorSession(action){
 if(mentorSessionUI.controlling||!mentorSessionConfigured())return;mentorSessionUI.controlling=true;++mentorSessionUI.epoch;renderMentorSessionStatus(mentorSessionUI.snapshot);mentorSessionMessage(({cancel:'正在取消当前咨询……',rebuild:'正在重新建立原生上下文；保留独立记忆与工作区……',chat_open:'正在打开专用普通 Chat 窗口……'})[action]||'正在提交本地操作……');
 try{mentorSessionPayload(await api('/api/mentor/session/control',{action}));mentorSessionMessage(({cancel:'已发出取消请求。此前保存的记忆与工作区保留；界面会继续确认后台状态。',rebuild:'已请求使用新的原生上下文。独立记忆、逻辑会话记录与工作区保留。',chat_open:'已请求打开普通 Chat 窗口，请你在该窗口中手动登录。桥接就绪情况以后台回报为准。'})[action]||'已提交本地操作。');}
 catch(error){mentorSessionMessage(mentorSessionError(error),true);}
 finally{mentorSessionUI.controlling=false;await refreshMentorSession(true);renderMentorSessionStatus(mentorSessionUI.snapshot);}
}
function mountMentorSessionPanel(){
 const card=document.querySelector('#plugin-list [data-plugin-id="mentor"]');if(!card)return;
 if(!mentorSessionUI.panel){
  const panel=el('section','mentor-session-panel');panel.dataset.mentorSession='true';panel.append(el('h4','','导师的连续会话'),el('p','helper','同一个逻辑会话保留导师自己的加密记忆和独立工作区。这里仅展示运行状态与数量。'));
  const routing=el('p','helper');routing.dataset.mentor='routing';const selected=el('p','helper');selected.dataset.mentor='selected';const quota=el('p','helper');quota.dataset.mentor='quota';quota.style.whiteSpace='pre-line';
  const web=el('p','helper');web.dataset.mentor='web';
  panel.append(el('p','helper','上方可设置模型路由。初始固定档为 gpt-6.1-sol / High；选择由小爱判断后，闲聊 / 普通 / 复杂档默认分别为 Low / Medium / High，模型与强度均可单独配置。省略档位时使用复杂档。'),routing,selected,quota,el('p','helper','三档均使用 Codex 工作通道。降低推理强度不等于切到免费普通 Chat。实验性网页回退需要先在专用普通 Chat 窗口登录，只用于交流与建议，不执行导师原生操作。'),web);
  const state=el('p','');state.dataset.mentor='state';state.setAttribute('aria-live','polite');const continuity=el('p','helper');continuity.dataset.mentor='continuity';const native=el('p','helper');native.dataset.mentor='native';panel.append(state,continuity,native);
  const facts=el('div','runtime-facts');for(const [key,label]of [['records','会话记录数'],['memories','独立记忆数']]){const item=el('div','runtime-fact');item.append(el('span','helper',label));const count=el('strong','','未报告');count.dataset.mentor=key;item.append(count);facts.append(item);}panel.append(facts);
  const encryption=el('p','helper');encryption.dataset.mentor='encryption';const workspaceLabel=el('label','','导师独立工作区'),workspace=el('input');workspace.dataset.mentor='workspace';workspace.readOnly=true;workspace.setAttribute('aria-label','导师独立工作区');workspaceLabel.append(workspace);
  const permission=el('p','helper');permission.dataset.mentor='permission';const issue=el('p','helper');issue.dataset.mentor='issue';panel.append(encryption,workspaceLabel,permission,issue);
  const actions=el('div','runtime-actions');for(const [key,label,fn]of [['refresh','刷新导师状态',()=>refreshMentorSession(true)],['chat_open','打开普通 Chat 并登录',()=>controlMentorSession('chat_open')],['cancel','取消当前咨询',()=>controlMentorSession('cancel')],['rebuild','重建原生上下文，保留记忆',()=>controlMentorSession('rebuild')]]){const button=el('button','quiet',label);button.type='button';button.dataset.mentor=key;button.onclick=fn;actions.append(button);}
  const message=el('p','helper');message.dataset.mentor='message';message.setAttribute('aria-live','polite');panel.append(actions,el('p','helper','重建只更换后续咨询使用的原生上下文，保留独立记忆、会话档案和工作区。正在咨询时先取消；这些管理操作不发起模型咨询。'),message);mentorSessionUI.panel=panel;
 }
 const parent=card.querySelector(':scope > .fold-content')||card;if(!parent.contains(mentorSessionUI.panel))parent.append(mentorSessionUI.panel);renderMentorSessionStatus(mentorSessionUI.snapshot);
 if(document.querySelector('#page-plugins.active')&&Date.now()-mentorSessionUI.lastRead>2000)refreshMentorSession();
}
function startMentorSessionPanel(){
 const root=document.getElementById('plugin-list');if(!root)return;new MutationObserver(mountMentorSessionPanel).observe(root,{childList:true});mountMentorSessionPanel();
 document.querySelector('nav [data-page="plugins"]')?.addEventListener('click',()=>queueMicrotask(()=>{mountMentorSessionPanel();refreshMentorSession();}));
 setInterval(()=>{if(document.hidden||!document.querySelector('#page-plugins.active'))return;mountMentorSessionPanel();},3000);
}
if(document.readyState==='loading')document.addEventListener('DOMContentLoaded',startMentorSessionPanel,{once:true});else startMentorSessionPanel();
