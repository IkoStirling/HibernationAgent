'use strict';
function renderPricingSettings(parent){
 const block=el('div','');block.append(el('h3','','DeepSeek 当前计费时段'));
 const status=el('p','helper');status.id='deepseek-pricing-status';block.append(status);
 const source=el('a','text-button','官方价格说明');source.href='https://api-docs.deepseek.com/zh-cn/quick_start/pricing/';source.target='_blank';source.rel='noopener noreferrer';block.append(source);
 const refresh=el('button','quiet','刷新官方价格');refresh.type='button';refresh.onclick=async()=>{refresh.disabled=true;try{await api('/api/pricing/refresh',{});toast('已安排刷新；最短间隔一分钟。');await load();}catch(e){toast(e.message);}finally{refresh.disabled=false;}};block.append(refresh);parent.append(block);updatePricingStatus();
}
function updatePricingStatus(){
 const target=$('deepseek-pricing-status'),p=current?.deepseek_pricing;if(!target||!p)return;
 const period={peak:'高峰',off_peak:'谷时',unknown:'未知',unknown_calendar:'节假日历尚未覆盖当前年份'}[p.period]||'未知';
 const mode={auto:'自动',off:'调节已关闭','force-economy':'手动节省','force-normal':'手动恢复原策略'}[p.mode]||p.mode;
 const effective=p.applied?`节省中；最多 ${p.effective_max_tool_rounds} 个操作阶段，审核改写最多 ${p.effective_max_rewrites} 次${p.defer_prefetch?'；暂缓新的后台预生成':''}`:'使用原有调用策略';
 let text=`插件${p.enabled?'已启用':'未启用'} · ${mode} · ${period}（北京时间）。${effective}。`;
 if(!p.provider_matches)text+='当前连接不是适用的官方 DeepSeek 连接。';
 if(p.fixture)text+='当前为本地模拟价格数据。';
 if(p.price)text+=` ${p.model}：每百万 token，输入缓存命中 ¥${p.price.input_cache_hit}，未命中 ¥${p.price.input_cache_miss}，输出 ¥${p.price.output}。`;
 if(p.fetched_ms)text+=` 价格更新：${new Date(p.fetched_ms).toLocaleString('zh-CN')}。`;
 if(p.next_change_ms)text+=` 下一时段：${new Date(p.next_change_ms).toLocaleString('zh-CN')}。`;
 if(p.last_error)text+=p.last_error==='pricing_format_changed'?' 官方页面格式或规则已变化，自动调节暂停。':' 最近刷新未成功；有效缓存过期后停止自动调节。';
 text+='启用、自动模式和手动选择位于“可选插件 → DeepSeek 峰谷价格”，保存后生效。';target.textContent=text;
}
