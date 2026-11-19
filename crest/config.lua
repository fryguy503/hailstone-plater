-- SPDX-License-Identifier: MIT
-- Data-only settings. Every disk/import value passes through this schema.
local M = {VERSION = 1, MAX_RULES = 64, MAX_PROFILES = 16}
M.schema = {
  {"general", "enabled", "Enable replacement", "bool", true},
  {"general", "clickable", "Click plates to target", "bool", true},
  {"general", "through_walls", "Show plates through walls", "bool", false},
  {"general", "max_distance", "Maximum distance", "number", 200, 20, 250},
  {"general", "target_only", "Only the current target", "bool", false},
  {"general", "combat_only", "Only while in combat", "bool", false},
  {"general", "injured_only", "Hide full health (except target)", "bool", false},
  {"general", "show_npcs", "NPCs", "bool", true},
  {"general", "show_players", "Players", "bool", true},
  {"general", "show_group", "Group members", "bool", true},
  {"general", "show_pets", "Pets", "bool", true},
  {"general", "show_mercenaries", "Mercenaries", "bool", true},
  {"general", "show_corpses", "Corpses", "bool", false},
  {"general", "show_self", "Your own plate", "bool", false},
  {"general", "friendly_names_only", "Players use names only", "bool", false},
  {"layout", "width", "Bar width", "number", 142, 40, 400},
  {"layout", "height", "Health height", "number", 12, 3, 40},
  {"layout", "mana_height", "Resource height", "number", 4, 2, 20},
  {"layout", "cast_height", "Cast height", "number", 9, 3, 30},
  {"layout", "scale", "Global scale", "number", 1, 0.5, 2},
  {"layout", "target_scale", "Target scale multiplier", "number", 1.15, 1, 1.5},
  {"layout", "font_scale", "Text size multiplier", "number", 1, 0.6, 1.6},
  {"layout", "offset_y", "Vertical offset", "number", 0, -100, 100},
  {"layout", "alpha", "Opacity", "number", 1, 0.15, 1},
  {"layout", "non_target_alpha", "Other plates while targeting", "number", 0.7, 0.15, 1},
  {"layout", "distance_fade", "Fade at maximum distance", "bool", true},
  {"layout", "smoothing_ms", "Bar smoothing (milliseconds)", "number", 120, 0, 500},
  {"appearance", "frame_style", "Frame design", "enum", "solid", {"minimal", "solid", "bevel", "glass", "runic"}},
  {"appearance", "texture", "Bar texture", "enum", "gradient", {"flat", "gradient", "gloss", "segments"}},
  {"appearance", "target_style", "Target decoration", "enum", "arrow", {"arrow", "chevrons", "brackets", "glow", "none"}},
  {"appearance", "badge_position", "Indicator placement", "enum", "above", {"above", "below", "left", "right"}},
  {"appearance", "badge_scale", "Indicator scale", "number", 1, 0.5, 2.5},
  {"appearance", "cast_gap", "Cast bar separation", "number", 2, 0, 24},
  {"appearance", "cast_width", "Cast bar width (0 inherits)", "number", 0, 0, 400},
  {"appearance", "health_text_align", "Health text alignment", "enum", "center", {"left", "center", "right"}},
  {"appearance", "cast_text_align", "Cast text alignment", "enum", "center", {"left", "center", "right"}},
  {"appearance", "name_position", "Name position", "enum", "above", {"above", "inside", "below"}},
  {"appearance", "text_align", "Name alignment", "enum", "center", {"left", "center", "right"}},
  {"appearance", "border_width", "Border thickness", "number", 1, 0, 4},
  {"appearance", "rounding", "Corner rounding", "number", 2, 0, 12},
  {"appearance", "stack", "Separate overlapping plates", "bool", true},
  {"appearance", "stack_gap", "Stack spacing", "number", 4, 0, 20},
  {"appearance", "max_stack", "Maximum stack displacement", "number", 96, 0, 200},
  {"combat", "combat_scale", "In combat scale multiplier", "number", 1, 0.5, 1.5},
  {"combat", "rest_scale", "Out of combat scale multiplier", "number", 1, 0.5, 1.5},
  {"combat", "rest_alpha", "Out of combat opacity multiplier", "number", 1, 0.15, 1},
  {"combat", "combat_width", "In combat width (0 inherits)", "number", 0, 0, 400},
  {"combat", "rest_width", "Out of combat width (0 inherits)", "number", 0, 0, 400},
  {"combat", "combat_height", "In combat height (0 inherits)", "number", 0, 0, 40},
  {"combat", "rest_height", "Out of combat height (0 inherits)", "number", 0, 0, 40},
  {"combat", "target_width", "Target width (0 inherits)", "number", 0, 0, 400},
  {"combat", "resource_target_only", "Resource bar only on target", "bool", false},
  {"combat", "cast_target_only", "Cast bar only on target", "bool", false},
  {"combat", "low_health_pulse", "Pulse low health border", "bool", false},
  {"personal", "personal_bar", "Personal resource window", "bool", false},
  {"personal", "personal_locked", "Lock personal window position", "bool", false},
  {"personal", "personal_width", "Personal window width", "number", 280, 140, 600},
  {"personal", "personal_height", "Personal health bar height", "number", 22, 12, 50},
  {"personal", "personal_mana", "Personal mana bar", "bool", true},
  {"personal", "personal_endurance", "Personal endurance bar", "bool", true},
  {"text", "show_name", "Show names", "bool", true},
  {"text", "short_names", "Use short names", "bool", true},
  {"text", "con_name_color", "Color name and level by con", "bool", false},
  {"text", "show_level", "Show level", "bool", true},
  {"text", "level_position", "Level position", "enum", "above", {"above", "below"}},
  {"text", "show_class", "Show class", "bool", false},
  {"text", "show_distance", "Show distance", "bool", false},
  {"text", "max_name", "Maximum name characters", "number", 36, 8, 80},
  {"bars", "health_text", "Health label", "enum", "percent", {"percent", "current", "both", "none"}},
  {"bars", "show_health", "Show health bars", "bool", true},
  {"bars", "resource", "Resource bar", "enum", "mana", {"mana", "endurance", "off"}},
  {"bars", "resource_text", "Resource percentage", "bool", false},
  {"bars", "color_mode", "Health color", "enum", "consider", {"consider", "class", "health", "custom"}},
  {"bars", "low_health", "Low health threshold", "number", 25, 0, 100},
  {"bars", "low_health_color", "Highlight low health", "bool", true},
  {"casts", "show_cast", "Show observed casts", "bool", true},
  {"casts", "cast_name", "Show spell name", "bool", true},
  {"casts", "cast_time", "Show remaining cast time", "bool", true},
  {"casts", "cast_reverse", "Cast bar counts down", "bool", false},
  {"casts", "highlight_cast", "Highlight casting plates", "bool", true},
  {"indicators", "target_marker", "Target arrow", "bool", true},
  {"indicators", "stock_marks", "Group / raid numeric marks", "bool", true},
  {"indicators", "concealment_display", "Concealment display", "enum", "Parentheses + tags", {"Parentheses + tags", "Parentheses only", "Tags", "Name suffix", "Off"}},
  {"indicators", "server_indicators_awareness", "Concealment awareness eye", "bool", true},
  {"indicators", "server_indicators_status", "Pacify and mez icons", "bool", true},
  {"indicators", "server_indicators_marks", "Shared marker symbols", "bool", true},
  {"indicators", "show_aggro", "Show known aggro percentage", "bool", false},
  {"indicators", "highlight_aggro", "Highlight known high aggro", "bool", false},
  {"indicators", "aggro_threshold", "High aggro threshold", "number", 90, 1, 100},
  {"auras", "show_auras", "Show known target / self auras", "bool", false},
  {"auras", "aura_mode", "Aura filter", "enum", "detrimental", {"detrimental", "all", "whitelist"}},
  {"auras", "aura_names", "Aura names (comma separated)", "string", "", 512},
  {"auras", "max_auras", "Maximum aura badges", "number", 3, 1, 5},
  {"colors", "npc_color", "NPC / custom", "color", 0xe5b454},
  {"colors", "player_color", "Player", "color", 0x55b9ee},
  {"colors", "group_color", "Group", "color", 0x55d69a},
  {"colors", "pet_color", "Pet", "color", 0x89ccdf},
  {"colors", "corpse_color", "Corpse", "color", 0x9097a1},
  {"colors", "low_color", "Low health", "color", 0xff535c},
  {"colors", "mana_color", "Mana", "color", 0x4d94ff},
  {"colors", "endurance_color", "Endurance", "color", 0xe6c657},
  {"colors", "cast_color", "Casting", "color", 0xf4bb55},
  {"colors", "aggro_color", "High aggro", "color", 0xff7354},
  {"colors", "text_color", "Text", "color", 0xf2f5fa},
  {"colors", "border_color", "Border", "color", 0x202c3c},
  {"colors", "target_color", "Target border", "color", 0xf9df86},
  {"colors", "background_color", "Empty bar", "color", 0x111823},
}
M.units={{'npc','NPCs'},{'player','Players'},{'group','Group members'},{'pet','Pets'},
  {'mercenary','Mercenaries'},{'corpse','Corpses'},{'self','Your own world plate'}}
M.unit_fields={
  {'override','Use independent appearance','bool',false},
  {'width','Bar width','number',142,40,400}, {'height','Health height','number',12,3,40},
  {'mana_height','Resource height','number',4,2,20}, {'cast_height','Cast height','number',9,3,30},
  {'font_scale','Text scale','number',1,0.6,1.6}, {'scale','Unit scale','number',1,0.5,2},
  {'alpha','Unit opacity','number',1,0.15,1}, {'offset_y','Vertical offset','number',0,-100,100},
  {'custom_color','Unit health color','color',0x55b9ee}, {'names_only','Names only','bool',false},
  {'resource','Show resource bar','bool',true}, {'cast','Show cast bar','bool',true},
  {'frame_style','Frame design','enum','solid',{'minimal','solid','bevel','glass','runic'}},
  {'texture','Bar texture','enum','gradient',{'flat','gradient','gloss','segments'}},
  {'name_position','Name position','enum','above',{'above','inside','below'}},
  {'badge_position','Indicator placement','enum','above',{'above','below','left','right'}},
}
for _,unit in ipairs(M.units) do
  for _,f in ipairs(M.unit_fields) do
    M.schema[#M.schema+1]={'unit_'..unit[1],unit[1]..'_'..f[1],f[2],f[3],f[4],f[5],f[6]}
  end
end
local index = {}
for _, d in ipairs(M.schema) do index[d[2]] = d end
local function finite(n) return type(n) == "number" and n == n and math.abs(n) < math.huge end
M.finite = finite
function M.text(s, max)
  if type(s) ~= "string" then return "" end
  return (s:gsub("[%z\1-\31\127]", " ")):sub(1, max or 128)
end
function M.value(key, v)
  local d = index[key]
  if not d then return nil end
  if d[4] == "bool" then return type(v) == "boolean" and v or (type(v) ~= "boolean" and d[5] or false) end
  if d[4] == "number" then return finite(v) and math.max(d[6], math.min(d[7], v)) or d[5] end
  if d[4] == "color" then return finite(v) and math.floor(math.max(0, math.min(0xffffff, v))) or d[5] end
  if d[4] == "string" then return type(v) == "string" and M.text(v, d[6]) or d[5] end
  if d[4] == "enum" then
    for _, s in ipairs(d[6]) do if s == v then return v end end
    return d[5]
  end
end
function M.rule(r)
  if type(r) ~= "table" then return nil end
  local name = M.text(rawget(r, "name"), 80)
  if name == "" then return nil end
  local kind = rawget(r, "kind")
  if kind ~= "npc" and kind ~= "player" and kind ~= "pet" then kind = "any" end
  local color = rawget(r, "color")
  local scale = rawget(r, "scale")
  return {name=name, exact=rawget(r,"exact") == true, kind=kind,
    enabled=rawget(r,"enabled") ~= false, hide=rawget(r,"hide") == true,
    color=finite(color) and math.floor(math.max(0,math.min(0xffffff,color))) or 0xe5b454,
    scale=finite(scale) and math.max(0.5,math.min(2,scale)) or 1}
end
function M.repair(s)
  s = type(s) == "table" and s or {}
  local out = {version=M.VERSION, rules={}}
  for _, d in ipairs(M.schema) do out[d[2]] = M.value(d[2], rawget(s,d[2])) end
  local rules = rawget(s,"rules")
  if type(rules) == "table" then
    for i=1,M.MAX_RULES do
      local r = M.rule(rawget(rules,i))
      if r then out.rules[#out.rules+1] = r end
    end
  end
  return out
end
function M.preset(name)
  local s = M.repair()
  if name == "Bare" or name == "Minimal" then
    s.width=110; s.height=8; s.font_scale=0.85; s.show_level=false; s.resource="off"
    s.frame_style='minimal'; s.texture='flat'; s.border_width=0; s.rounding=0
    s.name_position='below'; s.target_style='brackets'; s.badge_position='right'; s.badge_scale=0.8
    s.health_text_align='right'; s.cast_gap=7; s.cast_width=92
  elseif name == "Inset" or name == "Compact" then
    s.width=154; s.height=18; s.frame_style='solid'; s.texture='flat'; s.name_position='inside'
    s.text_align='left'; s.health_text_align='right'; s.target_style='chevrons'; s.show_level=false
    s.rounding=0; s.border_width=2; s.resource='off'; s.badge_position='above'
  elseif name == "Forged" then
    s.width=172; s.height=16; s.frame_style='bevel'; s.texture='gloss'; s.target_style='brackets'
    s.mana_height=8
    s.rounding=3; s.border_width=3; s.border_color=0x9d8656; s.name_position='above'
    s.badge_position='left'; s.badge_scale=1.1; s.show_level=true; s.health_text_align='right'
    s.cast_gap=5; s.cast_height=15; s.cast_text_align='center'
  elseif name == "Glass" then
    s.width=174; s.height=19; s.frame_style='glass'; s.texture='gloss'; s.target_style='glow'
    s.rounding=7; s.border_width=1; s.name_position='inside'; s.text_align='left'; s.health_text_align='right'
    s.badge_position='below'; s.cast_gap=10; s.cast_width=142; s.cast_height=12; s.show_level=false
    s.alpha=0.88; s.background_color=0x101d28
  elseif name == "Runic" then
    s.width=184; s.height=19; s.frame_style='runic'; s.texture='segments'; s.target_style='chevrons'
    s.mana_height=6
    s.rounding=0; s.border_width=2; s.border_color=0x8da4b8; s.name_position='above'; s.font_scale=1.15
    s.badge_position='left'; s.badge_scale=1.25; s.cast_gap=8; s.cast_width=146; s.cast_height=15
  elseif name == "Split" then
    s.width=176; s.height=11; s.frame_style='minimal'; s.texture='flat'; s.target_style='brackets'
    s.name_position='below'; s.show_level=false; s.cast_gap=18; s.cast_width=124; s.cast_height=14
    s.badge_position='right'; s.badge_scale=1.25; s.health_text_align='right'; s.cast_text_align='left'
    s.border_width=1; s.rounding=0; s.resource_text=true
  elseif name == "Tactical" then
    s.width=166; s.height=13; s.frame_style='solid'; s.texture='segments'; s.target_style='brackets'
    s.name_position='inside'; s.text_align='left'; s.health_text_align='right'; s.show_level=false
    s.badge_position='below'; s.cast_gap=6; s.cast_width=166; s.cast_height=11; s.rounding=0
    s.show_aggro=true; s.highlight_aggro=true; s.non_target_alpha=0.85
  elseif name == "Tank" then
    s.width=166; s.height=15; s.highlight_aggro=true; s.show_aggro=true; s.show_class=true
    s.frame_style='runic'; s.texture='segments'; s.target_style='chevrons'; s.badge_position='left'
  elseif name == "Healer" then
    s.width=154; s.height=14; s.resource_text=true; s.color_mode="class"; s.show_npcs=false
    s.frame_style='glass'; s.texture='gloss'; s.rounding=6; s.target_style='glow'; s.badge_position='right'
  elseif name == "Large" then
    s.width=180; s.height=18; s.font_scale=1.25; s.mana_height=7; s.cast_height=13
    s.frame_style='bevel'; s.texture='gloss'; s.target_style='brackets'; s.badge_scale=1.4
  elseif name == "Cast Focus" then
    s.cast_height=16; s.cast_color=0xffc35b; s.non_target_alpha=0.85; s.highlight_cast=true
    s.show_class=true; s.height=10; s.show_auras=true
    s.frame_style='glass'; s.texture='gloss'; s.cast_gap=12; s.cast_width=200; s.target_style='glow'
  elseif name == "Raid" then
    s.width=128; s.height=10; s.show_players=false; s.show_pets=false; s.show_mercenaries=false
    s.max_distance=170; s.stack=true; s.max_stack=150; s.show_aggro=true
    s.frame_style='minimal'; s.texture='flat'; s.name_position='inside'; s.target_style='brackets'; s.badge_position='right'
  elseif name == "Server" then
    s.width=155; s.show_class=true; s.show_auras=true; s.show_aggro=true; s.highlight_aggro=true
  elseif name == "Classic Names" then
    s.show_health=false; s.resource='off'; s.show_cast=false; s.show_level=false; s.stack=false
    s.frame_style='minimal'; s.target_style='none'
  end
  return s
end
function M.effective(s,kind,combat)
  local out={}; for key,value in pairs(s) do out[key]=value end
  if s[kind..'_override'] then
    for _,key in ipairs({'width','height','mana_height','cast_height','font_scale','scale','alpha','offset_y',
      'frame_style','texture','name_position','badge_position'}) do
      out[key]=s[kind..'_'..key]
    end
    out.unit_color=s[kind..'_custom_color']; out.unit_names_only=s[kind..'_names_only']
    if not s[kind..'_resource'] then out.resource='off' end
    if not s[kind..'_cast'] then out.show_cast=false end
  end
  out.scale=math.max(0.5,math.min(2,out.scale*(combat and s.combat_scale or s.rest_scale)))
  if not combat then out.alpha=out.alpha*s.rest_alpha end
  local width=combat and s.combat_width or s.rest_width
  local height=combat and s.combat_height or s.rest_height
  if width>0 then out.width=math.max(40,width) end
  if height>0 then out.height=math.max(3,height) end
  return out
end
function M.store(data)
  data = type(data)=="table" and data or {}
  local out = {version=M.VERSION, settings=M.repair(rawget(data,"settings")), profiles={}}
  local profiles = rawget(data,"profiles")
  if type(profiles)=="table" then
    -- Bounded traversal, even for a hostile manually edited data file.
    local key, count = nil, 0
    for _=1,64 do
      key = next(profiles,key)
      if key == nil or count >= M.MAX_PROFILES then break end
      if type(key)=="string" and #key>0 and #key<=32 and not key:find("[%c]") then
        out.profiles[key]=M.repair(rawget(profiles,key)); count=count+1
      end
    end
  end
  return out
end
local function hex(s) return (s:gsub(".", function(c) return string.format("%02x",c:byte()) end)) end
local function unhex(s)
  if #s%2~=0 or s:find("[^%da-fA-F]") then return nil end
  return (s:gsub("..",function(c) return string.char(tonumber(c,16)) end))
end
-- Text imports cannot contain code. Unknown keys and malformed records are refused.
function M.export(settings)
  local s, out = M.repair(settings), {"CREST1"}
  for _, d in ipairs(M.schema) do
    local v=s[d[2]]
    if d[4]=="string" then v=hex(v) end
    out[#out+1]=d[2].."="..tostring(v)
  end
  for _,r in ipairs(s.rules) do
    out[#out+1]=table.concat({"rule",hex(r.name),r.exact and "1" or "0",r.kind,r.enabled and "1" or "0",
      r.hide and "1" or "0",r.color,r.scale},"|")
  end
  return table.concat(out,"\n")
end
function M.import(text)
  if type(text)~="string" or #text>32768 then return nil,"Profile text is too large." end
  local first, s, seen, lines = true, {rules={}}, {}, 0
  for line in (text.."\n"):gmatch("([^\n]*)\n") do
    line=line:gsub("\r$",""); lines=lines+1
    if lines>512 then return nil,"Too many profile records." end
    if first then if line~="CREST1" then return nil,"Expected a CREST1 profile." end; first=false
    elseif line~="" then
      if line:sub(1,5)=="rule|" then
        local n,e,k,en,h,c,z=line:match("^rule|([%da-fA-F]*)|([01])|(%a+)|([01])|([01])|(%d+)|([%d%.]+)$")
        local name=n and unhex(n)
        if not name or #s.rules>=M.MAX_RULES then return nil,"Invalid rule record." end
        local r=M.rule{name=name,exact=e=="1",kind=k,enabled=en=="1",hide=h=="1",color=tonumber(c),scale=tonumber(z)}
        if not r then return nil,"Invalid rule name." end
        s.rules[#s.rules+1]=r
      else
        local key,v=line:match("^([%w_]+)=(.*)$")
        local d=key and index[key]
        if not d or seen[key] then return nil,"Unknown or duplicate setting." end
        seen[key]=true
        if d[4]=="bool" then
          if v~="true" and v~="false" then return nil,"Invalid boolean." end
          v=v=="true"
        elseif d[4]=="number" or d[4]=="color" then
          v=tonumber(v); if not finite(v) then return nil,"Invalid number." end
        elseif d[4]=="string" then v=unhex(v); if not v then return nil,"Invalid text." end
        else
          local valid=false; for _,entry in ipairs(d[6]) do if entry==v then valid=true end end
          if not valid then return nil,"Invalid selection." end
        end
        s[key]=v
      end
    end
  end
  return M.repair(s)
end
M.presets = {"Default", "Bare", "Inset", "Forged", "Glass", "Runic", "Split", "Tactical", "Tank", "Healer", "Large", "Cast Focus", "Raid", "Classic Names"}
M.hidden_presets = {"Server"}
M.descriptions = {
  Default='Balanced solid frame, names above, resources and casts grouped together.',
  Bare='Unframed thin strips, names below, indicators at the side and a detached cast bar.',
  Inset='Square boxed bars, names inside, health aligned right and large target chevrons.',
  Forged='Raised metal bevel, glossy fill, side indicators and bracketed targets.',
  Glass='Rounded translucent glass, inset names, lower indicators and a detached cast bar.',
  Runic='Angular ornamental frame, segmented fill, side sigils and strong target chevrons.',
  Split='Lightweight health strip with a narrower, separated cast bar and side indicators.',
  Tactical='Segmented dashboard bars, inset labels, lower indicators and target brackets.',
  Tank='Armored segmented frame with aggro emphasis and target chevrons.',
  Healer='Glass group plates with readable resource labels and side indicators.',
  Large='Large beveled bars and enlarged text, resources, casts and indicators.',
  ['Cast Focus']='Compact health with a larger detached cast bar and known aura badges.',
  Raid='Dense, minimal inset plates with side indicators and bounded separation.',
  ['Classic Names']='Names and indicators without health, resource or cast bars.',
}
return M
