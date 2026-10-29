import fs from 'node:fs/promises';
import path from 'node:path';
import http from 'node:http';
import assert from 'node:assert/strict';
import {fileURLToPath,pathToFileURL} from 'node:url';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const scratch=path.join(root,'artifacts',`supplies-ui-${Date.now()}`);await fs.mkdir(scratch,{recursive:true});
const settings=JSON.parse(await fs.readFile(path.join(root,'config/example.json'),'utf8'));
settings.plugins={widget:{enabled:true},habitat:{enabled:true,autoSpawn:true,maxObjects:6},canteen:{enabled:true}};settings.profiles=[{id:'fixture',label:'测试连接 <img src=x>',protocol:'mock',enabled:true}];settings.backgroundBudget={enabled:true,maxCallsPerDay:120,maxInputTokensPerDay:500000};
const usage={calls:3,usage_reported_calls:2,cache_reported_calls:1,input_tokens:1000,output_tokens:50,cached_input_tokens:300,uncached_input_tokens:100,cache_hit_ratio:.75};
const dashboard={settings,plugins:[{"id": "widget", "name": "桌面小角色", "description": "透明桌面角色，支持自定义像素动画、拖动、点击、游走和对白气泡。", "version": "0.6.0", "api_version": 2, "library": "Hibernation_Widget.dll", "capabilities": ["desktop.overlay", "desktop.pointer-interaction", "animation.state-machine"], "settings_ui": [{"key": "roamPauseMinSeconds", "label": "游走后最短停留（秒）", "type": "number", "default": 20, "min": 2, "max": 600, "step": 1}, {"key": "roamPauseMaxSeconds", "label": "游走后最长停留（秒）", "type": "number", "default": 50, "min": 2, "max": 600, "step": 1}, {"key": "roamStepPixels", "label": "随机游走时的最大距离（像素）", "type": "number", "default": 200, "min": 40, "max": 600, "step": 10}, {"key": "autonomousMode", "label": "自主控制：由角色批量选择空闲活动", "type": "toggle", "default": false}, {"key": "autonomyRefillSeconds", "label": "空闲活动预备间隔（秒）", "type": "number", "default": 60, "min": 30, "max": 600, "step": 5}, {"key": "autonomyIdleSeconds", "label": "交谈后空闲多久开始考虑活动（秒）", "type": "number", "default": 30, "min": 5, "max": 600, "step": 5}, {"label": "跟随 Windows 的减少动态效果设置", "key": "respectSystemMotion", "type": "toggle", "default": true}, {"label": "动画来源", "key": "animationMode", "type": "select", "options": [["sequence", "像素序列帧"], ["procedural", "内置简易动画"]], "default": "sequence"}, {"label": "自定义动画包目录（留空使用当前形象）", "key": "animationPack", "type": "text", "maxLength": 2048, "default": ""}, {"label": "动画图片缓存上限（MiB）", "default": 64, "type": "number", "max": 256, "step": 16, "key": "animationCacheMiB", "min": 16}], "settings_note": "自主模式由模型批量安排移动、姿态、停留、进食和休息，最多六项；到达后才进入下一项。模型移动不受随机游走距离上限约束，执行时再寻找实际物件；新输入、拖动、暂停会清空旧计划。需要桌面小物或安静节律插件才能进食或休息。开启后替代插件原有的随机接近和自动休息。", "enabled": true}, {"id": "habitat", "name": "桌面小物", "description": "有资源区域、食材采集和上层行为队列的桌面小世界。背包与食堂共用引擎库存。", "version": "0.4.0", "api_version": 2, "library": "Hibernation_Habitat.dll", "dependencies": ["widget"], "capabilities": ["desktop.objects", "world.observation", "world.interaction", "world.foraging", "world.regions"], "enabled": true}, {"id": "canteen", "name": "小食堂", "description": "与桌面采集共用食材库存的小厨房，提供背包、仓库、保鲜和三道料理。", "version": "0.2.0", "api_version": 2, "library": "Hibernation_Canteen.dll", "dependencies": ["habitat"], "capabilities": ["canteen.cooking", "canteen.shelf", "inventory.backpack", "inventory.warehouse"], "settings_ui": [{"key": "automatic", "type": "toggle", "label": "食堂自行少量备餐", "default": true}, {"key": "automaticIntervalSeconds", "type": "number", "label": "自动备餐检查间隔（秒）", "default": 900, "min": 60, "max": 43200, "step": 60}, {"key": "supplyIntervalSeconds", "type": "number", "label": "少量食材补给间隔（秒）", "default": 3600, "min": 300, "max": 86400, "step": 60}, {"key": "shelfLifeSeconds", "type": "number", "label": "成品架保留时长（秒）", "default": 1800, "min": 60, "max": 43200, "step": 60}], "enabled": true}],health:'ok',offline:false,paused:false,busy:false,selected_profile:'fixture',data_root:'E:/fixture/data',memory_encrypted:true,model_budget:{used:0,max:600,remaining:600},runtime_status:{code:'idle',label:'等待新的活动',detail:'当前没有等待执行的任务。',background_allowed:true,activity_queued:2,activity_active:false,speech_state:'idle'},background_budget:{enabled:true,day:'2026-10-28',timezone:'Asia/Shanghai',used_calls:10,remaining_calls:110,max_calls:120,input_tokens:4000,reported_calls:9,unknown_usage_calls:1,max_input_tokens:500000,blocked:false,resets_at_ms:1793203200000},model_cache:{since_ms:1793100000000,updated_ms:1793100010000,connections:{fixture:{total:usage,purposes:{dialogue:usage,reaction:{calls:1,usage_reported_calls:0,cache_reported_calls:0}}}}},memory_workshop:{jobs:[]}};
let status={state:'idle'},saved=null,expectedFailure=false,statusReads=0,modelCalls=0;const requests=[];
const server=http.createServer(async(req,res)=>{
 try{const url=new URL(req.url,'http://127.0.0.1');
  if(url.pathname.startsWith('/api/')){
   let body='';for await(const chunk of req)body+=chunk;const data=body?JSON.parse(body):null;requests.push(url.pathname);
   let result={};
   if(url.pathname==='/api/dashboard')result=dashboard;
   else if(url.pathname==='/api/skills')result={packages:[],available_tools:[]};
   else if(url.pathname==='/api/appearance')result={presets:[],skins:[],palettes:[]};
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
 const page=await browser.newPage({viewport:{width:1440,height:1000},locale:'zh-CN'});page.on('pageerror',error=>errors.push(error.message));await page.goto(origin+'/#session=supplies-ui-synthetic');await page.getByText('后台已连接',{exact:true}).waitFor();
 await page.locator('nav [data-page="plugins"]').click();await page.evaluate(()=>focusDashboardPanel('plugin-habitat',document.getElementById('page-plugins')));
 const card=page.locator('[data-plugin-id="habitat"]'),regions=card.locator('.resource-region');check(await regions.count()===3,'actual dashboard mounts three resource regions');
 await regions.first().getByLabel('区域名称',{exact:true}).fill('窗台的小花园');await regions.first().getByLabel('左边界（%）',{exact:true}).fill('12');
 await regions.first().getByLabel('区域最多物件',{exact:true}).fill('3');await regions.first().getByLabel('茶叶',{exact:true}).uncheck();
 await card.getByRole('button',{name:'添加区域',exact:true}).click();check(await regions.count()===4,'owner can add a resource region');
 await regions.last().getByLabel('区域名称',{exact:true}).fill('收藏角');await regions.last().getByRole('button',{name:'移除此区域',exact:true}).click();check(await regions.count()===3,'owner can remove a region while preserving at least one');
 await page.evaluate(()=>save());await page.waitForFunction(()=>document.getElementById('connection').textContent==='后台已连接');
 check(saved.plugins.habitat.regions[0].name==='窗台的小花园'&&saved.plugins.habitat.regions[0].left===.12,'saved names and normalized coordinates reflect edits');
 check(saved.plugins.habitat.regions[0].capacity===3&&!saved.plugins.habitat.regions[0].resources.includes('tea_leaf'),'saved capacity and ingredient restrictions reflect checkboxes');
 check(saved.plugins.habitat.regions[1].id==='garden'&&saved.plugins.habitat.regions[2].resources.includes('flour'),'editing one region preserves other regions');
 await page.evaluate(()=>focusDashboardPanel('plugin-habitat',document.getElementById('page-plugins')));await page.screenshot({path:path.join(scratch,'desktop.png'),fullPage:true});
 await page.setViewportSize({width:390,height:844});check(await page.evaluate(()=>document.documentElement.scrollWidth)<=390,'region controls fit narrow dashboard');await page.screenshot({path:path.join(scratch,'narrow.png'),fullPage:true});
 check(errors.length===0,`no browser script errors: ${errors.join('; ')}`);check(modelCalls===0,'configuration editing calls no model');
 await fs.writeFile(path.join(scratch,'acceptance.json'),JSON.stringify({completed:true,checks,paid_model_calls:0},null,2));console.log('Evidence: '+scratch);
}finally{if(browser)await browser.close();await new Promise(resolve=>server.close(resolve));}
