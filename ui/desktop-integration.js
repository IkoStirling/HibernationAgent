'use strict';
const desktopIntegrationMessages={
 napcat_installation_missing:'请选择包含 NapCatWinBootMain.exe、引导 DLL 和 napcat.mjs 的 NapCat 目录。',
 napcat_webui_token_missing:'NapCat 的网页配置尚未就绪，请检查安装目录。',
 napcat_webui_unreachable_or_auth_failed:'正在等待 NapCat 网页服务；若持续如此，请核对安装目录和网页端口。',
 napcat_webui_2fa_required:'NapCat 网页管理启用了双重验证，内嵌扫码不可用，请使用完整管理后台登录。',
 napcat_instance_not_owned:'该实例由其他程序启动，请从 NapCat 管理后台关闭后，再交给小爱启动。',
 napcat_stop_before_reconfigure:'请先关闭由小爱启动的 NapCat，再更改安装位置或端口。',
 startup_registration_failed:'无法修改当前用户的开机启动设置。',
 napcat_link_not_ready:'登录链接还没有准备好，请稍候或刷新二维码。',
 login_window_unavailable:'登录窗口尚未就绪，请确认桌面组件已安装。'
};
function desktopIntegrationIssue(code){return desktopIntegrationMessages[code]||code||'';}
function appendDesktopIntegrationControls(root){
 const card=el('section','desktop-integration'),title=el('h3','','开机启动与 QQ'),status=el('p','helper','正在读取本机启动状态……');
 card.append(title,status);status.setAttribute('aria-live','polite');
 const startup=el('input','');startup.type='checkbox';startup.setAttribute('aria-label','开机自启动小爱');
 const startupLabel=el('label','integration-toggle');startupLabel.append(startup,document.createTextNode('开机自启动小爱（当前 Windows 用户）'));card.append(startupLabel,el('p','helper','登录 Windows 后进入托盘，不弹出对话窗口或启动动画。'));
 settings.desktopIntegration=settings.desktopIntegration||{napcat:{}};
 const c=settings.desktopIntegration.napcat=settings.desktopIntegration.napcat||{};
 for(const k of ['directory','qqExecutable','webuiOrigin'])if(c[k]===undefined)c[k]='';if(c.autoStart===undefined)c.autoStart=false;
 const grid=el('div','form-grid');card.append(grid);
 for(const [key,title,placeholder] of [['directory','NapCat 安装目录','包含 napcat.mjs 与引导程序的文件夹'],['qqExecutable','QQ 程序（可留空自动查找）','QQ.exe 的完整路径'],['webuiOrigin','NapCat 网页服务地址（可留空）','默认从 NapCat 配置读取，例如 http://127.0.0.1:6099']]){
  const input=el('input','');input.value=c[key];input.maxLength=2048;input.placeholder=placeholder;input.setAttribute('aria-label',title);input.oninput=()=>c[key]=input.value.trim();const l=el('label','',title);l.append(input);grid.append(l);
 }
 const together=el('input','');together.type='checkbox';together.checked=c.autoStart;together.setAttribute('aria-label','随小爱启动 NapCat');together.onchange=()=>c.autoStart=together.checked;
 const togetherLabel=el('label','integration-toggle');togetherLabel.append(together,document.createTextNode('随小爱一同启动 NapCat'));card.append(togetherLabel,el('p','helper','目录和联动启动选项使用下方“保存界面偏好”保存。NapCat 后台不显示控制台；关闭小爱时会关闭它托管的实例。'));
 const row=el('div','test-row'),buttons={};let latest=current?.desktop_integration||null,working=false,notice='',noticeUntil=0;
 async function action(value){const r=await api('/api/desktop/integration/action',value);if(!r.ok)throw Error(desktopIntegrationIssue(r.error));return r;}
 function button(name,title,handler){const b=el('button','quiet',title);b.type='button';b.onclick=async()=>{if(working)return;notice='';noticeUntil=0;working=true;b.disabled=true;try{await handler();const value=await api('/api/desktop/integration');update(value);}catch(e){notice=desktopIntegrationIssue(e.message);noticeUntil=Date.now()+12000;status.textContent=notice;}finally{working=false;if(latest)update(latest);}};row.append(b);buttons[name]=b;}
 function saved(){const n=latest?.configuration?.napcat||{};if(['directory','qqExecutable','webuiOrigin','autoStart'].some(k=>c[k]!==n[k]))throw Error('请先保存界面偏好，再启动 NapCat。');}
 button('start','启动 NapCat',()=>{saved();return action({action:'start'});});
 button('stop','关闭 NapCat',()=>action({action:'stop'}));
 button('qr','显示 QQ 登录二维码',()=>api('/api/desktop/integration/show-login',{}));
 button('admin','打开 NapCat 管理后台',()=>action({action:'open_admin'}));
 button('refresh','刷新状态',async()=>{update(await api('/api/desktop/integration'));});
 card.append(row);
 const components=el('div','helper');card.append(components);let componentSignature='';
 function componentView(v){
  const i=v.installation||{},signature=JSON.stringify(i);if(signature===componentSignature)return;componentSignature=signature;components.replaceChildren();
  components.append(el('h3','','可选组件'));
  if(!i.available){components.append(el('p','helper',i.reason==='component_catalog_invalid'?'组件来源配置无效，安装功能已停用；已有本机安装不受影响。':'尚未配置可下载的组件；仍可使用已有本机安装。'));return;}
  if(!(i.components||[]).length)components.append(el('p','helper','安装服务已接入，尚未配置授权的组件发布来源。'));
  for(const item of i.components||[]){const line=el('div','test-row');line.append(el('span','',item.name));for(const [operation,title] of [['install','安装'],['update','更新'],['repair','修复']]){const b=el('button','quiet',title);b.type='button';b.disabled=!!i.busy;b.onclick=async()=>{b.disabled=true;try{await action({action:'component_install',component_id:item.id,operation});update(await api('/api/desktop/integration'));}catch(e){status.textContent=desktopIntegrationIssue(e.message);b.disabled=false;}};line.append(b);}components.append(line);}
  const job=i.job||{};if(job.id)components.append(el('p','helper',(job.completed?(job.result?.ok?'安装完成':'安装未完成：'+(job.result?.error||'')):'正在处理：'+(job.phase||''))+(job.total?' · '+Math.round(100*(job.bytes||0)/job.total)+'%':'')));
 }
 function update(v){
  if(!v)return;latest=v;startup.checked=!!v.startup_enabled;startup.disabled=working;
  componentView(v);
  const labels={stopped:'NapCat 未运行',starting:'正在启动 NapCat',preparing_login:'正在准备登录二维码',login_required:'等待 QQ 登录',online:'QQ 已登录',error:'NapCat 暂未就绪'};
  status.textContent=notice&&Date.now()<noticeUntil?notice:(labels[v.phase]||'正在检查 NapCat')+(v.managed?' · 由小爱托管':'')+(v.issue?'。'+desktopIntegrationIssue(v.issue):'');
  buttons.start.disabled=working||!!v.running;buttons.stop.disabled=working||!v.managed;buttons.qr.disabled=working;buttons.admin.disabled=working||!v.running;
 }
 startup.onchange=async()=>{const enabled=startup.checked;startup.disabled=true;try{await action({action:'startup',enabled});update(await api('/api/desktop/integration'));}catch(e){startup.checked=!enabled;notice=desktopIntegrationIssue(e.message);noticeUntil=Date.now()+12000;status.textContent=notice;}finally{startup.disabled=false;}};
 card._integrationUpdate=update;root.append(card);if(latest)update(latest);
}
function updateDesktopIntegration(value){for(const node of document.querySelectorAll('.desktop-integration'))node._integrationUpdate?.(value);}
