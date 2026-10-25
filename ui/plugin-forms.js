'use strict';
// Plain data only: labels use textContent and fields can change this plugin's
// configuration object. This manifest format does not load plugin JavaScript.
function renderDeclaredPluginSettings(parent,manifest,configuration){
  if(!Array.isArray(manifest.settings_ui))return;
  for(const spec of manifest.settings_ui.slice(0,32)){
    if(!spec||typeof spec!=='object'||typeof spec.key!=='string'||!/^[A-Za-z][A-Za-z0-9]{0,63}$/.test(spec.key)||typeof spec.label!=='string'||spec.label.length>120||['enabled','constructor','prototype'].includes(spec.key)||/(api.?key|secret|token|password|credential)/i.test(spec.key))continue;
    const value=configuration[spec.key]??spec.default;
    if(spec.type==='toggle'){toggle(parent,spec.label,typeof value==='boolean'?value:false,v=>configuration[spec.key]=v);continue;}
    if(spec.type==='number'){
      const input=field(parent,spec.label,Number.isFinite(value)?value:0,v=>configuration[spec.key]=Number(v));input.type='number';
      for(const property of ['min','max','step'])if(Number.isFinite(spec[property]))input[property]=String(spec[property]);continue;
    }
    if(spec.type==='select'&&Array.isArray(spec.options)){
      const options=spec.options.slice(0,64).filter(v=>Array.isArray(v)&&v.length===2&&v.every(x=>typeof x==='string'&&x.length<=120));
      if(options.length)field(parent,spec.label,typeof value==='string'?value:options[0][0],v=>configuration[spec.key]=v,options);continue;
    }
    if(spec.type==='text'){const input=field(parent,spec.label,typeof value==='string'?value:'',v=>configuration[spec.key]=v);input.maxLength=Number.isInteger(spec.maxLength)?Math.min(2048,Math.max(1,spec.maxLength)):256;}
  }
  if(typeof manifest.settings_note==='string')parent.append(el('p','helper',manifest.settings_note.slice(0,1024)));
}
function updatePluginDependencies(id,enabled,manifests,configurations){
  const declared=new Map(manifests.map(m=>[m.id,Array.isArray(m.dependencies)?m.dependencies.filter(d=>typeof d==='string'&&/^[a-z][a-z0-9_-]{0,63}$/.test(d)).slice(0,16):[]]));
  const visited=new Set();
  function visit(key,on){if(visited.has(key))return;visited.add(key);const cfg=configurations[key]||(configurations[key]={enabled:false});cfg.enabled=on;if(on){for(const dep of declared.get(key)||[])if(declared.has(dep))visit(dep,true);}else{for(const [other,deps]of declared)if(deps.includes(key)&&configurations[other]?.enabled)visit(other,false);}}
  visit(id,enabled);
}
