// Aggregate saved synthetic experiments; never reads requests or credentials.
import fs from 'node:fs';
import path from 'node:path';
import {summarize} from './CacheEventLab.mjs';
const root=path.resolve(process.argv[2]);
const read=file=>JSON.parse(fs.readFileSync(file,'utf8').replace(/^\uFEFF/,''));
const price={hit:.02,miss:1,output:4};
const estimate=s=>s.cache_reported_calls===s.calls&&s.output_tokens!==null?(s.hit_tokens*price.hit+s.miss_tokens*price.miss+s.output_tokens*price.output)/1e6:null;
const rows=[],files=fs.readdirSync(root,{withFileTypes:true});
for(const folder of ['review-fixed-replay','review-context-replay','review-views-replay','review-confirm-1','review-confirm-2','review-confirm-3','review-direct-replay','review-direct-bare','review-direct-plugin'])if(fs.existsSync(path.join(root,folder)))for(const mode of ['full','within_turn','cross_turn','context_only','views_only','direct','direct_bare'])if(fs.existsSync(path.join(root,folder,mode+'.json')))files.push({name:folder+'/'+mode+'.json',isDirectory:()=>false});
for(const file of files){
 const target=file.isDirectory()?path.join(root,file.name,'report.json'):path.join(root,file.name);
 if(!fs.existsSync(target)||(!file.isDirectory()&&(path.extname(file.name)!=='.json'||(!file.name.includes('/')&&!['ordinary','rewrite_snapshot','rewrite_initial','append_snapshot','append_delta','review_replace','review_append','review_events'].includes(path.parse(file.name).name)))))continue;
 const report=read(target);const requests=report.requests??report.rows;
 if(!Array.isArray(requests)||!requests.length)continue;
 const measured=summarize(requests),row={name:file.isDirectory()?file.name:file.name.replace(/\.json$/,''),completed:report.completed===true,...measured,estimated_cost_cny:estimate(measured)};
 if(report.steady){row.steady=report.steady;row.steady.estimated_cost_cny=estimate(row.steady);}
 if(report.turns)row.completed_turns=report.turns.length;
 if(report.semantic_checked!==undefined){row.semantic_checks=report.semantic_checked;row.semantic_passes=report.semantic_passes;}
 rows.push(row);
}
const ledger=read(path.join(root,'budget.json'));
const result={as_of:new Date().toISOString(),pricing_source:'https://api-docs.deepseek.com/zh-cn/quick_start/pricing/',currency:'CNY',per_million:price,off_peak:true,estimates_not_account_debits:true,rows,
 budget:{calls:ledger.entries.length,charged_tokens:ledger.entries.reduce((n,r)=>n+r.charged_tokens,0),reported_calls:ledger.entries.filter(r=>r.state==='reported').length,conservative_calls:ledger.entries.filter(r=>r.state!=='reported').length,limit_calls:ledger.limit_calls,limit_tokens:ledger.limit_tokens}};
fs.writeFileSync(path.join(root,'analysis.json'),JSON.stringify(result,null,2));
console.log(JSON.stringify({budget:result.budget,experiments:rows.map(r=>({name:r.name,calls:r.calls,hit_ratio:r.hit_ratio,estimated_cost_cny:r.estimated_cost_cny}))}));
