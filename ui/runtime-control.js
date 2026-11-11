'use strict';
// Administrator diagnostics contain counters and program state, never memory content.
const runtimeControlState={value:null,cacheSignature:'',mounted:false,portable:{bundle:null,busy:false,checked:false,watching:false,verified:false,operation:''}};
const runtimeControlErrors={
 portable_cancelled:'操作已取消。',
 portable_owner_only:'只有号主管理入口可执行此操作。',
 background_budget_invalid:'后台日预算超出允许范围，请检查调用数和输入 token 上限。',
 portable_pause_offline_required:'请先暂停后台并进入离线状态，再处理跨机密钥。',
 portable_busy:'另一项密钥操作正在进行，请等待它完成。',
 portable_password:'备份密码须占 12 至 1024 个 UTF-8 字节。',
 portable_bundle_invalid:'密钥包格式不正确。请选择程序导出的加密密钥包。',
 portable_unlock_failed:'密码不正确，或密钥包已损坏。',
 portable_key_unavailable:'当前存档密钥不可用，请检查存档和 Windows 用户。',
 portable_mentor_bundle_required:'这份备份含有导师独立记忆，需要同一检查点导出的 v2 加密包，旧密钥包不能迁移它。',
 portable_mentor_backup_mismatch:'导师加密包与备份不是同一检查点。请保留原件，暂停后重新备份并导出。',
 portable_mentor_limit:'导师记忆超出当前可迁移上限（4 MiB 解码总量或文件数）。未导出或恢复，请保留原账户备份。',
 portable_mentor_invalid:'导师状态结构不完整或不受支持。原件已保留，未把复制的文件算作可迁移成功。',
 portable_mentor_locked:'当前 Windows 账户无法解开导师记忆，不能导出可迁移附件。',
 portable_mentor_unsupported:'发现不在固定迁移名单中的导师 DPAPI 文件，已停止，避免遗漏或迁移登录凭据。',
 portable_mentor_credentials:'导师数据含有禁止迁移的凭据字段，已停止导出或恢复。',
 portable_backup_invalid:'完整备份不完整，请检查 manifest.json 和 data 目录。',
 portable_backup_mismatch:'密钥包与所选完整备份不匹配。',
 portable_target_exists:'恢复目录已经存在。请选择一个尚不存在的新目录。',
 portable_path_rejected:'目录不可用，请选择独立且不含链接的新数据目录。',
 portable_crypto_unavailable:'本机加密组件未就绪，暂时无法处理跨机密钥。',
 portable_operation_failed:'密钥操作未完成，请保留原始备份后重试。'
};
function runtimeNumber(value){return typeof value==='number'&&Number.isFinite(value)?new Intl.NumberFormat('zh-CN').format(value):'未报告';}
function runtimeTime(value){return typeof value==='number'&&Number.isFinite(value)&&value>0?new Date(value).toLocaleString('zh-CN',{hour12:false}):'尚无记录';}
function runtimeRatio(value){return typeof value==='number'&&Number.isFinite(value)?`${(value*100).toFixed(1)}%`:'未报告';}
function runtimeUsageKnown(usage,field){
 const known=usage?.[field+'_known'];
 if(typeof known==='boolean')return known;
 // Old records cannot identify which side of a partial report was supplied.
 // A positive total is evidence; a historical zero alone is not.
 return typeof usage?.[field]==='number'&&Number.isFinite(usage[field])&&usage[field]>0;
}
function runtimePurpose(value){return ({'$other_purposes':'其他用途（合并）',dialogue:'桌面对话',review:'独立审核',activity:'自主行为规划',activity_review:'行为规划审核',reaction:'互动反应预生成',reaction_review:'互动反应审核',solitude:'独处',outer_event:'外部事件',task_complete:'任务完成',qq_dialogue:'QQ 对话',qq_review:'QQ 审核',qq_outer_event:'QQ 事件'})[value]||value.slice(0,64);}
function runtimeCard(id,title,key){const card=el('article','card wide runtime-control');card.id=id;card.dataset.sectionKey=key;card.dataset.panelLabel=title;card.append(el('span','label',title));return card;}
function runtimeDefinition(parent,label,id){const item=el('div','runtime-fact'),name=el('span','helper',label),value=el('strong','runtime-fact-value','—');value.id=id;item.append(name,value);parent.append(item);return value;}
function downloadRuntimeFile(filename,body,type='application/json'){
 const blob=new Blob([body],{type}),url=URL.createObjectURL(blob),link=document.createElement('a');link.href=url;link.download=filename;document.body.append(link);link.click();link.remove();setTimeout(()=>URL.revokeObjectURL(url),1000);
}
function safeRuntimeDiagnostics(value){
 const pick=(source,keys)=>Object.fromEntries(keys.filter(key=>source?.[key]!==undefined).map(key=>[key,source[key]]));
 const usageKeys=['calls','usage_reported_calls','input_reported_calls','output_reported_calls','complete_usage_reported_calls','input_tokens_known','output_tokens_known','counter_saturated','cache_counter_saturated','cache_reported_calls','input_tokens','output_tokens','cached_input_tokens','uncached_input_tokens','cache_hit_ratio'];
 const connections={};for(const [id,connection] of Object.entries(value.model_cache?.connections||{}))connections[id]={total:pick(connection.total,usageKeys),purposes:Object.fromEntries(Object.entries(connection.purposes||{}).map(([purpose,usage])=>[purpose,pick(usage,usageKeys)]))};
 return {version:1,generated_at:new Date().toISOString(),program:pick(value,['version','health','offline','paused','busy']),runtime:pick(value.runtime_status,['code','background_allowed','background_defer_reason','activity_queued','activity_active','planning','speech_state']),own_time:pick(value.own_time,['enabled','ready','wait_reason','next_ms','active']),background_budget:pick(value.background_budget,['enabled','day','timezone','used_calls','remaining_calls','max_calls','input_tokens','reported_calls','unknown_usage_calls','max_input_tokens','blocked','blocked_reason','resets_at_ms']),model_cache:{since_ms:value.model_cache?.since_ms??null,updated_ms:value.model_cache?.updated_ms??null,connections}};
}
function mountRuntimeControls(){
 if(runtimeControlState.mounted)return;runtimeControlState.mounted=true;
 const state=runtimeCard('runtime-details','运行状态与活动','runtime-details'),title=el('h3','','正在读取运行状态');title.id='runtime-detail-label';const detail=el('p','helper');detail.id='runtime-detail-description';detail.setAttribute('aria-live','polite');
 const facts=el('div','runtime-facts');runtimeDefinition(facts,'后台自主活动','runtime-background-allowed');runtimeDefinition(facts,'活动队列','runtime-queue');runtimeDefinition(facts,'语音','runtime-speech');runtimeDefinition(facts,'自己的一小段时间','runtime-own-time');
 const buttons=el('div','runtime-actions'),refresh=el('button','quiet','刷新运行信息'),diagnostic=el('button','text-button','下载运行诊断');refresh.type=diagnostic.type='button';refresh.onclick=()=>load();diagnostic.onclick=()=>downloadRuntimeFile('xiaoa-runtime-diagnostics.json',JSON.stringify(safeRuntimeDiagnostics(runtimeControlState.value||{}),null,2));
 buttons.append(refresh,diagnostic);state.append(title,detail,facts,buttons,el('p','helper','这里显示程序正在等待什么，不推断角色的内心或将连接失败解释成自主沉默。诊断文件仅包含运行状态与用量计数。'));
 $('page-overview').querySelector('.status-grid').after(state);
 const cache=runtimeCard('model-cache-details','模型输入与缓存用量','model-cache');cache.append(el('h3','','把消耗看清楚。'));const period=el('p','helper');period.id='model-cache-period';cache.append(period);
 const totals=el('div','runtime-facts');runtimeDefinition(totals,'输入 token','model-cache-input');runtimeDefinition(totals,'输出 token','model-cache-output');runtimeDefinition(totals,'已报告输入的缓存命中率','model-cache-ratio');cache.append(totals);
 const rows=el('div','runtime-usage-connections');rows.id='model-cache-connections';cache.append(rows,el('p','helper','输入缓存与输出分开统计。未报告的缓存用量保持未知，不当作 0；费用以供应商账单为准。统计自功能启用后累积，不是今天的账单。'));$('budget-settings').after(cache);
 const trace=runtimeCard('cognitive-trace-details','连续活动核验','cognitive-trace');trace.append(el('p','helper','仅显示程序收到的状态与回执。私人判断、对话及动作内容留在加密档案中，不在此展开。'));
 const seal=el('p','helper');seal.id='cognitive-trace-seal';trace.append(seal);
 const traceButton=el('button','quiet','查看最近的执行痕迹'),traceRows=el('div','runtime-usage-connections');traceButton.type='button';traceRows.id='cognitive-trace-rows';
 traceButton.onclick=async()=>{traceButton.disabled=true;try{const result=await api('/api/mind/trace');traceRows.replaceChildren();for(const event of result.events||[]){const line=el('p','helper');const label=({'cognition.activity.queued':'动作已接受，尚未完成','cognition.activity.started':'动作开始','cognition.activity.result':'收到执行结果','cognition.activity.effect':'收到延后结果','cognition.activity.removed':'动作移出队列','cognition.activity.suspended':'打算暂存','cognition.attention.changed':'注意目标变化','cognition.interpretation.changed':'暂定判断已更新','cognition.sleep.receipt':'收到休息回执','cognition.sleep.input':'休息中收到输入','cognition.sleep.override':'管理员唤醒','cognition.runtime.started':'审计会话开始','cognition.runtime.stopped':'审计会话封存'})[event.kind]||event.kind;const status=({'rhythm.sleep_started':'实际进入长睡','rhythm.sleep_ended':'长睡结束','rhythm.sleep_cancelled':'准备休息已取消','rhythm.preferences_changed':'作息设置已更新','rhythm.wake_declined':'本次打扰没有唤醒','held_without_model':'保留输入，本轮未调用模型','woke':'已唤醒','recovered_after_unclean_exit':'上次尾部未封存','audit_tail_checkpointed':'待存轨迹已保存'})[event.status]||event.status||'';const reason=({'window_ended':'休息时段已结束','clock_window_changed':'本机时段发生变化','duration_reached':'已到本次休息时长','application_restarted':'程序重新启动','disabled':'作息已关闭','schedule_changed':'作息已调整','chosen_wake':'自主结束休息','message':'收到消息','pointer_interaction':'鼠标互动','intent_expired':'准备休息已过期','pose_unavailable':'未确认休息姿态','plugin_receipt_committed':'回执已提交'})[event.reason]||event.reason||'';line.textContent=`${runtimeTime(event.created_at)} · ${label} · ${[...new Set([status,reason].filter(Boolean))].join('；')} · ${event.action_id||event.id}`;traceRows.append(line);}if(!(result.events||[]).length)traceRows.append(el('p','helper','尚无记录。'));}catch{traceRows.replaceChildren(el('p','helper','读取未完成，请稍后重试。'));}finally{traceButton.disabled=false;}};
 trace.append(traceButton,traceRows);state.after(trace);
 const sleep=runtimeCard('circadian-details','自主作息','circadian');const sleepStatus=el('p','helper','尚未读取状态。');sleepStatus.id='circadian-current';const wake=el('button','quiet','现在唤醒');wake.type='button';wake.id='circadian-wake';wake.onclick=async()=>{wake.disabled=true;try{await api('/api/rhythm/wake',{});await load();}catch{sleepStatus.textContent='唤醒未完成，请稍后重试。';}finally{wake.disabled=false;}};sleep.append(sleepStatus,wake,el('p','helper','作息由角色选择。普通消息或拖动可能没有唤醒；消息仍会保存。这个按钮可直接结束休息，插件设置也可关闭作息功能。'));trace.after(sleep);
 mountPortableVault();
}
function renderBackgroundBudgetFields(root){
 const budget=settings.backgroundBudget||(settings.backgroundBudget={enabled:true,maxCallsPerDay:120,maxInputTokensPerDay:500000});
 const box=el('fieldset','runtime-background-settings');box.id='background-budget-fields';box.append(el('legend','','后台独立日预算'));
 toggle(box,'限制后台每天的模型调用',budget.enabled!==false,value=>budget.enabled=value);
 const grid=el('div','form-grid');const calls=field(grid,'后台每天最多调用（1 至 10000）',budget.maxCallsPerDay??120,value=>budget.maxCallsPerDay=Number(value));calls.type='number';calls.min='1';calls.max='10000';
 const tokens=field(grid,'后台每天未命中缓存输入 token 上限',budget.maxInputTokensPerDay??500000,value=>budget.maxInputTokensPerDay=Number(value));tokens.type='number';tokens.min='1000';tokens.max='100000000';box.append(grid);
 toggle(box,'整机输入空闲时自动节能，并保留返场预算',budget.autoEconomy===true,value=>budget.autoEconomy=value);
 const status=el('p','helper');status.id='background-budget-current';status.setAttribute('aria-live','polite');const economy=el('p','helper');economy.id='idle-economy-current';box.append(status,economy,el('p','helper','日输入预算只累计未命中缓存输入。服务商没有提供可靠缓存拆分时暂按未命中计入并标为未知；总输入和缓存命中仍保留统计。独处、规划、反应缓存和审核都计后台调用次数，前台交谈受原有每小时额度约束。整机空闲节能只读取系统输入空闲时长，不读取按键或屏幕；闲时活动变稀疏，已约定的结果接续仍有优先预算。'));
 root.append(box);updateRuntimeBudget(runtimeControlState.value||{});
 renderCognitionFields(root);
}
function renderCognitionFields(root){
 const c=settings.cognition||(settings.cognition={enabled:false,attention:true,observations:true,dwellMs:5000,resumeMinutes:15,observationCooldownMs:120000});
 const box=el('fieldset','runtime-background-settings');box.id='cognition-fields';box.append(el('legend','','连续注意与打算（实验）'));
 toggle(box,'启用连续注意与可恢复打算',c.enabled===true,value=>c.enabled=value);
 toggle(box,'保持注意驻留，减少来回切换',c.attention!==false,value=>c.attention=value);
 toggle(box,'按需观察建议与重复观察冷却',c.observations!==false,value=>c.observations=value);
 const grid=el('div','form-grid');
 const dwell=field(grid,'注意驻留秒数（1 至 60）',(c.dwellMs??5000)/1000,value=>c.dwellMs=Number(value)*1000);dwell.type='number';dwell.min='1';dwell.max='60';
 const resume=field(grid,'打算保留分钟（1 至 60）',c.resumeMinutes??15,value=>c.resumeMinutes=Number(value));resume.type='number';resume.min='1';resume.max='60';
 const cooldown=field(grid,'相同内容观察冷却秒数（30 至 3600）',(c.observationCooldownMs??120000)/1000,value=>c.observationCooldownMs=Number(value)*1000);cooldown.type='number';cooldown.min='30';cooldown.max='3600';
 box.append(grid,el('p','helper','保存后生效。角色自行决定继续或放下被交谈打断的打算；已执行或结果不明的操作不会重放。复用现有后台规划与日预算，不增加常驻模型或截图轮询。暂定判断和真实经历分开加密保存，管理页不展示内容。关闭可回到原流程。'));
 root.append(box);
}
function updateRuntimeBudget(value){
 const target=$('background-budget-current');if(!target)return;const budget=value.background_budget;if(!budget){target.textContent='后台日预算信息尚未就绪。';return;}
 const blocked=budget.blocked?(budget.blocked_reason==='daily_input_tokens'?'未命中输入预算已到，后台等待次日恢复。':'调用次数上限已到，后台等待次日恢复。'):budget.enabled?'后台日预算可用。':'后台独立日预算已关闭。';
 target.textContent=`${blocked} ${budget.day||''}（${budget.timezone||'Asia/Shanghai'}）：调用 ${runtimeNumber(budget.used_calls)} / ${runtimeNumber(budget.max_calls)}；计入预算 ${runtimeNumber(budget.input_tokens)} / ${runtimeNumber(budget.max_input_tokens)} token；总输入 ${runtimeNumber(budget.total_input_tokens)}，缓存命中 ${runtimeNumber(budget.cached_input_tokens)}，缓存拆分未知 ${runtimeNumber(budget.unknown_cache_input_tokens)}。${runtimeNumber(budget.unknown_usage_calls)} 次未报告输入用量。恢复时间：${runtimeTime(budget.resets_at_ms)}。`;
 const presence=$('idle-economy-current'),p=value.idle_economy;if(presence)presence.textContent=!p?.enabled?'整机空闲节能未启用。':`整机输入空闲：${p.input_idle_seconds==null?'未知':Math.floor(p.input_idle_seconds/60)+' 分钟'}；${p.mode==='deep_idle'?'深度节能：最多约每30分钟一次新规划':p.mode==='idle'?'节能：最多约每10分钟一次新规划':'正常活动'}。${p.optional_model_work_allowed?'后台可按节奏规划。':'保护返场预算，继续复用本地活动；明确约定的接续仍按原总额度执行。'}`;
}
function renderRuntimeCache(value){
 const data=value.model_cache||{},connections=data.connections||{},signature=JSON.stringify([connections,value.settings?.profiles,data.since_ms,data.updated_ms]);if(signature===runtimeControlState.cacheSignature)return;runtimeControlState.cacheSignature=signature;
 $('model-cache-period').textContent=Object.keys(connections).length?`累计区间：${runtimeTime(data.since_ms)} 至 ${runtimeTime(data.updated_ms)}。`:'尚无服务商用量记录。离线演示不会生成虚假的 token 数。';
 let input=0,output=0,hit=0,miss=0,hasInput=false,hasOutput=false,hasCache=false;const rows=$('model-cache-connections'),opened=new Set([...rows.querySelectorAll('details[open]')].map(section=>section.dataset.profileId));rows.replaceChildren();
 for(const [id,connection] of Object.entries(connections)){
  const total=connection.total||{},reported=Number(total.cache_reported_calls)>0;
  if(runtimeUsageKnown(total,'input_tokens')&&Number.isFinite(total.input_tokens)){input+=total.input_tokens;hasInput=true;}if(runtimeUsageKnown(total,'output_tokens')&&Number.isFinite(total.output_tokens)){output+=total.output_tokens;hasOutput=true;}if(reported&&Number.isFinite(total.cache_hit_ratio)&&Number.isFinite(total.cached_input_tokens)&&Number.isFinite(total.uncached_input_tokens)){hit+=total.cached_input_tokens;miss+=total.uncached_input_tokens;hasCache=true;}
  const section=el('details','runtime-usage-detail'),profile=value.settings?.profiles?.find(profile=>profile.id===id),label=id==='$other_profiles'?'其他连接（合并）':profile?.label||id,summary=el('summary','',`${label} · ${runtimeNumber(total.calls)} 次调用 · 缓存 ${reported?runtimeRatio(total.cache_hit_ratio):'未报告'}`);section.dataset.profileId=id;section.open=opened.has(id);section.append(summary);
  const note=el('p','helper',`已报告用量 ${runtimeNumber(total.usage_reported_calls)} 次；已报告缓存细项 ${runtimeNumber(total.cache_reported_calls)} 次。`);section.append(note);
  const table=el('table','runtime-usage-table'),head=el('thead'),header=el('tr');for(const text of ['用途','调用','输入','输出','缓存命中率'])header.append(el('th','',text));head.append(header);const body=el('tbody');
  for(const [purpose,usage] of Object.entries(connection.purposes||{})){const line=el('tr');for(const text of [runtimePurpose(purpose),runtimeNumber(usage.calls),runtimeUsageKnown(usage,'input_tokens')?runtimeNumber(usage.input_tokens):'未报告',runtimeUsageKnown(usage,'output_tokens')?runtimeNumber(usage.output_tokens):'未报告',Number(usage.cache_reported_calls)>0?runtimeRatio(usage.cache_hit_ratio):'未报告'])line.append(el('td','',text));body.append(line);}table.append(head,body);const wrap=el('div','runtime-table-wrap');wrap.append(table);section.append(wrap);rows.append(section);
 }
 $('model-cache-input').textContent=hasInput?runtimeNumber(input):'未报告';$('model-cache-output').textContent=hasOutput?runtimeNumber(output):'未报告';$('model-cache-ratio').textContent=hasCache&&hit+miss>0?runtimeRatio(hit/(hit+miss)):'未报告';
}
function updateRuntimeControls(value){
 runtimeControlState.value=value;mountRuntimeControls();const status=value.runtime_status||{};
 $('runtime-detail-label').textContent=status.label||(value.offline?'已离线':value.paused?'已暂停':value.busy?'正在处理':'等待新的活动');
 $('runtime-detail-description').textContent=status.detail||'等待后台提供详细运行状态。';$('runtime-details').dataset.runtimeCode=status.code||'unknown';
 $('runtime-background-allowed').textContent=status.background_allowed===true?'允许安排':status.background_allowed===false?'暂缓':'待确认';
 $('runtime-queue').textContent=typeof status.activity_queued==='number'?`${status.activity_active?'正在执行一项，另有':'等待执行'} ${status.activity_queued} 项`:'待确认';
 $('runtime-speech').textContent=({speaking:'正在朗读',waiting:'等待合成或播放',idle:'空闲'})[status.speech_state]||'待确认';
 const own=value.own_time||{};$('runtime-own-time').textContent=own.ready?'可以自行选择':({disabled:'未开启',offline:'已离线',paused:'已暂停',user_input:'先等你输入完',sleeping:'正在休息',in_progress:'正在自己的活动中',daily_budget:'等待后台额度恢复',return_budget_reserve:'为交谈保留余量',cooldown:'稍后再考虑',finishing_previous:'等待上一段活动收尾'})[own.wait_reason]||'待确认';
 const previousClean=value.cognitive_trace?.previous_run_clean;$('cognitive-trace-seal').textContent=previousClean===false?'上次审计未正常封存，尾部记录可能存在缺口；已提交的原始档案保留。':previousClean===true?'上次审计已正常封存。':'尚无上次审计封存记录。';
 const sleep=value.circadian||{};$('circadian-current').textContent=!sleep.available?'作息插件未启用。':!sleep.configured?'还没有自行安排作息。':sleep.phase==='sleeping'?'当前正在休息；未醒时暂缓回复。':sleep.phase==='queued'||sleep.phase==='requesting'?'已准备休息，等待实际姿态确认。':'当前没有长睡。';$('circadian-wake').disabled=!sleep.available;
 updateRuntimeBudget(value);renderRuntimeCache(value);updatePortableAvailability(value);
 if(runtimeControlState.portable.watching||!runtimeControlState.portable.checked)pollPortableOperation();
}

function portableError(error){const message=String(error?.message||error||'操作未完成。');return Object.entries(runtimeControlErrors).find(([code])=>message.includes(code))?.[1]||message;}
function clearPortablePasswords(){for(const id of ['portable-export-password','portable-export-confirm','portable-import-password']){const input=$(id);if(input)input.value='';}}
function portableProgress(text,error=false){const target=$('portable-vault-progress');if(target){target.textContent=text;target.classList.toggle('runtime-error',error);}}
function updatePortableAvailability(value){
 const state=runtimeControlState.portable,ready=Boolean(value.paused&&value.offline),hint=$('portable-vault-prerequisite');if(!hint)return;
 hint.textContent=ready?'后台已暂停且离线，可以处理跨机密钥。':'处理密钥前需暂停后台并离线，避免备份与恢复期间继续写入。';
 $('portable-pause-offline').disabled=state.busy||ready;$('portable-export-submit').disabled=state.busy||!ready;$('portable-verify').disabled=state.busy||!ready||!state.bundle;$('portable-restore').disabled=state.busy||!ready||!state.bundle||!state.verified;
}
function portableResult(operation,result){
 const state=runtimeControlState.portable;
 if(operation==='export'){
  if(!result?.bundle)throw new Error('后台未返回完整的加密密钥包。');downloadRuntimeFile(result.filename||'xiaoa-portable-keys.json',JSON.stringify(result.bundle));portableProgress(`已生成加密密钥包，包含 ${runtimeNumber(result.stores)} 个存档密钥。${result.includes_memory_contents?`另有 ${runtimeNumber(result.mentor_files)} 份经过密码加密的导师状态或记忆附件。`:'它不包含记忆正文。'}请与同一检查点的完整存档备份一同妥善保存。`);
 }else if(operation==='verify'){
  state.verified=true;portableProgress(`密钥包与备份验证通过：${runtimeNumber(result?.stores)} 个存档，${runtimeNumber(result?.records)} 条加密记录。请输入尚不存在的新数据目录；恢复时需再次输入密码。`);
 }else if(operation==='restore'){
  state.verified=false;portableProgress('已复制到新的恢复目录，并为当前 Windows 用户重新保护密钥。原档案和原备份未覆盖；检查结果后再选择是否切换档案。');
 }
}
async function pollPortableOperation(){
 const state=runtimeControlState.portable;if(state.polling)return;state.polling=true;
 try{const status=await api('/api/vault-portability/status');state.checked=true;
  if(status.state==='running'){state.busy=true;state.watching=true;state.operation=status.operation;portableProgress(`正在${({export:'生成加密密钥包',verify:'验证备份和密钥',restore:'复制并恢复存档'})[status.operation]||'处理密钥操作'}……请保留完整备份。`);}
  else if(status.state==='completed'&&state.watching){state.busy=false;state.watching=false;portableResult(state.operation||status.operation,status.result);}
  else if(status.state==='failed'&&state.watching){state.busy=false;state.watching=false;state.verified=false;portableProgress(portableError(status.error?.code||status.error||'操作未完成。'),true);}
 }catch(error){state.checked=true;portableProgress(portableError(error),true);state.busy=false;state.watching=false;}finally{state.polling=false;updatePortableAvailability(runtimeControlState.value||{});}
}
async function startPortableOperation(operation,data){
 const state=runtimeControlState.portable;if(state.busy)return;state.busy=true;state.operation=operation;updatePortableAvailability(runtimeControlState.value||{});portableProgress('正在提交密钥操作……');
 try{await api('/api/vault-portability/'+operation,data);data.password='';clearPortablePasswords();state.watching=true;await pollPortableOperation();}
 catch(error){state.busy=false;state.watching=false;portableProgress(portableError(error),true);}
 finally{if(data?.password)data.password='';clearPortablePasswords();updatePortableAvailability(runtimeControlState.value||{});}
}
function mountPortableVault(){
 const card=runtimeCard('portable-vault','跨机密钥备份与恢复','portable-vault');card.append(el('h3','','让加密档案能够迁移。'),el('p','helper','完整存档备份和加密密钥包需要一同保存。密钥包由你设置的密码保护，不展示记忆正文，也不包含模型 API key。遗失备份密码后无法靠程序找回。'));
 const prerequisite=el('p','helper');prerequisite.id='portable-vault-prerequisite';const pause=el('button','quiet','暂停后台并离线');pause.id='portable-pause-offline';pause.type='button';pause.onclick=async()=>{pause.disabled=true;try{await api('/api/control',{paused:true,offline:true});await load();}catch(error){portableProgress(portableError(error),true);}finally{updatePortableAvailability(runtimeControlState.value||{});}};card.append(prerequisite,pause);
 const exportDetails=el('details','runtime-portable-step');exportDetails.open=true;exportDetails.append(el('summary','','1 · 导出密码保护的密钥包'));const exportForm=el('form','runtime-portable-form'),grid=el('div','form-grid');
 const password=field(grid,'备份密码（12 至 1024 字节）','',()=>{}),confirm=field(grid,'再次输入备份密码','',()=>{});password.id='portable-export-password';confirm.id='portable-export-confirm';for(const input of [password,confirm]){input.type='password';input.autocomplete='new-password';input.maxLength=1024;input.required=true;}
 const exportButton=el('button','quiet','生成并下载加密密钥包');exportButton.id='portable-export-submit';exportButton.type='submit';exportForm.append(grid,exportButton,el('p','helper','推荐至少 12 位随机字母、数字与符号；中文按 UTF-8 字节计算。请单独保管密码。'));exportForm.onsubmit=async event=>{event.preventDefault();if(password.value!==confirm.value){clearPortablePasswords();portableProgress('两次密码不一致，请重新输入。',true);return;}const bytes=new TextEncoder().encode(password.value).length;if(bytes<12||bytes>1024){clearPortablePasswords();portableProgress(runtimeControlErrors.portable_password,true);return;}await startPortableOperation('export',{password:password.value});};exportDetails.append(exportForm);card.append(exportDetails);
 const restoreDetails=el('details','runtime-portable-step');restoreDetails.append(el('summary','','2 · 验证备份并恢复到新目录'));const form=el('div','runtime-portable-form');
 const fileLabel=el('label','','选择已导出的加密密钥包'),file=el('input');file.type='file';file.accept='.json,application/json';file.id='portable-import-file';file.setAttribute('aria-label','加密密钥包文件');fileLabel.append(file);const importGrid=el('div','form-grid');
 const backup=field(importGrid,'完整备份目录','',()=>{runtimeControlState.portable.verified=false;updatePortableAvailability(runtimeControlState.value||{});});backup.id='portable-backup-root';backup.placeholder='例如 E:/Backups/小爱完整备份';
 const importPassword=field(importGrid,'解锁密钥包的密码','',()=>{});importPassword.id='portable-import-password';importPassword.type='password';importPassword.autocomplete='off';importPassword.maxLength=1024;
 const destination=field(importGrid,'新的数据目录（须尚不存在）','',()=>{});destination.id='portable-target-root';destination.placeholder='例如 E:/Aliyat/Recovered/小爱/data';
 const actions=el('div','runtime-actions'),verify=el('button','quiet','验证密钥与备份'),restore=el('button','quiet','复制并恢复到新目录');verify.id='portable-verify';restore.id='portable-restore';verify.type=restore.type='button';
 file.onchange=async()=>{const state=runtimeControlState.portable;state.bundle=null;state.verified=false;try{const chosen=file.files?.[0];if(!chosen)return;if(chosen.size>10*1024*1024)throw new Error('密钥包超过 10 MB，请确认选择的是导出的 JSON 密钥包。');const bundle=JSON.parse(await chosen.text());if(!bundle||typeof bundle!=='object'||Array.isArray(bundle))throw new Error('密钥包格式不正确。');state.bundle=bundle;portableProgress('密钥包已选中；内容保持加密，请填写备份目录并验证。');}catch(error){file.value='';portableProgress(error instanceof SyntaxError?'密钥包不是有效 JSON 文件。':portableError(error),true);}finally{updatePortableAvailability(runtimeControlState.value||{});}};
 verify.onclick=async()=>{if(!backup.value.trim()||!importPassword.value){portableProgress('请填写完整备份目录和解锁密码。',true);return;}await startPortableOperation('verify',{password:importPassword.value,bundle:runtimeControlState.portable.bundle,backupRoot:backup.value.trim()});};
 restore.onclick=async()=>{if(!runtimeControlState.portable.verified||!destination.value.trim()||!importPassword.value){portableProgress('请先验证备份，再填写新的恢复目录并重新输入密码。',true);return;}await startPortableOperation('restore',{password:importPassword.value,bundle:runtimeControlState.portable.bundle,backupRoot:backup.value.trim(),targetRoot:destination.value.trim()});};
 actions.append(verify,restore);form.append(fileLabel,importGrid,actions,el('p','helper','完整备份目录应包含 manifest.json 和 data/。验证和恢复都会清空密码输入。恢复只建立新的数据目录并重新保护密钥；角色与程序配置仍保留在完整备份中，不会自动切换正在使用的档案。'));restoreDetails.append(form);card.append(restoreDetails);
 const progress=el('p','helper');progress.id='portable-vault-progress';progress.setAttribute('aria-live','polite');progress.textContent='请先完成一次完整存档备份，再导出密钥包。';const clear=el('button','text-button','清空密码与选中的密钥包');clear.type='button';clear.onclick=()=>{clearPortablePasswords();file.value='';runtimeControlState.portable.bundle=null;runtimeControlState.portable.verified=false;portableProgress('已清空本页输入。已下载的加密密钥包仍由你保管。');updatePortableAvailability(runtimeControlState.value||{});};card.append(progress,clear);
 $('page-workspace').querySelector('[data-section-key="archive"]').after(card);
 document.querySelectorAll('.nav').forEach(button=>button.addEventListener('click',()=>{if(button.dataset.page!=='workspace')clearPortablePasswords();}));window.addEventListener('pagehide',clearPortablePasswords);
}
