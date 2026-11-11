import fs from 'node:fs/promises';
import path from 'node:path';
import {fileURLToPath,pathToFileURL} from 'node:url';
import {spawn} from 'node:child_process';
import {createHash,randomUUID} from 'node:crypto';
import assert from 'node:assert/strict';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const build=path.resolve(process.env.HA_TEST_BUILD_ROOT||path.join(root,'out/build'));
const pluginBuild=path.resolve(process.env.HA_WORKSHOP_PLUGIN_BUILD||build);
const scratch=path.join(process.env.HA_WORKSHOP_SCRATCH||path.join(root,'artifacts'), 'expression-browser-'+Date.now());
await fs.mkdir(path.join(scratch,'plugins'),{recursive:true});
await fs.cp(path.join(pluginBuild,'plugins/style-lora'),path.join(scratch,'plugins/style-lora'),{recursive:true});
const canonical=v=>JSON.stringify(v&&typeof v==='object'?Array.isArray(v)?v.map(x=>JSON.parse(canonical(x))):Object.fromEntries(Object.keys(v).sort().map(k=>[k,JSON.parse(canonical(v[k]))])):v);
const digest=v=>createHash('sha256').update(canonical(v)).digest('hex');
function fixture(){
 const records=['conversation_sft','rewrite_sft','behavior_reference'].map((task,i)=>{
  const r={id:'S'+i,task,prompt:{scene_is_synthetic:true,scene_facts:['合成情景'],dialogue_context:[],runtime_observations:[],user_message:'还在吗？'},target:{kind:task==='behavior_reference'?'no_visible_reply':'assistant_text',text:task==='behavior_reference'?null:'嗯。'}};
  if(task==='rewrite_sft')r.rewrite={original:'是的，我还在这里。'};
  r.target_sha256=digest({task:r.task,prompt:r.prompt,target:r.target,rewrite:r.rewrite??null});return r;
 });
 const pack={schema:'xiaoa.training-demonstrations/1',version:'browser-fixture-1',archival_note:'SYNTHETIC_NOT_PRIVATE_'.repeat(4200),records};
 pack.sha256=digest(pack);
 return {pack,review:{schema:'xiaoa.demonstration-direction-review/1',version:pack.version,pack_sha256:pack.sha256,reviewer:'user',approval_scope:'direction_only_not_training_consent',complete:true,decisions:records.map(r=>({id:r.id,target_sha256:r.target_sha256,decision:'keep',note:'',suggested_text:''}))}};
}
const bundle=process.env.HA_WORKSHOP_DIRECTION_BUNDLE?JSON.parse(await fs.readFile(process.env.HA_WORKSHOP_DIRECTION_BUNDLE,'utf8')):fixture();
const bundleFile=path.join(scratch,'direction.json');await fs.writeFile(bundleFile,JSON.stringify(bundle));
const cfg=JSON.parse(await fs.readFile(path.join(root,'config/example.json'),'utf8'));
Object.assign(cfg,{dataRoot:path.join(scratch,'data'),characterRoot:path.join(root,'character'),uiRoot:path.join(root,'ui'),pluginRoot:path.join(scratch,'plugins'),
 plugins:{'style-lora':{enabled:true,applyEnabled:false}},review:{enabled:false,profileId:'',maxRewrites:1},computer:{enabled:false},solitudeIntervalMs:604800000,idleBeforeSolitudeMs:86400000});
const config=path.join(scratch,'config.json'),ready=path.join(scratch,'ready.json');await fs.writeFile(config,JSON.stringify(cfg));
const session='expression-synthetic-'+randomUUID();let service,origin,browser,page;const checks=[],errors=[];
function launch(){service=spawn(path.join(build,'hibernation-agent.exe'),['--config',config,'--ready-file',ready],{windowsHide:true,env:{...process.env,HIBERNATION_SESSION_KEY:session},stdio:'ignore'});}
async function wait(probe,name){const end=Date.now()+45000;while(Date.now()<end){if(service.exitCode!==null)throw Error('host exited: '+service.exitCode);try{if(await probe())return;}catch{}await new Promise(r=>setTimeout(r,90));}throw Error('timeout: '+name);}
async function api(route,data){const r=await fetch(origin+route,{method:data===undefined?'GET':'POST',headers:{Authorization:'Bearer '+session,'Content-Type':'application/json'},body:data===undefined?undefined:JSON.stringify(data)});assert(r.ok,route+' '+r.status);return r.json();}
async function tool(action,args={}){const request_id=randomUUID().replaceAll('-','');await api('/api/plugin/test',{tool:'style_lora.dataset.'+action,args,request_id});let result;await wait(async()=>{result=await api('/api/plugin/result');return result.request_id===request_id&&['succeeded','failed','paused'].includes(result.state);},action);return result.result;}
function check(v,name){assert(v,name);checks.push(name);console.log('PASS '+name);}
async function stop(){if(origin)await api('/api/shutdown',{}).catch(()=>{});await new Promise(resolve=>{if(service.exitCode!==null)return resolve();service.once('exit',resolve);setTimeout(()=>{service.kill();resolve();},12000).unref();});origin=null;}
try{
 launch();await wait(async()=>{origin='http://127.0.0.1:'+JSON.parse(await fs.readFile(ready,'utf8')).port;return true;},'ready');
 const {chromium}=await import(pathToFileURL(process.env.PLAYWRIGHT_PACKAGE_PATH||'C:/Users/lenovo/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright/index.mjs'));
 browser=await chromium.launch({headless:true,executablePath:process.env.HIBERNATION_TEST_BROWSER||'C:/Program Files/Google/Chrome/Application/chrome.exe'});
 page=await browser.newPage({viewport:{width:1440,height:1100},locale:'zh-CN'});page.on('pageerror',e=>errors.push(e.message));
 await page.goto(origin+'/#session='+session);await page.getByText('后台已连接',{exact:true}).waitFor();await page.locator('nav button[data-page="plugins"]').click();
 const card=page.locator('[data-plugin-id="style-lora"]');await card.waitFor({state:'attached'});await page.locator('#page-plugins .directory-expand').click();
 const panel=card.locator('.expression-workshop');await panel.getByRole('heading',{name:'候选语料工坊'}).waitFor();
 check(await panel.getByLabel('候选工坊生成连接').inputValue()==='workshop-mock-generator','simulated generation is the initial selection');
 check(!await panel.getByLabel('允许本批调用所选模型').isChecked(),'model send authorization is not preselected');
 await panel.getByLabel('候选工坊示范审核包').setInputFiles(bundleFile);await panel.getByRole('button',{name:'导入示范审核包',exact:true}).click();
 await panel.getByText(new RegExp('已加密导入'+bundle.review.decisions.filter(v=>v.decision==='keep').length+'例方向示范')).waitFor();
 check(true,'real host imports the reviewed direction bundle and verifies hashes');
 if((await fs.stat(bundleFile)).size>65536)check(true,'operator import accepts a bundle larger than the old 64KiB route limit');
 const dir=await panel.getByLabel('候选工坊方向版本').inputValue();
 await panel.getByLabel('候选数量',{exact:true}).fill('6');
 await panel.getByRole('button',{name:'开始生成并审核',exact:true}).click();
 await wait(()=>panel.getByText(/已完成；已处理 6\/6；调用 4/).isVisible(),'mock generation reviewed');
 check(await panel.getByLabel('待审核候选').locator('option').count()===6,'browser loads all separately reviewed candidates');
 check(await panel.getByText(/模拟候选，仅检查流程/).isVisible(),'mock results cannot be mistaken for language-quality evidence');
 await panel.getByLabel('候选修改建议').fill('这是一份待重审改稿。');
 await panel.getByRole('button',{name:'保留候选',exact:true}).click();await panel.getByText('修改后的文字先保存为待定，不能直接批准。',{exact:true}).waitFor();
 check(true,'edited target cannot be directly marked approved');
 await panel.getByRole('button',{name:'待定／保存修改建议',exact:true}).click();
 await wait(()=>panel.getByLabel('待审核候选').locator('option').first().textContent().then(t=>t.includes('hold')),'hold saved');
 await panel.getByLabel('待审核候选').selectOption({index:1});await panel.getByLabel('候选人工审核理由').fill('仅验证人工审核分离。');
 await panel.getByRole('button',{name:'保留候选',exact:true}).click();
 await wait(()=>panel.getByLabel('待审核候选').locator('option').nth(1).textContent().then(t=>t.includes('keep')),'keep saved');
 const downloadPromise=page.waitForEvent('download');await panel.getByRole('button',{name:'导出候选档案（不训练）',exact:true}).click();const download=await downloadPromise;const exportPath=path.join(scratch,'candidates.json');await download.saveAs(exportPath);const exported=JSON.parse(await fs.readFile(exportPath,'utf8'));
 check(exported.training_started===false&&exported.training_approved===false,'download is explicitly an untrained archive');
 const job=exported.job;check(job.exchanges.length===4&&job.candidates.length===6&&job.direction.original_pack.sha256===bundle.pack.sha256,'raw prompts, model text, review and original direction are exported');
 check(job.candidates[0].human_review.suggested_text==='这是一份待重审改稿。'&&job.candidates[0].response!=='这是一份待重审改稿。','suggestion is saved without overwriting the original answer');
 check(job.candidates[1].human_review.decision==='keep'&&job.candidates[1].training_eligible===false,'human keep does not trigger training');
 check((await api('/api/chat')).messages.length===0,'operator work creates no character conversation');
 const denied=await tool('start',{direction_id:dir,generator:{id:'remote-fixture',protocol:'responses',origin:'https://example.invalid',model:'not-called'},reviewer:{id:'mock-reviewer',protocol:'mock'},count:3});
 check(denied?.error?.code==='dataset_send_consent_required','real connection is rejected before any call without this-batch authorization');
 const binaryFiles=await fs.readdir(path.join(cfg.dataRoot,'runtime/style-lora/datasets'));
 check(binaryFiles.every(n=>n.endsWith('.dpapi')),'runtime workshop storage consists only of encrypted files');
 for(const n of binaryFiles)check(!(await fs.readFile(path.join(cfg.dataRoot,'runtime/style-lora/datasets',n))).includes(Buffer.from('这是一份待重审改稿。')),'no plaintext suggestion in '+n);
 await page.screenshot({path:path.join(scratch,'workshop-dashboard.png'),fullPage:true});check(errors.length===0,'dashboard has no script errors');
 await browser.close();browser=null;await stop();await fs.unlink(ready);launch();
 await wait(async()=>{origin='http://127.0.0.1:'+JSON.parse(await fs.readFile(ready,'utf8')).port;return true;},'restarted ready');
 const reopened=await tool('get',{job_id:job.id});check(reopened.ok&&reopened.result.candidates[1].human_review.decision==='keep'&&reopened.result.calls===4,'separate-process restart preserves the batch without more calls');
 await fs.writeFile(path.join(scratch,'acceptance.json'),JSON.stringify({completed:true,checks,api_calls:0,training_started:false,direction_sha256:bundle.pack.sha256},null,2));console.log('Evidence: '+scratch);
}catch(e){if(page)console.log((await page.locator('.expression-workshop').innerText().catch(()=>'' )).slice(0,4000));if(page)await page.screenshot({path:path.join(scratch,'failure.png'),fullPage:true}).catch(()=>{});throw e;}
finally{if(browser)await browser.close();if(service)await stop();}
