-- SPDX-License-Identifier: MIT
local R=require('crest.renderer')
local C=require('crest.config')
local E=require('crest.engine')
local tests,assertions=0,0
local function check(value,message) assertions=assertions+1; assert(value,message or 'check failed') end
local function equal(a,b,message) check(a==b,(message or 'unexpected value')..': '..tostring(a)..' ~= '..tostring(b)) end
local function near(a,b,message) check(math.abs(a-b)<0.00001,message or (tostring(a)..' ~= '..tostring(b))) end
local function test(name,fn) local ok,why=pcall(fn); assert(ok,name..': '..tostring(why)); tests=tests+1 end
local function copy(t)
  if type(t)~='table' then return t end
  local out={}; for k,v in pairs(t) do out[k]=copy(v) end; return out
end
local function measure(text,size)
  local count=assert(utf8.len(text))
  local w=count*size*0.53
  return {width=w,height=size,ink_x0=count>0 and -size*0.15 or 0,ink_y0=-size*0.12,
    ink_x1=w+size*0.18,ink_y1=size*1.17}
end
local metrics={measure=measure}
local function style()
  return {name='Obsidian Warden',subtitle='Lv 65',width=142,height=12,mana_height=4,cast_height=9,
    text_color=0xf2f5fa,border_color=0xf9df86,background_color=0x111823,
    health={value=72,color=0x55b9ee,text='72%'},mana={value=36,color=0x4d94ff,text=''},
    cast={value=55,color=0xf4bb55,text='Spell 1.2s'},badges={{icon='eye',label='Alert',color=0xffffff},
      {icon='peace',label='Calm',color=0x55d69a},{icon='star',label='1',color=0xf9df86}}}
end
local context={target=true,name='Fallback',font_size=14,font=3}
local known={Rect=true,RectFilled=true,Gradient=true,Line=true,Circle=true,CircleFilled=true,
  Triangle=true,TriangleFilled=true,Text=true,Image=true}
local function inside(b,x,y,why)
  check(x>=b.x-0.00001 and x<=b.x+b.w+0.00001,(why or '')..' outside X bounds')
  check(y>=b.y-0.00001 and y<=b.y+b.h+0.00001,(why or '')..' outside Y bounds')
end
-- Recompute geometry from public primitive fields and independent font metrics;
-- do not trust the renderer's per-command convenience bounds in these checks.
local function verify(model)
  check(type(model)=='table'); check(model.bounds.w>=0 and model.bounds.h>=0)
  check(model.bounds.w<=4096 and model.bounds.h<=4096); check(#model.commands<=R.MAX_COMMANDS)
  check(R.vertex_cost(model.commands)<=R.MAX_MODEL_VERTICES)
  for _,c in ipairs(model.commands) do
    check(known[c.kind],'unknown primitive '..tostring(c.kind))
    check(c.alpha>=0 and c.alpha<=1); check(c.color>=0 and c.color<=0xffffff)
    local fringe=1+(c.thickness or 0)*0.5
    if c.kind=='Text' then
      local m=measure(c.text,c.size)
      inside(model.bounds,c.x+m.ink_x0,c.y+m.ink_y0,c.text)
      inside(model.bounds,c.x+m.ink_x1,c.y+m.ink_y1,c.text)
      check(utf8.len(c.text)~=nil); check(not c.text:find('[%c]'))
    elseif c.kind=='Circle' or c.kind=='CircleFilled' then
      inside(model.bounds,c.x-c.radius-fringe,c.y-c.radius-fringe,c.kind)
      inside(model.bounds,c.x+c.radius+fringe,c.y+c.radius+fringe,c.kind)
    elseif c.kind=='Triangle' or c.kind=='TriangleFilled' then
      for i=0,2 do inside(model.bounds,c['x'..i]-fringe,c['y'..i]-fringe,c.kind)
        inside(model.bounds,c['x'..i]+fringe,c['y'..i]+fringe,c.kind) end
    elseif c.kind=='Image' then
      inside(model.bounds,c.x0,c.y0,c.kind); inside(model.bounds,c.x1,c.y1,c.kind)
      check(c.texture~=nil); check(c.u0>=0 and c.u1<=1 and c.v0>=0 and c.v1<=1)
    else
      inside(model.bounds,math.min(c.x0,c.x1)-fringe,math.min(c.y0,c.y1)-fringe,c.kind)
      inside(model.bounds,math.max(c.x0,c.x1)+fringe,math.max(c.y0,c.y1)+fringe,c.kind)
    end
  end
end
local function prepare(s,ctx,m) return R.prepare(s or style(),ctx or context,m or metrics) end
local function texts(model,text)
  local out={}; for _,c in ipairs(model.commands) do if c.kind=='Text' and c.text==text and c.color~=0 then out[#out+1]=c end end
  return out
end
local function signature(model)
  local values={}
  for _,c in ipairs(model.commands) do
    values[#values+1]=table.concat({c.kind,c.x or c.x0 or 0,c.y or c.y0 or 0,c.x1 or 0,c.y1 or 0,c.radius or 0,c.rounding or 0},':')
  end
  return table.concat(values,';')
end

test('name decoration survives narrow layouts and suffixes fall back to a text-only tag',function()
  local s=style()
  s.name_prefix,s.name_suffix='(',')'
  s.width=66
  local p=prepare(s)
  local wrapped=false
  for _,c in ipairs(p.commands) do
    if c.kind=='Text' and c.color==s.text_color and c.text:match('^%(.+%.%.%.%)$') then wrapped=true end
  end
  check(wrapped)
  s.name_prefix,s.name_suffix=nil,' [INV+IVU]'
  s.name_fallback_badge={text_only=true,label='INV+IVU',color=0xffd989}
  s.name_position='inside'; s.width=40; s.badges={}
  p=prepare(s)
  verify(p)
  equal(#texts(p,'INV+IVU'),1)
  for _,c in ipairs(p.commands) do check(c.kind~='Circle' and c.kind~='CircleFilled') end
end)

test('native fallback, explicit hiding and metric failures are transactional',function()
  equal(R.prepare(nil,context,metrics),nil)
  equal(R.prepare(false,context,metrics),nil)
  equal(R.prepare(style(),context,{}),nil)
  equal(R.prepare(style(),context,{measure=function() error('font unavailable') end}),nil)
  equal(R.prepare(style(),context,{measure=function() return {width=0/0,height=14} end}),nil)
  equal(R.prepare(style(),context,{measure=function() return {width=10,height=14,ink_x0=0,ink_x1=-1} end}),nil)
  local hidden=R.prepare({hide=true},context,{})
  check(hidden.hide); equal(#hidden.commands,0); check(not hidden.clickable)
  local s=style(); s.alpha=0; check(prepare(s).hide)
end)

test('glyph ink, text shadow and positive vertical offset fit exact measured bounds',function()
  local s=style(); s.name='jéW'; s.badges={}; s.health=nil; s.mana=nil; s.cast=nil
  local p=prepare(s); verify(p)
  near(p.bounds.y+p.bounds.h,0)
  s.offset_y=17; local raised=prepare(s); verify(raised)
  near(raised.bounds.y+raised.bounds.h,-17)
  near(p.bounds.w,raised.bounds.w); near(p.bounds.h,raised.bounds.h)
  local main=texts(p,'jéW')[1]; check(main~=nil)
  check(main.x+measure('jéW',main.size).ink_x0>=-s.width/2)
  check(main.x+measure('jéW',main.size).ink_x1<=s.width/2)
  local sum
  for _,command in ipairs(p.commands) do
    local b=command.bounds
    if not sum then sum=copy(b) else
      local right,bottom=math.max(sum.x+sum.w,b.x+b.w),math.max(sum.y+sum.h,b.y+b.h)
      sum.x=math.min(sum.x,b.x); sum.y=math.min(sum.y,b.y); sum.w=right-sum.x; sum.h=bottom-sum.y
    end
  end
  for _,key in ipairs({'x','y','w','h'}) do near(sum[key],p.bounds[key]) end
end)

test('all fourteen shipped presets render through the same addon painter',function()
  equal(#C.presets,14)
  for _,name in ipairs(C.presets) do
    local settings=C.preset(name)
    local spawn={id=7,name='Obsidian_Warden001',npc=name~='Healer',class=1,level=65}
    local ctx={name='Obsidian Warden',target=true,hp_percent=72,mana_percent=36,distance=15,now_ms=0,
      casting={name='Spell',elapsed_ms=550,duration_ms=1000},aggro_percent=76}
    local p=E.new():presentation(spawn,ctx,settings,{server_indicators=name=='Classic Names' and {} or {{icon='eye',color=0xffffff}}})
    local rendered=prepare(p); check(not rendered.hide,name); verify(rendered)
    check(#rendered.commands>0,name)
    if name=='Classic Names' then for _,command in ipairs(rendered.commands) do equal(command.kind,'Text') end end
  end
end)

test('frame designs, fill textures and target decorations have distinct geometry',function()
  local seen={}
  for _,frame in ipairs({'minimal','solid','bevel','glass','runic'}) do
    local s=style(); s.frame_style=frame; local p=prepare(s); verify(p)
    local key=signature(p); check(not seen[key],frame..' has duplicate geometry'); seen[key]=true
  end
  seen={}
  for _,texture in ipairs({'flat','gradient','gloss','segments'}) do
    local s=style(); s.texture=texture; local p=prepare(s); verify(p)
    local key=signature(p); check(not seen[key],texture..' has duplicate geometry'); seen[key]=true
  end
  seen={}
  for _,trim in ipairs({'none','arrow','chevrons','brackets','glow'}) do
    local s=style(); s.target_style=trim; local p=prepare(s); verify(p)
    local key=signature(p); check(not seen[key],trim..' has duplicate geometry'); seen[key]=true
    if trim~='none' then local off=prepare(s,{target=false,font_size=14,font=3}); check(signature(off)~=key) end
  end
end)

test('name placement, independent health and cast alignment and detached cast width',function()
  local s=style(); s.badges={}; s.subtitle=''; s.health.text='HP'; s.cast.text='CAST'; s.name='NAME'
  local above=prepare(s); local name_above=texts(above,'NAME')[1]; local hp=texts(above,'HP')[1]
  check(name_above.y<hp.y)
  s.name_position='below'; local below=prepare(s)
  check(texts(below,'NAME')[1].y>texts(below,'CAST')[1].y)
  s.name_position='inside'; s.text_align='left'; s.health_text_align='right'
  local inset=prepare(s); check(texts(inset,'NAME')[1].x<texts(inset,'HP')[1].x); verify(inset)
  s.name_position='above'
  for _,field in ipairs({'health_text_align','cast_text_align'}) do
    local label=field=='health_text_align' and 'HP' or 'CAST'; local positions={}
    for _,align in ipairs({'left','center','right'}) do s[field]=align; positions[#positions+1]=texts(prepare(s),label)[1].x end
    check(positions[1]<positions[2] and positions[2]<positions[3],field)
  end
  s.cast_width=80; s.cast_gap=20; s.texture='flat'
  local wide_gap=prepare(s); verify(wide_gap)
  local cast_bg
  for _,command in ipairs(wide_gap.commands) do
    if command.kind=='RectFilled' and command.color==s.background_color and math.abs(command.x1-command.x0-80)<0.001 then cast_bg=command end
  end
  check(cast_bg~=nil,'cast width was not independent')
  s.cast_gap=0; local small_gap=prepare(s)
  near(wide_gap.bounds.h-small_gap.bounds.h,20)
end)

test('badges cover every icon and all placements, with local spell art and safe fallback',function()
  local icons={'eye','peace','sleep','star','sun','moon','diamond','triangle','cross','skull','sword','circle'}
  for _,placement in ipairs({'above','below','left','right'}) do for _,token in ipairs(icons) do
    local s=style(); s.badge_position=placement; s.badges={{icon=token,label='Control',color=0xffaa44}}
    verify(prepare(s))
  end end
  local s=style(); s.badges={{icon='eye',spell_icon=19,color=0xffaa44}}
  local seen
  local p=prepare(s,context,{measure=measure,texture=function(index) equal(index,19); return {texture=123,u0=0.2,v0=0.3,u1=0.4,v1=0.6} end})
  for _,command in ipairs(p.commands) do if command.kind=='Image' then seen=command end end
  check(seen~=nil); equal(seen.texture,123); near(seen.u0,0.2); near(seen.v1,0.6); verify(p)
  for _,lookup in ipairs({function() return nil end,function() return {texture=0} end,function() error('retired texture') end}) do
    local fallback=prepare(s,context,{measure=measure,texture=lookup}); verify(fallback)
    for _,command in ipairs(fallback.commands) do check(command.kind~='Image') end
    check(#fallback.commands>#p.commands,'vector fallback was not drawn')
  end
end)

test('known zero resources remain framed while unknown values create no fabricated bar',function()
  local s=style(); s.health=nil; s.cast=nil; s.badges={}; s.show_name=false; s.subtitle=''
  s.mana={value=0,color=0x121314,text=''}
  local zero=prepare(s); check(not zero.hide); verify(zero)
  for _,command in ipairs(zero.commands) do check(command.color~=0x121314,'zero generated a positive fill') end
  s.mana=nil; check(prepare(s).hide)
  s.mana={value=0/0,color=0x121314}; check(prepare(s).hide)
  s.mana={value=36,color=0x121314,text=''}; s.mana_height=2; s.border_width=4
  local p=prepare(s); local fill
  for _,command in ipairs(p.commands) do if command.color==0x121314 then fill=command end end
  check(fill~=nil,'thick borders erased known mana'); check(fill.y1-fill.y0>=1); verify(p)
end)

test('opacity reaches fills, frame, font shadows, images and target glow',function()
  local s=style(); s.alpha=0.25; s.frame_style='glass'; s.texture='gloss'; s.target_style='glow'
  s.badges={{icon='circle',spell_icon=4}}
  local p=prepare(s,{target=true,font_size=14,alpha=0.5},{measure=measure,texture=function() return {texture=123} end})
  local image,fill,shadow=false,false,false
  for _,command in ipairs(p.commands) do
    check(command.alpha<=0.125+1e-9)
    if command.kind=='Image' then near(command.alpha,0.125); image=true end
    if command.kind=='Gradient' then near(command.alpha,0.125); near(command.alpha2,0.125); fill=true end
    if command.kind=='Text' and command.color==0 then near(command.alpha,0.125*0.82); shadow=true end
  end
  check(image and fill and shadow)
end)

test('narrow plates, huge fonts, eight badges and all structural layouts stay bounded',function()
  local icons={'eye','peace','star','sun','moon','skull','sword','diamond'}
  for _,frame in ipairs({'minimal','solid','bevel','glass','runic'}) do
    for _,position in ipairs({'above','inside','below'}) do for _,placement in ipairs({'above','below','left','right'}) do
      local s=style(); s.frame_style=frame; s.name_position=position; s.badge_position=placement
      s.width=24; s.cast_width=240; s.font_scale=2; s.badge_scale=2.5; s.border_width=4
      s.name=string.rep('éWj',60); s.subtitle=string.rep('S',256); s.badges={}
      for _,token in ipairs(icons) do s.badges[#s.badges+1]={icon=token,label=string.rep('jé',24),color=0xffffff} end
      local p,why=prepare(s,{target=true,font_size=32,font=3}); check(p~=nil,why); verify(p)
      equal(#texts(p,s.name),0,'long name escaped fitting')
    end end
  end
end)

test('presentation flags and settings are copied without mutating the input model',function()
  local s=style(); s.clickable=false; s.through_walls=true; s.stack=true; s.stack_gap=9; s.max_stack=44
  local before=copy(s); local p=prepare(s)
  equal(p.clickable,false); equal(p.through_walls,true); equal(p.stack,true); equal(p.stack_gap,9); equal(p.max_stack,44)
  equal(s.health.value,before.health.value); equal(s.name,before.name); equal(s.badges[1].label,before.badges[1].label)
  s.show_name=false; s.subtitle=''; s.health.text=''; s.cast.text=''; s.badges={}
  local hidden_name=prepare(s); for _,command in ipairs(hidden_name.commands) do check(command.kind~='Text') end
  local hostile=setmetatable({},{__index=function() error('metatable executed') end})
  local safe=R.prepare(hostile,context,metrics); check(safe~=nil)
end)

test('font sizes stay inside the generic canvas contract at tiny and giant scales',function()
  local function strict_measure(text,size,font)
    check(size>=4 and size<=256,'text size outside public canvas range')
    return measure(text,size,font)
  end
  for _,scale in ipairs({0.01,0.1,0.5,1,4,16}) do
    local s=style(); s.scale=scale; s.font_scale=scale; s.badge_scale=scale
    local p=prepare(s,{target=true,scale=scale,font_size=4,font=3},{measure=strict_measure})
    check(p~=nil); verify(p)
    for _,command in ipairs(p.commands) do
      if command.kind=='Text' then check(command.size>=4 and command.size<=256) end
      if command.thickness then check(command.thickness>=0.01 and command.thickness<=64) end
    end
  end
end)

local function stacked_model(maximum)
  return {bounds={x=-40,y=-20,w=80,h=20},commands={},stack=true,stack_gap=4,max_stack=maximum or 96}
end
local viewport={x=0,y=0,w=800,h=600}
local function screen_bounds(r)
  local b=r.model.bounds; return {x=r.x+b.x,y=r.y+b.y+r.offset_y,w=b.w,h=b.h}
end
local function separated(a,b,gap)
  return a.x>=b.x+b.w+gap-0.00001 or b.x>=a.x+a.w+gap-0.00001 or a.y>=b.y+b.h+gap-0.00001 or b.y>=a.y+a.h+gap-0.00001
end

test('stacking pins targets, prioritizes depth then identity and clears complete bounds',function()
  local records={
    {id=9,x=400,y=400,depth=20,model=stacked_model()},
    {id=5,x=400,y=400,depth=10,model=stacked_model()},
    {id=2,x=400,y=400,depth=10,model=stacked_model()},
    {id=99,x=400,y=400,target=true,depth=50,model=stacked_model()},
  }
  equal(R.stack(records,viewport),records)
  equal(records[4].offset_y,0); near(records[3].offset_y,-24); near(records[2].offset_y,-48); near(records[1].offset_y,-72)
  for i=1,#records do check(not records[i].hidden)
    for j=i+1,#records do check(separated(screen_bounds(records[i]),screen_bounds(records[j]),4)) end
  end
  -- Array order is never repurposed as render order; equivalent input identities
  -- resolve identically when the host candidate enumeration changes.
  local reversed={records[4],records[3],records[2],records[1]}; R.stack(reversed,viewport)
  near(records[3].offset_y,-24); near(records[2].offset_y,-48); near(records[1].offset_y,-72)
end)

test('stacking crosses connected obstruction intervals but does not move unstacked plates',function()
  local pinned=stacked_model(); pinned.stack=false
  local records={{id=1,x=400,y=400,depth=1,model=pinned},
    {id=2,x=400,y=375,depth=2,model=pinned},{id=3,x=400,y=400,depth=3,model=stacked_model()}}
  R.stack(records,viewport)
  equal(records[1].offset_y,0); equal(records[2].offset_y,0); near(records[3].offset_y,-49)
  check(not records[3].hidden)
  records[3].model=stacked_model(24); R.stack(records,viewport); check(records[3].hidden)
  records[3].model=stacked_model(200); records[3].y=10; R.stack(records,viewport); check(records[3].hidden)
end)

test('stacking caps movable candidates, rejects stale geometry and restores per-frame state',function()
  local records={}
  for i=1,300 do records[i]={id=i,x=(i%10)*70+50,y=550,depth=i,model=stacked_model(200)} end
  R.stack(records,viewport)
  for i=257,300 do check(records[i].hidden) end
  local r=records[1]; r.hidden=true; r.offset_y=-999; r.model.stack=false
  R.stack({r},viewport); equal(r.hidden,false); equal(r.offset_y,0)
  R.stack({{x=0/0,y=5,model=stacked_model()},false},viewport)
  local bad={x=400,y=400,model={bounds={x=0,y=0,w=math.huge,h=1}}}
  R.stack({bad},viewport); check(bad.hidden)
end)

print(string.format('Addon renderer: %d tests, %d assertions passed',tests,assertions))
