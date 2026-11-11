// Synthetic OneBot + model endpoints. Loopback only; never contacts QQ or a
// real inference service. Artifacts contain synthetic test material exclusively.
import fs from 'node:fs';
import path from 'node:path';
import http from 'node:http';
const root=path.resolve(process.argv[2]);fs.mkdirSync(root,{recursive:true});
let calls=0,sends=0;
const server=http.createServer(async(req,res)=>{
 let text='';for await(const chunk of req)text+=chunk;
 let input;try{input=text?JSON.parse(text):{};}catch{res.writeHead(400).end();return;}
 const success=data=>res.writeHead(200,{'content-type':'application/json'}).end(JSON.stringify({status:'ok',retcode:0,data}));
 if(req.url==='/get_status')return success({online:true,good:true});
 if(req.url==='/get_login_info')return success({user_id:'10001',nickname:'Synthetic bot'});
 if(req.url==='/send_private_msg'||req.url==='/send_group_msg'){
  if(req.headers.authorization!=='Bearer synthetic-qq-api'){res.writeHead(401).end();return;}
  fs.appendFileSync(path.join(root,'sent.jsonl'),JSON.stringify(input)+'\n');return success({message_id:++sends});
 }
 if(req.url==='/v1/chat/completions'){
  calls++;fs.appendFileSync(path.join(root,'requests.jsonl'),JSON.stringify(input)+'\n');
  const reply={version:1,kind:'reply',text:'SYNTHETIC_AWAKE_QQ_REPLY',calls:[]};
  const usage={prompt_tokens:100,completion_tokens:12,prompt_cache_hit_tokens:0,prompt_cache_miss_tokens:100};
  if(input.stream){
   const delta={choices:[{index:0,delta:{content:JSON.stringify(reply)},finish_reason:null}]};
   const finish={choices:[{index:0,delta:{},finish_reason:'stop'}]};
   res.writeHead(200,{'content-type':'text/event-stream'}).end(`data: ${JSON.stringify(delta)}\n\ndata: ${JSON.stringify(finish)}\n\ndata: ${JSON.stringify({choices:[],usage})}\n\ndata: [DONE]\n\n`);
  }else res.writeHead(200,{'content-type':'application/json'}).end(JSON.stringify({choices:[{message:{content:JSON.stringify(reply)},finish_reason:'stop'}],usage}));return;
 }
 if(req.url==='/fixture/status')return success({calls,sends});
 res.writeHead(404).end();
});
server.listen(0,'127.0.0.1',()=>fs.writeFileSync(path.join(root,'port'),String(server.address().port)));
