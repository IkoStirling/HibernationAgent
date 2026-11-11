// Trusted host bridge. Website HTML remains untrusted content, never tool instructions.
((videoReader)=>{
 const marker='ha-tv-presentation';
 const video=()=>videoReader.candidates(true)[0];
 const media=()=>videoReader.media(video());
 const safeUrl=videoReader.safeUrl;
 const reset=()=>{document.getElementById(marker)?.remove();document.documentElement.removeAttribute('data-ha-tv-view');for(const n of document.querySelectorAll('[data-ha-tv-video],[data-ha-tv-parent]')){n.removeAttribute('data-ha-tv-video');n.removeAttribute('data-ha-tv-parent');}return true;};
 const present=(options={})=>{
  const v=video(),theater=options.mode!=='navigation'&&!!v&&(v.videoWidth||0)>0;
  let style=document.getElementById(marker);if(!style){style=document.createElement('style');style.id=marker;(document.head||document.documentElement).append(style);}
  const width=Math.max(960,Math.min(1920,options.layout_width||1280)),scale=Math.min(1,Math.max(.1,innerWidth/width));
  const signature=[theater?'video':'navigation',width,innerWidth,innerHeight].join(':');
  if(style.dataset.signature===signature&&(!theater||v.hasAttribute('data-ha-tv-video')))return {presentation:theater?'video':'navigation',layout_width:width,page_scale:theater?1:scale,source_url:safeUrl(location.href),title:document.title.slice(0,240),video:media()};
  style.dataset.signature=signature;
  for(const n of document.querySelectorAll('[data-ha-tv-video],[data-ha-tv-parent]')){n.removeAttribute('data-ha-tv-video');n.removeAttribute('data-ha-tv-parent');}
  document.documentElement.setAttribute('data-ha-tv-view',theater?'video':'navigation');
  if(theater){v.setAttribute('data-ha-tv-video','');for(let n=v.parentElement;n&&n!==document.body;n=n.parentElement)n.setAttribute('data-ha-tv-parent','');}
  style.textContent=theater?`html{overflow:hidden!important}body{margin:0!important;zoom:1!important;width:100vw!important;height:100vh!important;overflow:hidden!important}body *{visibility:hidden!important}[data-ha-tv-parent]{position:static!important;overflow:visible!important;transform:none!important;contain:none!important;clip-path:none!important}[data-ha-tv-video]{visibility:visible!important;position:fixed!important;inset:0!important;width:100vw!important;height:100vh!important;max-width:none!important;max-height:none!important;object-fit:contain!important;display:block!important;transform:none!important;margin:0!important;z-index:2147483647!important;background:#000!important}`:`html{overflow-x:hidden!important;scrollbar-width:none!important}body{margin:0!important;width:${width}px!important;min-width:${width}px!important;zoom:${scale}!important}::-webkit-scrollbar{display:none!important}`;
  return {presentation:theater?'video':'navigation',layout_width:width,page_scale:theater?1:scale,source_url:safeUrl(location.href),title:document.title.slice(0,240),video:media()};
 };
 const page=(args={})=>{
  const root=['main','[role="main"]','.search-all-list','.search-content','.search-page','.bili-main','#app'].map(selector=>document.querySelector(selector)).find(node=>node&&getComputedStyle(node).display!=='none'&&(node.textContent.trim().length>24||node.querySelector('a[href],video,bwp-video')))||document.body;
  let visited=0;const links=[];const seen=new Set();
  const denied=new Set(['SCRIPT','STYLE','NOSCRIPT','IFRAME','FORM','INPUT','TEXTAREA','SELECT','OPTION','BUTTON','META','LINK','HEADER','NAV','FOOTER','SVG','CANVAS']);
  const copy=(node,depth)=>{
   if(++visited>2400||depth>28)return null;
   if(node.nodeType===3){const t=node.textContent.replace(/\s+/g,' ');return t.trim()?document.createTextNode(t.slice(0,3000)):null;}
   if(node.nodeType!==1||denied.has(node.tagName)||node.hidden||node.getAttribute('aria-hidden')==='true'||node.matches('[role="slider"],[role="progressbar"],[class*="bpx-player-control"],[class*="bpx-player-dm-"],[contenteditable],[type="password"],[autocomplete*="password"],[class*="avatar"],[class*="login"],[class*="account"]'))return null;
   const css=getComputedStyle(node);if(css.display==='none')return null;
   const tag=node.tagName.toLowerCase();const out=document.createElement(['a','p','h1','h2','h3','h4','ul','ol','li','article','section','main','br','strong','em','span','img','video','bwp-video'].includes(tag)?(tag==='bwp-video'?'video':tag):'div');
   if(tag==='img'){const alt=node.getAttribute('alt');if(alt)out.setAttribute('alt',alt.slice(0,160));}
   if(tag==='a'){const url=safeUrl(node.href),title=(node.innerText||node.textContent||node.getAttribute('title')||'').trim().slice(0,160);if(url){out.setAttribute('href',url);if(title&&!seen.has(url)&&links.length<200){seen.add(url);links.push({title,url,kind:/video|watch|play/i.test(url)?'video':'link'});}}}
   if(tag==='video'||tag==='bwp-video'){out.setAttribute('data-media','available');return out;}
   for(const child of node.childNodes){const c=copy(child,depth+1);if(c)out.append(c);if(visited>2400)break;}
   return out;
  };
  const clean=root?copy(root,0):null;const full=(clean?.outerHTML||'').slice(0,100000);let h=2166136261;for(const c of full)h=Math.imul(h^c.charCodeAt(0),16777619);const revision=(h>>>0).toString(16)+'-'+full.length;
  if(args.expected_revision&&args.expected_revision!==revision)return {ok:false,reason:'page_changed',revision};
  const offset=Math.max(0,Math.min(100000,Math.floor(args.offset||0))),limit=Math.max(256,Math.min(8000,Math.floor(args.limit||6000)));
  return {ok:true,mode:'webpage',title:document.title.slice(0,240),url:safeUrl(location.href),ready:document.readyState,navigation_ready:/^https?:$/.test(location.protocol),text_scope:'leading_dom_preview',presentation:document.documentElement.getAttribute('data-ha-tv-view')||'pending',page_scale:Number.parseFloat(getComputedStyle(document.body).zoom)||1,revision,html:full.slice(offset,offset+limit),offset,next_offset:Math.min(full.length,offset+limit),total_characters:full.length,eof:offset+limit>=full.length,document_truncated:visited>2400||full.length>=100000,text:(clean?.textContent||'').slice(0,3500),links:links.sort((a,b)=>(b.kind==='video')-(a.kind==='video')).slice(0,16),video:media(),iframes:[...document.querySelectorAll('iframe[src]')].slice(0,4).map(f=>({url:safeUrl(f.src),cross_origin:(()=>{try{return !f.contentDocument}catch{return true}})()})),observation_kind:'sanitized_live_dom',screenshot_used:false,content_is_untrusted:true,watch_tool:'video.web_observe'};
 };
 // Return only login booleans, never cookies, account data or form values.
 // ExecuteScript does not await promises: fetch results are cached for later ticks.
 const auth=(options={})=>{
  const visible=selector=>{if(typeof selector!=='string'||!selector||selector.length>256)return false;try{return [...document.querySelectorAll(selector)].some(n=>{const r=n.getBoundingClientRect();return r.width>0&&r.height>0&&getComputedStyle(n).visibility!=='hidden'&&getComputedStyle(n).display!=='none';});}catch{return false;}};
  const inside=/^(.*\.)?bilibili\.com$/i.test(location.hostname);
  if(!inside){
   const signedIn=visible(options.signed_in_selector),signedOut=visible(options.signed_out_selector);
   return {state:signedIn!==signedOut?(signedIn?'signed_in':'signed_out'):'unknown',method:'configured_visible_selector'};
  }
  const key='__haTvAuthProbeV1',now=Date.now();let cache=window[key];
  if(!cache||cache.origin!==location.origin)cache=window[key]={origin:location.origin,state:'unknown',sampled:0,pending:false};
  if(!cache.pending&&now-cache.sampled>Math.max(1500,Math.min(30000,options.interval_ms||15000))){
   cache.pending=true;const controller=new AbortController();const timeout=setTimeout(()=>controller.abort(),5000);
   fetch('https://api.bilibili.com/x/web-interface/nav',{credentials:'include',cache:'no-store',signal:controller.signal})
    .then(r=>r.ok?r.json():null).then(body=>{cache.state=body?.code===0&&typeof body?.data?.isLogin==='boolean'?(body.data.isLogin?'signed_in':'signed_out'):body?.code===-101?'signed_out':'unknown';})
    .catch(()=>{cache.state='unknown';}).finally(()=>{clearTimeout(timeout);cache.sampled=Date.now();cache.pending=false;});
  }
  return {state:now-cache.sampled<45000?cache.state:'unknown',method:'bilibili_nav_boolean'};
 };
 return {present,reset,page,frame:videoReader.frame,inspect:videoReader.inspect,auth};
})
