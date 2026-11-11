import assert from 'node:assert/strict';
import {pathToFileURL} from 'node:url';
const {chromium}=await import(pathToFileURL(process.env.PLAYWRIGHT_PACKAGE_PATH||'C:/Users/lenovo/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright/index.mjs'));
const [origin]=process.argv.slice(2),session='skills-fixture-only';
const browser=await chromium.launch({executablePath:process.env.HIBERNATION_TEST_BROWSER||'C:/Program Files/Google/Chrome/Application/chrome.exe',headless:true});
try{
 const page=await browser.newPage(),errors=[];page.on('pageerror',e=>{errors.push(e.message);console.error('Dashboard script error: '+e.message);});
 await page.goto(origin+'/#session='+session);await page.locator('[data-page="workspace"]').click();
 const fold=page.locator('#skill-settings > .fold-head > .fold-toggle');try{await fold.waitFor();}catch(error){console.error(JSON.stringify(await page.evaluate(()=>({activePages:[...document.querySelectorAll('.page.active')].map(n=>n.id),skillPanel:document.getElementById('skill-settings')?.outerHTML.slice(0,1600)}))));throw error;}if(await fold.getAttribute('aria-expanded')!=='true')await fold.click();
 const skill=page.locator('#skill-settings article.card').filter({hasText:'Notes'});await skill.getByText('内容已变化，待重新授权',{exact:true}).waitFor();
 await skill.locator('summary').click();await skill.locator('label').filter({hasText:'workspace.read · '}).locator('input').check();
 await skill.getByRole('button',{name:'授权选中的工具并保存',exact:true}).click();
 await skill.getByText('已授权',{exact:true}).waitFor();
 const state=await page.evaluate(async({origin,session})=>(await fetch(origin+'/api/skills',{headers:{Authorization:'Bearer '+session}})).json(),{origin,session});
 assert.equal(state.packages[0].approved,true);assert.deepEqual(state.packages[0].granted_tools,['workspace.read']);
 assert(!state.available_tools.find(t=>t.name==='activity.enqueue')?.skill_grantable);
 await skill.getByRole('button',{name:'撤销授权',exact:true}).click();await skill.getByText('未授权',{exact:true}).waitFor();
 assert.deepEqual(errors,[]);console.log('Skill dashboard content-bound approval, selected-tool grant, revocation and safe rendering passed.');
}finally{await browser.close();}
