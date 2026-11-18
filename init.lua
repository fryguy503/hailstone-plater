-- SPDX-License-Identifier: MIT
local mq=require('mq')
local hs=require('hailstone')
local ui=require('ImGui')
local C=require('crest.config')
local Engine=require('crest.engine')
local Renderer=require('crest.renderer')
local Server=require('crest.server_indicators')
local Panel=require('crest.panel')
local Personal=require('crest.personal')
local Resources=require('crest.resources')
local engine,server_indicators=Engine.new(),Server.new()
local resources,tracked_resources=Resources.new(),{}
local store=C.store()
local player,loaded_path,world_key,dirty_at=nil,nil,nil,nil
local aura_by_id,con_by_id,con_count={},{},0
local incarnations,incarnation_count={},0
local running,combat=true,false
local stats={plates=0,last_packet=0,packets=0,error=nil}
local panel,update_registration
local nameplates=type(hs.nameplates)=='table' and hs.nameplates or nil
local registered,registration_failed=false,false
local function say(s) hs.chat('[Crest] '..s,15) end
local function api_version()
  if not nameplates then return 0 end
  for _,method in ipairs({'Register','Unregister','MeasureText','SpellIcon'}) do
    if type(nameplates[method])~='function' then return 0 end
  end
  return tonumber(nameplates.version) or 0
end
local function now() return mq.gettime() end
local function encode(s) return (tostring(s or ''):gsub('.',function(c) return string.format('%02x',c:byte()) end)) end
local function first_text(...)
  for i=1,select('#',...) do
    local value=select(i,...)
    if type(value)=='string' and value~='' then return value end
  end
  return ''
end
local function settings_path(p)
  local server=hs.server()
  if not server or not p or p.name=='' or p.game_state~=5 or not p.in_zone then return nil end
  -- Stable world identity, collision-free encoding, no player/server text becomes a path separator.
  local identity=first_text(server.world_host,server.host,server.name)
  local world=first_text(server.world,server.name)
  return mq.configDir..'/crest/characters/'..encode(identity)..'_'..encode(world)..'/'..encode(p.name)..'.lua'
end
local function save()
  if not loaded_path then return false end
  local ok,why=pcall(mq.pickle,loaded_path,store)
  if ok then dirty_at=nil; stats.error=nil else stats.error='Settings could not be saved: '..tostring(why) end
  return ok
end
local function changed()
  dirty_at=now(); engine:reset(); registration_failed=false
  if update_registration then update_registration() end
end
local function replace(settings) store.settings=C.repair(settings); changed() end
local function reset_world()
  server_indicators:reset(); engine:reset(); resources:reset(); tracked_resources={}
  aura_by_id={}; con_by_id={}; con_count=0; stats.plates=0; stats.last_packet=0; stats.packets=0
  incarnations={}; incarnation_count=0
end
local function forget_actor(id)
  server_indicators:remove(id); engine:remove(id); resources:remove(id)
  tracked_resources[id]=nil; aura_by_id[id]=nil
  if con_by_id[id] then con_by_id[id]=nil; con_count=con_count-1 end
end
local function observe_incarnation(spawn)
  local id,value=spawn.id,spawn.incarnation
  if not C.finite(id) or not C.finite(value) or value<0 or value%1~=0 then return end
  local previous=incarnations[id]
  if previous~=nil and previous~=value then forget_actor(id) end
  if previous==nil then
    -- A crowded moving population must not grow the identity cache forever.
    -- Dropping observations along with a full cache keeps later reuse honest.
    if incarnation_count>=1024 then reset_world() end
    incarnation_count=incarnation_count+1
  end
  incarnations[id]=value
end
local function sync()
  local p=hs.player()
  local path=settings_path(p)
  if path~=loaded_path then
    -- Save before changing identity; no settings leak from one character to another.
    if dirty_at and loaded_path and not save() then
      player=nil; reset_world()
      if update_registration then update_registration() end
      return false
    end
    loaded_path=path
    store=C.store()
    dirty_at=nil
    if path then
      local chunk=loadfile(path)
      if chunk then
        local ok,value=pcall(chunk)
        if ok and type(value)=='table' then store=C.store(value)
        else stats.error='Saved settings could not be read. Defaults are active.' end
      end
    end
    reset_world()
    registration_failed=false
  end
  local key=p and p.in_zone and table.concat({path or '',p.zone_id or 0,p.spawn_id or 0,
    p.in_group and 'group' or 'solo',p.in_raid and 'raid' or 'party'},':') or nil
  if key~=world_key then world_key=key; reset_world() end
  player=p
  combat=p and p.in_zone and (mq.TLO.Me.CombatState()=='COMBAT' or mq.TLO.Me.Combat()==true) or false
  if update_registration then update_registration() end
  return path~=nil
end
local function refresh_resources()
  local tracked={}
  if player and player.in_zone then
    local function add(id)
      if type(id)~='number' or id<=0 or id==player.spawn_id then return end
      local spawn=hs.spawn(id)
      if spawn and not spawn.corpse and type(spawn.name)=='string' then
        observe_incarnation(spawn)
        tracked[id]=spawn.name
      end
    end
    local size=math.min(64,mq.TLO.Group.GroupSize() or 0)
    for i=0,size-1 do add(mq.TLO.Group.Member(i).Spawn.ID()) end
    for i=1,math.min(64,player.xtarget_slots or 0) do
      local slot=hs.xtarget(i)
      if slot then add(slot.spawn_id) end
    end
  end
  tracked_resources=tracked
  resources:prune(tracked)
end
local con_colors={GREY=0x999999,GREEN=0x58d07d,['LIGHT BLUE']=0x6ecfe4,BLUE=0x6599ef,WHITE=0xeeeeee,YELLOW=0xf1d85a,RED=0xf16b67}
local function read_auras()
  aura_by_id={}
  local s=store.settings
  if not s.show_auras or not player then return end
  local ids={player.spawn_id}
  if player.target_id~=0 and player.target_id~=player.spawn_id then ids[#ids+1]=player.target_id end
  for _,id in ipairs(ids) do
    local spawn=mq.TLO.Spawn(id)
    if spawn.BuffsPopulated() then
      local buffs={}
      for i=1,math.min(64,spawn.BuffCount() or 0) do
        local buff=spawn.Buff(i)
        local spell_id=buff.Spell.ID()
        local spell=spell_id and hs.spell(spell_id)
        if spell then
          local match=s.aura_mode=='all' or (s.aura_mode=='detrimental' and spell.spell_type==0)
          if s.aura_mode=='whitelist' then
            for word in s.aura_names:gmatch('[^,]+') do
              word=word:match('^%s*(.-)%s*$'):lower()
              if word~='' and spell.name:lower():find(word,1,true) then match=true; break end
            end
          end
          if match then
            local label=C.text(spell.name,20)
            local seconds=spawn.BuffDuration(i).TotalSeconds()
            if seconds and seconds>0 then label=label..string.format(' %.0fs',seconds) end
            local badge={icon='circle',label=C.text(label,32),color=spell.spell_type==0 and 0xf38696 or 0x83dca2}
            if C.finite(spell.spell_icon) and spell.spell_icon>=0 and spell.spell_icon<=65535 then
              badge.spell_icon=math.floor(spell.spell_icon)
            end
            buffs[#buffs+1]=badge
            if #buffs>=math.floor(s.max_auras) then break end
          end
        end
      end
      aura_by_id[id]={name=spawn.Name(),badges=buffs}
    end
  end
end
local function receive(opcode,data)
  -- Always observe only: a return value would let a packet hook replace/drop server traffic.
  if sync() then
    if server_indicators:receive(opcode,data,now()) then stats.packets=stats.packets+1; stats.last_packet=now() end
  end
end
local packet_ok,packet_error=pcall(function()
hs.on_packet('zone','in',0x7d1c,receive)
hs.on_packet('zone','in',0x7d1d,receive)
-- RoF2 OP_DeleteSpawn: the host reads the first little-endian u32 (optional fifth byte is its decay flag).
-- Clear observations before a later actor can reuse the same numeric id. Always leave traffic unchanged.
hs.on_packet('zone','in',0x7280,function(_,data)
  if type(data)=='string' and #data>=4 then
    local id=string.unpack('<I4',data)
    forget_actor(id)
    if incarnations[id]~=nil then incarnations[id]=nil; incarnation_count=incarnation_count-1 end
  end
end)
local function receive_points(opcode,data)
  if sync() then
    refresh_resources()
    resources:receive(opcode,data,tracked_resources)
  end
end
hs.on_packet('zone','in',0x3791,receive_points)
hs.on_packet('zone','in',0x5f42,receive_points)
end)
if not packet_ok then stats.packet_error='Optional server icons unavailable: '..C.text(tostring(packet_error),180) end
local function canvas_frame(frame)
  -- Every token belongs to this callback/frame alone. Never keep an anchor or a
  -- borrowed spawn/context table beyond this call; retained caches hold scalars.
  if not running or not store.settings.enabled or not player or not player.in_zone or not loaded_path then return {} end
  if type(frame)~='table' or type(frame.candidates)~='table' then return {} end
  local records={}
  local metrics={measure=function(text,size,font)
    local value,height,x0,y0,x1,y1=nameplates.MeasureText(text,size,font)
    if type(value)=='table' then return value end
    if value~=nil then return {width=value,height=height,ink_x0=x0,ink_y0=y0,ink_x1=x1,ink_y1=y1} end
  end,texture=function(icon)
    local value,u0,v0,u1,v1=nameplates.SpellIcon(icon)
    if type(value)=='table' then return value end
    if value~=nil then return {texture=value,u0=u0,v0=v0,u1=u1,v1=v1} end
  end}
  local time=now()
  for i=1,math.min(1024,#frame.candidates) do
    local candidate=frame.candidates[i]
    local spawn=type(candidate)=='table' and candidate.spawn
    local source=type(candidate)=='table' and candidate.context
    if type(spawn)=='table' and type(source)=='table' and type(candidate.token)=='string' then
      observe_incarnation(spawn)
      local ctx={}
      for k,v in pairs(source) do ctx[k]=v end
      -- Smoothing and pruning share the addon's clock. The host's monotonic
      -- frame clock can have a different epoch from mq.gettime().
      ctx.now_ms=time
      ctx.dt_ms=C.finite(frame.dt_ms) and frame.dt_ms or 0
      local auras=aura_by_id[spawn.id]
      local remote_mana,remote_endurance=resources:values(spawn.id,spawn.name)
      local extra={has_target=player.target_id~=0,combat=combat,
        current_hp=player.cur_hp,max_hp=player.max_hp,
        remote_mana=remote_mana,remote_endurance=remote_endurance,
        server_indicators=server_indicators:badges(spawn.id,time,player.in_raid==true),
        npc_concealment=spawn.npc and not spawn.corpse and server_indicators:concealment(spawn.id,time) or nil,
        auras=auras and auras.name==spawn.name and (ctx.self or ctx.target) and
          mq.TLO.Spawn(spawn.id).BuffsPopulated()==true and auras.badges or nil}
      local cached=con_by_id[spawn.id]
      if not cached or cached.name~=spawn.name or time-cached.at>1000 then
        cached={at=time,name=spawn.name,color=con_colors[mq.TLO.Spawn(spawn.id).ConColor()]}
        if not con_by_id[spawn.id] then con_count=con_count+1 end
        if con_count>1024 then con_by_id={}; con_count=1 end
        con_by_id[spawn.id]=cached
      end
      extra.con_color=cached.color
      stats.plates=stats.plates+1
      local style=engine:presentation(spawn,ctx,store.settings,extra)
      local model=Renderer.prepare(style,{name=ctx.name,target=ctx.target,font_size=frame.font_size,font=frame.font,
        scale=candidate.native_scale,now_ms=ctx.now_ms},metrics)
      if model then records[#records+1]={model=model,x=candidate.x,y=candidate.y,depth=candidate.depth,
        id=spawn.id,target=ctx.target,token=candidate.token} end
    end
  end
  -- Admit the target and nearest plates first. Explicit hides retain caption
  -- ownership for overflow instead of exceeding the public frame budget and
  -- having the host reject an otherwise valid complete submission.
  local priority={}
  for _,record in ipairs(records) do priority[#priority+1]=record end
  table.sort(priority,function(a,b)
    if (a.target==true)~=(b.target==true) then return a.target==true end
    if a.depth~=b.depth then return a.depth<b.depth end
    return a.id<b.id
  end)
  local remaining,vertices=Renderer.MAX_FRAME_COMMANDS,Renderer.MAX_FRAME_VERTICES
  for _,record in ipairs(priority) do
    local cost=#record.model.commands
    local weight=Renderer.vertex_cost(record.model.commands)
    if cost>remaining or weight>vertices then record.model={hide=true,bounds={x=0,y=0,w=0,h=0},commands={}}
    else remaining=remaining-cost; vertices=vertices-weight end
  end
  Renderer.stack(records,frame.viewport)
  local models={}
  for _,record in ipairs(records) do
    local model=record.model
    if record.hidden or model.hide then models[#models+1]={token=record.token,hide=true}
    else
      -- Only the generic public canvas fields leave Crest. The host knows no
      -- health bars, skins, indicator types or stacking policy.
      local commands={}
      for _,command in ipairs(model.commands) do
        local primitive={}
        for k,v in pairs(command) do if k~='bounds' then primitive[k]=v end end
        commands[#commands+1]=primitive
      end
      models[#models+1]={token=record.token,bounds=model.bounds,commands=commands,offset_y=record.offset_y,
        through_walls=model.through_walls,clickable=model.clickable}
    end
  end
  return models
end
local function detach()
  if registered then
    local ok,why=pcall(nameplates.Unregister,registered)
    if not ok then stats.canvas_error='Nameplate provider could not be released: '..C.text(tostring(why),160) end
    registered=false
  end
end
update_registration=function()
  local wanted=running and store.settings.enabled and player and player.in_zone and loaded_path~=nil and api_version()>=1
  if not wanted then detach(); return end
  if registered or registration_failed then return end
  local ok,value,why=pcall(nameplates.Register,'crest',canvas_frame)
  if ok and type(value)=='string' and value~='' then registered=value; stats.canvas_error=nil
  else
    registration_failed=true
    stats.canvas_error='World plates unavailable: '..C.text(tostring(ok and (why or 'registration refused') or value),160)
  end
end
if api_version()<1 then say('Update Hailstone for the generic nameplate canvas UI API. Ordinary nameplates are active.') end
panel=Panel.new(ui,{
  settings=function() return store.settings end,profiles=function() return store.profiles end,
  api_version=api_version,changed=changed,replace=replace,save=save,
  profile_save=function(name)
    name=C.text(name,32):match('^%s*(.-)%s*$')
    if name=='' then return 'Enter a profile name.' end
    local count=0; for _ in pairs(store.profiles) do count=count+1 end
    if not store.profiles[name] and count>=C.MAX_PROFILES then return 'Profile limit reached.' end
    store.profiles[name]=C.repair(store.settings); changed(); return 'Saved '..name..'.'
  end,
  status=function()
    local lines={'Crest 0.3.0-preview.2',
      loaded_path and 'Character settings loaded.' or 'Waiting for a character in a zone.',
      stats.last_packet>0 and 'Optional indicators are receiving supported data.' or 'Optional indicators are waiting for supported data.',
      'HP / mana / casts / auras reflect only information supplied by your server.'}
    if stats.error then lines[#lines+1]=stats.error end
    if stats.packet_error then lines[#lines+1]=stats.packet_error end
    if stats.canvas_error then lines[#lines+1]=stats.canvas_error end
    return lines
  end,
})
mq.imgui.init('crest',function()
  panel:draw()
  local casting=player and player.casting_spell_id
  Personal.draw(ui,store.settings,player,changed,casting and casting>0 and hs.spell(casting) or nil)
end)
hs.menu('Crest','/crest')
mq.bind('/crest',function(args)
  args=(args or ''):lower():match('^%s*(.-)%s*$')
  if args=='on' or args=='off' or args=='toggle' then
    store.settings.enabled=args=='on' or (args=='toggle' and not store.settings.enabled); changed()
  elseif args=='save' then say(save() and 'Settings saved.' or 'Settings are not saved; see Preview & status.')
  elseif args=='status' then say('Crest is '..(store.settings.enabled and 'enabled' or 'disabled')..'.')
  elseif args=='stop' then if not dirty_at or save() then running=false; detach() end
  elseif args=='help' then say('/crest [on|off|toggle|save|status|stop] — open settings with /crest.')
  else panel.open=not panel.open end
end)
sync()
say('Loaded. /crest opens settings. /crest off restores ordinary plates.')
local next_auras=0
local loop_ok,loop_error=xpcall(function() while running do
  sync()
  refresh_resources()
  local time=now()
  engine:prune(time)
  if time>=next_auras then read_auras(); con_by_id={}; con_count=0; next_auras=time+500 end
  if dirty_at and time-dirty_at>=1000 then save() end
  mq.delay(100)
end end,function(why) return tostring(why) end)
running=false
detach()
mq.imgui.destroy('crest')
if not loop_ok then error(loop_error,0) end
mq.exit()
