import fs from 'node:fs/promises';
import path from 'node:path';
import http from 'node:http';
import assert from 'node:assert/strict';
import {fileURLToPath,pathToFileURL} from 'node:url';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const scratch=path.join(root,'artifacts',`runtime-ui-${Date.now()}`);await fs.mkdir(scratch,{recursive:true});
const settings=JSON.parse(await fs.readFile(path.join(root,'config/example.json'),'utf8'));
settings.plugins={};settings.profiles=[{id:'fixture',label:'测试连接 <img src=x>',protocol:'mock',enabled:true}];settings.backgroundBudget={enabled:true,maxCallsPerDay:120,maxInputTokensPerDay:500000};
const usage={calls:3,usage_reported_calls:2,cache_reported_calls:1,input_tokens:1000,output_tokens:50,cached_input_tokens:300,uncached_input_tokens:100,cache_hit_ratio:.75};
const dashboard={settings,plugins:[],health:'ok',offline:false,paused:false,busy:false,selected_profile:'fixture',data_root:'E:/fixture/data',memory_encrypted:true,model_budget:{used:0,max:600,remaining:600},runtime_status:{code:'idle',label:'等待新的活动',detail:'当前没有等待执行的任务。',background_allowed:true,activity_queued:2,activity_active:false,speech_state:'idle'},background_budget:{enabled:true,day:'2026-10-28',timezone:'Asia/Shanghai',used_calls:10,remaining_calls:110,max_calls:120,input_tokens:4000,reported_calls:9,unknown_usage_calls:1,max_input_tokens:500000,blocked:false,resets_at_ms:1793203200000},model_cache:{since_ms:1793100000000,updated_ms:1793100010000,connections:{fixture:{total:usage,purposes:{dialogue:usage,reaction:{calls:1,usage_reported_calls:0,cache_reported_calls:0}}}}},memory_workshop:{jobs:[]}};
dashboard.own_time={enabled:true,ready:false,wait_reason:'return_budget_reserve',next_ms:1793100010000,active:false,private_note:'PRIVATE_OWN_TIME_MARKER'};
let status={state:'idle'},saved=null,expectedFailure=false,statusReads=0,modelCalls=0;const requests=[];
const server=http.createServer(async(req,res)=>{
 try{const url=new URL(req.url,'http://127.0.0.1');
  if(url.pathname.startsWith('/api/')){
   let body='';for await(const chunk of req)body+=chunk;const data=body?JSON.parse(body):null;requests.push(url.pathname);
   let result={};
   if(url.pathname==='/api/dashboard')result=dashboard;
   else if(url.pathname==='/api/mind/trace')result={private_content_included:false,events:[{id:'audit-fixture',kind:'cognition.activity.queued',status:'accepted_not_completed',created_at:1793290000000,private:{text:'PRIVATE_TRACE_UI_MARKER'}},{id:'sleep-fixture',kind:'cognition.sleep.receipt',status:'rhythm.sleep_ended',reason:'window_ended',created_at:1793290001000}]};
   else if(url.pathname==='/api/rhythm/wake'){dashboard.circadian.phase='idle';result={ok:true,result:{changed:true}};}
   else if(url.pathname==='/api/skills')result={packages:[],available_tools:[]};
   else if(url.pathname==='/api/appearance')result={presets:[]};
   else if(url.pathname==='/api/settings'){saved=data.settings;dashboard.settings=structuredClone(saved);}
   else if(url.pathname==='/api/control'){Object.assign(dashboard,data);}
   else if(url.pathname==='/api/vault-portability/status'){statusReads++;result=status;}
   else if(url.pathname.startsWith('/api/vault-portability/')){
    const operation=url.pathname.split('/').at(-1);
    if(expectedFailure){expectedFailure=false;status={state:'failed',operation,error:'portable_unlock_failed'};}
    else if(operation==='export')status={state:'completed',operation,result:{bundle:{version:1,ciphertext:'ENCRYPTED_FIXTURE_ONLY'},filename:'fixture-keys.json',stores:2,includes_memory_contents:false}};
    else if(operation==='verify'){assert(data.bundle.ciphertext==='ENCRYPTED_FIXTURE_ONLY');status={state:'completed',operation,result:{verified:true,stores:2,records:42,files:3,archive_bytes:1200}};}
    else if(operation==='restore'){assert.equal(data.backupRoot,'E:/Backups/fixture');assert.equal(data.targetRoot,'E:/Recovered/new-data');status={state:'completed',operation,result:{restored:true,targetRoot:data.targetRoot,stores:2,records:42,files:3,originals_unchanged:true}};}
    result={operation_id:'synthetic-operation'};
   }else if(url.pathname==='/api/chat'){modelCalls++;throw Error('No model calls allowed');}
   res.writeHead(200,{'Content-Type':'application/json'});res.end(JSON.stringify(result));return;
  }
  const target=url.pathname==='/'?path.join(root,'ui/index.html'):path.join(root,url.pathname.slice(1));if(!target.startsWith(path.join(root,'ui')))throw Error('path rejected');
  const ext=path.extname(target);res.writeHead(200,{'Content-Type':({'.js':'text/javascript','.css':'text/css','.svg':'image/svg+xml','.html':'text/html'})[ext]||'application/octet-stream'});res.end(await fs.readFile(target));
 }catch{res.writeHead(500,{'Content-Type':'application/json'});res.end('{"error":"fixture_failed"}');}
});
await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));const origin=`http://127.0.0.1:${server.address().port}`;
let browser;const checks=[],errors=[];const check=(value,name)=>{assert(value,name);checks.push(name);console.log('PASS '+name);};
try{
 const {chromium}=await import(pathToFileURL(process.env.PLAYWRIGHT_PACKAGE_PATH||'C:/Users/lenovo/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright/index.mjs'));
 browser=await chromium.launch({headless:true,executablePath:process.env.HIBERNATION_TEST_BROWSER||'C:/Program Files/Google/Chrome/Application/chrome.exe'});
 const page=await browser.newPage({viewport:{width:1440,height:1000},locale:'zh-CN'});page.on('pageerror',error=>errors.push(error.message));await page.goto(origin+'/#session=runtime-ui-synthetic');await page.getByText('后台已连接',{exact:true}).waitFor();
 await page.evaluate(()=>focusDashboardPanel('section-runtime-details',document.getElementById('page-overview')));
 check(await page.locator('#runtime-detail-label').textContent()==='等待新的活动','actual dashboard mounts detailed runtime state');
 check(await page.locator('#runtime-own-time').textContent()==='为交谈保留余量','own-time waiting has a readable budget reason without private notes');
 dashboard.own_time.wait_reason='sleeping';await page.evaluate(async()=>{await load();});check(await page.locator('#runtime-own-time').textContent()==='正在休息','own-time state follows the actual program sleep reason');
 await page.evaluate(()=>focusDashboardPanel('section-cognitive-trace',document.getElementById('page-overview')));await page.getByText('查看最近的执行痕迹',{exact:true}).click();await page.getByText(/动作已接受，尚未完成/).waitFor();check(!(await page.locator('#cognitive-trace-rows').textContent()).includes('PRIVATE_TRACE_UI_MARKER'),'trace renders whitelisted metadata even if an unexpected private field is present');
 check((await page.locator('#cognitive-trace-rows').textContent()).includes('休息时段已结束'),'sleep receipts show a readable actual reason alongside their status');
 dashboard.cognitive_trace={previous_run_clean:false};await page.evaluate(async()=>{await load();});check((await page.locator('#cognitive-trace-seal').textContent()).includes('尾部记录可能存在缺口'),'unclean audit closure is explicit without claiming a complete trace');
 dashboard.circadian={available:true,configured:true,phase:'sleeping'};await page.evaluate(async()=>{await load();focusDashboardPanel('section-circadian',document.getElementById('page-overview'));});check((await page.locator('#circadian-current').textContent()).includes('当前正在休息'),'routine state is understandable without private settings');await page.locator('#circadian-wake').click();await page.waitForFunction(()=>document.getElementById('circadian-current').textContent.includes('当前没有长睡'));check(requests.includes('/api/rhythm/wake'),'administrator wake uses the native override endpoint');
 await page.locator('nav [data-page="models"]').click();await page.evaluate(()=>focusDashboardPanel('section-budget',document.getElementById('page-models')));
 const limit=page.getByLabel('后台每天最多调用（1 至 10000）');await limit.fill('37');await page.evaluate(()=>updateRuntimeControls(current));check(await limit.inputValue()==='37','runtime refresh preserves unsaved daily-budget edits');
 await page.getByLabel('启用连续注意与可恢复打算').check();await page.getByLabel('注意驻留秒数（1 至 60）').fill('7');await page.getByLabel('打算保留分钟（1 至 60）').fill('9');
 await page.locator('#page-models .save-settings').click();await page.getByText('设置已保存。',{exact:true}).waitFor();check(saved.backgroundBudget.maxCallsPerDay===37,'daily budget saves through existing settings workflow');check(saved.cognition.enabled&&saved.cognition.dwellMs===7000&&saved.cognition.resumeMinutes===9,'continuity switches and durations save without exposing private content');
 await page.evaluate(()=>focusDashboardPanel('section-model-cache',document.getElementById('page-models')));check(await page.locator('#model-cache-ratio').textContent()==='75.0%','cache rate uses reported input counters only');
 const detail=page.locator('#model-cache-connections details');await detail.locator('summary').click();check((await detail.textContent()).includes('互动反应预生成'),'per-purpose token breakdown is available');check((await detail.textContent()).includes('未报告'),'missing usage remains unknown');
 await page.evaluate(()=>{current.model_cache.updated_ms++;updateRuntimeControls(current);});check(await detail.evaluate(node=>node.open),'usage refresh preserves expanded connection');check(await page.locator('#model-cache-connections img').count()===0,'profile label is rendered safely as text');
 await page.evaluate(()=>{delete current.model_cache.connections.fixture.total.cache_hit_ratio;updateRuntimeControls(current);});check(await page.locator('#model-cache-ratio').textContent()==='未报告','missing cache ratio is not inferred as a zero hit rate');
 const partial=await page.evaluate(()=>{
  const saved=structuredClone(current.model_cache);
  const zero={calls:1,usage_reported_calls:1,input_reported_calls:1,output_reported_calls:0,complete_usage_reported_calls:0,input_tokens:0,output_tokens:0,input_tokens_known:true,output_tokens_known:false};
  current.model_cache.connections={fixture:{total:zero,purposes:{dialogue:zero}}};updateRuntimeControls(current);
  const result={input:document.getElementById('model-cache-input').textContent,output:document.getElementById('model-cache-output').textContent,row:[...document.querySelectorAll('#model-cache-connections tbody td')].map(n=>n.textContent),exported:safeRuntimeDiagnostics(current)};
  current.model_cache=saved;updateRuntimeControls(current);return result;
 });
 check(partial.input==='0'&&partial.output==='未报告'&&partial.row[2]==='0'&&partial.row[3]==='未报告','reported zero input is shown while missing output remains unknown');
 check(partial.exported.model_cache.connections.fixture.total.input_tokens_known===true&&partial.exported.model_cache.connections.fixture.total.output_reported_calls===0,'diagnostics preserve per-side reporting evidence without inventing output');
 const legacy=await page.evaluate(()=>{
  const saved=structuredClone(current.model_cache);current.model_cache.connections={fixture:{total:{calls:4,usage_reported_calls:4,input_tokens:0,output_tokens:10},purposes:{}}};updateRuntimeControls(current);
  const result=[document.getElementById('model-cache-input').textContent,document.getElementById('model-cache-output').textContent];current.model_cache=saved;updateRuntimeControls(current);return result;
 });check(legacy[0]==='未报告'&&legacy[1]==='10','legacy zero totals are not fabricated as reported values');
 const overflow=await page.evaluate(()=>{
  const saved=structuredClone(current.model_cache),usage={calls:2,usage_reported_calls:2,input_tokens:80,output_tokens:4,input_tokens_known:true,output_tokens_known:true};current.model_cache.connections={'$other_profiles':{total:usage,purposes:{'$other_purposes':usage}}};updateRuntimeControls(current);
  const result=document.getElementById('model-cache-connections').textContent;current.model_cache=saved;updateRuntimeControls(current);return result;
 });check(overflow.includes('其他连接（合并）')&&overflow.includes('其他用途（合并）'),'overflow usage stays visible under readable aggregate labels');
 const diagnostics=await page.evaluate(()=>JSON.stringify(safeRuntimeDiagnostics({...current,secret:'PRIVATE_SENTINEL',messages:['PRIVATE_SENTINEL'],runtime_status:{...current.runtime_status,detail:'PRIVATE_SENTINEL'}})));check(!diagnostics.includes('PRIVATE_SENTINEL')&&!diagnostics.includes('PRIVATE_OWN_TIME_MARKER')&&!diagnostics.includes('data_root')&&!diagnostics.includes('profiles'),'diagnostic export whitelists counters and excludes private payloads');
 await page.locator('nav [data-page="workspace"]').click();await page.evaluate(()=>focusDashboardPanel('section-portable-vault',document.getElementById('page-workspace')));
 check(await page.locator('#portable-export-submit').isDisabled(),'key operations require paused and offline');await page.locator('#portable-pause-offline').click();await page.waitForFunction(()=>!document.getElementById('portable-export-submit').disabled);
 await page.locator('#portable-export-password').fill('synthetic-passphrase');await page.locator('#portable-export-confirm').fill('synthetic-passphrase');const downloadPromise=page.waitForEvent('download');await page.locator('#portable-export-submit').click();const download=await downloadPromise;await download.saveAs(path.join(scratch,'encrypted-fixture.json'));await page.waitForFunction(()=>document.getElementById('portable-export-password').value==='');
 check((await fs.readFile(path.join(scratch,'encrypted-fixture.json'),'utf8')).includes('ENCRYPTED_FIXTURE_ONLY'),'encrypted key package is downloaded without being rendered');check(!(await page.locator('body').textContent()).includes('ENCRYPTED_FIXTURE_ONLY'),'key package internals remain hidden');
 await page.locator('#portable-vault details').nth(1).locator('summary').click();await page.locator('#portable-import-file').setInputFiles(path.join(scratch,'encrypted-fixture.json'));await page.locator('#portable-backup-root').fill('E:/Backups/fixture');await page.locator('#portable-import-password').fill('synthetic-passphrase');await page.locator('#portable-verify').click();await page.getByText(/密钥包与备份验证通过：/).waitFor();check(await page.locator('#portable-import-password').inputValue()==='','verification clears the password');check(await page.locator('#portable-restore').isEnabled(),'successful verification unlocks separate-directory restore');
 await page.locator('#portable-target-root').fill('E:/Recovered/new-data');await page.locator('#portable-import-password').fill('synthetic-passphrase');await page.locator('#portable-restore').click();await page.getByText(/已复制到新的恢复目录/).waitFor();check(await page.locator('#portable-import-password').inputValue()==='','restore clears the password and preserves active archive');
 expectedFailure=true;await page.locator('#portable-import-password').fill('synthetic-wrong-password');await page.locator('#portable-verify').click();await page.getByText('密码不正确，或密钥包已损坏。',{exact:true}).waitFor();check(await page.locator('#portable-import-password').inputValue()==='','failed unlock is readable and clears password');
 const readsBefore=statusReads;await page.waitForTimeout(200);check(statusReads-readsBefore<=1,'async status does not enter an uncontrolled polling loop');
 const stored=await page.evaluate(()=>JSON.stringify({local:{...localStorage},session:{...sessionStorage}}));check(!stored.includes('passphrase')&&!stored.includes('wrong-password')&&!stored.includes('ENCRYPTED_FIXTURE_ONLY'),'passwords and encrypted package are not persisted in browser storage');
 await page.screenshot({path:path.join(scratch,'desktop.png'),fullPage:true});await page.setViewportSize({width:390,height:844});check(await page.evaluate(()=>document.documentElement.scrollWidth)<=390,'portable recovery wizard fits a narrow dashboard');await page.screenshot({path:path.join(scratch,'narrow.png'),fullPage:true});check(errors.length===0,`no browser script errors: ${errors.join('; ')}`);check(modelCalls===0,'all browser checks use synthetic data without model calls');
 await fs.writeFile(path.join(scratch,'acceptance.json'),JSON.stringify({completed:true,checks,paid_model_calls:0},null,2));console.log('Evidence: '+scratch);
}finally{if(browser)await browser.close();await new Promise(resolve=>server.close(resolve));}
