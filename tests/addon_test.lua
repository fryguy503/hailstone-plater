-- SPDX-License-Identifier: MIT
-- Offline host-contract tests. No game process, game assets, or network used.
-- Run from the repository root: lua tests/addon_test.lua
local C = require('crest.config')
local Panel = require('crest.panel')
local Engine = require('crest.engine')
local Renderer = require('crest.renderer')
local tests, assertions, failures = 0, 0, {}
local function check(value, message)
  assertions=assertions+1
  assert(value,message or 'check failed')
end
local function equal(actual,expected,message)
  check(actual==expected,(message or 'unexpected value')..': '..tostring(actual)..' ~= '..tostring(expected))
end
local function test(name,fn)
  local ok,why=pcall(fn)
  tests=tests+1
  if not ok then failures[#failures+1]=name..': '..tostring(why) end
end
local function copy(value)
  if type(value)~='table' then return value end
  local out={}; for k,v in pairs(value) do out[k]=copy(v) end; return out
end

-- ImGui's MQ wrapper reverses ui.Begin's outputs. This deliberately follows
-- src/mq/lua_mq.cpp, rather than the lower-level ui documentation.
local function mock_ui()
  local state={tab='Settings',visible=true,opened=true,buttons={},inputs={},widgets={},texts={},stack={},bars={},windows={},popups={},swatches={}}
  local ui={Col={PlotHistogram=1},Cond={FirstUseEver=4,Always=1},WindowFlags={NoResize=2,NoCollapse=32,NoMove=4}}
  local function push(kind) state.stack[#state.stack+1]=kind end
  local function pop(kind)
    equal(table.remove(state.stack),kind,'unbalanced ImGui '..kind)
  end
  local function clicked(label)
    if state.buttons[label] then state.buttons[label]=nil; return true end
    return false
  end
  local function input(label,value)
    state.widgets[label]=true
    if state.inputs[label]~=nil then local v=state.inputs[label]; state.inputs[label]=nil; return v,true end
    return value,false
  end
  ui.Begin=function(name,open,flags)
    state.windows[#state.windows+1]={name=name,flags=flags}; push('window'); return open and state.opened,state.visible
  end
  ui.End=function() pop('window') end
  ui.BeginTabBar=function() push('tabbar'); return true end
  ui.EndTabBar=function() pop('tabbar') end
  ui.BeginTabItem=function(label) if state.tab==label then push('tab'); return true end; return false end
  ui.EndTabItem=function() pop('tab') end
  ui.BeginCombo=function(label) state.widgets[label]=true; push('combo'); return true end
  ui.EndCombo=function() pop('combo') end
  ui.TreeNode=function() push('tree'); return true end
  ui.TreePop=function() pop('tree') end
  ui.PushID=function() push('id') end
  ui.PopID=function() pop('id') end
  ui.PushStyleColor=function(_,r,g,b,a)
    check(r>=0 and r<=1 and g>=0 and g<=1 and b>=0 and b<=1 and a>=0 and a<=1)
    push('style')
  end
  ui.PopStyleColor=function() pop('style') end
  ui.Button=clicked; ui.SmallButton=clicked; ui.Selectable=clicked
  ui.ColorButton=function(label,r,g,b,a,flags,w,h)
    check(r>=0 and r<=1 and g>=0 and g<=1 and b>=0 and b<=1)
    state.swatches[label]={r=r,g=g,b=b}; state.widgets[label:gsub('##swatch$','')]=true
    return clicked(label)
  end
  ui.ColorPicker3=function(label,r,g,b)
    local value=state.inputs[label]
    if value then state.inputs[label]=nil; return value[1],value[2],value[3],true end
    return r,g,b,false
  end
  ui.OpenPopup=function(label) state.popups[label]=true end
  ui.BeginPopup=function(label)
    if state.popups[label] then state.current_popup=label; push('popup'); return true end
    return false
  end
  ui.CloseCurrentPopup=function() state.popups[state.current_popup]=nil end
  ui.EndPopup=function() pop('popup') end
  ui.Checkbox=input; ui.SliderFloat=input; ui.InputText=input; ui.InputTextMultiline=input
  ui.InputTextWithHint=function(label,_,value) return input(label,value) end
  ui.BeginDisabled=function() push('disabled') end
  ui.EndDisabled=function() pop('disabled') end
  ui.CollapsingHeader=function() return true end
  ui.IsItemHovered=function() return false end
  local function text(value) check(type(value)=='string','UI text must be a string'); state.texts[#state.texts+1]=value end
  ui.Text=text; ui.TextWrapped=text; ui.TextDisabled=text
  ui.TextColored=function(_,_,_,_,value) text(value) end
  ui.ProgressBar=function(value,width,height,label)
    check(value>=0 and value<=1 and (width>0 or width==-1) and height>0 and type(label)=='string')
    state.bars[#state.bars+1]={value=value,label=label,width=width,height=height}
  end
  local noop=function() end
  for _,key in ipairs({'SetNextWindowSize','SetNextItemWidth','SameLine','NewLine','Separator','SetTooltip'}) do ui[key]=noop end
  return setmetatable(ui,{__index=function(_,key) error('Unexpected ImGui API: '..tostring(key)) end}),state
end

local function panel_fixture()
  local ui,state=mock_ui()
  local data=C.store()
  local calls={changed=0,saved=0}
  local app={settings=function() return data.settings end,profiles=function() return data.profiles end,
    api_version=function() return 1 end,
    changed=function() calls.changed=calls.changed+1 end,
    save=function() calls.saved=calls.saved+1; return true end,
    replace=function(settings) data.settings=C.repair(settings); calls.changed=calls.changed+1 end,
    profile_save=function(name) data.profiles[name]=C.repair(data.settings); return 'Saved '..name end,
    status=function() return {'Offline mock status','No network connection.'} end}
  local panel=Panel.new(ui,app); panel.open=true
  return panel,state,data,calls,app
end

test('all settings controls and every tab draw with balanced ImGui calls',function()
  local p,state,data=panel_fixture()
  data.settings.rules={C.rule{name='Guardian'}}
  data.profiles.Sample=C.preset('Tank')
  for _,tab in ipairs({'Settings','Unit types','Name rules','Profiles','Preview & status'}) do
    state.tab=tab; p:draw(); equal(#state.stack,0)
  end
  for _,descriptor in ipairs(C.schema) do check(state.widgets[descriptor[3]],'missing control '..descriptor[3]) end
end)

test('collapsing a settings window preserves its open state',function()
  local p,state=panel_fixture()
  state.visible=false
  p:draw()
  equal(p.open,true,'collapsed window should remain open')
  equal(#state.stack,0)
  state.visible=true; p:draw(); equal(p.open,true)
end)

test('closing a settings window observes MQ Begin return ordering',function()
  local p,state=panel_fixture()
  state.opened=false; state.visible=true
  p:draw()
  equal(p.open,false,'close button should close the panel')
  equal(#state.stack,0)
end)

test('settings widgets validate changed values before applying',function()
  local p,state,data,calls=panel_fixture()
  state.inputs['Bar width']=999999
  state.buttons['NPC / custom##swatch']=true
  state.inputs['Hex']='12AB34'
  state.inputs['Show names']=false
  p:draw()
  equal(data.settings.width,400)
  equal(data.settings.npc_color,0x12ab34)
  equal(data.settings.show_name,false)
  equal(calls.changed,3)
  state.inputs['Hex']='oops'; p:draw(); equal(data.settings.npc_color,0x12ab34)
end)

test('rules can be created, edited and removed through UI',function()
  local p,state,data,calls=panel_fixture()
  state.tab='Name rules'
  state.inputs['Name or fragment']='Guardian'
  state.inputs['Exact name']=true
  state.buttons['Add rule']=true
  p:draw()
  equal(#data.settings.rules,1)
  equal(data.settings.rules[1].name,'Guardian')
  equal(data.settings.rules[1].exact,true)
  state.inputs['Hide matching plates']=true; p:draw()
  equal(data.settings.rules[1].hide,true)
  state.buttons.Remove=true; p:draw()
  equal(#data.settings.rules,0)
  check(calls.changed>=3)
end)

test('profile export and validated text import work through UI',function()
  local p,state,data=panel_fixture()
  state.tab='Profiles'; state.buttons['Export current']=true
  p:draw(); check(p.profile_text:sub(1,6)=='CREST1')
  local imported=C.preset('Minimal'); imported.width=215
  state.inputs['Profile data']=C.export(imported); p:draw()
  state.buttons['Import profile text']=true; p:draw()
  equal(data.settings.width,215)
  p.profile_text='return os.execute("bad")'; state.buttons['Import profile text']=true; p:draw()
  equal(data.settings.width,215)
  check(p.message:find('CREST1',1,true))
end)

test('setting search uses literal fragments and unit pages preserve independent values',function()
  local p,state,data,calls=panel_fixture()
  state.inputs.Search='Bar width'; p:draw()
  check(state.widgets['Bar width']); check(not state.widgets['Show names'])
  state.inputs.Search='['; state.widgets={}; p:draw()
  check(not state.widgets['Bar width'],'search punctuation must not be treated as a Lua pattern')
  state.tab='Unit types'
  for index,unit in ipairs(C.units) do
    p.unit=unit[1]
    state.inputs['Use independent appearance']=true
    state.inputs['Bar width']=200+index
    p:draw()
    equal(data.settings[unit[1]..'_override'],true)
    equal(data.settings[unit[1]..'_width'],200+index)
    equal(#state.stack,0)
  end
  equal(data.settings.width,C.repair().width)
  check(calls.changed>=#C.units*2)
end)

local function encode(text) return (text:gsub('.',function(c) return string.format('%02x',c:byte()) end)) end
local function first_text(...)
  for i=1,select('#',...) do
    local text=select(i,...)
    if type(text)=='string' and text~='' then return text end
  end
  return ''
end
local function path_for(server,name)
  return 'mock/config/crest/characters/'..encode(first_text(server.world_host,server.host,server.name))..'_'
    ..encode(first_text(server.world,server.name))..'/'..encode(name)..'.lua'
end
local function runtime(options)
  options=options or {}
  local ui,ui_state=mock_ui()
  local state={time=0,packets={},hooks={},chats={},saves={},fs={},loads={},buffs={},spells={},styles={},models={},
    registrations=0,unregistrations=0,canvas_serial=0,icons={},
    server={name='Example world',host='zone.example',world='Example world',world_host='login.example'},
    player={name='First',spawn_id=1,game_state=5,in_zone=true,zone_id=10,target_id=2,
      in_group=true,in_raid=false,cur_hp=40,max_hp=100},combat=false,exited=false,ui_state=ui_state}
  if options.player==false then state.player=nil end
  if options.prepare then options.prepare(state) end
  local hs={}
  if not options.missing_canvas then
    hs.nameplates={version=options.api_version==nil and 1 or options.api_version}
    hs.nameplates.Register=function(name,fn)
      equal(name,'crest'); equal(type(fn),'function'); state.registrations=state.registrations+1
      if options.registration_error then error('canvas registration permission denied') end
      if state.registration_refused or options.registration_refused then return nil,'canvas unavailable' end
      local handle='mock-canvas:'..state.registrations
      state.provider=fn; state.provider_handle=handle
      return handle
    end
    hs.nameplates.Unregister=function(handle)
      equal(handle,state.provider_handle,'must release opaque registration handle')
      state.unregistrations=state.unregistrations+1
      state.provider=nil; state.provider_handle=nil; return true
    end
    hs.nameplates.MeasureText=function(text,size,font)
      if state.metrics_missing then return nil end
      equal(font,3); check(size>0)
      local width=assert(utf8.len(text))*size*0.5
      return {width=width,height=size,ink_x0=0,ink_y0=0,ink_x1=width,ink_y1=size}
    end
    hs.nameplates.SpellIcon=function(icon)
      state.last_icon=icon
      if state.icons[icon] then return state.icons[icon],0.1,0.2,0.3,0.4 end
    end
    if options.incomplete_canvas then hs.nameplates.MeasureText=nil end
  end
  hs.player=function() if state.read_error then error('mock player read failed') end; return copy(state.player) end
  hs.spawn=function(id)
    if state.entities then return copy(state.entities[id]) end
    return {id=id,incarnation=state.incarnation or 1,name=state.spawn_name or 'Guardian',corpse=false}
  end
  hs.xtarget=function(i) return copy(state.xtargets and state.xtargets[i]) end
  hs.server=function() return copy(state.server) end
  hs.chat=function(text) state.chats[#state.chats+1]=text end
  hs.spell=function(id) return copy(state.spells[id]) end
  hs.hooks=function() error('the old structured nameplate hook must never be used') end
  hs.menu=function(label,command) equal(label,'Crest'); equal(command,'/crest') end
  hs.on_packet=function(stream,direction,opcode,fn)
    if options.packets_denied then error("hailstone.on_packet needs the server's 'packets' permission") end
    equal(stream,'zone'); equal(direction,'in'); check(opcode==0x7d1c or opcode==0x7d1d or opcode==0x7280 or opcode==0x3791 or opcode==0x5f42)
    state.packets[opcode]=fn
  end
  local mq={configDir='mock/config',TLO={Group={GroupSize=function() return #(state.group or {}) end,
    Member=function(i) return {Spawn={ID=function() return (state.group or {})[i+1] end}} end},Me={Combat=function() return state.combat end,
    CombatState=function() return state.combat_state or 'ACTIVE' end}},imgui={}}
  mq.gettime=function() return state.time end
  mq.delay=function(ms) return coroutine.yield(ms) end
  mq.bind=function(name,fn) equal(name,'/crest'); state.command=fn end
  mq.unbind=function() end
  mq.exit=function() state.exited=true end
  mq.pickle=function(path,data)
    if state.save_error then error('mock disk unavailable') end
    state.saves[#state.saves+1]={path=path,value=copy(data)}
    state.fs[path]=copy(data)
  end
  mq.imgui.init=function(name,fn) equal(name,'crest'); state.draw=fn end
  mq.imgui.destroy=function() state.draw=nil end
  mq.TLO.Spawn=function(id)
    local buffs=state.buffs[id]
    return {
      Name=function() return state.spawn_name or 'Guardian' end,
      ConColor=function() return state.con_color or 'BLUE' end,
      BuffsPopulated=function() return buffs~=nil end,
      BuffCount=function() check(buffs~=nil,'unknown buffs were inspected'); return #buffs end,
      Buff=function(index)
        check(buffs~=nil,'unknown buffs were inspected')
        local entry=buffs[index]
        return {ID=function() return index end,Spell={ID=function() return entry and entry.id end}}
      end,
      BuffDuration=function(index) return {TotalSeconds=function() return buffs[index].seconds end} end,
    }
  end
  local modules={mq=mq,hailstone=hs,ImGui=ui,['crest.renderer']=options.renderer or Renderer,['crest.engine']={new=function()
    local engine=Engine.new(); local presentation=engine.presentation
    state.engine=engine
    -- Observation tests retain access to the internal style decisions while
    -- exercising the real renderer and the public canvas callback end to end.
    engine.presentation=function(self,spawn,...)
      local value=presentation(self,spawn,...); state.styles[spawn.id]=copy(value); return value
    end
    return engine
  end},['crest.panel']={new=function(actual_ui,app)
    state.app=app; state.panel=Panel.new(actual_ui,app); return state.panel
  end}}
  local env=setmetatable({require=function(name) return modules[name] or require(name) end,
    loadfile=function(path)
      state.loads[#state.loads+1]=path
      if state.load_error then return function() error('invalid settings data') end end
      if state.fs[path] then return function() return copy(state.fs[path]) end end
      return nil,'missing settings'
    end},{__index=_G})
  local chunk=assert(loadfile('init.lua','t',env))
  state.thread=coroutine.create(chunk)
  function state:step(milliseconds)
    self.time=self.time+(milliseconds or 100)
    local ok,result=coroutine.resume(self.thread)
    check(ok,'addon main loop failed: '..tostring(result))
    return result
  end
  function state:canvas(candidates,fields)
    self.styles={}; self.models={}; self.canvas_serial=self.canvas_serial+1
    if not self.provider then return self.models end
    local frame={candidates=candidates,serial=self.canvas_serial,now_ms=self.time,dt_ms=16,font_size=14,font=3,
      viewport={x=0,y=0,w=1600,h=1000}}
    for k,v in pairs(fields or {}) do frame[k]=v end
    local allowed={}
    for _,candidate in ipairs(candidates) do
      candidate.token=candidate.token or ('frame:'..self.canvas_serial..':spawn:'..candidate.spawn.id)
      allowed[candidate.token]=true
    end
    local models=self.provider(frame)
    equal(type(models),'table')
    for _,model in ipairs(models) do
      check(allowed[model.token],'submission did not use a current frame token')
      for key in pairs(model) do
        check(({token=true,hide=true,bounds=true,commands=true,offset_y=true,offset_x=true,through_walls=true,clickable=true})[key],
          'non-generic submission field '..key)
      end
      if not model.hide then
        check(model.bounds.w>0 and model.bounds.h>0); check(type(model.commands)=='table')
        for _,command in ipairs(model.commands) do equal(command.bounds,nil,'internal geometry metadata leaked to host') end
      end
    end
    self.models=models; return models
  end
  function state:plate(id,context,raw_name)
    local ctx={name='Guardian',distance=10,now_ms=self.time,hp_percent=60,target=id==2}
    for k,v in pairs(context or {}) do ctx[k]=v end
    self:canvas({{x=800,y=700,depth=10,native_scale=1,
      spawn={id=id,incarnation=self.incarnation or 1,name=raw_name or 'Guardian',npc=true,class=1,level=65},context=ctx}})
    return self.styles[id]
  end
  function state:packet(opcode,data)
    local callback=assert(self.packets[opcode])
    equal(select('#',callback(opcode,data)),0,'packet observer must return no replacement/drop value')
  end
  function state:stop()
    self.command('stop'); self:step(); equal(coroutine.status(self.thread),'dead'); check(self.exited)
  end
  state:step(0)
  return state
end

local function marker(icon,scope,id)
  return string.pack('<I4BBBBI4I4',0x4e504d31,1,1,icon,scope,id,0)
end
local function has_icon(plate,icon)
  for _,b in ipairs((plate or {}).badges or {}) do if b.icon==icon then return true end end
  return false
end

test('startup without a logged-in character uses ordinary plates',function()
  local r=runtime{player=false}
  equal(r:plate(2),nil)
  equal(#r.loads,0); equal(#r.saves,0)
  r:packet(0x7d1c,marker(1,1,2)); equal(r:plate(2),nil)
  r:stop()
end)

test('older clients degrade to ordinary nameplates and usable settings',function()
  local r=runtime{api_version=0}
  equal(r.provider,nil); equal(r.registrations,0)
  r.command(''); r.draw(); equal(#r.ui_state.stack,0)
  r:packet(0x7d1c,marker(1,1,2))
  r:stop()
end)

test('denied optional packet observation preserves generic plates settings and a diagnostic',function()
  local r=runtime{packets_denied=true}
  equal(next(r.packets),nil)
  local p=r:plate(2,{mana_percent=0,casting={duration_ms=2000,elapsed_ms=1000,name='Observed spell'}})
  check(p.health); equal(p.mana.value,0); check(p.cast)
  check(not has_icon(p,'eye') and not has_icon(p,'star'))
  equal(r:plate(2,{concealed=true}).badges[1].label,'HID')
  r.command(''); r.ui_state.tab='Preview & status'; r.draw()
  equal(#r.ui_state.stack,0)
  local diagnostics=table.concat(r.app.status(),'\n')
  check(diagnostics:find('Optional server icons unavailable',1,true))
  check(diagnostics:find('packets',1,true))
  r.command('off'); equal(r:plate(2),nil)
  r:stop()
end)

test('concealment tags use observed types and clear with the NPC snapshot',function()
  local r=runtime{}
  local function snapshot(generation, flags)
    local bytes=string.pack('<I4BBI2I4I2I2',0x4e504331,1,1,flags and 1 or 0,generation,0,1)
    if flags then bytes=bytes..string.pack('<I2BB',2,flags,0) end
    r:packet(0x7d1d,bytes)
  end
  equal(#r:plate(2).badges,0)
  equal(r:plate(2,{invisible=true}).badges[1].label,'INV')
  equal(r:plate(2,{invisible_undead=true}).badges[1].label,'IVU')
  snapshot(1,3)
  local p=r:plate(2,{invisible=true})
  equal(p.name_prefix,'('); equal(p.name_suffix,')'); equal(p.badges[1].label,'INV+IVU')
  local s=r.app.settings()
  s.concealment_display='Tags'; r.app.changed()
  p=r:plate(2); equal(p.name_prefix,nil); equal(p.badges[1].label,'INV+IVU')
  s.concealment_display='Name suffix'; r.app.changed()
  p=r:plate(2); equal(p.name_suffix,' [INV+IVU]'); equal(#p.badges,0)
  s.show_name=false; r.app.changed()
  p=r:plate(2); equal(p.show_name,false); equal(p.badges[1].label,'INV+IVU')
  s.concealment_display='Off'; r.app.changed()
  p=r:plate(2); equal(p.name_suffix,nil); equal(#p.badges,0)
  s.concealment_display='Tags'; r.app.changed()
  snapshot(2,nil)
  equal(#r:plate(2).badges,0)
  r:stop()
end)

test('replacement toggles on/off and packet observers never drop traffic',function()
  local r=runtime()
  check(r:plate(2).health)
  r.command('off'); equal(r:plate(2),nil)
  r.command('on'); check(r:plate(2).health)
  for _,payload in ipairs({'',string.rep('x',1000),marker(1,1,2)}) do r:packet(0x7d1c,payload) end
  r:packet(0x7d1d,string.rep('x',504))
  check(has_icon(r:plate(2),'star'))
  r:stop()
end)

test('all runtime tabs draw and stop saves dirty configuration',function()
  local r=runtime()
  r.command('')
  for _,tab in ipairs({'Settings','Unit types','Name rules','Profiles','Preview & status'}) do
    r.ui_state.tab=tab; r.draw(); equal(#r.ui_state.stack,0)
  end
  r.app.settings().width=204; r.app.changed()
  r:stop()
  equal(#r.saves,1); equal(r.saves[1].value.settings.width,204)
end)

test('settings are separated by character and stable world identity',function()
  local r=runtime()
  r.app.settings().width=201; r.app.changed()
  local first_path=path_for(r.server,'First')
  r.player.name='Second'; r.player.spawn_id=3; r:step()
  equal(r.fs[first_path].settings.width,201)
  equal(r.app.settings().width,C.repair().width)
  r.app.settings().width=211; r.app.changed()
  local second_path=path_for(r.server,'Second')
  r.player.name='First'; r.player.spawn_id=1; r:step()
  equal(r.fs[second_path].settings.width,211)
  equal(r.app.settings().width,201)
  r.server.world='Different world'; r:step()
  equal(r.app.settings().width,C.repair().width)
  check(r.loads[#r.loads]~=first_path)
  r:stop()
end)

test('character and server path text cannot escape config directory',function()
  local r=runtime{prepare=function(s)
    s.player.name='../Mixed\\Name'; s.server.world_host='../../host'; s.server.world='A/B\\C'
  end}
  local path=r.loads[1]
  check(path:match('^mock/config/crest/characters/[%da-f]+_[%da-f]+/[%da-f]+%.lua$'))
  r:stop()
end)

test('empty legacy server identity fields fall back to nonempty host and name',function()
  local r=runtime{prepare=function(s)
    s.server.world_host=''; s.server.world=''; s.server.host='direct-zone.example'; s.server.name='Legacy world'
    s.fs[path_for(s.server,'First')]={settings={width=221}}
  end}
  equal(r.loads[1],path_for(r.server,'First'))
  equal(r.app.settings().width,221)
  local initial_path=r.loads[1]
  r.server.host='different-direct-zone.example'; r:step()
  check(r.loads[#r.loads]~=initial_path,'different direct server must have a distinct settings path')
  equal(r.app.settings().width,C.repair().width)
  r:stop()
end)

test('zone and group/raid membership changes clear shared markers',function()
  local r=runtime()
  local function status_has(text) return table.concat(r.app.status(),'\n'):find(text,1,true)~=nil end
  r:step()
  r:packet(0x7d1c,marker(1,1,2)); check(has_icon(r:plate(2),'star'))
  check(status_has('Optional indicators are receiving supported data.'))
  r.player.in_group=false; r:step(); check(not has_icon(r:plate(2),'star'))
  r.player.in_group=true; r:step(); check(not has_icon(r:plate(2),'star'))
  r:packet(0x7d1c,marker(2,2,2)); check(not has_icon(r:plate(2),'sun'))
  r.player.in_raid=true; r:step(); check(not has_icon(r:plate(2),'sun'))
  r:packet(0x7d1c,marker(2,2,2)); check(has_icon(r:plate(2),'sun'))
  r.player.zone_id=11; r:step(); check(not has_icon(r:plate(2),'sun'))
  check(status_has('Optional indicators are waiting for supported data.'))
  check(not status_has('Optional indicators are receiving supported data.'))
  r:packet(0x7d1c,marker(2,2,2))
  check(status_has('Optional indicators are receiving supported data.'))
  r.server.world='Different world'; r:step(); check(not has_icon(r:plate(2),'sun'))
  check(status_has('Optional indicators are waiting for supported data.'))
  check(not status_has('Optional indicators are receiving supported data.'))
  r:stop()
end)

test('settings write failure preserves old data and suspends cross-character rendering',function()
  local r=runtime()
  r.app.settings().width=222; r.app.changed(); r.save_error=true
  r.player.name='Second'; r.player.spawn_id=3; r:step()
  equal(r:plate(2),nil,'failed old-character save must not expose old settings to new character')
  equal(#r.saves,0)
  r.save_error=false; r:step()
  equal(r.saves[1].value.settings.width,222)
  equal(r.app.settings().width,C.repair().width)
  check(r:plate(2).health)
  r:stop()
end)

test('invalid saved data falls back to validated defaults',function()
  local r=runtime{prepare=function(s)
    s.fs[path_for(s.server,'First')]={settings={width=math.huge,height=-999,enabled='yes',rules={false}}}
  end}
  equal(r.app.settings().width,C.repair().width)
  equal(r.app.settings().height,3)
  equal(r.app.settings().enabled,true)
  equal(#r.app.settings().rules,0)
  check(r:plate(2).health)
  r:stop()
end)

test('aura lookups use spell IDs rather than buff-slot IDs',function()
  local r=runtime{prepare=function(s)
    s.fs[path_for(s.server,'First')]={settings={show_auras=true,aura_mode='all'}}
    s.buffs[1]={{id=12345,seconds=42}}
    s.buffs[2]={{id=54321}}
    s.spells[12345]={id=12345,name='Self blessing',spell_type=1}
    s.spells[54321]={id=54321,name='Target debuff',spell_type=0}
    s.spells[1]={id=1,name='Wrong slot spell',spell_type=1}
  end}
  local self_plate=r:plate(1,{self=true})
  r.app.settings().show_self=true; r.app.changed(); self_plate=r:plate(1,{self=true})
  local target_plate=r:plate(2)
  check(has_icon(self_plate,'circle'))
  equal(self_plate.badges[1].label,'Self blessing 42s')
  equal(target_plate.badges[1].label,'Target debuff')
  r:stop()
end)

test('unknown target buffs remain absent and whitelist is literal',function()
  local r=runtime{prepare=function(s)
    s.fs[path_for(s.server,'First')]={settings={show_auras=true,aura_mode='whitelist',aura_names='A.B'}}
    s.buffs[1]={{id=12345,seconds=0},{id=12346,seconds=0}}
    s.spells[12345]={id=12345,name='AxB blessing',spell_type=1}
    s.spells[12346]={id=12346,name='A.B blessing',spell_type=1}
  end}
  r.app.settings().show_self=true; r.app.changed()
  local p=r:plate(1,{self=true})
  equal(#p.badges,1); equal(p.badges[1].label,'A.B blessing')
  equal(#r:plate(2).badges,0)
  r:stop()
end)

test('despawn clears custom icons cached auras and smoothing before an id is reused',function()
  local r=runtime{prepare=function(s)
    s.fs[path_for(s.server,'First')]={settings={show_auras=true,aura_mode='all'}}
    s.buffs[2]={{id=54321,seconds=42}}
    s.spells[54321]={id=54321,name='Prior target debuff',spell_type=0}
  end}
  local function snapshot(magic,generation,flags)
    return string.pack('<I4BBI2I4I2I2I2BB',magic,1,1,1,generation,0,1,2,flags,0)
  end
  local awareness=snapshot(0x4e504131,1,1)
  local status=snapshot(0x4e505331,1,3)
  r:packet(0x7d1c,marker(1,1,2))
  r:packet(0x7d1d,awareness); r:packet(0x7d1d,status)
  local p=r:plate(2,{hp_percent=90})
  for _,icon in ipairs({'star','eye','sleep','peace','circle'}) do check(has_icon(p,icon)) end
  r:packet(0x7280,'abc'); check(has_icon(r:plate(2),'star'),'short delete payload is ignored')
  r:packet(0x7280,string.pack('<I4B',2,1))
  p=r:plate(2,{hp_percent=10})
  equal(#p.badges,0,'the reused id must not inherit marks status or aura art')
  equal(p.health.value,10,'the reused id must not animate from the removed actor health')
  r:packet(0x7d1d,awareness); r:packet(0x7d1d,status)
  equal(#r:plate(2).badges,0,'an old complete generation must not revive removed observations')
  r:packet(0x7d1d,snapshot(0x4e504131,2,1))
  check(has_icon(r:plate(2),'eye'),'a new server generation may establish fresh observation')
  r:stop()
end)

test('a cached aura is dropped immediately when its plate is no longer the authoritative target',function()
  local r=runtime{prepare=function(s)
    s.fs[path_for(s.server,'First')]={settings={show_auras=true,aura_mode='all'}}
    s.buffs[2]={{id=54321,seconds=42}}
    s.spells[54321]={id=54321,name='Target debuff',spell_type=0,spell_icon=37}
  end}
  check(has_icon(r:plate(2),'circle'))
  equal(r:plate(2).badges[1].spell_icon,37,'known art id survives the aura cache')
  equal(#r:plate(2,{target=false}).badges,0,'the per-frame target flag wins over the cached player snapshot')
  r:stop()
end)

test('a cached aura is dropped when the raw entity name differs even before the next refresh',function()
  local r=runtime{prepare=function(s)
    s.fs[path_for(s.server,'First')]={settings={show_auras=true,aura_mode='all'}}
    s.spawn_name='Former_Guardian01'
    s.buffs[2]={{id=54321,seconds=42}}
    s.spells[54321]={id=54321,name='Former target debuff',spell_type=0}
  end}
  check(has_icon(r:plate(2,nil,'Former_Guardian01'),'circle'))
  equal(#r:plate(2,nil,'Different_Guardian01').badges,0)
  r:stop()
end)

test('exact remote pools appear only while the actor is an active extended target',function()
  local r=runtime{prepare=function(s)
    s.player.xtarget_slots=2; s.xtargets={{spawn_id=2}}
  end}
  r:packet(0x3791,string.pack('<I4I4I2',0,200,2))
  equal(r:plate(2).mana.value,0,'zero remote mana is known, not absent')
  r:packet(0x3791,string.pack('<I4I4I2',100,200,3))
  equal(r:plate(3).mana,nil,'untracked remote updates never become a plate pool')
  r:packet(0x5f42,string.pack('<I4I4I2',50,200,2))
  r.app.settings().resource='endurance'; r.app.changed()
  equal(r:plate(2).mana.value,25)
  r.xtargets={}; r:step()
  equal(r:plate(2).mana,nil,'leaving the tracker clears exact pools')
  r:stop()
end)

test('group remote pools validate packets and clear on despawn',function()
  local r=runtime{prepare=function(s) s.group={2} end}
  r:packet(0x3791,string.pack('<I4I4I2',100,200,2))
  equal(r:plate(2).mana.value,50)
  r:packet(0x3791,string.pack('<I4I4I2',300,200,2))
  equal(r:plate(2).mana,nil,'invalid pools fail closed')
  r:packet(0x3791,string.pack('<I4I4I2',100,200,2))
  equal(r:plate(2).mana.value,50)
  r:packet(0x7280,string.pack('<I4',2))
  equal(r:plate(2).mana,nil)
  r:stop()
end)

test('cached auras require the host populated flag even when packet access is denied',function()
  local r=runtime{packets_denied=true,prepare=function(s)
    s.fs[path_for(s.server,'First')]={settings={show_auras=true,aura_mode='all'}}
    s.buffs[2]={{id=54321,seconds=42}}
    s.spells[54321]={id=54321,name='Former target debuff',spell_type=0}
  end}
  check(has_icon(r:plate(2),'circle'))
  r.buffs[2]=nil
  equal(#r:plate(2).badges,0,'authoritative unknown state wins over the cached aura before refresh')
  r:stop()
end)

test('incoming combat state enables combat filtering without automatic melee attack',function()
  local r=runtime{prepare=function(s)
    s.fs[path_for(s.server,'First')]={settings={combat_only=true,combat_width=222}}
    s.combat=false; s.combat_state='COMBAT'
  end}
  local p=r:plate(2)
  check(not p.hide and p.health,'a player attacked while not autoattacking is still in combat')
  equal(p.width,222)
  r.combat_state='ACTIVE'; r:step()
  check(r:plate(2).hide)
  r:stop()
end)

test('unknown resources and casts stay absent in the running addon',function()
  local r=runtime()
  local p=r:plate(2)
  equal(p.mana,nil); equal(p.cast,nil)
  p=r:plate(2,{mana_percent=0,casting={duration_ms=2000,elapsed_ms=1000,name='Observed spell'}})
  equal(p.mana.value,0); check(p.cast.text:find('Observed spell',1,true)); equal(p.cast.value,50)
  r:stop()
end)

test('personal window uses known local pools and follows close/lock settings',function()
  local r=runtime{prepare=function(s)
    s.fs[path_for(s.server,'First')]={settings={personal_bar=true,personal_locked=true}}
    s.player.cur_mana=0; s.player.max_mana=200
    s.player.cur_endurance=90; s.player.max_endurance=300
    s.player.casting_spell_id=12345; s.spells[12345]={name='Local spell'}
  end}
  r.draw()
  equal(#r.ui_state.stack,0)
  equal(r.ui_state.windows[#r.ui_state.windows].name,'Crest Personal')
  check((r.ui_state.windows[#r.ui_state.windows].flags & 4)~=0)
  equal(#r.ui_state.bars,3)
  equal(r.ui_state.bars[1].value,0.4)
  equal(r.ui_state.bars[2].value,0)
  equal(r.ui_state.bars[3].value,0.3)
  check(table.concat(r.ui_state.texts,'\n'):find('Local spell',1,true))
  r.ui_state.opened=false; r.draw(); equal(r.app.settings().personal_bar,false)
  r:stop()
end)

test('personal window omits unavailable pools and never appears out of world',function()
  local r=runtime{prepare=function(s)
    s.fs[path_for(s.server,'First')]={settings={personal_bar=true}}
    s.player.cur_mana=100; s.player.max_mana=0
    s.player.cur_endurance=0/0; s.player.max_endurance=100
  end}
  r.draw(); equal(#r.ui_state.bars,1)
  r.player.in_zone=false; r:step()
  r.ui_state.windows={}; r.draw(); equal(#r.ui_state.windows,0)
  r:stop()
end)

test('missing or incomplete generic canvas modules keep native plates and explain the update',function()
  for _,options in ipairs({{missing_canvas=true},{incomplete_canvas=true}}) do
    local r=runtime(options)
    equal(r.provider,nil); equal(r.registrations,0); equal(r:plate(2),nil)
    check(table.concat(r.chats,'\n'):find('generic nameplate canvas UI API',1,true))
    r.command(''); r.draw(); equal(#r.ui_state.stack,0)
    local text=table.concat(r.ui_state.texts,'\n')
    check(text:find('Update Hailstone',1,true)); check(not text:find('companion client',1,true))
    r:stop()
  end
end)

test('registration refusals and permission errors preserve usable settings and native fallback',function()
  for _,options in ipairs({{registration_refused=true},{registration_error=true}}) do
    local r=runtime(options)
    equal(r.provider,nil); equal(r.registrations,1); equal(r:plate(2),nil)
    r:step(); r:step(); equal(r.registrations,1,'a failed registration must not retry every frame')
    check(table.concat(r.app.status(),'\n'):find('World plates unavailable',1,true))
    r.command(''); r.draw(); equal(#r.ui_state.stack,0)
    r:stop(); equal(r.unregistrations,0)
  end
end)

test('a deliberate enable can recover registration and release its opaque handle',function()
  local r=runtime{prepare=function(s) s.registration_refused=true end}
  equal(r.provider,nil); r.registration_refused=false; r.command('on')
  check(r.provider~=nil); equal(r.registrations,2)
  local callback=r.provider
  r.command('off'); equal(r.provider,nil); equal(r.unregistrations,1)
  equal(#callback({candidates={}}),0,'an already queued callback must be inert after disabling')
  r.command('on'); check(r.provider~=nil); equal(r.registrations,3)
  r:stop(); equal(r.provider,nil); equal(r.unregistrations,2); equal(r.draw,nil)
end)

test('panel disable and leaving a zone unregister immediately without waiting for another canvas frame',function()
  local r=runtime(); equal(r.registrations,1)
  r.app.settings().enabled=false; r.app.changed(); equal(r.provider,nil); equal(r.unregistrations,1)
  r.app.settings().enabled=true; r.app.changed(); check(r.provider~=nil)
  r.player.in_zone=false; r:step(); equal(r.provider,nil); equal(r.unregistrations,2)
  r.player.in_zone=true; r:step(); check(r.provider~=nil)
  r:stop(); equal(r.unregistrations,3)
end)

test('a main loop failure explicitly releases the provider and settings window',function()
  local r=runtime(); local callback=r.provider
  r.read_error=true
  local ok,why=coroutine.resume(r.thread)
  equal(ok,false); check(tostring(why):find('mock player read failed',1,true))
  equal(r.provider,nil); equal(r.unregistrations,1); equal(r.draw,nil)
  -- The host also owns VM-lifetime cleanup; this verifies our finally path.
  check(callback~=nil)
end)

local function candidate(id,target,token)
  return {token=token,x=800,y=700,depth=id,native_scale=1,
    spawn={id=id,incarnation=1,name='Guardian',npc=true,class=1,level=65},
    context={name='Guardian',distance=10,target=target,hp_percent=60}}
end
test('the frame adapter submits generic commands with only current opaque tokens',function()
  local r=runtime()
  local models=r:canvas({candidate(2,true,'current:first')},{now_ms=450,dt_ms=20})
  equal(#models,1); equal(models[1].token,'current:first'); check(#models[1].commands>0)
  check(r.styles[2].health~=nil)
  models=r:canvas({candidate(9,false,'current:second')},{now_ms=500})
  equal(#models,1); equal(models[1].token,'current:second'); equal(r.styles[2],nil)
  equal(next(r.hooks),nil,'no structured host appearance hook should be installed')
  r:stop()
end)

test('Crest owns frame stacking and sends explicit hiding for crowded overflow',function()
  local r=runtime()
  local models=r:canvas({candidate(2,true),candidate(3,false),candidate(4,false)})
  equal(#models,3); equal(models[1].offset_y,0)
  check(models[2].hide or models[2].offset_y<0)
  local moved=false
  for _,model in ipairs(models) do if not model.hide and model.offset_y<0 then moved=true end end
  check(moved,'overlap layout must happen inside Crest')
  r.app.settings().max_stack=0; r.app.changed()
  models=r:canvas({candidate(2,true),candidate(3,false)})
  equal(models[1].hide,nil); equal(models[2].hide,true)
  equal(models[2].commands,nil,'crowded plate should be an explicit generic hide submission')
  r:stop()
end)

test('manual-click and wall-display options pass through while projection scale and offset stay local',function()
  local r=runtime(); local c=candidate(2,true)
  local models=r:canvas({c})
  equal(models[1].clickable,true); equal(models[1].through_walls,false)
  local s=r.app.settings(); s.clickable=false; s.through_walls=true; s.offset_y=7; r.app.changed()
  c=candidate(2,true); c.native_scale=0.75
  models=r:canvas({c}); local model=models[1]
  equal(model.clickable,false); equal(model.through_walls,true); equal(model.offset_y,0)
  check(math.abs(model.bounds.y+model.bounds.h+7*0.75*s.target_scale)<0.00001,'offset applied twice or scale lost')
  r:stop()
end)

test('real font measurement failure omits only that replacement instead of fabricating geometry',function()
  local r=runtime(); r.metrics_missing=true
  local models=r:canvas({candidate(2,true)})
  equal(#models,0,'missing metrics must leave the native caption eligible')
  r.metrics_missing=false; equal(#r:canvas({candidate(2,true)}),1)
  r:stop()
end)

test('known consider color and known aura art reach generic primitives without host recipes',function()
  local r=runtime{prepare=function(s)
    s.fs[path_for(s.server,'First')]={settings={texture='flat',show_auras=true,aura_mode='all'}}
    s.buffs[2]={{id=54321,seconds=42}}
    s.spells[54321]={id=54321,name='Target debuff',spell_type=0,spell_icon=37}; s.icons[37]=123
  end}
  local models=r:canvas({candidate(2,true)}); local image,health=false,false
  for _,command in ipairs(models[1].commands) do
    if command.kind=='Image' then equal(command.texture,123); equal(command.u0,0.1); equal(command.v1,0.4); image=true end
    if command.kind=='RectFilled' and command.color==0x6599ef then health=true end
  end
  check(image and health); equal(r.last_icon,37)
  r.buffs[2]=nil; models=r:canvas({candidate(2,true)})
  for _,command in ipairs(models[1].commands) do check(command.kind~='Image','unknown aura art must disappear immediately') end
  r:stop()
end)

test('a changed incarnation clears same-ID same-name observations before the next plate',function()
  local r=runtime{prepare=function(s)
    s.fs[path_for(s.server,'First')]={settings={show_auras=true,aura_mode='all'}}
    s.buffs[2]={{id=54321,seconds=42}}; s.xtargets={{spawn_id=2}}; s.player.xtarget_slots=1
    s.spells[54321]={id=54321,name='Prior target debuff',spell_type=0}
  end}
  r:packet(0x7d1c,marker(1,1,2))
  r:packet(0x3791,string.pack('<I4I4I2',100,200,2))
  local prior=r:plate(2,{hp_percent=90})
  -- First sighting must preserve legitimate observations already delivered.
  check(has_icon(prior,'star') and has_icon(prior,'circle')); equal(prior.mana.value,50)
  equal(prior.health.color,0x6599ef)
  r.con_color='RED'; r.incarnation=2
  local fresh=r:plate(2,{hp_percent=10})
  equal(fresh.health.value,10,'replacement actor must not inherit health smoothing')
  equal(fresh.mana,nil,'replacement actor must not inherit a remote resource pool')
  equal(#fresh.badges,0,'replacement actor must not inherit indicators or cached auras')
  -- Disable low-health coloring to isolate the refreshed consider lookup.
  r.app.settings().low_health_color=false; r.app.changed()
  equal(r:plate(2,{hp_percent=10}).health.color,0xf16b67)
  r:stop()
end)

test('zone changes clear incarnation identity without discarding fresh first-sighting observations',function()
  local r=runtime()
  r:plate(2); r.player.zone_id=20; r:step(); r.incarnation=2
  r:packet(0x7d1c,marker(1,1,2))
  check(has_icon(r:plate(2),'star'),'a new zone must not compare its first actor against an old identity')
  r:stop()
end)

test('aggregate command and geometry budgets prioritize targets and explicitly hide overflow',function()
  for _,case in ipairs({{count=70,commands=512,rounding=0,admitted=64},
    {count=12,commands=128,rounding=2,admitted=8}}) do
    -- A deliberately expensive, otherwise valid addon canvas layout exercises
    -- admission independently of the normal visual presets' modest costs.
    local renderer={MAX_FRAME_COMMANDS=Renderer.MAX_FRAME_COMMANDS,MAX_FRAME_VERTICES=Renderer.MAX_FRAME_VERTICES,
      vertex_cost=Renderer.vertex_cost,stack=Renderer.stack,
      prepare=function(style)
        if not style then return nil end
        local commands={}
        for _=1,case.commands do commands[#commands+1]={kind='RectFilled',x0=-20,y0=-20,x1=20,y1=0,
          rounding=case.rounding,color=0xffffff,alpha=1} end
        return {bounds={x=-21,y=-21,w=42,h=22},commands=commands,stack=false,through_walls=false,clickable=true}
      end}
    local r=runtime{renderer=renderer}; local candidates={}
    for id=1,case.count do candidates[#candidates+1]=candidate(id,id==case.count) end
    local models=r:canvas(candidates)
    equal(#models,case.count); check(not models[#models].hide,'target must reserve admission before nearer units')
    local admitted,commands,vertices=0,0,0
    for _,model in ipairs(models) do
      if not model.hide then admitted=admitted+1; commands=commands+#model.commands; vertices=vertices+Renderer.vertex_cost(model.commands) end
    end
    equal(admitted,case.admitted); check(commands<=Renderer.MAX_FRAME_COMMANDS); check(vertices<=Renderer.MAX_FRAME_VERTICES)
    r:stop()
  end
end)

test('a fresh exact pool received before the first replacement frame survives identity validation',function()
  local r=runtime{prepare=function(s) s.xtargets={{spawn_id=2}}; s.player.xtarget_slots=1 end}
  r:packet(0x3791,string.pack('<I4I4I2',100,200,2))
  equal(r:plate(2).mana.value,50)
  r.incarnation=2
  -- Resource tracking reads the new public identity before accepting this pool;
  -- the later canvas candidate has that same identity and must keep fresh data.
  r:packet(0x3791,string.pack('<I4I4I2',60,200,2))
  equal(r:plate(2).mana.value,30)
  r:stop()
end)

test('smoothing and pruning share addon time even when host frame timestamps have another epoch',function()
  local r=runtime()
  local c=candidate(2,true); c.context.hp_percent=90
  r:canvas({c},{now_ms=1000000000000,dt_ms=16})
  equal(r.engine.count,1); equal(r.engine.cache[2].seen,0)
  c=candidate(2,true); c.context.hp_percent=10
  r.time=100; r:canvas({c},{now_ms=1000000000100,dt_ms=16})
  check(r.styles[2].health.value>10 and r.styles[2].health.value<90,'health should smooth in addon milliseconds')
  r:step(6000)
  equal(r.engine.count,0,'old smoothing entries must expire despite unrelated host clock epoch')
  c=candidate(2,true); c.context.hp_percent=20
  r:canvas({c},{now_ms=1000000006100,dt_ms=16})
  equal(r.styles[2].health.value,20,'fresh observation must snap after pruning')
  r:stop()
end)

for _,failure in ipairs(failures) do io.stderr:write(failure..'\n') end
assert(#failures==0,string.format('%d / %d addon tests failed',#failures,tests))
print(string.format('Addon integration: %d tests, %d assertions passed',tests,assertions))
