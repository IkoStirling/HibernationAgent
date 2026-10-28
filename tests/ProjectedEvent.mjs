export const decode=x=>{try{if(Array.isArray(x))x=x.filter(c=>c.type==='text'||c.type==='input_text').map(c=>c.text).join('');return JSON.parse(x);}catch{return null;}};
export function reviewOf(b){
 const capabilities=new Map(),evidence=new Map();let current;
 for(const m of b.messages||b.input||[]){const v=decode(m.content);if(!v)continue;
  if(v.review_capability)capabilities.set(v.review_capability.id,v.review_capability.data);
  else if(v.review_evidence)evidence.set(v.review_evidence.id,v.review_evidence.data);
  else if(v.review_snapshot)current=structuredClone(v.review_snapshot.data);
  else if(v.candidate&&(v.target||v.context))current=structuredClone(v);
 }
 if(!current)throw Error('Missing current review snapshot');
 const cap=current.current_capabilities?.$review_capability;if(cap)current.current_capabilities=structuredClone(capabilities.get(cap));
 const states=current.current_plugin_state;if(states)for(const p of current.current_capabilities?.plugins??[])if(!Object.hasOwn(p,'current')&&Object.hasOwn(states,p.id))p.current=structuredClone(states[p.id]);
 const typed=current.event_context&&typeof current.event_context==='object';const context=typed?structuredClone(current.event_context):decode(current.event_context);if(context){
  if(typed){for(const name of ['current_capabilities','authoritative_state','current_task','expression_preference','perception','open_questions','time_context','shared_understanding','conversation_delivery'])if(!Object.hasOwn(context,name)&&Object.hasOwn(current,name))context[name]=structuredClone(current[name]);if(!Object.hasOwn(context,'input')&&Object.hasOwn(current,'current_user_message'))context.input=current.current_user_message;}
  if(context.authorized_context)context.authorized_context=context.authorized_context.map(r=>r.$review_evidence?structuredClone(evidence.get(r.$review_evidence)):r);
  for(const[k,v]of Object.entries(context))if(v?.$review_field)context[k]=structuredClone(current[v.$review_field]);
  current.event_context=JSON.stringify(context);
 }
 return current;
}
export function eventOf(b){let frame={},rows=new Map();const pointer=p=>p.split('/').slice(1).map(s=>s.replaceAll('~1','/').replaceAll('~0','~'));for(const m of (b.messages||b.input||[])){const v=decode(m.content);if(v?.runtime_event){const e=v.runtime_event;if(e.source_event_id===frame.event_id)Object.assign(frame,e.changes);continue;}if(!v?.event_kind)continue;if(v.projection?.mode==='delta'){for(const op of v.projection_patch||[]){const path=pointer(op.path);let dest=frame;for(const key of path.slice(0,-1))dest=dest[key];const key=path.at(-1);if(op.op==='remove'){if(Array.isArray(dest))dest.splice(Number(key),1);else delete dest[key];}else if(Array.isArray(dest)&&op.op==='add'){dest.splice(key==='-'?dest.length:Number(key),0,op.value);}else dest[key]=op.value;}}else frame={...v};for(const r of v.authorized_context||[])rows.set(r.id,r);for(const [k,val] of Object.entries(v))if(!['projection','projection_patch','authorized_context'].includes(k))frame[k]=val;frame.authorized_context=[...rows.values()];}return frame;}
export function currentMessages(b){const rows=b.messages||b.input||[];const i=rows.findLastIndex(m=>decode(m.content)?.event_kind);return rows.slice(i<0?0:i);}
