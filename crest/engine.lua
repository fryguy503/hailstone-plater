-- SPDX-License-Identifier: MIT
-- Pure presentation decisions. No commands, targeting, network, files or game memory.
local C = require('crest.config')
local M = {}
local classes={"Warrior","Cleric","Paladin","Ranger","Shadow Knight","Druid","Monk","Bard","Rogue","Shaman","Necromancer","Wizard","Magician","Enchanter","Beastlord","Berserker"}
local class_colors={0xc79c6e,0xffffff,0xf58cba,0xabd473,0xc41f3b,0xff7d0a,0x00ff96,0xe6cc80,0xfff569,0x0070de,0x9482c9,0x69ccf0,0x40c7eb,0xa78be8,0x7fbb88,0xe89569}
local concealment_tags={
  {label='INV',color=0x79def3,text_only=true},
  {label='IVU',color=0xc4a6ff,text_only=true},
  {label='INV+IVU',color=0xffd989,text_only=true},
  {label='HID',color=0xaebccc,text_only=true},
}
local function percent(n) return C.finite(n) and math.max(0,math.min(100,n)) or nil end
local function clean(name) return C.text(name,128):gsub('^#+',''):gsub('%d+$',''):gsub('_',' ') end
local function trim_name(text, max)
  -- Byte-safe UTF-8 truncation; do not leave a partial continuation sequence.
  local n=0
  for p in text:gmatch('()[\0-\127\194-\244]') do
    n=n+1; if n>max then return text:sub(1,p-1)..'...' end
  end
  return text
end
local function blend(a,b,t)
  local function channel(shift) return math.floor(((a>>shift)&255)*(1-t)+((b>>shift)&255)*t+0.5) end
  return (channel(16)<<16)|(channel(8)<<8)|channel(0)
end
local function category(spawn, ctx)
  if ctx.self then return 'self' end
  if spawn.corpse then return 'corpse' end
  if spawn.mercenary then return 'mercenary' end
  if (spawn.owner or 0)>0 then return 'pet' end
  if ctx.in_group then return 'group' end
  return spawn.npc and 'npc' or 'player'
end
local function matches(r,spawn,kind,name)
  if not r.enabled then return false end
  if r.kind~='any' and r.kind~=kind and not (r.kind=='player' and (kind=='group' or kind=='self')) then return false end
  local needle=r.name:lower()
  return r.exact and name==needle or (not r.exact and name:find(needle,1,true)~=nil)
end
function M.new()
  return setmetatable({cache={},count=0,last_prune=0,styles={}},{__index=M})
end
function M:reset() self.cache={}; self.count=0; self.last_prune=0; self.styles={} end
function M:remove(id) if self.cache[id] then self.cache[id]=nil; self.count=self.count-1 end end
function M:smooth(id,name,kind,value,now,tau)
  local state=self.cache[id]
  if not state or state.name~=name or now<state.seen then
    if not state then
      if self.count>=1024 then self:reset() end
      self.count=self.count+1
    end
    state={name=name,seen=now}; self.cache[id]=state
  end
  local old=state[kind]
  local at=state[kind..'_at'] or now
  -- Stale/reused entities snap rather than animating health from a former occupant.
  local result=(not old or now-at>1000 or tau<=0 or value==0) and value or old+(value-old)*(1-math.exp(-math.max(0,now-at)/tau))
  state[kind]=result; state[kind..'_at']=now; state.seen=now
  return result
end
function M:prune(now)
  if now-self.last_prune<2000 then return end
  self.last_prune=now
  for id,s in pairs(self.cache) do if now-s.seen>5000 or now<s.seen then self:remove(id) end end
end
function M:presentation(spawn,ctx,s,extra)
  if not s.enabled then return nil end
  extra=extra or {}
  local kind=category(spawn,ctx)
  local style_key=kind..(extra.combat and '_combat' or '_rest')
  if self.settings~=s then self.styles={}; self.settings=s end
  if not self.styles[style_key] then self.styles[style_key]=C.effective(s,kind,extra.combat) end
  s=self.styles[style_key]
  local visible={npc=s.show_npcs,player=s.show_players,group=s.show_group,pet=s.show_pets,
    mercenary=s.show_mercenaries,corpse=s.show_corpses,self=s.show_self}
  local hp=percent(ctx.hp_percent)
  local targeted=ctx.target==true
  local distance=C.finite(ctx.distance) and ctx.distance or 0
  if not visible[kind] or distance>s.max_distance or (s.target_only and not targeted) or
    (s.combat_only and not extra.combat) or (s.injured_only and not targeted and hp and hp>=100) then return {hide=true} end
  local name=clean(spawn.name or '')
  local rule
  for _,r in ipairs(s.rules) do if matches(r,spawn,kind,name:lower()) then rule=r; break end end
  if rule and rule.hide then return {hide=true} end
  local con_text=s.con_name_color and not spawn.corpse and (s.show_name or s.show_level)
  local con_health=s.color_mode=='consider' and kind=='npc'
  local con
  if con_text or con_health then
    con=extra.con_color or (extra.get_con_color and extra.get_con_color(spawn))
    if not C.finite(con) then con=nil end
  end
  local color=kind=='corpse' and s.corpse_color or kind=='pet' and s.pet_color or
    kind=='group' and s.group_color or (kind=='player' or kind=='self') and s.player_color or s.npc_color
  if s.color_mode=='class' then color=class_colors[spawn.class] or color
  elseif s.color_mode=='health' and hp then color=hp<50 and blend(0xf15260,0xecc55a,hp/50) or blend(0xecc55a,0x59d9a0,(hp-50)/50)
  elseif con_health and con then color=con
  elseif s.color_mode=='custom' then color=s.npc_color end
  if hp and s.low_health_color and hp<=s.low_health then color=s.low_color end
  if rule then color=rule.color end
  if not rule and s.unit_color then color=s.unit_color end
  local now=extra.now_ms or ctx.now_ms or 0
  local alpha=s.alpha*(extra.has_target and not targeted and s.non_target_alpha or 1)
  if s.distance_fade and not targeted then alpha=alpha*(1-0.65*math.max(0,(distance/s.max_distance-0.65)/0.35)) end
  local p={name=C.text(trim_name(s.short_names and name or C.text(ctx.name or name,256),math.floor(s.max_name)),256),
    name_color=con_text and con or s.text_color,level_color=con_text and con or s.text_color,
    level=s.show_level and ('Lv '..tostring(spawn.level or '?')) or '',level_position=s.level_position,
    show_name=s.show_name,show_marker=targeted and s.target_marker,
    width=targeted and s.target_width>0 and math.max(40,s.target_width) or s.width,
    height=s.height,mana_height=s.mana_height,cast_height=s.cast_height,
    font_scale=s.font_scale,scale=math.max(0.5,math.min(2,s.scale*(targeted and s.target_scale or 1)*(rule and rule.scale or 1))),
    offset_y=s.offset_y,alpha=math.max(0.05,alpha),text_color=s.text_color,
    border_color=targeted and s.target_color or s.border_color,background_color=s.background_color,badges={},
    texture=s.texture,name_position=s.name_position,text_align=s.text_align,border_width=s.border_width,
    rounding=s.rounding,stack=s.stack,stack_gap=s.stack_gap,max_stack=s.max_stack,
    frame_style=s.frame_style,target_style=s.target_style,badge_position=s.badge_position,badge_scale=s.badge_scale,
    cast_gap=s.cast_gap,cast_width=s.cast_width,health_text_align=s.health_text_align,cast_text_align=s.cast_text_align,
    through_walls=s.through_walls,clickable=s.clickable}
  if s.low_health_pulse and hp and hp<=s.low_health then
    p.border_color=blend(s.border_color,s.low_color,0.5+0.5*math.sin(now/180))
  end
  local sub={}
  if s.show_class and classes[spawn.class] then sub[#sub+1]=classes[spawn.class] end
  if s.show_distance then sub[#sub+1]=string.format('%.0fu',distance) end
  p.subtitle=table.concat(sub,' | ')
  local names_only=s.unit_names_only or (s.friendly_names_only and (kind=='player' or kind=='group'))
  if s.show_health and not names_only and hp and not spawn.corpse then
    local text=''
    if s.health_text~='none' then
      text=string.format('%.0f%%',hp)
      if ctx.self and C.finite(extra.current_hp) and C.finite(extra.max_hp) and s.health_text~='percent' then
        text=string.format('%.0f / %.0f',extra.current_hp,extra.max_hp)..(s.health_text=='both' and ('  '..text) or '')
      end
    end
    p.health={value=self:smooth(spawn.id,spawn.name,'hp',hp,now,s.smoothing_ms),color=color,text=text}
  end
  local mana=percent(ctx.mana_percent) or percent(extra.remote_mana)
  local endurance=percent(ctx.endurance_percent) or percent(extra.remote_endurance)
  local resource=s.resource=='mana' and mana or s.resource=='endurance' and endurance
  if resource and not names_only and not spawn.corpse and (not s.resource_target_only or targeted) then
    p.mana={value=self:smooth(spawn.id,spawn.name,'resource_'..s.resource,resource,now,s.smoothing_ms),
      color=s.resource=='mana' and s.mana_color or s.endurance_color,
      text=s.resource_text and string.format('%.0f%%',resource) or ''}
  end
  local cast=ctx.casting
  if not names_only and s.show_cast and (not s.cast_target_only or targeted) and cast and C.finite(cast.duration_ms) and cast.duration_ms>0 and
    C.finite(cast.elapsed_ms) and cast.elapsed_ms>=0 and cast.elapsed_ms<cast.duration_ms and not spawn.corpse then
    local text=s.cast_name and C.text(cast.name,80) or ''
    if s.cast_time then text=text..(text~='' and ' ' or '')..string.format('%.1fs',(cast.duration_ms-cast.elapsed_ms)/1000) end
    local progress=100*cast.elapsed_ms/cast.duration_ms
    p.cast={value=s.cast_reverse and 100-progress or progress,color=s.cast_color,text=text}
    if s.highlight_cast and not targeted then p.border_color=s.cast_color end
  end
  local function badge(b) if #p.badges<8 then p.badges[#p.badges+1]=b end end
  local display=s.concealment_display
  if display~='Off' and not spawn.corpse then
    local flags=extra.npc_concealment or ((ctx.invisible==true and 1 or 0)+(ctx.invisible_undead==true and 2 or 0))
    local tag=concealment_tags[flags] or (ctx.concealed==true and concealment_tags[4])
    if tag then
      if display=='Parentheses only' or display=='Parentheses + tags' then p.name_prefix,p.name_suffix='(',')' end
      if display=='Name suffix' and p.show_name and p.name~='' then
        p.name_suffix=' ['..tag.label..']'
        p.name_fallback_badge=tag
      elseif display~='Parentheses only' then
        badge(tag)
      end
    end
  end
  for _,b in ipairs(extra.server_indicators or {}) do
    local awareness=b.icon=='eye'; local status=b.icon=='peace' or b.icon=='sleep'
    if (awareness and s.server_indicators_awareness) or (status and s.server_indicators_status) or
      (not awareness and not status and s.server_indicators_marks) then badge(b) end
  end
  if s.stock_marks then
    if (ctx.raid_mark or 0)>0 then badge{icon='circle',label='R'..ctx.raid_mark,color=0xffb974} end
    if (ctx.group_mark or 0)>0 then badge{icon='circle',label='G'..ctx.group_mark,color=0xffe073} end
  end
  local aggro=percent(ctx.aggro_percent)
  if aggro then
    if s.show_aggro then badge{icon='sword',label=string.format('%.0f%%',aggro),color=s.aggro_color} end
    if s.highlight_aggro and aggro>=s.aggro_threshold and not targeted then p.border_color=s.aggro_color end
  end
  if s.show_auras then for i=1,math.min(#(extra.auras or {}),math.floor(s.max_auras)) do badge(extra.auras[i]) end end
  return p
end
M.classes=classes
M.class_colors=class_colors
M.category=category
return M
