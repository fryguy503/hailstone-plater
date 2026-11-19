-- SPDX-License-Identifier: MIT
local C=require and require('crest.config') or dofile('crest/config.lua')
local M={}
function M.draw(ui,s,p,changed,spell)
  if not s.enabled or not s.personal_bar or not p or not p.in_zone or p.game_state~=5 then return end
  local flags=ui.WindowFlags.NoResize|ui.WindowFlags.NoCollapse
  if s.personal_locked then flags=flags|ui.WindowFlags.NoMove end
  ui.SetNextWindowSize(s.personal_width,0,ui.Cond.Always)
  local open,visible=ui.Begin('Crest Personal',true,flags)
  if not open then s.personal_bar=false; changed() end
  if visible then
    local function bar(current,maximum,color,label,height)
      if not C.finite(current) or not C.finite(maximum) or maximum<=0 then return end
      ui.PushStyleColor(ui.Col.PlotHistogram,((color>>16)&255)/255,((color>>8)&255)/255,(color&255)/255,1)
      ui.ProgressBar(math.max(0,math.min(1,current/maximum)),-1,height,
        string.format('%s  %.0f / %.0f  (%.0f%%)',label,current,maximum,100*current/maximum))
      ui.PopStyleColor()
    end
    bar(p.cur_hp,p.max_hp,s.player_color,'HP',s.personal_height)
    if s.personal_mana then bar(p.cur_mana,p.max_mana,s.mana_color,'Mana',17) end
    if s.personal_endurance then bar(p.cur_endurance,p.max_endurance,s.endurance_color,'End',17) end
    if spell then ui.Text('Casting: '..C.text(spell.name,80)) end
  end
  ui.End()
end
return M
