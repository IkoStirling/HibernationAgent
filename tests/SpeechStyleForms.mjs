import fs from 'node:fs/promises';
import path from 'node:path';
import http from 'node:http';
import assert from 'node:assert/strict';
import {fileURLToPath,pathToFileURL} from 'node:url';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const scratch=path.join(root,'artifacts',`speech-style-ui-${Date.now()}`);await fs.mkdir(scratch,{recursive:true});
const settings=JSON.parse(await fs.readFile(path.join(root,'config/example.json'),'utf8'));settings.profiles=[{id:'mock',label:'离线测试',protocol:'mock',enabled:true}];settings.plugins={'speech-style':{enabled:true,activePreset:'natural',allowModelSwitch:false}};
const manifest=JSON.parse(await fs.readFile(path.join(root,'plugins/speech-style/plugin.json'),'utf8'));
const dashboard={settings,plugins:[manifest],health:'ok',offline:false,paused:false,busy:false,selected_profile:'mock',data_root:'E:/fixture/data',memory_encrypted:true,memory_workshop:{jobs:[]}};
let saved=null,modelCalls=0,settingsWrites=0;const requests=[];
const server=http.createServer(async(req,res)=>{
 try{const url=new URL(req.url,'http://127.0.0.1');if(url.pathname.startsWith('/api/')){
   let body='';for await(const chunk of req)body+=chunk;const data=body?JSON.parse(body):null;requests.push(url.pathname);let result={};
   if(url.pathname==='/api/dashboard')result=dashboard;
   else if(url.pathname==='/api/appearance')result={presets:[]};
   else if(url.pathname==='/api/skills')result={packages:[],available_tools:[]};
   else if(url.pathname==='/api/vault-portability/status')result={state:'idle'};
   else if(url.pathname==='/api/settings'){
    const config=data.settings.plugins['speech-style'];assert(config.presets.length<=16);assert(Buffer.byteLength(JSON.stringify(config))<=16384);assert(config.presets.some(preset=>preset.id===config.activePreset));
    for(const preset of config.presets){assert(Buffer.byteLength(preset.name)<=160);assert(Buffer.byteLength(preset.instructions)<=2048);if(preset.id==='natural')assert.equal(preset.instructions,'');}
    saved=structuredClone(data.settings);dashboard.settings=structuredClone(saved);settingsWrites++;
   }else{modelCalls++;throw Error('Unexpected operation: '+url.pathname);}
   res.writeHead(200,{'Content-Type':'application/json'});res.end(JSON.stringify(result));return;
  }
  const target=url.pathname==='/'?path.join(root,'ui/index.html'):path.join(root,url.pathname.slice(1));if(!target.startsWith(path.join(root,'ui')))throw Error('path rejected');res.writeHead(200,{'Content-Type':({'.js':'text/javascript','.css':'text/css','.svg':'image/svg+xml','.html':'text/html'})[path.extname(target)]||'application/octet-stream'});res.end(await fs.readFile(target));
 }catch{res.writeHead(500,{'Content-Type':'application/json'});res.end('{"error":"fixture_failed"}');}
});await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));const origin=`http://127.0.0.1:${server.address().port}`;
let browser;const checks=[],errors=[];const check=(condition,name)=>{assert(condition,name);checks.push(name);console.log('PASS '+name);};
try{
 const {chromium}=await import(pathToFileURL(process.env.PLAYWRIGHT_PACKAGE_PATH||'C:/Users/lenovo/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright/index.mjs'));
 browser=await chromium.launch({headless:true,executablePath:process.env.HIBERNATION_TEST_BROWSER||'C:/Program Files/Google/Chrome/Application/chrome.exe'});const page=await browser.newPage({viewport:{width:1440,height:1050},locale:'zh-CN'});page.on('pageerror',error=>errors.push(error.message));
 await page.goto(origin+'/#session=speech-style-synthetic');await page.getByText('后台已连接',{exact:true}).waitFor();await page.locator('nav [data-page="plugins"]').click();await page.evaluate(()=>focusDashboardPanel('plugin-speech-style',document.getElementById('page-plugins')));
 const card=page.locator('[data-plugin-id="speech-style"]'),active=card.getByLabel('当前说话预设',{exact:true}),editor=card.getByLabel('编辑预设',{exact:true}),name=card.getByLabel('预设名称',{exact:true}),instructions=card.getByLabel('表达偏好（最多 2048 字节）',{exact:true}),preview=card.getByLabel('本地规则预览（不会调用模型）',{exact:true});
 check(await active.locator('option').count()===4,'default presets appear in actual plugin card');check(await instructions.evaluate(node=>node.readOnly),'natural fallback does not accept custom instructions');check(!await card.getByLabel('允许小爱切换已保存的预设').isChecked(),'model switching starts disabled');check(await preview.evaluate(node=>node.readOnly),'rule preview is local and read-only');
 await active.selectOption('hesitant');check((await card.locator('[data-speech-style="description"]').textContent()).includes('不固定放在句首'),'selected preset explains occasional rather than mandatory hesitation');check(settingsWrites===0&&modelCalls===0,'choosing preset does not call model or auto-save');
 await editor.selectOption('concise');await name.fill('<img src=x onerror=alert(1)>');await instructions.fill('自然短句。\n保留必要事实。');check(await preview.inputValue()==='自然短句。\n保留必要事实。','multiline edited rule is shown in local preview');check(await card.locator('img').count()===0,'preset name is rendered as text');
 const focused=await instructions.elementHandle();await page.evaluate(()=>load());check(await focused.evaluate(node=>node.isConnected&&document.activeElement===node&&node.value==='自然短句。\n保留必要事实。'),'dashboard polling keeps unsaved text and focused editor node');
 await instructions.fill('汉'.repeat(700));check(Buffer.byteLength(await instructions.inputValue())<=2048&&!(await instructions.inputValue()).includes('\ufffd'),'UTF-8 instruction cap preserves complete characters');
 await name.fill('爱'.repeat(80));check(Buffer.byteLength(await name.inputValue())<=160,'name cap is enforced in UTF-8 bytes');
 await card.getByRole('button',{name:'复制此预设',exact:true}).click();const clonedId=await editor.inputValue();check(/^custom-[a-z0-9]+$/.test(clonedId)&&clonedId.length<=48,'cloning creates unique safe value IDs');check(await instructions.inputValue()==='汉'.repeat(682),'cloning retains edited source instructions');
 await name.fill('我的轻声');await instructions.fill('嗯，只在确实需要时稍作停顿。');await active.selectOption(clonedId);check(await card.getByRole('button',{name:'删除此预设',exact:true}).isDisabled(),'active preset cannot be deleted');await card.getByLabel('允许小爱切换已保存的预设').check();
 await card.getByRole('button',{name:'新增预设',exact:true}).click();const disposable=await editor.inputValue();await name.fill('待删除');await instructions.fill('临时规则');await card.getByRole('button',{name:'删除此预设',exact:true}).click();check(!await editor.locator(`option[value="${disposable}"]`).count(),'inactive custom preset can be deleted');
 while(await editor.locator('option').count()<16){await card.getByRole('button',{name:'新增预设',exact:true}).click();await instructions.fill('x'.repeat(1500));}
 check(await card.getByText('预设集合不能超过 16 KB，请先精简其他预设。',{exact:true}).count()===1,'overfull catalog edit is rejected with a readable explanation');
 check(await card.getByRole('button',{name:'新增预设',exact:true}).isDisabled()&&await card.getByRole('button',{name:'复制此预设',exact:true}).isDisabled(),'sixteen-preset limit blocks add and clone');
 const size=await page.evaluate(()=>speechStyleBytes(JSON.stringify(settings.plugins['speech-style'])));check(size<=16384,'all edited presets stay within full configuration byte cap');
 await page.locator('#page-plugins .save-settings').click();await page.getByText('设置已保存。',{exact:true}).waitFor();check(saved.plugins['speech-style'].activePreset===clonedId&&saved.plugins['speech-style'].allowModelSwitch===true,'existing settings save persists preset selection and switch permission');check(saved.plugins['speech-style'].presets.length===16,'custom catalog survives save round trip');
 await page.evaluate(()=>focusDashboardPanel('plugin-speech-style',document.getElementById('page-plugins')));check(await active.inputValue()===clonedId,'saved active preset is restored without restart');
 // IDs are scalar values and are never used as object property names.
 await page.evaluate(()=>{const config=settings.plugins['speech-style'];config.presets=[{id:'natural',name:'自然',instructions:''},{id:'constructor',name:'安全值',instructions:'普通偏好'}];config.activePreset='constructor';renderPlugins();focusDashboardPanel('plugin-speech-style',document.getElementById('page-plugins'));});check(await active.inputValue()==='constructor'&&await page.evaluate(()=>Object.getPrototypeOf({})===Object.prototype),'valid scalar ID constructor does not alter prototypes');
 await card.screenshot({path:path.join(scratch,'desktop.png')});await page.setViewportSize({width:390,height:844});check(await page.evaluate(()=>document.documentElement.scrollWidth)<=390,'editor fits narrow plugin layout');await card.screenshot({path:path.join(scratch,'narrow.png')});check(errors.length===0,`no browser script errors: ${errors.join('; ')}`);check(modelCalls===0,'all edits and previews use zero model or tool calls');
 await fs.writeFile(path.join(scratch,'acceptance.json'),JSON.stringify({completed:true,checks,paid_model_calls:0},null,2));console.log('Evidence: '+scratch);
}finally{if(browser)await browser.close();await new Promise(resolve=>server.close(resolve));}
