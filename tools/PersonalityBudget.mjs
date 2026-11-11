// Local-only metered relay for explicitly authorized, isolated personality work.
// No payloads, headers, credentials or model reasoning are written to the ledger.
import http from 'node:http';
import https from 'node:https';
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {pathToFileURL} from 'node:url';

export class Ledger {
  constructor(file, {calls = 4000, tokens = 22000000} = {}) {
    if (!Number.isSafeInteger(calls) || calls < 1 || !Number.isSafeInteger(tokens) || tokens < 1) throw Error('invalid_budget');
    this.file = path.resolve(file); fs.mkdirSync(path.dirname(this.file), {recursive:true});
    this.lock = this.file + '.lock';
    this.handle = fs.openSync(this.lock, 'wx'); // An owner must resolve a crashed relay; never steal a live lock.
    try {
      this.data = fs.existsSync(this.file) ? JSON.parse(fs.readFileSync(this.file, 'utf8')) : {version:1, limit_calls:calls, limit_tokens:tokens, entries:[]};
      if (this.data.version !== 1 || this.data.limit_calls !== calls || this.data.limit_tokens !== tokens || !Array.isArray(this.data.entries)) throw Error('budget_identity_mismatch');
      for (const item of this.data.entries) if (!Number.isSafeInteger(item.charged_tokens) || item.charged_tokens < 0) throw Error('invalid_ledger');
    } catch (e) {this.close(); throw e;}
  }
  totals() {return {calls:this.data.entries.length, tokens:this.data.entries.reduce((n,e)=>n+e.charged_tokens,0)};}
  save() {
    const temporary = this.file + '.pending';
    const descriptor = fs.openSync(temporary, 'w');
    try {fs.writeFileSync(descriptor, JSON.stringify(this.data, null, 2)); fs.fsyncSync(descriptor);} finally {fs.closeSync(descriptor);}
    fs.renameSync(temporary, this.file);
  }
  reserve(route, bytes, output) {
    const total = this.totals(), reservation = bytes + output + 2048;
    if (total.calls >= this.data.limit_calls || total.tokens + reservation > this.data.limit_tokens) return null;
    const entry = {id:crypto.randomUUID(), at:new Date().toISOString(), route, request_bytes:bytes, max_output_tokens:output, reserved_tokens:reservation, charged_tokens:reservation, state:'reserved'};
    this.data.entries.push(entry); this.save(); return entry;
  }
  settle(entry, status, usage, cache = null) {
    entry.http_status = status;
    entry.elapsed_ms = Math.max(0,Date.now()-Date.parse(entry.at));
    if (usage && Number.isSafeInteger(usage.input) && usage.input >= 0 && Number.isSafeInteger(usage.output) && usage.output >= 0) {
      entry.input_tokens = usage.input; entry.output_tokens = usage.output;
      entry.charged_tokens = usage.input + usage.output; entry.state = 'reported';
    } else entry.state = 'conservative'; // Errors and missing usage do not become free calls.
    entry.cache_reported = false;
    if(cache && Number.isSafeInteger(entry.input_tokens) && Number.isSafeInteger(cache.hit) && Number.isSafeInteger(cache.miss) && cache.hit>=0 && cache.miss>=0 && cache.hit+cache.miss===entry.input_tokens){
      entry.cache_reported=true;entry.cached_input_tokens=cache.hit;entry.uncached_input_tokens=cache.miss;
    }
    this.save();
  }
  close() {if (this.handle !== undefined) {fs.closeSync(this.handle); this.handle = undefined; fs.unlinkSync(this.lock);}}
}

export function readUsage(body, type) {
  function extract(value) {
    const u = value?.usage ?? value?.response?.usage;
    if (!u) return null;
    return {input:u.input_tokens ?? u.prompt_tokens, output:u.output_tokens ?? u.completion_tokens};
  }
  try {
    if (!type.includes('text/event-stream')) return extract(JSON.parse(body));
    let usage = null, completed = false;
    for (const line of body.split(/\r?\n/)) if (line.startsWith('data:')) {
      const data = line.slice(5).trim(); if (!data) continue;
      if (data === '[DONE]') {completed = true; continue;}
      try {
        const value = JSON.parse(data);
        const terminal = ['response.completed','response.incomplete','response.failed'].includes(value.type);
        const measured = extract(value);
        // Responses may announce a zero-valued usage at creation. A truncated
        // stream must retain its reservation instead of being counted as free.
        if (terminal) {completed = true; usage = measured ?? usage;}
        else if (measured && !value.type?.startsWith('response.')) usage = measured;
      } catch {}
    }
    return completed ? usage : null;
  } catch {return null;}
}

// Provider-reported input caching is distinct from locally measured prefix bytes.
// Absent, malformed, or truncated usage stays unknown instead of becoming zero.
export function readCacheUsage(body,type) {
  const usageOf=value=>value?.usage??value?.response?.usage;
  let usage=null,completed=!type.includes('text/event-stream');
  try{
    if(completed)usage=usageOf(JSON.parse(body));
    else for(const line of body.split(/\r?\n/))if(line.startsWith('data:')){
      const raw=line.slice(5).trim();if(raw==='[DONE]'){completed=true;continue;}if(!raw)continue;
      try{const value=JSON.parse(raw),terminal=['response.completed','response.incomplete','response.failed'].includes(value.type),next=usageOf(value);
        if(terminal){completed=true;usage=next??usage;}else if(next&&!value.type?.startsWith('response.'))usage=next;
      }catch{}
    }
    if(!completed||!usage)return null;
    const total=usage.input_tokens??usage.prompt_tokens;
    const hit=usage.prompt_cache_hit_tokens??usage.input_tokens_details?.cached_tokens??usage.prompt_tokens_details?.cached_tokens;
    const miss=usage.prompt_cache_miss_tokens??(Number.isSafeInteger(total)&&Number.isSafeInteger(hit)?total-hit:undefined);
    return Number.isSafeInteger(total)&&Number.isSafeInteger(hit)&&Number.isSafeInteger(miss)&&hit>=0&&miss>=0&&hit+miss===total?{hit,miss}:null;
  }catch{return null;}
}

export function createRelay({ledger, routes, capability, deadline, allowLocalFixture = false, observeRequest, observeResponse, beforeReserve}) {
  if (!/^[a-f0-9]{48}$/.test(capability)) throw Error('invalid_relay_capability');
  const destinations = new Map();
  for (const [id, value] of Object.entries(routes)) {
    if (!/^[a-zA-Z0-9_-]{1,64}$/.test(id)) throw Error('invalid_route');
    const origin = new URL(value.origin);
    const local = ['127.0.0.1','[::1]'].includes(origin.hostname);
    if ((origin.protocol !== 'https:' && !(allowLocalFixture && local && origin.protocol === 'http:')) || origin.username || origin.password || origin.pathname !== '/' || origin.search || origin.hash) throw Error('invalid_upstream');
    if (!/^\/(?:[a-zA-Z0-9_-]+\/?)*$/.test(value.basePath)) throw Error('invalid_base_path');
    destinations.set(id, {origin, base:value.basePath.replace(/\/$/,'')});
  }
  const expires = Date.parse(deadline);
  if (!Number.isFinite(expires)) throw Error('invalid_deadline');
  function error(response, status, code) {response.writeHead(status, {'content-type':'application/json'}); response.end(JSON.stringify({error:{code,message:code}}));}
  const server = http.createServer(async (request, response) => {
    if (request.method === 'POST' && request.url === `/${capability}/stop`) {
      response.end('{}'); server.close(() => ledger.close()); return;
    }
    const route = request.url?.match(/^\/([a-f0-9]{48})\/([a-zA-Z0-9_-]+)\/(responses|chat\/completions)$/);
    if (request.method !== 'POST' || !route || route[1] !== capability || !destinations.has(route[2])) return error(response,404,'lab_route_unavailable');
    if (Date.now() >= expires) return error(response,429,'lab_window_ended');
    let size = 0, chunks = [];
    try {
      for await (const chunk of request) {size += chunk.length; if (size > 2*1024*1024) return error(response,413,'lab_request_too_large'); chunks.push(chunk);}
      const body = Buffer.concat(chunks); chunks = [];
      let document; try {document = JSON.parse(body.toString('utf8'));} catch {return error(response,400,'lab_invalid_json');}
      const maximum = document.max_output_tokens ?? document.max_completion_tokens ?? document.max_tokens;
      if (!Number.isSafeInteger(maximum) || maximum < 1 || maximum > 8192) return error(response,400,'lab_output_limit_required');
      if (beforeReserve && !beforeReserve({route:route[2], totals:ledger.totals(),document})) return error(response,429,'lab_review_budget_unavailable');
      const entry = ledger.reserve(route[2], body.length, maximum);
      if (!entry) return error(response,429,'lab_budget_exhausted');
      if (observeRequest) try {observeRequest({id:entry.id, route:route[2], document});} catch {ledger.settle(entry,0,null);return error(response,500,'lab_observer_failed');}
      const destination = destinations.get(route[2]);
      const url = new URL(destination.base + '/' + route[3], destination.origin);
      // Redirects are intentionally not followed. No arbitrary destination/headers.
      const transport = url.protocol === 'https:' ? https : http;
      let settled = false;
      const settle = (status, usage, cache=null) => {if (!settled) {settled = true; ledger.settle(entry,status,usage,cache);}};
      const upstream = transport.request(url, {method:'POST', signal:AbortSignal.timeout(Math.max(1,Math.min(120000,expires-Date.now()))), headers:{'content-type':'application/json', 'content-length':body.length, ...(request.headers.authorization ? {authorization:request.headers.authorization} : {})}}, incoming => {
        const output = []; let outputSize = 0;
        const type = String(incoming.headers['content-type'] ?? 'application/json');
        response.writeHead(incoming.statusCode ?? 502, {'content-type':type, 'cache-control':'no-store'});
        incoming.on('data', chunk => {
          outputSize += chunk.length;
          if (outputSize > 16*1024*1024) {incoming.destroy(); settle(incoming.statusCode ?? 502,null); response.destroy(); return;}
          output.push(chunk); response.write(chunk);
        });
        incoming.on('end', () => {const received=Buffer.concat(output).toString('utf8');settle(incoming.statusCode ?? 502,readUsage(received,type),readCacheUsage(received,type));if(observeResponse)try{observeResponse({id:entry.id,route:route[2],status:incoming.statusCode??502,body:received,type});}catch{}response.end();});
        incoming.on('error', () => {settle(incoming.statusCode ?? 502,null); response.destroy();});
      });
      upstream.setTimeout(120000, () => upstream.destroy());
      response.on('close', () => {if (!response.writableEnded) upstream.destroy();});
      upstream.on('error', () => {settle(502,null); if (!response.headersSent) error(response,502,'lab_upstream_unavailable'); else response.destroy();});
      upstream.end(body);
    } catch {if (!response.headersSent) error(response,500,'lab_relay_failed'); else response.destroy();}
  });
  return server;
}

if (process.argv[1] && import.meta.url === pathToFileURL(path.resolve(process.argv[1])).href) {
  const [configPath, readyPath] = process.argv.slice(2);
  const config = JSON.parse(fs.readFileSync(configPath,'utf8'));
  const ledger = new Ledger(config.ledger, {calls:config.maxCalls, tokens:config.maxTokens});
  const server = createRelay({...config, ledger});
  server.listen(0, '127.0.0.1', () => fs.writeFileSync(readyPath,JSON.stringify({port:server.address().port, pid:process.pid})));
  const finish = () => server.close(() => {ledger.close(); process.exit(0);});
  process.on('SIGINT',finish); process.on('SIGTERM',finish);
}
