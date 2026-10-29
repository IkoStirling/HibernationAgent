import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import path from 'node:path';
import {spawn} from 'node:child_process';
import {fileURLToPath,pathToFileURL} from 'node:url';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..'),build=path.resolve(process.env.HA_TEST_BUILD_ROOT||path.join(root,'out/skills-release'));
const scratch=path.join(root,'artifacts',`session-projection-ui-${Date.now()}`);await fs.mkdir(scratch,{recursive:true});
const cfg=JSON.parse(await fs.readFile(path.join(root,'config/example.json'),'utf8'));Object.assign(cfg,{dataRoot:path.join(scratch,'data'),characterRoot:path.join(root,'character'),uiRoot:path.join(root,'ui'),pluginRoot:path.join(build,'plugins'),plugins:{},computer:{enabled:false},solitudeIntervalMs:604800000,idleBeforeSolitudeMs:86400000,dashboard:{sections:{plugins:['surfaces','plugins','plugin-test']}}});
const configPath=path.join(scratch,'config.json'),ready=path.join(scratch,'ready.json');await fs.writeFile(configPath,JSON.stringify(cfg));
process.env.TEMP=scratch;process.env.TMP=scratch;
const session='session-projection-ui-fixture',env={...process.env,HIBERNATION_SESSION_KEY:session};
const service=spawn(path.join(build,'hibernation-agent.exe'),['--config',configPath,'--ready-file',ready],{env,windowsHide:true,stdio:['ignore','ignore','pipe']});let origin,browser,errors=[],checks=0,serviceError='';service.stderr.on('data',d=>serviceError+=d);
const check=(condition,label)=>{assert(condition,label);checks++;console.log('PASS '+label);};
async function wait(probe,label){const end=Date.now()+20000;while(Date.now()<end){if(service.exitCode!==null)throw Error(`Backend exited ${serviceError}`);if(await probe())return;await new Promise(r=>setTimeout(r,70));}throw Error('Timeout '+label);}
async function api(route,data){const response=await fetch(origin+route,{method:data===undefined?'GET':'POST',headers:{Authorization:`Bearer ${session}`,'Content-Type':'application/json'},body:data===undefined?undefined:JSON.stringify(data)});assert(response.ok,`${route} ${response.status}`);return response.json();}

try{
 await wait(async()=>{try{origin='http://127.0.0.1:'+JSON.parse(await fs.readFile(ready,'utf8')).port;return true;}catch{return false;}},'ready');
 const {chromium}=await import(pathToFileURL('C:/Users/lenovo/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright/index.mjs'));
 browser=await chromium.launch({executablePath:'C:/Program Files/Google/Chrome/Application/chrome.exe',headless:true});const page=await browser.newPage({viewport:{width:1280,height:900}});page.on('pageerror',e=>errors.push(e.message));
 await page.goto(origin+'/#session='+session);await page.getByText('后台已连接',{exact:true}).waitFor();
 async function models(){await page.locator('[data-page="models"]').click();await page.locator('#page-models .directory-expand').click();}
 await models();const enabled=page.getByLabel('保留连续对话上下文',{exact:true});const size=page.getByLabel('连续上下文容量（KB，16 至 512）',{exact:true}),turns=page.getByLabel('每段最多对话轮数（2 至 64）',{exact:true});
 check(await enabled.isChecked()&&await size.inputValue()==='128'&&await turns.inputValue()==='16','bounded cross-turn projection defaults are visible');
 await page.getByLabel('审核流程',{exact:true}).selectOption('layered');await page.getByLabel('审核上下文',{exact:true}).selectOption('events');await page.getByLabel('审核思考模式',{exact:true}).selectOption('none');await page.getByLabel('审核首次输出上限（256 至 8192 token）',{exact:true}).fill('1536');
 await size.fill('64');await turns.fill('5');await enabled.uncheck();await page.getByRole('button',{name:'保存连接设置',exact:true}).click();
 await wait(async()=>{const v=(await api('/api/dashboard')).settings.contextControl;return v.sessionBytes===65536&&v.sessionMaxTurns===5&&v.sessionProjection===false;},'saved projection controls');check(true,'projection switch, byte capacity and turn boundary persist through the real settings API');
 const reviewSettings=(await api('/api/dashboard')).settings.contextControl;check(reviewSettings.reviewPolicy==='layered'&&reviewSettings.reviewLayout==='events'&&reviewSettings.reviewReasoningEffort==='none'&&reviewSettings.reviewInitialTokens===1536,'layered review controls persist through actual settings validation');
 await page.reload();await page.getByText('后台已连接',{exact:true}).waitFor();await models();check(!await enabled.isChecked()&&await size.inputValue()==='64'&&await turns.inputValue()==='5','saved projection configuration survives page reload');
 check(await page.getByLabel('审核流程',{exact:true}).inputValue()==='layered'&&await page.getByLabel('审核上下文',{exact:true}).inputValue()==='events','review modes survive page reload');
 await enabled.check();await page.getByRole('button',{name:'保存连接设置',exact:true}).click();await wait(async()=>(await api('/api/dashboard')).context_control.sessionProjection===true,'enabled projection');check((await api('/api/dashboard')).model_budget.used===0,'configuration changes make no paid model calls');
 await page.screenshot({path:path.join(scratch,'desktop.png'),fullPage:true});await page.setViewportSize({width:390,height:844});check(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),'projection controls fit a narrow window');await page.screenshot({path:path.join(scratch,'narrow.png'),fullPage:true});check(errors.length===0,'no browser exceptions');
 console.log(JSON.stringify({completed:true,checks,scratch}));
}finally{if(browser)await browser.close();if(origin){try{await api('/api/shutdown',{});}catch{}}await new Promise(r=>setTimeout(r,200));if(service.exitCode===null)service.kill();}
