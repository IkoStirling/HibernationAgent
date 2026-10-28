// Fixed real-application synthetic candidates isolate cache layout from actor choices.
import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {Ledger} from './PersonalityBudget.mjs';
import {usageOf,summarize,complete} from './CacheEventLab.mjs';
const file=process.argv[2],root=path.dirname(path.resolve(file));
const cfg=JSON.parse(fs.readFileSync(file,'utf8'));
const session=JSON.parse(fs.readFileSync(path.join(root,'session.json'),'utf8')),deadline=Date.parse(session.deadline_utc);
if(cfg.origin!=='https://api.deepseek.com'||cfg.protocol!=='responses'||Date.now()>=deadline)throw Error('unexpected_configuration');
const key=process.env.HIBERNATION_CACHE_LAB_API_KEY;if(!key)throw Error('credential_missing');
const phase=process.argv[3]??'review-fixed-replay';if(!['review-fixed-replay','review-context-replay','review-views-replay','review-direct-replay','review-direct-bare','review-direct-plugin'].includes(phase)&&!/^review-confirm-[1-3]$/.test(phase))throw Error('invalid_phase');
const directory=path.join(root,phase);if(fs.existsSync(directory))throw Error('existing_experiment_requires_review');fs.mkdirSync(directory);
const original=JSON.parse(fs.readFileSync(path.join(root,'app-baseline/report.json'),'utf8'));
const frames=[];let offset=0;
for(const turn of original.turns){
 for(const r of original.requests.slice(offset,offset+turn.request_count))if(r.profile===cfg.profileId){
  const body=JSON.parse(fs.readFileSync(path.join(root,'app-baseline',r.id+'-request.json'),'utf8'));
  frames.push({turn:turn.index,body,expected_approve:true});
 }
 offset+=turn.request_count;
}
if(frames.length!==7)throw Error('unexpected_baseline_shape');
const falseClaim=structuredClone(frames.at(-1));falseClaim.turn=4;falseClaim.expected_approve=false;
const frame=JSON.parse(falseClaim.body.input[1].content);frame.candidate.text='我刚刚把纸船拿去水里划了一圈，船已经湿透了。';falseClaim.body.input[1].content=JSON.stringify(frame);frames.push(falseClaim);
if(phase==='review-direct-plugin'){
 const seed=structuredClone(frames[0].body);frames.length=0;
 const scenes=[
  ['widget',{pose:'sleeping',visible:true},'你现在在做什么？','我还躺着。','我正在桌面上走来走去。'],
  ['television',{playing:false,paused:true,title:'合成短片'},'小电视现在在播放吗？','小电视现在没有在播放。','小电视现在正在播放。'],
  ['habitat',{objects:[]},'桌面上现在还有苹果吗？','桌面上目前没有苹果。','桌面上还有一颗苹果。']
 ];
 for(const [plugin,current,question,safe,wrong]of scenes)for(const approved of [true,false]){
  const body=structuredClone(seed),capabilities={computer:{enabled:false},vision_available:false,plugins:[{id:plugin,enabled:true,current}],guide:[]};
  const context={event_kind:'user_message',event_id:'synthetic-plugin-'+frames.length,input:question,authorized_context:[],current_capabilities:capabilities};
  const root={target:'reply',current_capabilities:capabilities,current_user_message:question,current_user_source_id:context.event_id,event_context:JSON.stringify(context),candidate:{version:1,kind:'reply',text:approved?safe:wrong,calls:[]},approved_receipts:[],program_executed_operations:[],shared_understanding:{},style_edit:null,open_questions:[],authoritative_state:{energy:.5}};
  body.input[1].content=JSON.stringify(root);frames.push({turn:frames.length,body,expected_approve:approved});
 }
}
const source=path.join(directory,'source.json'),prepared=path.join(directory,'prepared.json');
fs.writeFileSync(source,JSON.stringify({namespace:session.namespace+(phase==='review-fixed-replay'?'':'/'+phase),frames,...(phase==='review-context-replay'?{modes:['context_only']}:phase==='review-views-replay'?{modes:['views_only']}:phase.startsWith('review-confirm-')?{modes:['full','within_turn']}:phase==='review-direct-replay'?{modes:['full','direct']}:(phase==='review-direct-bare'||phase==='review-direct-plugin')?{modes:['full','direct_bare']}:{} )}));
const child=spawnSync(path.join(cfg.buildRoot,'Hibernation_CacheActivityProbe.exe'),['--prepare-review-replay',source,prepared],{windowsHide:true,encoding:'utf8'});
if(child.status!==0)throw Error('prepare_replay_failed');
const variants=JSON.parse(fs.readFileSync(prepared,'utf8'));
const ledger=new Ledger(path.join(root,'budget.json'),{calls:3000,tokens:6000000});
try{
 for(const [mode,steps]of (phase.endsWith('-2')?Object.entries(variants).reverse():Object.entries(variants))){
  const report={phase,mode,synthetic_only:true,production_archive_read:false,rows:[],completed:false};
  const save=()=>{report.summary=summarize(report.rows);fs.writeFileSync(path.join(directory,mode+'.json'),JSON.stringify(report,null,2))};save();
  for(let index=0;index<steps.length;++index){
   if(Date.now()>=deadline)throw Error('authorized_window_ended');
   const item=steps[index],raw=JSON.stringify(item.body),entry=ledger.reserve(cfg.profileId,Buffer.byteLength(raw),item.body.max_output_tokens);if(!entry)throw Error('shared_budget_exhausted');
   const start=Date.now();let response;try{response=await complete(cfg.origin,cfg.basePath,key,item.body,deadline)}catch(e){ledger.settle(entry,0,null);throw e}
   const usage=usageOf(response.data);ledger.settle(entry,response.status,usage);
   const text=(response.data.output??[]).filter(x=>x.type==='message').flatMap(x=>x.content??[]).filter(x=>x.type==='output_text').map(x=>x.text).join('');let answer;try{answer=JSON.parse(text)}catch{}
   const original_decision_match=item.expected_approve?answer?.decision==='approve':['deny','reset','rewrite'].includes(answer?.decision);
   const semantic_ok=item.expected_approve?undefined:original_decision_match;
   report.rows.push({index,turn:item.turn,elapsed_ms:Date.now()-start,http_status:response.status,response_status:response.data.status??null,incomplete_reason:response.data.incomplete_details?.reason??null,usage,semantic_ok,original_decision_match,answer,expected_approve:item.expected_approve});save();
   if(response.status!==200||!answer)throw Error('review_replay_unavailable');
  }
  report.completed=true;save();console.log(JSON.stringify({mode,summary:report.summary}));
 }
}catch(e){console.error(e.message);process.exitCode=1}finally{ledger.close()}
