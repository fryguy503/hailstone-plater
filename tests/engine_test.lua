-- SPDX-License-Identifier: MIT
local C=require('crest.config')
local E=require('crest.engine')
local tests,assertions=0,0
local function check(value,message) assertions=assertions+1; assert(value,message or 'check failed') end
local function equal(a,b,message) check(a==b,(message or 'unexpected value')..': '..tostring(a)..' ~= '..tostring(b)) end
local function test(name,fn) local ok,why=pcall(fn); assert(ok,name..': '..tostring(why)); tests=tests+1 end
local function fixture()
  return E.new(),{id=7,name='#Guardian000',npc=true,class=1,level=65},
    {name='Guardian',distance=10,now_ms=0,hp_percent=60},C.repair(),{}
end
local function presentation(changes)
  local e,spawn,ctx,s,extra=fixture()
  if changes then changes(spawn,ctx,s,extra) end
  return e:presentation(spawn,ctx,s,extra),s
end

test('disabled replacement returns the native fallback',function()
  local p=presentation(function(_,_,s) s.enabled=false end); equal(p,nil)
end)

test('every entity category has an independent visibility filter',function()
  local cases={
    {'npc','show_npcs',{},{}}, {'player','show_players',{npc=false},{}},
    {'group','show_group',{npc=false},{in_group=true}}, {'pet','show_pets',{owner=42},{}},
    {'mercenary','show_mercenaries',{mercenary=true},{}},
    {'corpse','show_corpses',{corpse=true},{}}, {'self','show_self',{}, {self=true}},
  }
  for _,case in ipairs(cases) do
    local e,spawn,ctx,s,extra=fixture()
    for k,v in pairs(case[3]) do spawn[k]=v end
    for k,v in pairs(case[4]) do ctx[k]=v end
    equal(E.category(spawn,ctx),case[1])
    s[case[2]]=false; check(e:presentation(spawn,ctx,s,extra).hide)
    s[case[2]]=true; e:reset(); check(not e:presentation(spawn,ctx,s,extra).hide)
  end
end)

test('distance, target, combat and full-health visibility rules',function()
  check(presentation(function(_,ctx,s) ctx.distance=s.max_distance+1 end).hide)
  check(presentation(function(_,_,s) s.target_only=true end).hide)
  check(not presentation(function(_,ctx,s) s.target_only=true; ctx.target=true end).hide)
  check(presentation(function(_,_,s) s.combat_only=true end).hide)
  check(not presentation(function(_,_,s,x) s.combat_only=true; x.combat=true end).hide)
  check(presentation(function(_,ctx,s) s.injured_only=true; ctx.hp_percent=100 end).hide)
  check(not presentation(function(_,ctx,s) s.injured_only=true; ctx.hp_percent=100; ctx.target=true end).hide)
end)

test('unknown HP and resources are absent, valid zero percentages remain visible',function()
  local p=presentation(function(_,ctx) ctx.hp_percent=nil end)
  equal(p.health,nil); equal(p.mana,nil)
  p=presentation(function(_,ctx) ctx.hp_percent=0; ctx.mana_percent=0 end)
  equal(p.health.value,0); equal(p.mana.value,0)
  for _,invalid in ipairs({math.huge,-math.huge,0/0}) do
    p=presentation(function(_,ctx) ctx.hp_percent=invalid; ctx.mana_percent=invalid end)
    equal(p.health,nil); equal(p.mana,nil)
  end
  p=presentation(function(_,ctx,s) s.resource='endurance'; ctx.mana_percent=60; ctx.endurance_percent=40 end)
  equal(p.mana.value,40)
  p=presentation(function(_,ctx,s) s.resource='off'; ctx.mana_percent=60; ctx.endurance_percent=40 end)
  equal(p.mana,nil)
end)

test('HP pool text applies only to self and known percentages remain honest',function()
  local p=presentation(function(_,ctx,s,x)
    ctx.self=true; s.show_self=true; s.health_text='both'; x.current_hp=123; x.max_hp=456
  end)
  check(p.health.text:find('123 / 456',1,true)); check(p.health.text:find('60%',1,true))
  p=presentation(function(_,_,s,x) s.health_text='current'; x.current_hp=123; x.max_hp=456 end)
  equal(p.health.text,'60%')
end)

test('corpses do not receive health/resource/cast bars',function()
  local p=presentation(function(spawn,ctx,s)
    spawn.corpse=true; s.show_corpses=true; ctx.mana_percent=50
    ctx.casting={name='Stale cast',elapsed_ms=100,duration_ms=1000}
  end)
  equal(p.health,nil); equal(p.mana,nil); equal(p.cast,nil)
end)

test('observed casts stop at expiry and support reverse progress',function()
  local function cast(ctx) ctx.casting={name='Observed spell',elapsed_ms=500,duration_ms=2000} end
  local p=presentation(function(_,ctx) cast(ctx) end)
  equal(p.cast.value,25); check(p.cast.text:find('Observed spell',1,true)); check(p.cast.text:find('1.5s',1,true))
  p=presentation(function(_,ctx,s) cast(ctx); s.cast_reverse=true end); equal(p.cast.value,75)
  for _,bad in ipairs({{duration_ms=0,elapsed_ms=0},{duration_ms=1000,elapsed_ms=-1},
    {duration_ms=1000,elapsed_ms=1000},{duration_ms=math.huge,elapsed_ms=0},
    {duration_ms=1000,elapsed_ms=0/0}}) do
    p=presentation(function(_,ctx) ctx.casting=bad end); equal(p.cast,nil)
  end
end)

test('name rules match literal text, respect entity kind and use first match',function()
  local p=presentation(function(_,_,s)
    s.rules={C.rule{name='guardian',exact=true,color=0x123456,scale=1.5},C.rule{name='Guardian',hide=true}}
  end)
  check(not p.hide); equal(p.health.color,0x123456); equal(p.scale,1.5)
  p=presentation(function(spawn,_,s) spawn.name='AxB'; s.rules={C.rule{name='A.B',hide=true}} end)
  check(not p.hide)
  p=presentation(function(spawn,_,s) spawn.name='A.B'; s.rules={C.rule{name='A.B',hide=true}} end)
  check(p.hide)
  p=presentation(function(_,_,s) s.rules={C.rule{name='Guardian',kind='player',hide=true}} end)
  check(not p.hide)
  p=presentation(function(_,_,s) s.rules={C.rule{name='Guardian',enabled=false,hide=true}} end)
  check(not p.hide)
end)

test('friendly names-only mode removes player bars',function()
  local p=presentation(function(spawn,ctx,s)
    spawn.npc=false; s.friendly_names_only=true; ctx.mana_percent=50
    ctx.casting={name='Observed spell',elapsed_ms=100,duration_ms=1000}
  end)
  equal(p.health,nil); equal(p.mana,nil); equal(p.cast,nil); check(p.show_name)
end)

test('independent unit names-only mode removes health resources and observed cast bars',function()
  local p=presentation(function(_,ctx,s)
    s.npc_override=true; s.npc_names_only=true; ctx.mana_percent=50
    ctx.casting={name='Observed spell',elapsed_ms=100,duration_ms=1000}
  end)
  equal(p.health,nil); equal(p.mana,nil); equal(p.cast,nil); check(p.show_name)
end)

test('target emphasis, name truncation and percentage colors stay bounded',function()
  local p,s=presentation(function(spawn,ctx,settings)
    spawn.name='abcdefghijklmnopqrstuvwxyz'; ctx.name=spawn.name; ctx.target=true
    settings.max_name=8; settings.color_mode='health'; ctx.hp_percent=0
  end)
  equal(p.scale,s.scale*s.target_scale); equal(p.border_color,s.target_color)
  equal(p.name,'abcdefgh...'); equal(p.health.value,0)
  local utf8_name='éééééééééééé'
  p=presentation(function(spawn,ctx,settings) spawn.name=utf8_name; ctx.name=utf8_name; settings.max_name=8 end)
  check(utf8.len(p.name)~=nil,'UTF-8 name was cut mid-codepoint')
  for _,mode in ipairs({'consider','class','health','custom'}) do
    for _,hp in ipairs({0,1,25,50,75,100}) do
      p=presentation(function(_,ctx,settings,x) ctx.hp_percent=hp; settings.color_mode=mode; x.con_color=0xabcdef end)
      check(p.health.color>=0 and p.health.color<=0xffffff)
      check(p.alpha>=0.05 and p.alpha<=1)
    end
  end
end)

test('Server indicator toggles and badge capacity are independent',function()
  local tokens={'eye','sleep','peace','star','sun','moon','diamond','triangle','cross','skull','sword'}
  local p=presentation(function(_,_,s,x)
    x.server_indicators={}; for _,token in ipairs(tokens) do x.server_indicators[#x.server_indicators+1]={icon=token,label=token,color=0xffffff} end
    s.server_indicators_awareness=false; s.server_indicators_status=false
  end)
  equal(#p.badges,8)
  for _,b in ipairs(p.badges) do check(b.icon~='eye' and b.icon~='sleep' and b.icon~='peace') end
  p=presentation(function(_,_,s,x)
    s.server_indicators_marks=false; x.server_indicators={{icon='star'},{icon='eye'},{icon='sleep'},{icon='peace'}}
  end)
  equal(#p.badges,3)
end)

test('known aggro highlights only when configured',function()
  local p,s=presentation(function(_,ctx,settings) ctx.aggro_percent=95; settings.show_aggro=true; settings.highlight_aggro=true end)
  equal(p.border_color,s.aggro_color); equal(p.badges[1].label,'95%')
  p=presentation(function(_,_,settings) settings.show_aggro=true; settings.highlight_aggro=true end)
  equal(#p.badges,0)
end)

test('smoothing converges, snaps at zero, and resets reused or stale entries',function()
  local e=E.new()
  equal(e:smooth(1,'First','hp',100,0,100),100)
  local smooth=e:smooth(1,'First','hp',50,100,100)
  check(smooth>50 and smooth<100)
  equal(e:smooth(1,'First','hp',0,101,100),0)
  equal(e:smooth(1,'Changed','hp',80,102,100),80)
  equal(e:smooth(1,'Changed','hp',20,2000,100),20)
  equal(e:smooth(1,'Changed','hp',60,1000,100),60)
  equal(e:smooth(1,'Changed','hp',90,1001,0),90)
end)

test('smoothing cache is bounded, prunes old actors and can remove/reset',function()
  local e=E.new()
  for id=1,5000 do e:smooth(id,'Actor'..id,'hp',50,0,100) end
  check(e.count<=1024)
  local count=e.count; e:remove(5000); equal(e.count,count-1)
  e:prune(6001); equal(e.count,0)
  e:smooth(1,'Actor','hp',40,7000,100); e:reset(); equal(e.count,0); equal(next(e.cache),nil)
end)

test('independent unit styles inherit globally and update on explicit reset',function()
  local e,spawn,ctx,s,x=fixture()
  s.npc_override=true; s.npc_width=222; s.npc_height=23; s.npc_custom_color=0x123456
  s.npc_resource=false; s.npc_cast=false; ctx.mana_percent=55
  ctx.casting={name='Observed spell',duration_ms=1000,elapsed_ms=300}
  local p=e:presentation(spawn,ctx,s,x)
  equal(p.width,222); equal(p.height,23); equal(p.health.color,0x123456)
  equal(p.mana,nil); equal(p.cast,nil)
  s.npc_width=230; e:reset(); equal(e:presentation(spawn,ctx,s,x).width,230)
  spawn.npc=false; local other=e:presentation(spawn,ctx,s,x)
  equal(other.width,s.width); check(other.mana); check(other.cast)
end)

test('combat, rest and target dimensions compose with unit settings',function()
  local e,spawn,ctx,s,x=fixture()
  s.combat_width=260; s.combat_height=21; s.rest_width=190; s.rest_height=11
  s.rest_scale=0.8; s.rest_alpha=0.5; s.target_width=300
  local rest=e:presentation(spawn,ctx,s,x)
  equal(rest.width,190); equal(rest.height,11); equal(rest.scale,0.8)
  check(rest.alpha<=0.5)
  x.combat=true; local combat=e:presentation(spawn,ctx,s,x)
  equal(combat.width,260); equal(combat.height,21)
  ctx.target=true; local target=e:presentation(spawn,ctx,s,x)
  equal(target.width,300); equal(target.scale,s.scale*s.target_scale)
end)

test('target-only resource and cast options do not invent missing data',function()
  local p=presentation(function(_,ctx,s)
    s.resource_target_only=true; s.cast_target_only=true; ctx.mana_percent=40
    ctx.casting={name='Observed spell',duration_ms=1000,elapsed_ms=300}
  end)
  equal(p.mana,nil); equal(p.cast,nil)
  p=presentation(function(_,ctx,s)
    s.resource_target_only=true; s.cast_target_only=true; ctx.target=true; ctx.mana_percent=40
    ctx.casting={name='Observed spell',duration_ms=1000,elapsed_ms=300}
  end)
  equal(p.mana.value,40); equal(p.cast.value,30)
end)

test('appearance settings and low-health pulse produce valid presentation fields',function()
  local p,s=presentation(function(_,ctx,settings)
    settings.texture='flat'; settings.name_position='inside'; settings.text_align='left'
    settings.border_width=4; settings.rounding=12; settings.stack=true
    settings.stack_gap=8; settings.max_stack=150; settings.low_health_pulse=true
    ctx.hp_percent=10; ctx.now_ms=1234
  end)
  for _,key in ipairs({'texture','name_position','text_align','border_width','rounding','stack','stack_gap','max_stack'}) do equal(p[key],s[key]) end
  check(p.border_color>=0 and p.border_color<=0xffffff)
end)

test('full visual themes and explicit wall and click choices reach the host presentation',function()
  local p,s=presentation(function(_,_,settings)
    local theme=C.preset('Runic')
    for key,value in pairs(theme) do settings[key]=value end
    settings.through_walls=true; settings.clickable=false
  end)
  for _,key in ipairs({'frame_style','target_style','badge_position','badge_scale','cast_gap','cast_width',
    'health_text_align','cast_text_align','through_walls','clickable'}) do equal(p[key],s[key]) end
  local defaults=presentation()
  equal(defaults.clickable,true); equal(defaults.through_walls,false)
end)

print(string.format('Presentation engine: %d tests, %d assertions passed',tests,assertions))
