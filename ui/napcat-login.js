'use strict';
const fragment=new URLSearchParams(location.hash.slice(1));
const session=fragment.get('session')||sessionStorage.getItem('hibernation.napcat.session')||'';
if(fragment.has('session')){sessionStorage.setItem('hibernation.napcat.session',session);history.replaceState(null,'',location.pathname);}
const byId=id=>document.getElementById(id);let lastQr='',busy=false,last=null;
const errors={napcat_installation_missing:'请先在 dashboard 设置 NapCat 安装目录。',napcat_webui_token_missing:'NapCat 网页配置还没有准备好。',napcat_webui_unreachable_or_auth_failed:'正在等待 NapCat 网页服务；若持续如此，请在 dashboard 检查配置。',napcat_webui_2fa_required:'内嵌扫码不支持 NapCat 网页双重验证，请使用完整管理后台登录。',napcat_instance_not_owned:'这个实例由其他程序启动，请在 NapCat 管理后台关闭。'};
async function request(path,value){const r=await fetch(path,{method:value===undefined?'GET':'POST',headers:{Authorization:'Bearer '+session,'Content-Type':'application/json'},body:value===undefined?undefined:JSON.stringify(value),cache:'no-store'});const v=await r.json();if(!r.ok)throw Error(errors[v.error]||v.error||'暂时无法读取登录状态。');return v;}
function drawQr(text){
 const canvas=byId('qr-code');
 if(text===lastQr)return;
 lastQr=text;if(!text){canvas.hidden=true;return;}
 const qr=qrcodegen.QrCode.encodeText(text,qrcodegen.QrCode.Ecc.MEDIUM),border=4,scale=Math.max(2,Math.floor(300/(qr.size+border*2)));
 canvas.width=canvas.height=(qr.size+border*2)*scale;const context=canvas.getContext('2d');context.fillStyle='#fff';context.fillRect(0,0,canvas.width,canvas.height);context.fillStyle='#111';
 for(let y=0;y<qr.size;y++)for(let x=0;x<qr.size;x++)if(qr.getModule(x,y))context.fillRect((x+border)*scale,(y+border)*scale,scale,scale);
 canvas.hidden=false;
}
async function update(){
 if(busy||!session||document.visibilityState==='hidden')return;
 busy=true;
 try{
  const v=await request('/api/desktop/integration/qr');last=v;
  const labels={stopped:'NapCat 未运行',starting:'正在启动 NapCat……',preparing_login:'正在准备二维码……',login_required:'请用手机 QQ 扫码登录',online:'QQ 已登录，可以关闭这个窗口。',error:'登录服务暂未就绪'};
  byId('login-status').textContent=labels[v.phase]||'正在准备登录……';drawQr(v.qr_url||'');
  byId('qr-placeholder').hidden=!!v.qr_url;byId('qr-placeholder').textContent=v.phase==='online'?'已登录':v.running?'正在准备二维码……':'请启动 NapCat';
  byId('login-error').textContent=errors[v.issue]||'';
  byId('start').disabled=!!v.running;byId('stop').disabled=!v.managed;byId('open-link').disabled=!v.qr_url;byId('refresh').disabled=!v.running;
 }catch(e){byId('login-error').textContent=e.message;}finally{busy=false;}
}
for(const [id,action] of [['start','start'],['stop','stop'],['refresh','refresh_qr'],['open-link','open_login_link']])byId(id).onclick=async()=>{try{await request('/api/desktop/integration/action',{action});await update();}catch(e){byId('login-error').textContent=e.message;}};
document.addEventListener('visibilitychange',()=>{if(document.visibilityState!=='hidden')update();});
if(session){update();setInterval(update,2000);}else byId('login-error').textContent='请从小爱的托盘或 dashboard 重新打开登录窗口。';
