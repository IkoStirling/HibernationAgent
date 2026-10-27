import fs from 'node:fs/promises';
import path from 'node:path';
import {pathToFileURL,fileURLToPath} from 'node:url';
import {spawn} from 'node:child_process';
import assert from 'node:assert/strict';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const build=path.resolve(process.env.HA_TEST_BUILD_ROOT||path.join(root,'out/napcat-release'));
const scratch=path.join(root,'artifacts',`context-form-${Date.now()}`);await fs.mkdir(scratch,{recursive:true});
const config=JSON.parse(await fs.readFile(path.join(root,'config/example.json'),'utf8'));
Object.assign(config,{dataRoot:path.join(scratch,'data'),characterRoot:path.join(root,'character'),uiRoot:path.join(root,'ui'),pluginRoot:path.join(build,'plugins'),plugins:{},solitudeIntervalMs:604800000,idleBeforeSolitudeMs:86400000});
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
 await page.goto(origin+'/#session='+session);await page.getByText('后台已连接',{exact:true}).waitFor();await page.locator('[data-page="models"]').click();
 const preset=page.getByLabel('调用策略',{exact:true});assert.equal(await preset.inputValue(),'balanced');
 for(const choice of ['economy','full','balanced']){
  await preset.selectOption(choice);await page.getByRole('button',{name:'保存连接设置',exact:true}).click();
  await wait(async()=>(await dashboard()).settings.contextControl.preset===choice);
  await wait(async()=>await page.getByRole('button',{name:'保存连接设置',exact:true}).isEnabled());
  const effective=(await dashboard()).context_control;assert.equal(effective.preset,choice);assert.equal(effective.enabled,choice!=='full');
 }
 await page.screenshot({path:path.join(scratch,'context-settings.png'),fullPage:true});assert.equal(JSON.parse(await fs.readFile(ready,'utf8')).pid,identity.pid);assert.deepEqual(errors,[]);
 console.log('PASS context presets render, save and hot apply without restarting; no page errors');console.log('Screenshot: '+path.join(scratch,'context-settings.png'));
}finally{
 if(browser)await browser.close();if(origin)try{await fetch(origin+'/api/shutdown',{method:'POST',headers:{Authorization:'Bearer '+session,'Content-Type':'application/json'},body:'{}'});}catch{}
 await new Promise(resolve=>{if(service.exitCode!==null)return resolve();service.once('exit',resolve);setTimeout(()=>{service.kill();resolve();},10000).unref();});
}
