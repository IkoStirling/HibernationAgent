'use strict';
function appendStyleLoraControls(card,configuration){
 const panel=el('div','style-lora-controls'),status=el('p','helper','先保存 Python 与本地模型路径，再检查环境。训练不会自动启用。');status.setAttribute('aria-live','polite');
 const errors={style_python_required:'请填写已安装依赖的 Python 完整路径。',training_dependencies_missing:'训练环境缺少依赖，请先运行 PrepareStyleLora.ps1。',local_base_model_required:'请填写本地基座模型目录。',safetensors_base_model_required:'模型目录需要 config.json 和 safetensors 权重。',approved_corpus_required:'请先导入明确认可的表达样本。',operator_approval_required:'请确认样本是你认可并允许训练的内容。',training_sample_too_long:'样本超过序列上限；请精简上下文或提高序列上限，程序不会悄悄截断。',trained_adapter_required:'请先完成训练并选择版本 ID。',adapter_base_mismatch:'适配器与当前基座不匹配，请选择训练时使用的模型目录。',training_busy:'训练正在占用模型，请稍后预览。',rewrite_incomplete:'编辑输出没有完整结束，本次保留原稿。',style_service_unavailable:'本地风格服务没有及时返回，请查看状态。',training_failed:'训练失败，请检查模型格式、设备内存和依赖。',training_device_unavailable:'所选设备不可用；CUDA需要对应的 PyTorch。',style_storage_full:'档案所在磁盘空间不足；请先整理空间。',saved_adapter_limit:'已达到16个版本；请先备份整理旧版本。',rewrite_context_too_long:'原稿与上下文超出序列上限，本次保留原稿。',invalid_tool_arguments:'输入不符合要求，请检查原稿是否为空或超长。'};
 const describe=code=>errors[code]||('未完成：'+code);
 const run=async(tool,args={})=>{const saved=(await api('/api/dashboard')).settings?.plugins?.['style-lora']||{};if(!saved.enabled||new Set([...Object.keys(saved),...Object.keys(configuration)]).size!==Object.keys(saved).length||Object.entries(configuration).some(([key,value])=>saved[key]!==value))throw new Error('请先点击“保存插件设置”，再进行训练或预览。');const result=await runOperatorTool(tool,args,tool==='style_lora.preview'?Math.min(70000,Number(configuration.timeoutMs||12000)+7000):20000);if(!result?.ok){const code=result?.error?.code||'unknown';throw new Error(tool.startsWith('style_lora.dataset.')?code:describe(code));}return result.result;};
 const versions=el('select','');versions.setAttribute('aria-label','已训练的风格版本');versions.append(new Option('检查状态后选择版本',''));
 const choose=el('button','quiet','选择此版本');choose.type='button';choose.onclick=async()=>{if(operatorBusy||saving){toast('请等待当前操作完成。');return;}if(!versions.value)return;configuration.adapterId=versions.value;try{if(await save())toast('已选择版本；需要单独开启“启用 LoRA 对白编辑”。');}catch(e){status.textContent=e.message;}};
 const render=value=>{
  const missing=Object.entries(value.dependencies||{}).filter(([,ready])=>!ready).map(([name])=>name),job=value.job||{};
  status.textContent=`已认可样本 ${value.samples||0} 条；训练：${job.state||'idle'}${job.step?` ${job.step}/${job.total_steps}`:''}${Number.isFinite(job.loss)?`，损失 ${job.loss.toFixed(4)}`:''}。${job.error?describe(job.error):''}${missing.length?' 缺少：'+missing.join('、'):''}`;
  versions.replaceChildren(new Option('请选择已完成的版本',''));for(const item of [...(value.adapters||[])].reverse())versions.append(new Option(`${item.id} · ${item.samples}条 / r${item.rank}`,item.id));versions.value=configuration.adapterId||'';
 };
 const controls=el('div','test-row');
 const action=(label,operation)=>{const button=el('button','quiet',label);button.type='button';button.onclick=async()=>{button.disabled=true;try{await operation();}catch(e){status.textContent=e.message;}finally{button.disabled=false;}};controls.append(button);return button;};
 action('检查环境与训练状态',async()=>render(await run('style_lora.status')));
 const corpus=el('textarea','');corpus.setAttribute('aria-label','风格训练对照 JSONL');corpus.rows=7;corpus.maxLength=524288;
 corpus.placeholder='每行一个 JSON 对象：context、original、preferred、reason；不粘贴密钥。样本应保留原意，不是只把句子变短。';
 const consentLabel=el('label','helper style-lora-consent'),consent=el('input','');consent.type='checkbox';consent.setAttribute('aria-label','我认可这些表达并允许用于训练');consentLabel.append(consent,document.createTextNode('我认可这些修改，并允许将这些样本用于本地训练。'));
 action('导入已认可对照',async()=>{
  if(!consent.checked)throw new Error(errors.operator_approval_required);
  let samples;try{samples=corpus.value.split(/\r?\n/).filter(v=>v.trim()).map(line=>({...JSON.parse(line),approved:true}));}catch{throw new Error('JSONL 格式不正确；每行需要一个完整对象。');}
  if(!samples.length||samples.length>256)throw new Error('每次可导入1至256条样本。');
  const result=await run('style_lora.import',{consent:true,samples});corpus.value='';consent.checked=false;status.textContent=`已加密保存 ${result.samples} 条样本；重复对照不会重复加入。`;
 });
 action('开始一批训练',async()=>{const result=await run('style_lora.train');status.textContent='已开始训练：'+result.adapter_id+'。点击检查状态查看进度；完成后不会自动启用。';});
 action('取消训练',async()=>{await run('style_lora.cancel');status.textContent='已请求取消。未完成的训练不会发布为可用版本。';});
 const input=el('textarea',''),preview=el('p','helper');input.rows=3;input.maxLength=4000;input.setAttribute('aria-label','风格 LoRA 预览原稿');input.placeholder='输入一段原稿，手动预览所选版本；不会发成角色对白。';preview.style.whiteSpace='pre-wrap';
 action('预览所选 LoRA',async()=>{const result=await run('style_lora.preview',{text:input.value,context:[]});preview.textContent=result.text;status.textContent='这是编辑候选，请自行检查原意。实际交谈仍需独立审核，未通过不会发布。';});
 panel.append(el('p','helper','语料只接受明确认可的对照，不自动读取聊天、网页或记忆。原始存档不改写。'),corpus,consentLabel,controls,versions,choose,input,preview,status);appendExpressionWorkshopControls(panel,run);card.append(panel);
}


// Operator-only dataset preparation. This does not invoke the training worker.
function appendExpressionWorkshopControls(parent,run){
 const panel=el('section','expression-workshop'),heading=el('h3','','候选语料工坊'),status=el('p','helper','导入已选择的示范包，再用模拟连接检查流程。候选不会自动训练或写入角色记忆。');
 status.setAttribute('aria-live','polite');panel.append(heading,status);
 const errors={dataset_direction_binding:'示范与选择文件不匹配，请使用同一版本的审核包。',dataset_target_digest:'示范内容已变化，需要重新审核。',dataset_direction_unreviewed_edit:'选择中含未经重审的改稿，请先整理为新的示范版本。',dataset_send_consent_required:'请确认允许本次调用所选模型。',dataset_distinct_profiles_required:'生成与审核请选择两个不同的连接。',dataset_edit_requires_re_review:'修改后的文字先保存为待定，不能直接批准。',dataset_disagreement_confirmation:'保留与模型审核意见不同的候选时，请勾选确认并填写理由。',dataset_call_budget:'已达到本批调用上限，已保留现有结果。',dataset_input_budget:'已达到本批输入量上限，已保留现有结果。',dataset_token_budget:'已达到本批用量阈值，已保留现有结果。',dataset_insufficient_balance:'模型 API 余额不足，已保留此前结果。',dataset_credentials:'模型连接密钥无效，请检查连接设置。',dataset_rate_limited:'模型服务频率受限，本批已停止。',dataset_provider_failed:'模型服务未完成本次请求，本批已停止。',dataset_generated_schema:'生成结果格式不合格，经过一次修正仍未通过。',dataset_review_schema:'审核结果格式不合格，经过一次修正仍未通过。',dataset_busy:'本批还在运行，请暂停或取消后整理结果。',dataset_task_seed_missing:'导入的示范包中没有这种类型的已保留示范。',dataset_restart_interrupted:'上次运行被中断；保留已有材料，不会自动补调用。',dataset_candidate_changed:'候选版本已变化，请刷新后再审核。',dataset_storage_limit:'本批记录达到存储上限，请缩小批量。'};
 const describe=code=>errors[code]||('本次未完成：'+code);
 let requests=Promise.resolve();const call=(action,args={})=>{const next=requests.then(()=>run('style_lora.dataset.'+action,args));requests=next.catch(()=>{});return next;};
 const row=el('div','test-row'),importFile=el('input','');importFile.type='file';importFile.accept='.json,application/json';importFile.setAttribute('aria-label','候选工坊示范审核包');
 const direction=el('select',''),generator=el('select',''),reviewer=el('select',''),task=el('select',''),jobs=el('select','');
 direction.setAttribute('aria-label','候选工坊方向版本');generator.setAttribute('aria-label','候选工坊生成连接');reviewer.setAttribute('aria-label','候选工坊审核连接');task.setAttribute('aria-label','候选工坊任务类型');jobs.setAttribute('aria-label','候选工坊历史批次');
 for(const [value,label] of [['conversation_sft','完整对话'],['rewrite_sft','对白润色对照'],['behavior_reference','不发消息等行为参考']])task.append(new Option(label,value));
 const mock=id=>({id,label:id.endsWith('generator')?'内置模拟生成':'内置模拟审核',protocol:'mock',model:'workshop-mock',enabled:true});
 const mockGen=mock('workshop-mock-generator'),mockReview=mock('workshop-mock-reviewer');
 const profiles=()=>[mockGen,mockReview,...(settings.profiles||[]).filter(p=>p.enabled&&p.protocol!=='mock')];
 const fillProfiles=()=>{for(const select of [generator,reviewer]){const before=select.value;select.replaceChildren();for(const p of profiles())select.append(new Option(p.label||p.id,p.id));if([...select.options].some(o=>o.value===before))select.value=before;}if(!reviewer.value||reviewer.value===generator.value)reviewer.value=mockReview.id;};fillProfiles();
 const grid=el('div','form-grid');
 const label=(title,control)=>{const l=el('label','',title);l.append(control);grid.append(l);};
 label('方向示范',direction);label('生成连接',generator);label('独立审核连接',reviewer);label('本批类型',task);
 const count=el('input',''),budget=el('input',''),tokenBudget=el('input','');
 for(const [input,name,value,min,max] of [[count,'候选数量',36,1,64],[budget,'调用次数上限（含生成、审核及格式修正）',32,1,128],[tokenBudget,'用量阈值（按服务回报的 token）',200000,1,1000000]]){input.type='number';input.min=min;input.max=max;input.step=1;input.value=value;input.setAttribute('aria-label',name);label(name,input);}
 const consentLine=el('label','helper'),consent=el('input','');consent.type='checkbox';consent.setAttribute('aria-label','允许本批调用所选模型');consentLine.append(consent,document.createTextNode('允许本批调用所选模型，发送已选示范与合成候选；不含正式聊天或记忆。模拟连接不需要此项。'));
 let selected='',currentJob=null,timer=null,busy=false,finishedRefresh='';const drafts=new Map();
 const candidateSelect=el('select','');candidateSelect.setAttribute('aria-label','待审核候选');const candidateBox=el('div','');candidateBox.style.whiteSpace='pre-wrap';
 function saveDraft(){const id=candidateBox._candidateId;if(!id||!candidateBox._inputs)return;const [note,edit,confirm]=candidateBox._inputs;drafts.set(id,{note:note.value,edit:edit.value,confirm:confirm.checked});}
 function renderCandidate(){
  candidateBox.replaceChildren();candidateBox._inputs=null;candidateBox._candidateId=null;const c=currentJob?.candidates?.find(x=>x.id===candidateSelect.value);if(!c)return;candidateBox._candidateId=c.id;
  candidateBox.append(el('h4','',c.id+(c.mock_only?' · 模拟候选，仅检查流程':'')),el('p','helper','以下为合成情景：'+c.scene_facts.join('；')));
  for(const m of c.context)candidateBox.append(el('p','',(m.role==='user'?'用户（前文）：':'小爱（前文）：')+m.text));
  candidateBox.append(el('p','','用户：'+c.user_message));
  if(c.original!==null)candidateBox.append(el('p','helper','本次生成的对照原稿：'+c.original));
  candidateBox.append(el('p','','候选：'+(c.response===null?'（不发新消息；行为参考）':c.response)));
  const v=c.reviewer_review;candidateBox.append(el('p','helper',v?`模型审核：${v.decision} · ${v.reason}`:'模型尚未完成该条审核。'));
  if(v?.suggested_text)candidateBox.append(el('p','helper','审核者建议（未替换原稿）：'+v.suggested_text));
  candidateBox.append(el('p','helper',`来源示范：${c.source_ids.join('、')}；生成：${c.generator.model}；审核：${c.reviewer?.model||'未完成'}。`));
  const note=el('textarea',''),edit=el('textarea',''),confirm=el('input','');note.rows=2;note.maxLength=2000;note.setAttribute('aria-label','候选人工审核理由');edit.rows=3;edit.maxLength=800;edit.setAttribute('aria-label','候选修改建议');confirm.type='checkbox';confirm.setAttribute('aria-label','确认保留有审核分歧的候选');
  const draft=drafts.get(c.id);note.value=draft?.note??c.human_review.note??'';edit.value=draft?.edit??c.human_review.suggested_text??'';confirm.checked=draft?.confirm??false;
  const noteLabel=el('label','helper','我的理由'),editLabel=el('label','helper','修改建议（另存，修改后需重审）'),confirmLabel=el('label','helper');noteLabel.append(note);editLabel.append(edit);confirmLabel.append(confirm,document.createTextNode('我已看过分歧，仍想保留此例（同时填写理由）。'));candidateBox.append(noteLabel,editLabel,confirmLabel);
  candidateBox._inputs=[note,edit,confirm];for(const e of [note,edit,confirm])e.oninput=saveDraft;
  const votes=el('div','test-row');for(const [value,title] of [['keep','保留候选'],['hold','待定／保存修改建议'],['reject','剔除候选']]){const b=el('button','quiet',title);b.type='button';b.onclick=async()=>{saveDraft();b.disabled=true;try{await call('decide',{job_id:currentJob.id,candidate_id:c.id,candidate_sha256:c.candidate_sha256,decision:value,note:note.value,suggested_text:edit.value,accept_disagreement:confirm.checked});drafts.delete(c.id);await loadJob(currentJob.id);}catch(e){status.textContent=describe(e.message);}finally{b.disabled=false;}};votes.append(b);}candidateBox.append(votes,el('p','helper','人工保留仍不等于训练同意。原候选、修改建议与历次意见分别保留。'));
 }
 candidateSelect.onchange=()=>{saveDraft();renderCandidate();};
 async function loadJob(id){if(!id)return;saveDraft();const previous=candidateSelect.value;currentJob=await call('get',{job_id:id});selected=id;candidateSelect.replaceChildren();for(const c of currentJob.candidates||[])candidateSelect.append(new Option(`${c.id} · ${c.human_review.decision} / 模型${c.reviewer_review?.decision||'未审'}`,c.id));if([...candidateSelect.options].some(o=>o.value===previous))candidateSelect.value=previous;renderCandidate();}
 function renderStatus(value){const j=value.job||{},dir=direction.value,priorJob=jobs.value;direction.replaceChildren(new Option('请选择已导入方向',''));for(const d of value.directions||[])direction.append(new Option(`${d.version} · ${d.samples}例`,d.id));if([...direction.options].some(o=>o.value===dir))direction.value=dir;else if(direction.options.length>1)direction.selectedIndex=1;
  jobs.replaceChildren(new Option('请选择批次',''));for(const item of [...(value.jobs||[])].reverse())jobs.append(new Option(`${item.id} · ${item.task}`,item.id));if([...jobs.options].some(o=>o.value===priorJob))jobs.value=priorJob;else if(j.id)jobs.value=j.id;
  const names={running:'准备中',generating:'生成中',reviewing:'独立审核中',pausing:'当前调用结束后暂停',paused:'已暂停',completed:'已完成',cancelled:'已取消',failed:'已停止',interrupted:'上次运行中断',idle:'尚无批次'};
  status.textContent=`${names[j.state]||j.state||'尚无批次'}；已处理 ${j.processed_slots||0}/${j.requested||0}；调用 ${j.calls||0}（真实 ${j.real_model_calls||0}、模拟 ${j.mock_calls||0}）；已回报用量 ${j.reported_tokens||0} token${j.unknown_usage_calls?`，另有${j.unknown_usage_calls}次未知用量`:''}。${j.error?describe(j.error):''}${j.review_independence==='separate_profile_same_model'?' 两个连接使用同一模型，属于独立上下文审核，不是跨模型验证。':''}`;
  if(j.id&&!['completed','failed','cancelled','interrupted'].includes(j.state)){if(!timer)timer=setInterval(async()=>{if(!panel.isConnected){clearInterval(timer);timer=null;return;}if(busy||operatorBusy||saving||current?.busy)return;try{await refresh(false);}catch{}},2500);}else if(timer){clearInterval(timer);timer=null;}
 }
 async function refresh(load=true){if(busy)return;busy=true;try{const value=await call('status');renderStatus(value);const j=value.job;if(load&&jobs.value)await loadJob(jobs.value);else if(j?.id&&['completed','failed','cancelled','interrupted'].includes(j.state)&&finishedRefresh!==j.id){finishedRefresh=j.id;await loadJob(j.id);}}finally{busy=false;}}
 function button(title,action){const b=el('button','quiet',title);b.type='button';b.onclick=async()=>{b.disabled=true;try{await action();}catch(e){status.textContent=describe(e.message);}finally{b.disabled=false;}};row.append(b);return b;}
 button('导入示范审核包',async()=>{const file=importFile.files[0];if(!file||file.size>850000)throw Error('请选择小于850KB的示范审核包。');let bundle;try{bundle=JSON.parse(await file.text());}catch{throw Error('审核包不是有效JSON。');}if(!bundle.pack||!bundle.review)throw Error('需要包含示范与对应人工选择的审核包。');const outcome=await call('import',bundle);importFile.value='';await refresh(false);direction.value=outcome.id;status.textContent='已加密导入'+outcome.samples+'例方向示范；没有导入训练。';});
 button('查看批次状态',()=>refresh());
 button('开始生成并审核',async()=>{const dashboard=await api('/api/dashboard'),saved=dashboard.settings.profiles||[];const find=id=>id===mockGen.id?mockGen:id===mockReview.id?mockReview:saved.find(p=>p.id===id&&p.enabled);const a=find(generator.value),b=find(reviewer.value);if(!a||!b)throw Error('请先保存并启用所选模型连接。');const fields=['id','label','enabled','protocol','origin','basePath','model','credentialEnvironment','supportsJsonSchema','supportsJsonObject','legacyMaxTokens','proxyOrigin','useSystemProxy','timeoutMs','reasoningEffort','maxOutputTokens'];const safe=p=>Object.fromEntries(fields.filter(k=>p[k]!==undefined).map(k=>[k,p[k]]));const result=await call('start',{direction_id:direction.value,generator:safe(a),reviewer:safe(b),task:task.value,count:Number(count.value),max_calls:Number(budget.value),max_reported_tokens:Number(tokenBudget.value),allow_model_calls:consent.checked});consent.checked=false;selected=result.id;await refresh(false);jobs.value=result.id;await loadJob(result.id);});
 button('暂停本批',async()=>{await call('pause');await refresh(false);});button('继续本批',async()=>{await call('resume');await refresh(false);});button('取消本批',async()=>{await call('cancel');await refresh(false);});
 button('导出候选档案（不训练）',async()=>{const id=jobs.value||selected;if(!id)throw Error('请先选择批次。');const bundle=await call('export',{job_id:id});const url=URL.createObjectURL(new Blob([JSON.stringify(bundle,null,2)+'\n'],{type:'application/json;charset=utf-8'})),a=document.createElement('a');a.href=url;a.download=id+'-candidates.json';a.click();setTimeout(()=>URL.revokeObjectURL(url),3000);status.textContent='已导出候选原稿、提示版本、调用记录与审核意见；没有训练。';});
 jobs.onchange=async()=>{try{await loadJob(jobs.value);}catch(e){status.textContent=describe(e.message);}};
 panel.append(el('p','helper','先导入已审核的方向包。每批单独选择完整对话、润色对照或行为参考；开始后由独立连接审核。暂停在当前调用结束后生效，取消会中止当前请求。'),importFile,grid,consentLine,row,jobs,candidateSelect,candidateBox);parent.append(panel);
}
