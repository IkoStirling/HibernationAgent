'use strict';
// Small application-authored recipes, never generated on behalf of the role.
// Definitions are copied before use so editor state cannot change the recipe.
const pixelCanvasExampleRecipes=(()=>{
 const palette=['#00000000','#293438FF','#F7F5ECFF','#5D7F67FF','#A1C18BFF','#E5BE63FF','#FFF0B2FF','#C78F61FF','#8BAAC4FF','#8E6B4EFF','#F4D89680'];
 const line=(x,y,x2,y2,color)=>({op:'line',x,y,x2,y2,color});
 const rect=(x,y,x2,y2,color)=>({op:'rect',x,y,x2,y2,color,filled:true});
 const dot=(x,y,color)=>({op:'pixel',x,y,color});
 const frame=(duration_ms,commands)=>({duration_ms,commands});
 const pot=[rect(4,11,11,14,1),rect(5,11,10,13,7),line(4,10,11,10,1),line(5,10,10,10,5),line(7,5,7,10,3),line(8,6,8,9,4)];
 const star=[rect(7,2,8,13,5),rect(2,7,13,8,5),rect(5,5,10,10,5),line(4,6,4,9,5),line(11,6,11,9,5),line(6,4,9,4,5),line(6,11,9,11,5),rect(7,6,8,9,6),dot(6,7,6)];
 return Object.freeze({
  lamp:{label:'小灯 · 静态',description:'16×16的桌边小灯，1帧；可试着更换灯罩颜色。',width:16,height:16,palette,frames:[frame(500,[rect(5,3,10,6,5),line(5,2,10,2,1),line(4,3,4,4,1),line(3,5,3,6,1),line(11,3,11,4,1),line(12,5,12,6,1),line(3,7,12,7,1),line(4,5,4,6,5),line(11,5,11,6,5),line(6,3,6,5,6),line(7,8,7,12,1),line(8,8,8,12,9),rect(4,13,11,14,1),line(5,13,10,13,5)])]},
  sprout:{label:'嫩芽 · 两帧',description:'16×16的盆栽嫩芽，2帧轻摆；可以继续画出新的叶片。',width:16,height:16,palette,frames:[
   frame(650,[...pot,rect(3,4,6,5,3),line(4,3,6,3,4),line(5,5,7,7,4),rect(9,3,12,4,3),line(9,2,11,2,4),line(8,6,10,4,3)]),
   frame(650,[...pot,rect(2,4,5,5,3),line(3,3,5,3,4),line(5,5,7,7,4),rect(9,2,11,3,3),dot(12,3,3),line(9,4,10,4,4),line(8,6,10,4,3)])]},
  star:{label:'闪烁星 · 三帧',description:'16×16的小星星，3帧闪光；可调整帧时长或添加自己的细节。',width:16,height:16,palette,frames:[
   frame(450,[...star,dot(1,3,10),dot(14,11,10)]),
   frame(250,[...star,dot(1,2,6),dot(2,1,6),dot(13,3,6),dot(14,4,6),dot(12,13,6)]),
   frame(350,[...star,dot(2,3,6),dot(13,12,6),dot(14,11,6),dot(1,10,10)])]}
 });
})();
function pixelCanvasExampleCatalog(){return Object.entries(pixelCanvasExampleRecipes).map(([id,value])=>({id,label:value.label,description:value.description}));}
function pixelCanvasExampleDefinition(id){if(typeof id!=='string'||!Object.hasOwn(pixelCanvasExampleRecipes,id))return null;const value=pixelCanvasExampleRecipes[id];return JSON.parse(JSON.stringify({width:value.width,height:value.height,palette:value.palette,frames:value.frames}));}
