// Explicit opt-in only. This adapter never loads upstream ASR configuration.
import fs from 'node:fs/promises';
import path from 'node:path';
import {execFile} from 'node:child_process';
import {promisify} from 'node:util';
const run=promisify(execFile);
export async function transcribeWindow(request,from,to){
  const c=request.asr||{};
  if(c.enabled!==true)return {segments:[],status:'disabled'};
  let endpoint;
  try{
    endpoint=new URL(c.endpoint);
    const local=['127.0.0.1','localhost','[::1]'].includes(endpoint.hostname);
    if(endpoint.username||endpoint.password||endpoint.search||endpoint.hash||(!local&&endpoint.protocol!=='https:')||(local&&!['http:','https:'].includes(endpoint.protocol)))throw Error();
    if(typeof c.model!=='string'||!c.model.trim()||c.model.length>128)throw Error();
    if(c.keyEnv&&!/^[A-Za-z_][A-Za-z0-9_]{0,127}$/.test(c.keyEnv))throw Error();
  }catch{return {segments:[],status:'configuration_invalid'};}
  const segments=[];let chunks=0;
  // At most four 30s audio chunks. Never send the whole file or read website cookies.
  for(let start=from;start<to&&chunks<4;start+=30){
    const end=Math.min(to,start+30),audio=path.join(request.scratch,`speech-${chunks++}.wav`);
    try{
      await run(request.ffmpeg,['-v','error','-ss',String(start),'-t',String(end-start),'-i',request.input,'-vn','-ac','1','-ar','16000','-c:a','pcm_s16le','-y',audio],{timeout:12000,maxBuffer:65536,windowsHide:true});
      const bytes=await fs.readFile(audio);if(bytes.length>1024*1024)throw Error();
      const body=new FormData();body.append('file',new Blob([bytes],{type:'audio/wav'}),'window.wav');body.append('model',c.model);body.append('response_format','verbose_json');
      if(c.language)body.append('language',String(c.language).slice(0,16));
      const key=c.keyEnv?process.env[c.keyEnv]:'';
      const response=await fetch(endpoint,{method:'POST',body,headers:key?{Authorization:`Bearer ${key}`}:{},redirect:'error',signal:AbortSignal.timeout(10000)});
      if(!response.ok)throw Error();
      let size=0;const parts=[];for await(const chunk of response.body){size+=chunk.length;if(size>512*1024)throw Error();parts.push(chunk);}
      const result=JSON.parse(Buffer.concat(parts).toString('utf8'));
      if(Array.isArray(result.segments)){
        for(const s of result.segments.slice(0,500)){
          const a=Number(s.start),b=Number(s.end);
          if(Number.isFinite(a)&&Number.isFinite(b)&&a>=0&&b>a&&b<=end-start+0.001&&typeof s.text==='string'&&s.text.trim())segments.push({from:start+a,to:Math.min(end,start+b),text:s.text.slice(0,1000)});
        }
      }else if(typeof result.text==='string'&&result.text.trim())segments.push({from:start,to:end,text:result.text.slice(0,10000),timing:'chunk_only'});
    }catch{return {segments,status:segments.length?'partial':'unavailable',chunks};}
    finally{await fs.rm(audio,{force:true}).catch(()=>{});}
  }
  return {segments,status:segments.length?'transcribed':'no_speech',chunks};
}
