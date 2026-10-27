// Owner-only settings. Skill text is always rendered as text, never as HTML.
let skillRenderGeneration=0;
async function renderSkills(){
 const generation=++skillRenderGeneration;
 const root=document.getElementById('skill-settings');if(!root)return;
 const s=settings.skills||(settings.skills={enabled:true,useBuiltins:true,grants:[]});s.grants=s.grants||[];root.replaceChildren();
 root.append(el('h3','','受控 Skill'),el('p','helper','Skill 提供操作步骤。应用决定可用工具、文件范围、预算及审核；本版不执行 Skill 自带脚本。'));
 toggle(root,'启用 Skill',s.enabled!==false,v=>s.enabled=v);
 toggle(root,'使用内置的固定内容与权限',s.useBuiltins!==false,v=>s.useBuiltins=v);
 field(root,'Skill 目录（空白使用程序自带目录）',s.root||'',v=>s.root=v);
 const refresh=el('button','quiet','重新扫描并刷新');refresh.onclick=async()=>{if(await save())await renderSkills();};root.append(refresh);
 let data;try{data=await api('/api/skills');}catch(e){root.append(el('p','helper',e.message));return;}
 if(generation!==skillRenderGeneration)return;
 if(!data.packages.length)root.append(el('p','helper','目录内每个子文件夹放一个 SKILL.md。新增或修改后重新扫描，选择工具并授权；文件变更需要重新授权。'));
 for(const p of data.packages){
  const card=el('article','card'),head=el('div','card-head');head.append(el('h4','',p.name||p.id),el('span','pill',p.approved?'已授权':p.issue==='content_changed'?'内容已变化，待重新授权':'未授权'));card.append(head,el('p','helper',p.description||p.id));
  if(!p.digest){card.append(el('p','helper error','目录、文件或容量检查未通过；链接目录和超限文件不能使用。'));root.append(card);continue;}
  const details=el('details'),summary=el('summary','','可使用的工具（选择后保存）');details.append(summary);const boxes=[];
  for(const t of data.available_tools.filter(t=>t.skill_grantable)){const label=el('label'),box=document.createElement('input');box.type='checkbox';box.checked=(p.granted_tools||[]).includes(t.name);box.dataset.tool=t.name;label.append(box,document.createTextNode(' '+t.name+' · '+t.description.slice(0,65)));details.append(label);boxes.push(box);}
  card.append(details,el('p','helper','内容指纹：'+p.digest.slice(0,16)+'… · 权限与当前启用工具取交集。'));
  const setGrant=async enabled=>{s.grants=s.grants.filter(g=>g.id!==p.id);s.grants.push({id:p.id,digest:p.digest,tools:enabled?boxes.filter(b=>b.checked).map(b=>b.dataset.tool):[],enabled});await save();};
  const approve=el('button','quiet','授权选中的工具并保存');approve.onclick=()=>setGrant(true);const revoke=el('button','text-button','撤销授权');revoke.onclick=()=>setGrant(false);card.append(approve,revoke);root.append(card);
 }
}
