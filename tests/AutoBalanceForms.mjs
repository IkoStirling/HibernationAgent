import fs from 'node:fs/promises';
import http from 'node:http';
let funded=false,modelCalls=0;const fake=http.createServer((req,res)=>{if(req.url!=='/user/balance'){modelCalls++;res.writeHead(402);res.end('{}');return;}if(req.headers.authorization!=='Bearer balance-ui-fixture'){res.writeHead(401);res.end('{}');return;}res.setHeader('Content-Type','application/json');res.end(JSON.stringify({is_available:funded,balance_infos:[{currency:'CNY',total_balance:funded?'0.01':'0.00'}]}));});await new Promise(resolve=>fake.listen(0,'127.0.0.1',resolve));
import path from 'node:path';
import {pathToFileURL,fileURLToPath} from 'node:url';
import {spawn} from 'node:child_process';
import assert from 'node:assert/strict';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const build=path.resolve(process.env.HA_TEST_BUILD_ROOT||path.join(root,'out/napcat-release'));
const scratch=path.join(root,'artifacts',`auto-balance-form-${Date.now()}`);await fs.mkdir(scratch,{recursive:true});
const config=JSON.parse(await fs.readFile(path.join(root,'config/example.json'),'utf8'));
Object.assign(config,{dataRoot:path.join(scratch,'data'),characterRoot:path.join(root,'character'),uiRoot:path.join(root,'ui'),pluginRoot:path.join(build,'plugins'),plugins:{},solitudeIntervalMs:604800000,idleBeforeSolitudeMs:86400000});
config.profiles=[config.profiles[0],{id:"actor",label:"Balance UI fixture",enabled:true,protocol:"chat-completions",origin:"http://127.0.0.1:"+fake.address().port,basePath:"/v1",model:"balance-fixture",credentialEnvironment:"HA_AUTO_BALANCE_UI_KEY"}];config.plugins={};
const ready=path.join(scratch,'ready.json'),file=path.join(scratch,'config.json');await fs.writeFile(file,JSON.stringify(config));
const session='context-ui-local-fixture';const service=spawn(path.join(build,'hibernation-agent.exe'),['--config',file,'--ready-file',ready],{windowsHide:true,env:{...process.env,HIBERNATION_SESSION_KEY:session,HA_AUTO_BALANCE_UI_KEY:"balance-ui-fixture"},stdio:'ignore'});
let browser,origin;const errors=[];
async function wait(probe){const end=Date.now()+20000;while(Date.now()<end){if(await probe())return;await new Promise(r=>setTimeout(r,50));}throw Error('context form timeout');}
async function dashboard(){return (await fetch(origin+'/api/dashboard',{headers:{Authorization:'Bearer '+session}})).json();}
try{
 await wait(async()=>{try{await fs.access(ready);return true;}catch{return false;}});const identity=JSON.parse(await fs.readFile(ready,'utf8'));origin='http://127.0.0.1:'+identity.port;
 const pkg=process.env.PLAYWRIGHT_PACKAGE_PATH||'C:/Users/lenovo/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright/index.mjs';
 const {chromium}=await import(pathToFileURL(pkg));browser=await chromium.launch({executablePath:process.env.HIBERNATION_TEST_BROWSER||'C:/Program Files/Google/Chrome/Application/chrome.exe',headless:true});
 const page=await browser.newPage({viewport:{width:1440,height:1000},locale:'zh-CN'});page.on('pageerror',e=>errors.push(e.message));
 await page.goto(origin+'/#session='+session);await page.getByText('后台已连接',{exact:true}).waitFor();
 await fetch(origin+'/api/model',{method:'POST',headers:{Authorization:'Bearer '+session,'Content-Type':'application/json'},body:JSON.stringify({id:'actor'})});
 await wait(async()=>(await dashboard()).balance_control.balance_offline);await page.getByRole('button',{name:'保持离线（手动）',exact:true}).waitFor();assert((await page.locator('#balance-state').innerText()).includes('余额不足'));
 await page.getByRole('button',{name:'保持离线（手动）',exact:true}).click();await wait(async()=>(await dashboard()).balance_control.manual_offline);
 funded=true;await wait(async()=>Date.now()-(await dashboard()).balance_control.last_attempt_ms>=5100);
 await page.getByRole('button',{name:'立即检查余额',exact:true}).click();await wait(async()=>!(await dashboard()).balance_control.balance_offline);
 assert.equal((await dashboard()).offline,true);await page.getByRole('button',{name:'取消手动离线（QQ 和桌面）',exact:true}).click();await wait(async()=>(await dashboard()).offline===false);
 assert.equal(modelCalls,0);assert.deepEqual(errors,[]);assert.equal((await dashboard()).model_budget.used,0);await page.screenshot({path:path.join(scratch,'auto-balance-dashboard.png'),fullPage:true});
 console.log('PASS automatic balance UI, separate manual latch, manual refresh, automatic recovery; zero model calls');console.log('Screenshot: '+path.join(scratch,'auto-balance-dashboard.png'));
}finally{
 if(browser)await browser.close();if(origin)try{await fetch(origin+'/api/shutdown',{method:'POST',headers:{Authorization:'Bearer '+session,'Content-Type':'application/json'},body:'{}'});}catch{}
 await new Promise(resolve=>{if(service.exitCode!==null)return resolve();service.once('exit',resolve);setTimeout(()=>{service.kill();resolve();},10000).unref();});await new Promise(resolve=>fake.close(resolve));
}
