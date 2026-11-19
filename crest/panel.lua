-- SPDX-License-Identifier: MIT
local C=require and require('crest.config') or dofile('crest/config.lua')
local Colors=require and require('crest.colors') or dofile('crest/colors.lua')
local M={}
local sections={
  {'general','Visibility'}, {'layout','Layout'}, {'appearance','Texture, anchors & stacking'},
  {'combat','Combat & target'}, {'text','Text'}, {'bars','Health & resources'},
  {'casts','Cast bars'}, {'indicators','Indicators'}, {'auras','Auras'}, {'personal','Personal window'}, {'colors','Colors'},
}
local function help(ui,text) if ui.IsItemHovered() then ui.SetTooltip(text) end end
local function combo(ui,label,value,values)
  local changed=false
  if ui.BeginCombo(label,tostring(value)) then
    for _,v in ipairs(values) do if ui.Selectable(v,v==value) then value=v; changed=true end end
    ui.EndCombo()
  end
  return value,changed
end
local color=Colors.draw
function M.new(ui,app)
  local self={open=false,preview=true,profile_name='',profile_text='',message='',rule_name='',rule_kind='any',rule_exact=false,search='',unit='npc'}
  local function setting(s,d)
    local value,changed=s[d[2]],false
    ui.SetNextItemWidth(260)
    if d[4]=='bool' then value,changed=ui.Checkbox(d[3],value)
    elseif d[4]=='number' then value,changed=ui.SliderFloat(d[3],value,d[6],d[7])
    elseif d[4]=='enum' then value,changed=combo(ui,d[3],value,d[6])
    elseif d[4]=='color' then value,changed=color(ui,d[3],value)
    else value,changed=ui.InputText(d[3],value,d[6]+1) end
    if changed then s[d[2]]=C.value(d[2],value); app.changed() end
  end
  function self:draw()
    if not self.open then return end
    ui.SetNextWindowSize(690,710)
    local opened,visible=ui.Begin('Hailstone Crest',self.open)
    self.open=opened
    if visible then
      local s=app.settings()
      ui.TextColored(0.4,0.8,1,1,'CREST  |  World nameplate replacement')
      if app.api_version()<1 then
        ui.TextColored(1,0.65,0.3,1,'The generic nameplate canvas UI API is unavailable.')
        ui.TextWrapped('Update Hailstone to enable addon-drawn world plates. Your ordinary nameplates remain active.')
      end
      if ui.BeginTabBar('crest_tabs') then
        if ui.BeginTabItem('Settings') then
          if ui.Button(s.enabled and 'Disable replacement' or 'Enable replacement') then s.enabled=not s.enabled; app.changed() end
          ui.SameLine()
          if ui.Button('Save now') then app.save() end
          ui.TextDisabled('Saved per character. Host name switches and the 250-unit visibility limit still apply.')
          self.search=ui.InputTextWithHint('Search','Find a setting...',self.search,81)
          for _,section in ipairs(sections) do
            local searching=self.search~=''
            if searching or ui.CollapsingHeader(section[2]) then
              if section[1]=='bars' then ui.TextWrapped('Remote HP is a percentage. Mana and endurance appear only when the server supplies them. Actual HP labels apply to yourself.') end
              if section[1]=='general' then ui.TextWrapped('Clicking plates targets the unit with a normal left click. Dragging or interacting with other windows does not target a plate. Show through walls is an explicit display option; invisibility and server targeting rules still apply.') end
              if section[1]=='casts' then ui.TextWrapped('Casts use observed server events. A cast bar does not imply that a spell can be interrupted.') end
              if section[1]=='indicators' then
                ui.TextWrapped('INV means invisible, IVU means invisible to undead, and HID means the concealment type is unknown. Tags follow Indicator placement under Texture, anchors & stacking.')
                ui.TextWrapped('The eye means an NPC detects your current concealment or is already engaged. Pacify, mez and shared symbols require matching server support. Status expires after five seconds without a complete refresh.')
              end
              if section[1]=='auras' then ui.TextWrapped('Aura badges use known self or current-target buffs only. Servers may restrict target buff inspection. Whitelist matches literal names or name fragments.') end
              if section[1]=='colors' then ui.TextDisabled('Click a color swatch for the hue wheel, RGB values, hexadecimal input and quick colors.') end
              for _,d in ipairs(C.schema) do
                if d[1]==section[1] and (not searching or (section[2]..' '..d[3]..' '..d[2]):lower():find(self.search:lower(),1,true)) then setting(s,d) end
              end
            end
          end
          if self.search~='' then
            for _,unit in ipairs(C.units) do
              for _,d in ipairs(C.schema) do
                if d[1]=='unit_'..unit[1] and (unit[2]..' '..d[3]..' '..d[2]):lower():find(self.search:lower(),1,true) then
                  ui.PushID(d[2]); ui.TextDisabled(unit[2]); setting(s,d); ui.PopID()
                end
              end
            end
          end
          ui.EndTabItem()
        end
        if ui.BeginTabItem('Unit types') then
          ui.TextWrapped('Each unit type can inherit global appearance or have independent bars, color, scale, opacity, resources and casts. Combat and target multipliers apply afterward.')
          for _,unit in ipairs(C.units) do
            if ui.Selectable(unit[2],self.unit==unit[1]) then self.unit=unit[1] end
          end
          ui.Separator()
          ui.PushID(self.unit)
          for _,d in ipairs(C.schema) do
            if d[1]=='unit_'..self.unit then
              if d[2]~=self.unit..'_override' then ui.BeginDisabled(not s[self.unit..'_override']) end
              setting(s,d)
              if d[2]~=self.unit..'_override' then ui.EndDisabled() end
            end
          end
          ui.PopID()
          ui.EndTabItem()
        end
        if ui.BeginTabItem('Name rules') then
          ui.TextWrapped('First matching rule wins. Names are literal text: there is no Lua or pattern execution. Rules can recolor, resize or hide matching plates.')
          self.rule_name=ui.InputText('Name or fragment',self.rule_name,81)
          self.rule_kind=combo(ui,'Entity kind',self.rule_kind,{'any','npc','player','pet'})
          self.rule_exact=ui.Checkbox('Exact name',self.rule_exact)
          if ui.Button('Add rule') then
            local rule=C.rule{name=self.rule_name,kind=self.rule_kind,exact=self.rule_exact}
            if rule and #s.rules<C.MAX_RULES then s.rules[#s.rules+1]=rule; self.rule_name=''; app.changed() end
          end
          ui.TextDisabled(tostring(#s.rules)..' / '..C.MAX_RULES..' rules')
          local remove,up,down
          for i,r in ipairs(s.rules) do
            ui.PushID(i)
            if ui.TreeNode(tostring(i)..'. '..r.name) then
              local changed,v=false,nil
              v,changed=ui.Checkbox('Enabled',r.enabled); if changed then r.enabled=v; app.changed() end
              v,changed=ui.Checkbox('Hide matching plates',r.hide); if changed then r.hide=v; app.changed() end
              v,changed=color(ui,'Health color',r.color); if changed then r.color=v; app.changed() end
              v,changed=ui.SliderFloat('Scale multiplier',r.scale,0.5,2); if changed then r.scale=v; app.changed() end
              if ui.Button('Move up') and i>1 then up=i end; ui.SameLine()
              if ui.Button('Move down') and i<#s.rules then down=i end; ui.SameLine()
              if ui.Button('Remove') then remove=i end
              ui.TreePop()
            end
            ui.PopID()
          end
          if remove then table.remove(s.rules,remove); app.changed() end
          if up then s.rules[up],s.rules[up-1]=s.rules[up-1],s.rules[up]; app.changed() end
          if down then s.rules[down],s.rules[down+1]=s.rules[down+1],s.rules[down]; app.changed() end
          ui.EndTabItem()
        end
        if ui.BeginTabItem('Profiles') then
          ui.TextWrapped('Choose a complete visual design. Frames, fills, label positions, target decorations, indicators and cast layouts change together. Named profiles and text exports also include name rules.')
          for i,name in ipairs(C.presets) do
            if ui.Button(name) then app.replace(C.preset(name)); self.message='Applied '..name..'.' end
            if ui.IsItemHovered() then ui.SetTooltip(C.descriptions[name] or '') end
            if i%5~=0 then ui.SameLine() end
          end
          ui.NewLine()
          self.profile_name=ui.InputText('Profile name',self.profile_name,33)
          if ui.Button('Save named profile') then self.message=app.profile_save(self.profile_name) end
          local names={}; for name in pairs(app.profiles()) do names[#names+1]=name end; table.sort(names)
          for _,name in ipairs(names) do
            ui.PushID(name)
            if ui.SmallButton('Load') then app.replace(app.profiles()[name]); self.message='Loaded '..name..'.' end
            ui.SameLine()
            if ui.SmallButton('Delete') then app.profiles()[name]=nil; app.changed(); self.message='Deleted '..name..'.' end
            ui.SameLine(); ui.Text(name); ui.PopID()
          end
          ui.Separator()
          if ui.Button('Export current') then self.profile_text=C.export(app.settings()); self.message='Profile text is ready to select and copy.' end
          ui.SameLine()
          if ui.Button('Import profile text') then
            local imported,why=C.import(self.profile_text)
            if imported then app.replace(imported); self.message='Imported profile.' else self.message=why end
          end
          self.profile_text=ui.InputTextMultiline('Profile data',self.profile_text,32769,-1,260)
          ui.TextWrapped(self.message)
          ui.EndTabItem()
        end
        if ui.BeginTabItem('Preview & status') then
          ui.TextWrapped('These sample bars demonstrate colors and labels. World plates use the host renderer with scene depth and your normal name visibility settings.')
          ui.Text('Sample: Lv 65 Named guardian')
          local function sample(label,n,c)
            ui.PushStyleColor(ui.Col.PlotHistogram,((c>>16)&255)/255,((c>>8)&255)/255,(c&255)/255,1)
            ui.ProgressBar(n,math.min(380,s.width*2),math.max(14,s.height),label)
            ui.PopStyleColor()
          end
          sample('Health  62%',0.62,s.npc_color); sample('Mana  40%',0.4,s.mana_color); sample('Casting  1.8s',0.7,s.cast_color)
          ui.Separator()
          local status=app.status()
          for _,text in ipairs(status) do ui.TextWrapped(text) end
          ui.TextWrapped('Left-click plates to target when enabled. Through-wall display is optional; dragging and open windows keep their normal input behavior.')
          ui.TextWrapped('Stopping Crest restores ordinary plates automatically. No combat actions, packet sends, game-folder writes, arbitrary profile code or shared-mark commands are used.')
          ui.EndTabItem()
        end
        ui.EndTabBar()
      end
    end
    ui.End()
  end
  return self
end
return M
