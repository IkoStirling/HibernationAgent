import fs from 'node:fs/promises';
import path from 'node:path';
import {fileURLToPath,pathToFileURL} from 'node:url';
import {spawn} from 'node:child_process';
import assert from 'node:assert/strict';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const build=path.resolve(process.env.HA_TEST_BUILD_ROOT||path.join(root,'out/style-lora-release'));
const scratch=path.join(root,'artifacts',`style-lora-browser-${Date.now()}`);await fs.mkdir(path.join(scratch,'plugins'),{recursive:true});
await fs.cp(path.join(build,'plugins/style-lora'),path.join(scratch,'plugins/style-lora'),{recursive:true});
await fs.copyFile(path.join(root,'tests/StyleLoraServiceFixture.py'),path.join(scratch,'plugins/style-lora/worker.py'));
const cfg=JSON.parse(await fs.readFile(path.join(root,'config/example.json'),'utf8'));
cfg.dataRoot=path.join(scratch,'data');cfg.characterRoot=path.join(root,'character');cfg.uiRoot=path.join(root,'ui');cfg.pluginRoot=path.join(scratch,'plugins');
cfg.plugins={'style-lora':{enabled:true,applyEnabled:false,pythonExecutable:process.env.HA_STYLE_PYTHON||path.join(root,'out/deps/style-lora-venv/Scripts/python.exe'),adapterId:'',rank:8}};
cfg.review={enabled:false,profileId:'',maxRewrites:1};cfg.computer={enabled:false};cfg.solitudeIntervalMs=604800000;cfg.idleBeforeSolitudeMs=86400000;
const config=path.join(scratch,'config.json'),ready=path.join(scratch,'ready.json');await fs.writeFile(config,JSON.stringify(cfg));
const session='style-browser-synthetic';const service=spawn(path.join(build,'hibernation-agent.exe'),['--config',config,'--ready-file',ready],{windowsHide:true,env:{...process.env,HIBERNATION_SESSION_KEY:session},stdio:['ignore','pipe','pipe']});
let origin,browser;const errors=[],checks=[];
async function wait(probe,name){const end=Date.now()+25000;while(Date.now()<end){if(service.exitCode!==null)throw Error('host exited');try{if(await probe())return;}catch{}await new Promise(r=>setTimeout(r,80));}throw Error('timeout: '+name);}
const check=(value,name)=>{assert(value,name);checks.push(name);console.log('PASS '+name);};
async function api(route,data){const response=await fetch(origin+route,{method:data===undefined?'GET':'POST',headers:{Authorization:'Bearer '+session,'Content-Type':'application/json'},body:data===undefined?undefined:JSON.stringify(data)});assert(response.ok,route);return response.json();}
try{
 await wait(async()=>{const value=JSON.parse(await fs.readFile(ready,'utf8'));origin='http://127.0.0.1:'+value.port;return true;},'ready');
 const {chromium}=await import(pathToFileURL(process.env.PLAYWRIGHT_PACKAGE_PATH||'C:/Users/lenovo/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright/index.mjs'));
 browser=await chromium.launch({headless:true,executablePath:process.env.HIBERNATION_TEST_BROWSER||'C:/Program Files/Google/Chrome/Application/chrome.exe'});
 const page=await browser.newPage({viewport:{width:1440,height:1000},locale:'zh-CN'});page.on('pageerror',e=>errors.push(e.message));
 await page.goto(origin+'/#session='+session);await page.getByText('后台已连接',{exact:true}).waitFor();await page.locator('nav button[data-page="plugins"]').click();
 const card=page.locator('[data-plugin-id="style-lora"]');await card.waitFor({state:'attached'});
 await page.locator('#page-plugins .directory-expand').click();
 await card.getByRole('button',{name:'检查环境与训练状态',exact:true}).click();await card.getByText(/已认可样本 1 条/).waitFor();
 check(true,'real dashboard uses operator status without rebuilding away its result');
 const corpus=card.getByLabel('风格训练对照 JSONL');await corpus.fill(JSON.stringify({context:[{role:'user',text:'兼容吗？'}],original:'不清楚，不敢瞎说。',preferred:'不清楚。',reason:'删除重复保证。'}));
 await card.getByRole('button',{name:'导入已认可对照',exact:true}).click();await card.getByText(/请确认样本是你认可/).waitFor();check(true,'manual corpus consent is visible and required');
 await card.getByLabel('我认可这些表达并允许用于训练').check();await card.getByRole('button',{name:'导入已认可对照',exact:true}).click();await card.getByText(/已加密保存 1 条样本/).waitFor();check(await corpus.inputValue()==='','successful import clears private input rather than persisting it in browser storage');
 await card.getByRole('button',{name:'检查环境与训练状态',exact:true}).click();await wait(()=>card.getByRole('button',{name:'检查环境与训练状态',exact:true}).isEnabled(),'status completed');await card.getByLabel('已训练的风格版本').selectOption('style-fixture');await card.getByRole('button',{name:'选择此版本',exact:true}).click();await page.locator('#toast').filter({hasText:'已选择版本；需要单独开启'}).waitFor();
 await wait(async()=>(await api('/api/dashboard')).settings.plugins['style-lora'].adapterId==='style-fixture','version persisted');
 check(!(await api('/api/dashboard')).settings.plugins['style-lora'].applyEnabled,'choosing a version does not silently enable editing');
 const next=page.locator('[data-plugin-id="style-lora"]');if(!await next.getByLabel('风格 LoRA 预览原稿').isVisible())await page.locator('#page-plugins .directory-expand').click();
 await next.getByLabel('风格 LoRA 预览原稿').fill('我不清楚，不敢瞎说。');await next.getByRole('button',{name:'预览所选 LoRA',exact:true}).click();await next.getByText('我不清楚。',{exact:true}).waitFor();check(true,'manual preview is shown without publishing a character message');
 check(await next.getByLabel('编辑输出 token 上限',{exact:true}).isVisible(),'output limit is configurable rather than hidden by credential-field filtering');
 check((await api('/api/chat')).messages.length===0,'operator preview does not add a role conversation');
 await page.screenshot({path:path.join(scratch,'dashboard.png'),fullPage:true});check(errors.length===0,'dashboard has no script errors');
 await fs.writeFile(path.join(scratch,'acceptance.json'),JSON.stringify({completed:true,checks,api_calls:0},null,2));console.log('Evidence: '+scratch);
}finally{if(browser)await browser.close();if(origin)await api('/api/shutdown',{}).catch(()=>{});await new Promise(r=>{if(service.exitCode!==null)return r();service.once('exit',r);setTimeout(()=>{service.kill();r();},12000).unref();});}
