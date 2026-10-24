import fs from 'node:fs/promises';
import path from 'node:path';
import {pathToFileURL,fileURLToPath} from 'node:url';
import {spawn} from 'node:child_process';
import assert from 'node:assert/strict';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const scratch=path.join(root,'artifacts',`browser-${Date.now()}`);await fs.mkdir(scratch,{recursive:true});
const config=JSON.parse(await fs.readFile(path.join(root,'config/example.json'),'utf8'));
config.dataRoot=path.join(scratch,'data');config.characterRoot=path.join(root,'character');config.uiRoot=path.join(root,'ui');config.idleBeforeSolitudeMs=0;
const configPath=path.join(scratch,'config.json'),readyPath=path.join(scratch,'ready.json');await fs.writeFile(configPath,JSON.stringify(config));
const session='browser-integration-fixture-only';const env={...process.env,HIBERNATION_SESSION_KEY:session,TEMP:path.join(root,'out/build/tmp'),TMP:path.join(root,'out/build/tmp')};
const service=spawn(path.join(root,'out/build/hibernation-agent.exe'),['--config',configPath,'--ready-file',readyPath],{env,windowsHide:true,stdio:['ignore','pipe','pipe']});
let serviceError='';service.stderr.on('data',d=>serviceError+=d);let browser,origin,checks=0;const errors=[];
async function waitFor(probe,label){const end=Date.now()+20000;while(Date.now()<end){if(service.exitCode!==null)throw new Error(`Backend exited: ${serviceError}`);try{const value=await probe();if(value)return value;}catch{}await new Promise(r=>setTimeout(r,100));}throw new Error(`Timeout: ${label}`);}
async function api(route,data){const res=await fetch(origin+route,{method:data===undefined?'GET':'POST',headers:{Authorization:`Bearer ${session}`,'Content-Type':'application/json'},body:data===undefined?undefined:JSON.stringify(data)});assert(res.ok,`API ${route}: ${res.status}`);return res.json();}
try{
 const ready=await waitFor(async()=>JSON.parse(await fs.readFile(readyPath,'utf8')),'ready');origin=`http://127.0.0.1:${ready.port}`;
 const pkg=process.env.PLAYWRIGHT_PACKAGE_PATH||'C:/Users/lenovo/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright/index.mjs';
 const {chromium}=await import(pathToFileURL(pkg));
 browser=await chromium.launch({executablePath:process.env.HIBERNATION_TEST_BROWSER||'C:/Program Files/Google/Chrome/Application/chrome.exe',headless:true,env});
 const page=await browser.newPage({viewport:{width:1440,height:1000},locale:'zh-CN'});page.on('pageerror',e=>errors.push(e.message));page.on('console',m=>{if(m.type()==='error')errors.push(m.text());});
 await page.goto(`${origin}/#session=${session}`);await page.getByText('后台已连接',{exact:true}).waitFor();assert(!page.url().includes('session='));checks++;
 await page.screenshot({path:path.join(scratch,'01-welcome.png'),fullPage:true});
 await page.locator('#message').fill('一颗恒星如何理解等待？');await page.locator('#message').press('Enter');await page.locator('.message.assistant').waitFor();checks++;
 await waitFor(async()=>!(await api('/api/state')).busy,'first turn');await page.locator('#solitude').click();
 await waitFor(async()=>{const m=await api('/api/memories');return m.memories.some(x=>x.origin==='solitude');},'independent work memory');
 await waitFor(async()=>!(await api('/api/state')).busy,'solitude complete');
 await page.waitForFunction(()=>!document.getElementById('send').disabled);
 await page.locator('#message').fill('接着刚才的思路。');await page.locator('#message').press('Enter');await page.locator('.message.assistant').nth(1).waitFor();assert((await page.locator('.message.assistant').nth(1).innerText()).includes('上次独处时'));checks++;
 await page.screenshot({path:path.join(scratch,'02-conversation.png'),fullPage:true});
 await page.locator('[data-page="memories"]').click();await page.locator('.card').first().waitFor();const count=await page.locator('#memory-list .card').count();assert(count>=3);checks++;
 await page.locator('#memory-list .toggle').first().click();await page.getByRole('button',{name:'恢复访问',exact:true}).first().waitFor();checks++;
 await page.screenshot({path:path.join(scratch,'03-memories.png'),fullPage:true});
 await page.locator('[data-page="activity"]').click();await page.locator('#event-filter').selectOption('agent.solitude');await page.getByText('一个独处时段结束',{exact:true}).waitFor();checks++;
 assert((await page.locator('.timeline-row strong').allTextContents()).every(text=>text==='一个独处时段结束'));checks++;
 await page.screenshot({path:path.join(scratch,'04-events.png'),fullPage:true});
 await page.locator('[data-page="workspace"]').click();await page.getByRole('button',{name:'reflections',exact:true}).click();await page.locator('#file-list button').filter({hasText:/\.md$/}).first().click();await page.waitForFunction(()=>document.getElementById('file-content').textContent.includes('独处草稿'));checks++;
 await page.screenshot({path:path.join(scratch,'05-workspace.png'),fullPage:true});
 await page.locator('[data-page="chat"]').click();await page.setViewportSize({width:390,height:844});assert(await page.locator('#composer').isVisible());assert((await page.evaluate(()=>document.documentElement.scrollWidth))<=390);checks++;
 await page.screenshot({path:path.join(scratch,'06-mobile.png'),fullPage:true});
 assert.deepEqual(errors,[]);checks++;
 const report={completed:true,checks,errors,scratch,engine:'existing Chrome via isolated Playwright headless context',timestamp:new Date().toISOString()};await fs.writeFile(path.join(scratch,'acceptance.json'),JSON.stringify(report,null,2));console.log(JSON.stringify(report));
}finally{if(browser)await browser.close();if(origin){try{await api('/api/shutdown',{});}catch{}}await new Promise(resolve=>{if(service.exitCode!==null)return resolve();service.once('exit',resolve);setTimeout(()=>{service.kill();resolve();},10000).unref();});}
