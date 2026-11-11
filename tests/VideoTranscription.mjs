import assert from 'node:assert/strict';
import http from 'node:http';
import fs from 'node:fs/promises';
import path from 'node:path';
import {pathToFileURL} from 'node:url';
import {execFile} from 'node:child_process';
import {promisify} from 'node:util';
const [project,scratch,ffmpeg]=process.argv.slice(2);
const {transcribeWindow}=await import(pathToFileURL(path.join(project,'plugins/video-analysis/transcribe.mjs')));
const root=await fs.mkdtemp(path.join(scratch,'asr-fixture-'));
let requests=0,redirected=0,mode='normal';
const server=http.createServer(async(req,res)=>{
 if(req.url==='/must-not-follow'){redirected++;res.end('{}');return;}
 requests++;const parts=[];for await(const chunk of req)parts.push(chunk);
 const body=Buffer.concat(parts);
 assert.equal(req.headers.authorization,'Bearer synthetic-asr-fixture');
 assert(body.includes(Buffer.from('RIFF')),'multipart contains actual WAV');
 assert(body.includes(Buffer.from('fixture-model')));
 if(mode==='redirect'){res.writeHead(302,{Location:'/must-not-follow'});res.end();return;}
 if(mode==='large'){res.end(' '.repeat(600000));return;}
 if(mode==='text'){res.end(JSON.stringify({text:'没有细粒度时间戳的台词'}));return;}
 res.setHeader('Content-Type','application/json');res.end(JSON.stringify({segments:[{start:0,end:.8,text:'本地转写夹具。'},{start:10000,end:10001,text:'无效越界台词'}]}));
});
try{
 await fs.mkdir(scratch,{recursive:true});
 const input=path.join(root,'tone.mp4');
 await promisify(execFile)(ffmpeg,['-hide_banner','-loglevel','error','-f','lavfi','-i','color=s=32x32:r=1','-f','lavfi','-i','sine=frequency=440:sample_rate=16000','-t','35','-c:v','mpeg4','-c:a','aac','-y',input],{windowsHide:true});
 await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
 process.env.XIAOAI_ASR_FIXTURE='synthetic-asr-fixture';
 const request={input,scratch:root,ffmpeg,asr:{enabled:true,endpoint:`http://127.0.0.1:${server.address().port}/transcribe`,model:'fixture-model',keyEnv:'XIAOAI_ASR_FIXTURE',language:'zh'}};
 let r=await transcribeWindow({...request,asr:{enabled:false}},2,33);assert.equal(r.status,'disabled');assert.equal(requests,0);
 r=await transcribeWindow({...request,asr:{...request.asr,endpoint:'http://not-local.invalid/transcribe'}},2,33);assert.equal(r.status,'configuration_invalid');assert.equal(requests,0);
 r=await transcribeWindow(request,2,33);assert.equal(r.status,'transcribed');assert.equal(requests,2);assert.equal(r.segments.length,2);assert.equal(r.segments[0].from,2);assert.equal(r.segments[1].from,32);assert.equal(r.segments[1].to,32.8);
 assert.equal((await fs.readdir(root)).filter(f=>f.endsWith('.wav')).length,0,'temporary audio removed');
 mode='text';r=await transcribeWindow(request,2,3);assert.equal(r.segments[0].timing,'chunk_only');assert.equal(r.segments[0].to,3);
 mode='redirect';r=await transcribeWindow(request,2,3);assert.equal(r.status,'unavailable');assert.equal(redirected,0,'redirect cannot forward credentials');
 mode='large';r=await transcribeWindow(request,2,3);assert.equal(r.status,'unavailable');
 console.log('ASR fixture passed: opt-in, real audio chunks, absolute timing, timestamp rejection, text-only fallback, redirect refusal, response bounds, cleanup. No paid calls.');
}finally{delete process.env.XIAOAI_ASR_FIXTURE;await new Promise(resolve=>server.close(resolve));await fs.rm(root,{recursive:true,force:true});}
