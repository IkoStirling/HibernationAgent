import fs from 'node:fs/promises';
import path from 'node:path';
import {pathToFileURL,fileURLToPath} from 'node:url';
import {spawn} from 'node:child_process';
import assert from 'node:assert/strict';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const build=path.resolve(process.env.HA_TEST_BUILD_ROOT||path.join(root,'out/napcat-release'));
const scratch=path.join(root,'artifacts',`offline-form-${Date.now()}`);await fs.mkdir(scratch,{recursive:true});
const config=JSON.parse(await fs.readFile(path.join(root,'config/example.json'),'utf8'));
Object.assign(config,{dataRoot:path.join(scratch,'data'),characterRoot:path.join(root,'character'),uiRoot:path.join(root,'ui'),pluginRoot:path.join(build,'plugins'),plugins:{},solitudeIntervalMs:604800000,idleBeforeSolitudeMs:86400000});
config.profiles=[config.profiles[0]];config.plugins={};
const ready=path.join(scratch,'ready.json'),file=path.join(scratch,'config.json');await fs.writeFile(file,JSON.stringify(config));
const session='context-ui-local-fixture';const service=spawn(path.join(build,'hibernation-agent.exe'),['--config',file,'--ready-file',ready],{windowsHide:true,env:{...process.env,HIBERNATION_SESSION_KEY:session},stdio:'ignore'});
let browser,origin;const errors=[];
async function wait(probe){const end=Date.now()+20000;while(Date.now()<end){if(await probe())return;await new Promise(r=>setTimeout(r,50));}throw Error('context form timeout');}
async function dashboard(){return (await fetch(origin+'/api/dashboard',{headers:{Authorization:'Bearer '+session}})).json();}
try{
 await wait(async()=>{try{await fs.access(ready);return true;}catch{return false;}});const identity=JSON.parse(await fs.readFile(ready,'utf8'));origin='http://127.0.0.1:'+identity.port;
 const pkg=process.env.PLAYWRIGHT_PACKAGE_PATH||'C:/Users/lenovo/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright/index.mjs';
 const {chromium}=await import(pathToFileURL(pkg));browser=await chromium.launch({executablePath:process.env.HIBERNATION_TEST_BROWSER||'C:/Program Files/Google/Chrome/Application/chrome.exe',headless:true});
 const page=await browser.newPage({viewport:{width:1440,height:1000},locale:'zh-CN'});page.on('pageerror',e=>errors.push(e.message));
 await page.goto(origin+'/#session='+session);await page.getByText('后台已连接',{exact:true}).waitFor();
 await page.getByRole('button',{name:'进入离线（QQ 和桌面）',exact:true}).click();await wait(async()=>(await dashboard()).offline===true);
 await page.getByText('已离线',{exact:true}).waitFor();await page.screenshot({path:path.join(scratch,'offline-dashboard.png'),fullPage:true});
 await page.getByRole('button',{name:'暂停后台活动',exact:true}).click();await wait(async()=>(await dashboard()).paused===true);
 await page.getByRole('button',{name:'取消手动离线（QQ 和桌面）',exact:true}).click();await wait(async()=>(await dashboard()).offline===false);
 assert.equal((await dashboard()).paused,true);await page.getByText('暂时休息',{exact:true}).waitFor();
 await page.getByRole('button',{name:'恢复后台活动',exact:true}).click();await wait(async()=>(await dashboard()).paused===false);
 assert.equal((await dashboard()).model_budget.used,0);assert.deepEqual(errors,[]);
 console.log('PASS dashboard offline/online controls, status, independent pause, zero model calls');console.log('Screenshot: '+path.join(scratch,'offline-dashboard.png'));
}finally{
 if(browser)await browser.close();if(origin)try{await fetch(origin+'/api/shutdown',{method:'POST',headers:{Authorization:'Bearer '+session,'Content-Type':'application/json'},body:'{}'});}catch{}
 await new Promise(resolve=>{if(service.exitCode!==null)return resolve();service.once('exit',resolve);setTimeout(()=>{service.kill();resolve();},10000).unref();});
}
