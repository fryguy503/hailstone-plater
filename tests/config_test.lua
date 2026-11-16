-- SPDX-License-Identifier: MIT
local C=require('crest.config')
local tests,assertions=0,0
local function check(value,message) assertions=assertions+1; assert(value,message or 'check failed') end
local function equal(a,b,message) check(a==b,(message or 'unexpected value')..': '..tostring(a)..' ~= '..tostring(b)) end
local function test(name,fn) local ok,why=pcall(fn); assert(ok,name..': '..tostring(why)); tests=tests+1 end
local function same(a,b)
  equal(type(a),type(b))
  if type(a)=='number' then check(math.abs(a-b)<1e-9)
  elseif type(a)=='table' then
    for k,v in pairs(a) do same(v,b[k]) end
    for k in pairs(b) do check(a[k]~=nil,'unexpected key '..tostring(k)) end
  else equal(a,b) end
end

test('all settings default, clamp, reject nonfinite values and retain correct types',function()
  local defaults=C.repair()
  local names={}
  for _,d in ipairs(C.schema) do
    check(not names[d[2]],'duplicate key '..d[2]); names[d[2]]=true
    equal(defaults[d[2]],d[5]); equal(C.value(d[2],nil),d[5])
    if d[4]=='number' then
      equal(C.value(d[2],-1e20),d[6]); equal(C.value(d[2],1e20),d[7])
      for _,bad in ipairs({math.huge,-math.huge,0/0,'4',{},false}) do equal(C.value(d[2],bad),d[5]) end
    elseif d[4]=='color' then
      equal(C.value(d[2],-1),0); equal(C.value(d[2],0xffffff+1),0xffffff)
      equal(C.value(d[2],1.9),1); equal(C.value(d[2],math.huge),d[5])
    elseif d[4]=='bool' then
      equal(C.value(d[2],true),true); equal(C.value(d[2],false),false)
      equal(C.value(d[2],'false'),d[5]); equal(C.value(d[2],0),d[5])
    elseif d[4]=='enum' then
      for _,option in ipairs(d[6]) do equal(C.value(d[2],option),option) end
      equal(C.value(d[2],'unknown'),d[5])
    elseif d[4]=='string' then
      equal(#C.value(d[2],string.rep('x',d[6]+100)),d[6])
      check(not C.value(d[2],'a\0b\1c\127'):find('[%c]'))
    end
  end
  equal(C.value('unknown_key',true),nil)
end)

test('repair ignores executable metatables, cycles, unknown fields and invalid rules',function()
  local hostile=setmetatable({},{__index=function() error('metatable executed') end,__pairs=function() error('pairs executed') end})
  same(C.repair(hostile),C.repair())
  local settings={unknown='ignored',rules={false,{},C.rule{name='valid'}}}
  settings.rules[4]=settings
  local result=C.repair(settings)
  equal(result.unknown,nil); equal(#result.rules,1); equal(result.rules[1].name,'valid')
  equal(C.rule(false),nil); equal(C.rule{name=''},nil)
end)

test('rules are bounded, sanitized, type safe and copied',function()
  local input={rules={}}
  for i=1,C.MAX_RULES+100 do input.rules[i]={name='rule'..i,color=-100,scale=100,enabled=true} end
  local repaired=C.repair(input)
  equal(#repaired.rules,C.MAX_RULES)
  equal(repaired.rules[1].color,0); equal(repaired.rules[1].scale,2)
  repaired.rules[1].name='changed'; equal(input.rules[1].name,'rule1')
  local r=C.rule{name=string.rep('x',200),kind='invalid',scale=0/0,color=math.huge,exact='yes',hide='true'}
  equal(#r.name,80); equal(r.kind,'any'); equal(r.scale,1); equal(r.exact,false); equal(r.hide,false)
end)

test('named profile storage stays bounded and rejects invalid names',function()
  local profiles={}
  for i=1,100 do profiles['Profile '..i]={width=1000} end
  profiles['']=C.repair(); profiles['bad\nname']=C.repair(); profiles[string.rep('x',33)]=C.repair()
  local store=C.store{settings={height=999},profiles=profiles}
  local count=0
  for name,profile in pairs(store.profiles) do
    count=count+1; check(#name>0 and #name<=32 and not name:find('[%c]')); equal(profile.width,400)
  end
  equal(count,C.MAX_PROFILES); equal(store.settings.height,40)
end)

test('per-unit and combat settings inherit and override predictably',function()
  for _,unit in ipairs(C.units) do
    local kind=unit[1]
    local s=C.repair(); s.width=180; s[kind..'_width']=220
    equal(C.effective(s,kind,false).width,180)
    s[kind..'_override']=true; s[kind..'_custom_color']=0x123456
    s[kind..'_resource']=false; s[kind..'_cast']=false; s[kind..'_names_only']=true
    local style=C.effective(s,kind,false)
    equal(style.width,220); equal(style.unit_color,0x123456)
    equal(style.resource,'off'); equal(style.show_cast,false); equal(style.unit_names_only,true)
    s.combat_width=260; s.combat_height=25; s.combat_scale=1.5
    local combat=C.effective(s,kind,true)
    equal(combat.width,260); equal(combat.height,25); equal(combat.scale,1.5)
    equal(s.width,180); equal(s[kind..'_width'],220)
  end
end)

test('every preset roundtrips through data-only text',function()
  for _,name in ipairs(C.presets) do
    local settings=C.preset(name)
    settings.rules={C.rule{name='A.B [literal] % + | =',exact=true,kind='npc',scale=1.5,color=0x123abc}}
    local encoded=C.export(settings)
    local decoded,why=C.import(encoded)
    check(decoded,why); same(settings,decoded)
    equal(C.export(decoded),encoded)
    local windows=encoded:gsub('\n','\r\n')
    local crlf=C.import(windows); same(settings,crlf)
  end
end)

test('a maximal profile remains importable and within text limits',function()
  local settings=C.repair()
  for _,d in ipairs(C.schema) do
    if d[4]=='string' then settings[d[2]]=string.rep('z',d[6]) end
  end
  for i=1,C.MAX_RULES do settings.rules[i]=C.rule{name=string.rep('x',80),color=0xffffff,scale=2} end
  local encoded=C.export(settings)
  check(#encoded<=32768)
  local decoded,why=C.import(encoded); check(decoded,why); same(settings,decoded)
end)

test('unknown, duplicate, oversized, malformed and executable profile text is refused',function()
  local bad={
    '', 'return {}', 'CREST2', 'CREST1\nunknown=true', 'CREST1\nenabled=true\nenabled=false',
    'CREST1\nenabled=yes', 'CREST1\nwidth=nan', 'CREST1\nwidth=1e999',
    'CREST1\nwidth=os.execute("bad")', 'CREST1\ncolor_mode=unknown',
    'CREST1\naura_names=0', 'CREST1\naura_names=GG',
    'CREST1\nrule|FF0|1|npc|1|0|4|1', 'CREST1\nrule|61|2|npc|1|0|4|1',
    'CREST1\nrule||1|npc|1|0|4|1', string.rep('x',32769), 'CREST1'..string.rep('\n',513),
  }
  for _,text in ipairs(bad) do
    local ok,value,why=pcall(C.import,text)
    check(ok,'parser raised'); equal(value,nil,'invalid profile accepted'); check(type(why)=='string')
  end
  local rule='\nrule|61|1|npc|1|0|4|1'
  equal(C.import('CREST1'..rule:rep(C.MAX_RULES+1)),nil)
end)

test('deterministic input mutations never raise and accepted values remain valid',function()
  local text=C.export(C.repair())
  for i=1,#text,7 do
    local mutated=text:sub(1,i-1)..string.char((i*17)%256)..text:sub(i+1)
    local ok,result=pcall(C.import,mutated)
    check(ok,'mutation raised at '..i)
    if result then same(result,C.repair(result)) end
  end
  for _,value in ipairs({false,12,{}}) do equal(C.import(value),nil) end
end)

test('visual themes have distinct structural layouts and private packs stay hidden',function()
  local seen={}
  for _,name in ipairs({'Default','Bare','Inset','Forged','Glass','Runic','Split','Tactical'}) do
    local s=C.preset(name)
    local signature=table.concat({s.frame_style,s.texture,s.name_position,s.target_style,s.badge_position,
      s.cast_gap,s.cast_width,s.health_text_align,s.cast_text_align},'|')
    check(not seen[signature],'visual theme only changes color or size: '..name)
    seen[signature]=name
    check(type(C.descriptions[name])=='string' and #C.descriptions[name]>20)
    if s.resource~='off' then
      check(s.mana_height-2*s.border_width>0,'known-resource fill is hidden by the theme border: '..name)
    end
    same(s,C.repair(s))
  end
  for _,name in ipairs(C.presets) do check(name~='Server','private support pack exposed') end
  equal(C.repair().clickable,true)
  equal(C.repair().through_walls,false)
end)

print(string.format('Configuration: %d tests, %d assertions passed',tests,assertions))
