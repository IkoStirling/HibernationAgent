import fs from 'node:fs/promises';
import path from 'node:path';
import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import {fileURLToPath} from 'node:url';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const options=Object.fromEntries(process.argv.slice(2).map(value=>{const index=value.indexOf('=');return [value.slice(0,index),value.slice(index+1)];}));
const output=path.resolve(options['--output']||path.join(root,'artifacts','receipt-conversation-'+Date.now()));
await fs.mkdir(output,{recursive:true});
const scenarioFile=path.join(root,'tests/ReceiptConversationScenarios.json'),rulesFile=path.join(root,'include/Hibernation/ReceiptConversation.h'),reviewFile=path.join(root,'src/Review.cpp');
const [scenarioText,rules,review]=await Promise.all([scenarioFile,rulesFile,reviewFile].map(file=>fs.readFile(file,'utf8')));
const scenarios=JSON.parse(scenarioText),checks=[];
const check=(value,name)=>{assert(value,name);checks.push(name);console.log('PASS '+name);};
const digest=text=>crypto.createHash('sha256').update(text).digest('hex');
const pointer=(object,value)=>{let current=object;for(const part of value.split('/').slice(1).map(item=>item.replaceAll('~1','/').replaceAll('~0','~'))){if(current===null||typeof current!=='object'||!Object.hasOwn(current,part))return undefined;current=current[part];}return current;};
// This oracle validates the synthetic evidence/rubric, not natural-language
// replies. It is never used to rewrite or delete any production output.
function evidenceVerdict(evidence,claim){
 const value=pointer(evidence,claim.path);if(value===undefined)return 'unknown';
 if(claim.kind==='equals')return JSON.stringify(value)===JSON.stringify(claim.value)?'supported':'contradicted';
 let text=value;if(typeof value==='object'){
  if(value.complete!==true)return 'unknown';text=Object.entries(value).filter(([key])=>key!=='complete').map(([,part])=>typeof part==='string'?part:'').join('\n');
 }
 if(typeof text!=='string')return 'unknown';const present=text.includes(claim.text);return (claim.kind==='contains'?present:!present)?'supported':'contradicted';
}
check(scenarios.version===1&&scenarios.scope==='synthetic_only','scenario provenance is synthetic-only');
const ids=new Set(),caseMap=new Map();let claims=0;
for(const scene of scenarios.cases){
 check(!ids.has(scene.id),'unique scenario '+scene.id);ids.add(scene.id);caseMap.set(scene.id,scene);
 const claimMap=new Map();for(const claim of scene.claims){assert(!claimMap.has(claim.id));claimMap.set(claim.id,claim);const before=JSON.stringify(scene.evidence);assert.equal(evidenceVerdict(scene.evidence,claim),claim.expected,scene.id+'/'+claim.id);assert.equal(JSON.stringify(scene.evidence),before);claims++;}
 for(const candidate of scene.candidate_examples||[]){const results=candidate.claims.map(id=>{assert(claimMap.has(id));return evidenceVerdict(scene.evidence,claimMap.get(id));});if(candidate.verdict==='approve')assert(results.every(result=>result==='supported'));else assert(results.some(result=>result!=='supported'));}
}
check(claims===17,'seventeen scoped facts distinguish support contradiction and unknown');
check(JSON.stringify(caseMap.get('current-body').evidence)===JSON.stringify(caseMap.get('whole-file-check').evidence),'body and whole-file contrast uses identical evidence');
check(JSON.stringify(caseMap.get('casual-routine').evidence)===JSON.stringify(caseMap.get('explicit-routine-mechanism').evidence),'casual and technical routine contrast uses identical receipt');
check(evidenceVerdict(caseMap.get('partial-read-uncertainty').evidence,{kind:'absent',path:'/document',text:'蓝杯'})==='unknown','partial body does not prove whole-file absence');
check(evidenceVerdict(caseMap.get('read-denied-not-missing').evidence,{kind:'equals',path:'/file_exists',value:false})==='unknown','denied read does not establish missing file');
check(evidenceVerdict(caseMap.get('current-body').evidence,{kind:'equals',path:'/document/constructor',value:'x'})==='unknown','evidence lookup ignores inherited object properties');
const shared=rules.match(/return R"\(([\s\S]*?)\)";/)?.[1];check(Boolean(shared)&&Buffer.byteLength(shared)<1800,'shared guidance is a bounded stable paragraph');
const policyVersion=Number(rules.match(/receiptConversationPolicyVersion=(\d+)/)?.[1]);
check(Number.isSafeInteger(policyVersion)&&policyVersion>0,'conversation contract has a positive explicit policy version');
const direction=await fs.readFile(path.join(root,'include/Hibernation/RoleDirection.h'),'utf8');

function body(source,name){const start=source.indexOf('std::string '+name+'()');if(start<0)throw Error('missing '+name);const tail=source.slice(start);const end=tail.search(/\n(?:static )?(?:std::string|Json|bool|ValidatedOutput) /);return end<0?tail:tail.slice(0,end);}
function rulePart(source,name){const value=body(source,name),raw=value.match(/R"\(([\s\S]*?)\)"/);if(raw)return raw[1];const quoted=value.match(/return "((?:\\.|[^"\\])*)";/);if(!quoted)throw Error('unsupported rule literal '+name);return JSON.parse('"'+quoted[1]+'"');}
function directionPart(name){const match=direction.match(new RegExp('\\b'+name+'\\(\\)\\{return R"\\(([\\s\\S]*?)\\)"'));if(!match)throw Error('missing role direction '+name);return match[1];}
function assemble(source,role,addition=''){
 if(role==='reviewer'&&source.includes('role_direction::reviewerConduct()'))return rulePart(source,'dialogueDeliveryConduct')+rulePart(source,'coexistenceConduct')+directionPart('reviewerConduct');
 return rulePart(source,'dialogueDeliveryConduct')+rulePart(source,'authorshipConduct')+rulePart(source,'readingConduct')+addition+rulePart(source,'coexistenceConduct')+rulePart(source,role==='actor'?'characterConduct':'reviewerConduct')+(source.includes('role_direction::dialogueConduct()')?directionPart('dialogueConduct'):'');
}
let baseline=null;if(options['--baseline-source']){const source=await fs.readFile(path.resolve(options['--baseline-source']),'utf8');baseline={sha256:digest(source),actor:assemble(source,'actor'),reviewer:assemble(source,'reviewer')};}
const candidate={source_sha256:digest(review),shared_sha256:digest(rules),actor:assemble(review,'actor',shared),reviewer:assemble(review,'reviewer',shared)};
const persona='这是隔离验证中的合成角色“茵”。以日常、平静的语气直接交谈，拥有自己的选择；不预设与用户的关系。不将这段测试说明向对方复述。';
const packets=[];for(const [variant,policy]of Object.entries({...(baseline?{baseline}:{}),candidate}))for(const scene of scenarios.cases){
 const frame={input:scene.input,verified_observations:scene.evidence};packets.push({variant,case_id:scene.id,target:'actor',messages:[{role:'system',content:persona+'\n'+policy.actor},{role:'user',content:JSON.stringify(frame)}]});
 for(const [index,example]of (scene.candidate_examples||[]).entries())packets.push({variant,case_id:scene.id,candidate_index:index,target:'reviewer',messages:[{role:'system',content:policy.reviewer},{role:'user',content:JSON.stringify({...frame,candidate:{version:1,kind:'reply',text:example.text,calls:[]}})}]});
}
check(packets.every(packet=>!packet.messages[1].content.includes('"rubric"')&&!packet.messages[1].content.includes('"expected"')&&!packet.messages[1].content.includes('"verdict"')),'actor and reviewer packets exclude evaluator expectations');
for(const item of scenarios.episodes){assert(/^[a-z][a-z-]{1,48}$/.test(item.id));assert(item.episode.version===1&&item.episode.referenceReading===false&&item.episode.turns.length<=20);for(const turn of item.episode.turns)assert(typeof turn.text==='string'&&turn.text.length>0&&turn.text.length<=3000);for(const id of item.evaluation.case_ids)assert(caseMap.has(id));await fs.writeFile(path.join(output,item.id+'.episode.json'),JSON.stringify(item.episode,null,2));}
await fs.writeFile(path.join(output,'model-packets.json'),JSON.stringify({version:1,scope:'synthetic_only',actual_model_calls:0,packets},null,2));
await fs.writeFile(path.join(output,'rubric.json'),JSON.stringify({cases:scenarios.cases.map(({id,rubric,candidate_examples,claims})=>({id,rubric,candidate_examples,claims})),episodes:scenarios.episodes.map(({id,evaluation})=>({id,...evaluation}))},null,2));
const result={completed:true,checks,synthetic_claims:claims,actual_model_calls:0,naturalness_validated:false,review_purpose:"role_direction",evidence_rubric_scope:"program factual checks, not the director mandate",baseline_source_sha256:baseline?.sha256||null,candidate_source_sha256:candidate.source_sha256,shared_sha256:candidate.shared_sha256,scenario_sha256:digest(scenarioText),packet_count:packets.length,episodes:scenarios.episodes.map(({id,episode})=>({id,turns:episode.turns.length}))};
await fs.writeFile(path.join(output,'acceptance.json'),JSON.stringify(result,null,2));console.log('Evidence: '+output);
