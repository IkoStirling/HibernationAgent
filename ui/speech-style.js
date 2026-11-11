'use strict';
// Expression preferences only. This editor never calls a model or runs a tool.
const speechStyleEditors=new WeakMap();
function speechStyleDefaults(){return [
 {id:'natural',name:'自然',instructions:''},
 {id:'concise',name:'简洁',instructions:'优先使用简短、清楚的日常表达。信息足够时就停下，不为凑篇幅追加总结。需要解释复杂事情时仍保留必要细节。'},
 {id:'hesitant',name:'轻微迟疑',instructions:'偶尔在确实迟疑或思考措辞时自然使用“嗯”或短暂停顿，语气平静；不要每条回复都加，不固定放在句首，不故意结巴，不改变已经确定的事实。'},
 {id:'literary',name:'轻文学',instructions:'可以少量使用具体、克制的意象，让表达稍有文学感；保留日常交流的清晰和直接，不堆砌比喻，不强行抒情或渲染亲密关系。'}
];}
function speechStyleBytes(value){return new TextEncoder().encode(value).length;}
function speechStyleClip(value,limit){let result='',bytes=0;for(const point of value){if(point.length===1&&point.charCodeAt(0)>=0xd800&&point.charCodeAt(0)<=0xdfff)continue;const size=speechStyleBytes(point);if(bytes+size>limit)break;result+=point;bytes+=size;}return result;}
function speechStyleSafeId(value){return typeof value==='string'&&/^[a-z][a-z0-9_-]{0,47}$/.test(value);}
function appendSpeechStyleControls(card,configuration){
 if(!Array.isArray(configuration.presets)||!configuration.presets.length)configuration.presets=speechStyleDefaults();
 if(typeof configuration.enabled!=='boolean')configuration.enabled=false;
 if(typeof configuration.allowModelSwitch!=='boolean')configuration.allowModelSwitch=false;
 const presets=configuration.presets;
 if(!presets.some(preset=>preset.id===configuration.activePreset))configuration.activePreset=presets.find(preset=>preset.id==='natural')?.id||presets[0].id;
 const previous=speechStyleEditors.get(configuration),state={selected:previous?.selected||configuration.activePreset};speechStyleEditors.set(configuration,state);
 const panel=el('div','speech-style-controls'),status=el('p','helper'),counts=el('p','helper');status.setAttribute('aria-live','polite');
 const notify=(message,error=false)=>{status.textContent=message;status.classList.toggle('runtime-error',error);};
 const collectionBytes=()=>speechStyleBytes(JSON.stringify(configuration));
 const refreshCounts=()=>{counts.textContent=`预设 ${presets.length} / 16；插件配置 ${collectionBytes()} / 16384 字节。`;};
 const grid=el('div','form-grid'),active=field(grid,'当前说话预设',configuration.activePreset,value=>{const previous=configuration.activePreset;configuration.activePreset=value;if(collectionBytes()>16384){configuration.activePreset=previous;active.value=previous;notify('插件配置不能超过 16 KB，请先精简预设。',true);return;}state.selected=value;renderEditor();notify('已选择预设，保存插件设置后生效。');},[]);
 const modelSwitch=toggle(grid,'允许小爱切换已保存的预设',configuration.allowModelSwitch,value=>{const previous=configuration.allowModelSwitch;configuration.allowModelSwitch=value;if(collectionBytes()>16384){configuration.allowModelSwitch=previous;modelSwitch.checked=previous;notify('插件配置不能超过 16 KB，请先精简预设。',true);return;}refreshCounts();notify('切换权限已修改，保存后生效。');});
 const description=el('p','helper');description.dataset.speechStyle='description';
 const edit=field(grid,'编辑预设',state.selected,value=>{state.selected=value;renderEditor();},[]);
 const name=field(grid,'预设名称','',value=>editText('name',value,160));name.maxLength=160;
 const label=el('label','','表达偏好（最多 2048 字节）'),instructions=el('textarea');instructions.rows=5;instructions.maxLength=2048;instructions.setAttribute('aria-label','表达偏好（最多 2048 字节）');label.append(instructions);instructions.oninput=()=>editText('instructions',instructions.value,2048);
 const styleArea=input=>Object.assign(input.style,{width:'100%',minWidth:'0',resize:'vertical',font:'inherit',lineHeight:'1.7',padding:'11px 12px',border:'1px solid var(--border)',borderRadius:'7px',background:'var(--paper)',color:'inherit'});styleArea(instructions);
 const textCount=el('p','helper');textCount.dataset.speechStyle='instruction-count';
 const actions=el('div','runtime-actions'),button=(text,fn)=>{const value=el('button','quiet',text);value.type='button';value.onclick=fn;actions.append(value);return value;};
 const freshId=()=>{let id;do{id='custom-'+(globalThis.crypto?.randomUUID?.().replaceAll('-','')||(Date.now().toString(36)+Math.random().toString(36).slice(2)));}while(presets.some(preset=>preset.id===id));return id;};
 const addPreset=copy=>{
  if(presets.length>=16){notify('最多保留 16 个预设，请先删除不使用的预设。',true);return;}
  const source=presets.find(preset=>preset.id===state.selected),item={id:freshId(),name:copy?`${source?.name||'预设'} 副本`:'新预设',instructions:copy?source?.instructions||'':''};item.name=speechStyleClip(item.name,160);
  presets.push(item);if(collectionBytes()>16384){presets.pop();notify('预设集合已达到 16 KB，请先精简内容。',true);return;}state.selected=item.id;renderEditor();name.focus();name.select();notify('已添加预设；选择“当前说话预设”后保存，即可使用。');
 };
 const add=button('新增预设',()=>addPreset(false)),clone=button('复制此预设',()=>addPreset(true)),remove=button('删除此预设',()=>{
  if(presets.length<=1||state.selected===configuration.activePreset||state.selected==='natural'){notify('请先切换当前预设并保存，再删除不使用的预设；自然预设作为回退保留。',true);return;}
  const index=presets.findIndex(preset=>preset.id===state.selected);if(index>=0)presets.splice(index,1);state.selected=configuration.activePreset;renderEditor();notify('预设已从待保存配置移除。');
 });
 const deleteHint=el('p','helper');deleteHint.dataset.speechStyle='delete-hint';
 const previewLabel=el('label','','本地规则预览（不会调用模型）'),preview=el('textarea');preview.rows=4;preview.readOnly=true;preview.setAttribute('aria-label','本地规则预览（不会调用模型）');styleArea(preview);previewLabel.append(preview);
 function options(select,value){select.replaceChildren();for(const preset of presets){if(!speechStyleSafeId(preset.id))continue;const option=el('option','',preset.name);option.value=preset.id;select.append(option);}select.value=value;}
 function refreshPreview(){
  const chosen=presets.find(preset=>preset.id===configuration.activePreset),editing=presets.find(preset=>preset.id===state.selected);
  description.textContent=`当前选择：${chosen?.name||'自然'}。${chosen?.instructions||'不附加额外表达偏好，沿用角色本身的表达。'}`;
  preview.value=editing?.instructions||'此预设不添加额外表达偏好。';textCount.textContent=`本预设 ${speechStyleBytes(editing?.instructions||'')} / 2048 字节。`;refreshCounts();
 }
 function editText(key,value,limit){
  const preset=presets.find(preset=>preset.id===state.selected);if(!preset)return;if(key==='instructions'&&preset.id==='natural')return;const original=preset[key],clean=value.replace(key==='name'?/[\u0000-\u001f]/g:/[\u0000-\u0008\u000b\u000c\u000e-\u001f]/g,''),clipped=speechStyleClip(clean,limit);
  if(key==='name'&&!clipped.trim()){name.value=original;notify('预设名称不能为空。',true);return;}
  preset[key]=clipped;if(collectionBytes()>16384){preset[key]=original;(key==='name'?name:instructions).value=original;notify('预设集合不能超过 16 KB，请先精简其他预设。',true);return;}
  if(value!==clipped)(key==='name'?name:instructions).value=clipped;
  if(key==='name'){options(active,configuration.activePreset);options(edit,state.selected);}
  refreshPreview();notify(value!==clipped?'内容超过字节上限，已保留可容纳的完整字符。':'修改尚未保存。',value!==clipped);
 }
 function renderEditor(){
  if(!presets.some(preset=>preset.id===state.selected))state.selected=configuration.activePreset;options(active,configuration.activePreset);options(edit,state.selected);
  const preset=presets.find(preset=>preset.id===state.selected);name.value=preset?.name||'';instructions.value=preset?.instructions||'';instructions.readOnly=state.selected==='natural';
  add.disabled=clone.disabled=presets.length>=16;remove.disabled=presets.length<=1||state.selected===configuration.activePreset||state.selected==='natural';deleteHint.textContent=state.selected==='natural'?'自然预设不附加表达规则，作为回退保留；可复制后编写自己的偏好。':state.selected===configuration.activePreset?'当前使用的预设不能直接删除，请先切换到自然或其他预设。':'删除仅修改待保存的预设列表。';refreshPreview();
 }
 panel.append(el('h4','','说话方式'),el('p','helper','只调整表达习惯，不修改人设、权限或记忆。选择和预览都在本地进行；点击“保存插件设置”后即时生效，无需重启。'),grid,description,label,textCount,actions,deleteHint,previewLabel,counts,status);card.append(panel);renderEditor();
}
