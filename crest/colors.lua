-- SPDX-License-Identifier: MIT
local M={}
function M.rgb(value) return ((value>>16)&255)/255,((value>>8)&255)/255,(value&255)/255 end
function M.pack(r,g,b)
  local function channel(n)
    if type(n)~='number' or n~=n or math.abs(n)==math.huge then return nil end
    return math.floor(math.max(0,math.min(1,n))*255+0.5)
  end
  local x,y,z=channel(r),channel(g),channel(b)
  if not x or not y or not z then return nil end
  return (x<<16)|(y<<8)|z
end
function M.draw(ui,label,value)
  if not ui.ColorButton or not ui.ColorPicker3 then
    local text,changed=ui.InputText(label,string.format('%06X',value),7)
    if changed and text:match('^%x%x%x%x%x%x$') then return tonumber(text,16),true end
    return value,false
  end
  local r,g,b=M.rgb(value)
  local changed=false
  local popup=label..'##color'
  if ui.ColorButton(label..'##swatch',r,g,b,1,0,32,20) then ui.OpenPopup(popup) end
  ui.SameLine(); ui.Text(label)
  if ui.IsItemHovered() then ui.SetTooltip(string.format('#%06X — click the swatch to choose a color',value)) end
  if ui.BeginPopup(popup) then
    ui.Text(label)
    ui.SetNextItemWidth(300)
    local picked
    r,g,b,picked=ui.ColorPicker3('##wheel',r,g,b)
    if picked then local packed=M.pack(r,g,b); if packed then value=packed; changed=true end end
    local text,edited=ui.InputText('Hex',string.format('%06X',value),7)
    if edited and text:match('^%x%x%x%x%x%x$') then value=tonumber(text,16); changed=true end
    local palette={0xffffff,0xe7c76b,0xf15b64,0x75da9b,0x5ab9ec,0x9c89df,0x778da7,0x161e2a}
    for i,c in ipairs(palette) do
      local pr,pg,pb=M.rgb(c)
      if ui.ColorButton('Quick '..i,pr,pg,pb,1,0,24,20) then value=c; changed=true end
      if i<#palette then ui.SameLine() end
    end
    if ui.Button('Done') then ui.CloseCurrentPopup() end
    ui.EndPopup()
  end
  return value,changed
end
return M
