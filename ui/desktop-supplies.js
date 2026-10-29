'use strict';
const desktopResourceNames={berry:'浆果',leaf:'嫩叶',tea_leaf:'茶叶',seed:'种子',apple:'小苹果',flour:'面粉',water:'清水',butter:'黄油',milk:'牛奶',sugar:'糖'};
function defaultDesktopRegions(){return [
 {id:'windowsill',name:'窗边',left:.05,top:.2,right:.35,bottom:.75,resources:['berry','leaf','tea_leaf'],capacity:2,refresh_seconds:120},
 {id:'garden',name:'种植角',left:.65,top:.25,right:.95,bottom:.75,resources:['seed','apple'],capacity:2,refresh_seconds:150},
 {id:'supplies',name:'补给角',left:.35,top:.65,right:.65,bottom:.92,resources:['flour','water','butter','milk','sugar'],capacity:2,refresh_seconds:180}
];}
function appendDesktopResources(card,p){
 const section=el('section','desktop-resources');section.append(el('h4','','食材区域'));
 section.append(el('p','helper','食材可收进八格背包，也可存入食堂共用仓库。需要启用小食堂。收集、存取不会刷新新鲜时间；普通装饰仍可出现。区域按整个桌面的百分比定位，重叠处归列表靠前的区域。'));
 const enabled=el('div','form-grid');toggle(enabled,'按区域出现食材',p.resourceSpawn??p.autoSpawn!==false,v=>p.resourceSpawn=v);section.append(enabled);
 const list=el('div','resource-region-list');section.append(list);
 function render(){list.replaceChildren();const regions=p.regions||defaultDesktopRegions();
  regions.forEach((region,index)=>{const box=el('div','resource-region'),grid=el('div','form-grid');box.append(el('strong','',region.name||region.id));
   const edit=(key,value)=>{if(!p.regions)p.regions=structuredClone(regions);p.regions[index][key]=value;};
   field(grid,'区域名称',region.name,v=>edit('name',v));
   for(const [key,label] of [['left','左边界（%）'],['top','上边界（%）'],['right','右边界（%）'],['bottom','下边界（%）']]){const input=field(grid,label,Math.round(region[key]*100),v=>edit(key,Number(v)/100));input.type='number';input.min=0;input.max=100;input.step=1;}
   const cap=field(grid,'区域最多物件',region.capacity,v=>edit('capacity',Number(v)));cap.type='number';cap.min=1;cap.max=8;
   const refresh=field(grid,'补充检查间隔（秒）',region.refresh_seconds,v=>edit('refresh_seconds',Number(v)));refresh.type='number';refresh.min=30;refresh.max=43200;
   box.append(grid);const resources=el('div','resource-options');for(const [id,name] of Object.entries(desktopResourceNames))toggle(resources,name,region.resources.includes(id),v=>{const set=new Set((p.regions||regions)[index].resources);v?set.add(id):set.delete(id);edit('resources',[...set]);});box.append(resources);
   const remove=el('button','quiet','移除此区域');remove.type='button';remove.disabled=regions.length<=1;remove.addEventListener('click',()=>{p.regions=structuredClone(p.regions||regions);p.regions.splice(index,1);render();});box.append(remove);list.append(box);
  });
 }
 render();const add=el('button','quiet','添加区域');add.type='button';add.addEventListener('click',()=>{const regions=p.regions||defaultDesktopRegions();if(regions.length>=8){toast('最多设置八个区域。');return;}p.regions=structuredClone(regions);p.regions.push({id:'region-'+Date.now().toString(36),name:'新区域',left:.1,top:.2,right:.4,bottom:.7,resources:['berry'],capacity:1,refresh_seconds:180});render();});section.append(add);card.append(section);
}
