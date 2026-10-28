'use strict';
function appendStyleLoraControls(card,configuration){
 const panel=el('div','style-lora-controls'),status=el('p','helper','先保存 Python 与本地模型路径，再检查环境。训练不会自动启用。');status.setAttribute('aria-live','polite');
 const errors={style_python_required:'请填写已安装依赖的 Python 完整路径。',training_dependencies_missing:'训练环境缺少依赖，请先运行 PrepareStyleLora.ps1。',local_base_model_required:'请填写本地基座模型目录。',safetensors_base_model_required:'模型目录需要 config.json 和 safetensors 权重。',approved_corpus_required:'请先导入明确认可的表达样本。',operator_approval_required:'请确认样本是你认可并允许训练的内容。',training_sample_too_long:'样本超过序列上限；请精简上下文或提高序列上限，程序不会悄悄截断。',trained_adapter_required:'请先完成训练并选择版本 ID。',adapter_base_mismatch:'适配器与当前基座不匹配，请选择训练时使用的模型目录。',training_busy:'训练正在占用模型，请稍后预览。',rewrite_incomplete:'编辑输出没有完整结束，本次保留原稿。',style_service_unavailable:'本地风格服务没有及时返回，请查看状态。',training_failed:'训练失败，请检查模型格式、设备内存和依赖。',training_device_unavailable:'所选设备不可用；CUDA需要对应的 PyTorch。',style_storage_full:'档案所在磁盘空间不足；请先整理空间。',saved_adapter_limit:'已达到16个版本；请先备份整理旧版本。',rewrite_context_too_long:'原稿与上下文超出序列上限，本次保留原稿。',invalid_tool_arguments:'输入不符合要求，请检查原稿是否为空或超长。'};
 const describe=code=>errors[code]||('未完成：'+code);
 const run=async(tool,args={})=>{const saved=(await api('/api/dashboard')).settings?.plugins?.['style-lora']||{};if(!saved.enabled||new Set([...Object.keys(saved),...Object.keys(configuration)]).size!==Object.keys(saved).length||Object.entries(configuration).some(([key,value])=>saved[key]!==value))throw new Error('请先点击“保存插件设置”，再进行训练或预览。');const result=await runOperatorTool(tool,args,tool==='style_lora.preview'?Math.min(70000,Number(configuration.timeoutMs||12000)+7000):20000);if(!result?.ok)throw new Error(describe(result?.error?.code||'unknown'));return result.result;};
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
 panel.append(el('p','helper','语料只接受明确认可的对照，不自动读取聊天、网页或记忆。原始存档不改写。'),corpus,consentLabel,controls,versions,choose,input,preview,status);card.append(panel);
}
