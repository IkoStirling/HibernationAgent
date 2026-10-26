"""Bake the application's original code-native pixel art into editable PNG packs.
No downloaded art or runtime Python dependency. Re-run after editing this authoring tool.
"""
from pathlib import Path
import json, re, struct, zlib, math, sys
root=Path(__file__).resolve().parents[1]
dest=Path(sys.argv[1]) if len(sys.argv)>1 else root/'plugins/widget/characters'
source=(root/'include/Hibernation/WidgetAppearance.h').read_text(encoding='utf-8-sig')
patterns=re.findall(r'static const char\* const rows\[\]=\{(.*?)\};',source)
colors=[0,0x305543,0x7ba769,0xd2dea3,0x192b21,0xafca92]
def png(path,width,height,pixels):
 def chunk(kind,data):return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data)&0xffffffff)
 raw=b''.join(b'\0'+bytes(v for pixel in pixels[y*width:(y+1)*width] for v in pixel) for y in range(height))
 path.write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',width,height,8,6,0,0,0))+chunk(b'IDAT',zlib.compress(raw,9))+chunk(b'IEND',b''))
def rgba(cell,alpha=255):
 c=colors[cell];return ((c>>16)&255,(c>>8)&255,c&255,alpha) if cell else (0,0,0,0)
for skin,rowsrc,top in zip(['moonbird','lumen','dinosaur'],patterns,[2,3,1]):
 folder=dest/skin;folder.mkdir(parents=True,exist_ok=True)
 base=[[0]*24 for _ in range(24)]
 for y,row in enumerate(re.findall(r'"([ 1-5]+)"',rowsrc)):
  for x,c in enumerate(row[:24]):
   if c!=' ':base[y+top][x]=int(c)
 def pose(state,frame,count):
  cells=[r[:] for r in base];phase=frame/max(1,count-1);bob=0;scale_y=1;lean=0
  eyes={'dinosaur':[(16,3)],'moonbird':[(7,8),(14,8)],'lumen':[(8,12),(11,12)]}[skin]
  if state=='resting' or (state=='idle' and frame==count-2):
   for x,y in eyes:
    cells[y][x]=3 if skin!='dinosaur' else 2
    for dx in [-1,0,1]:cells[min(23,y+1)][x+dx]=4
  if state=='walk':
   if skin=='dinosaur':
    for y in range(20,24):
     for x in range(6,16):cells[y][x]=0
    for leg,x in enumerate([8,12]):
     shift=round(math.sin(phase*math.tau+leg*math.pi)*2)
     for dx,dy,c in [(0,0,1),(0,1,2),(0,2,1),(1,2,1)]:cells[20+dy][x+shift+dx]=c
   elif skin=='moonbird':
    for y in range(20,24):
     for x in range(6,15):cells[y][x]=0
    for leg,x in enumerate([8,12]):
     y=21+int((frame//2+leg)%2);cells[y][x]=4;cells[y][x+1]=4
   else:lean=math.sin(phase*math.tau)*.05
  if state=='dragged':
   lean=math.sin(phase*math.tau)*.08
   for y in range(20,24):
    for x in range(7,15):cells[y][x]=0
   for x in [8,12]:
    for y in [20,21,22]:cells[y][x+int(math.sin(phase*math.tau))]=1 if y!=21 else 2
  if state in ['curious','listening','receiving','preparing']:lean=math.sin(phase*math.pi)*(.09 if state!='preparing' else -.09)
  if state=='idle':
   bob=int(frame in [3,4]);
   if skin=='dinosaur':
    tail=[(x,y,cells[y][x]) for y in range(7,17) for x in range(5) if cells[y][x]]
    for x,y,c in tail:cells[y][x]=0
    for x,y,c in tail:cells[max(0,min(23,y+(1 if frame>=count//2 else 0)))][x]=c
  if state=='landing':scale_y=.77+.23*phase
  if state=='resting':scale_y=.64+(frame%3)*.015
  if state=='sleep_start':scale_y=1-.36*phase
  if state=='sleep_end':scale_y=.64+.36*phase
  if state=='eating':
   mouth={'dinosaur':(18,7),'moonbird':(10,12),'lumen':(10,15)}[skin]
   if frame%4<2:
    for dy in [0,1]:
     for dx in [0,1]:cells[mouth[1]+dy][mouth[0]+dx]=4
  out=[(0,0,0,0)]*1024
  for x in range(10,23):out[30*32+x]=rgba(5,70)
  for y in range(24):
   for x in range(24):
    if not cells[y][x]:continue
    xx=round(4+x+(12-y)*lean);yy=round(29+(y-23)*scale_y+bob)
    if 0<=xx<32 and 0<=yy<32:out[yy*32+xx]=rgba(cells[y][x])
  return out
 def sheet(name,state,count,ms=110,hand=False,front=False):
  frames=[pose(state,i,count) for i in range(count)]
  pixels=[frames[x//32][y*32+x%32] for y in range(32) for x in range(count*32)]
  png(folder/(name+'.png'),count*32,32,pixels)
  result=[{'image':name+'.png','rect':[i*32,0,32,32],'ms':ms} for i in range(count)]
  if hand:
   for i,f in enumerate(result):f['hand']=[25,21+(i%2)];f['front']={'image':'front.png'}
  return result
 states={}
 for state in ['idle','walk','dragged','curious','listening','receiving','preparing','eating','speaking','resting','landing']:
  start=[];end=[]
  if state=='resting':start=sheet('sleep_start','sleep_start',6,70);end=sheet('sleep_end','sleep_end',6,70)
  if state=='landing':start=sheet('landing_start','landing',4,65)
  states[state]=[{'id':state+'_main','weight':3,'start':start,'loop':sheet(state,state,8,130 if state=='resting' else 110,hand=state=='eating'),'end':end}]
 states['idle'].append({'id':'look_around','weight':1,'loop':sheet('idle_look','curious',10,140)})
 mouth=[(0,0,0,0)]*1024;mx,my={'dinosaur':(23,13),'moonbird':(14,18),'lumen':(14,21)}[skin]
 for dy in [0,1]:
  for dx in [0,1]:mouth[(my+dy)*32+mx+dx]=rgba(4)
 png(folder/'mouth.png',32,32,mouth)
 front=[(0,0,0,0)]*1024
 for x,y,c in [(22,19,1),(23,19,2),(24,20,2),(25,20,1),(23,20,1),(24,21,1)]:front[y*32+x]=rgba(c)
 png(folder/'front.png',32,32,front)
 manifest={'version':1,'id':skin+'-pixel','canvas':[32,32],'anchor':[16,32],'hitbox':[4,5,29,30],'palette':'builtin','states':states,'fallbacks':{'thinking':'preparing','planting':'curious'},'mouth_open':{'image':'mouth.png'}}
 (folder/'character.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print('Built 3 original PNG animation packs:',dest)
