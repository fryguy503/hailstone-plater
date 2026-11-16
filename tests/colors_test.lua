-- SPDX-License-Identifier: MIT
-- Pure Lua color-widget flow tests. Native interaction lives in the companion client tests.
local Colors=require('crest.colors')
local tests,assertions=0,0
local function check(value,message) assertions=assertions+1; assert(value,message or 'check failed') end
local function equal(a,b,message) check(a==b,(message or 'unexpected value')..': '..tostring(a)..' ~= '..tostring(b)) end
local function near(a,b) check(type(a)=='number' and math.abs(a-b)<1e-12) end
local function test(name,fn) local ok,why=pcall(fn); assert(ok,name..': '..tostring(why)); tests=tests+1 end

local function mock_ui()
  local state={buttons={},text={},open={},swatches={},pickers={},popups={},depth=0,picker=nil,hover=false}
  local ui={}
  local function button(label)
    if state.buttons[label] then state.buttons[label]=nil; return true end
    return false
  end
  ui.ColorButton=function(label,r,g,b,a,flags,w,h)
    for _,value in ipairs({r,g,b,a}) do
      check(type(value)=='number' and value==value and value>=0 and value<=1,'invalid native color input')
    end
    equal(flags,0); check(w==32 or w==24); equal(h,20)
    state.swatches[#state.swatches+1]={id=label,r=r,g=g,b=b,a=a,width=w,height=h}
    return button(label)
  end
  ui.ColorPicker3=function(label,r,g,b,flags)
    equal(label,'##wheel')
    equal(flags,nil,'default native hue-wheel options should apply')
    state.pickers[#state.pickers+1]={r=r,g=g,b=b}
    local value=state.picker
    if value then return value[1],value[2],value[3],value[4] end
    return r,g,b,false
  end
  ui.OpenPopup=function(id) state.open[id]=true; state.popups[#state.popups+1]=id end
  ui.BeginPopup=function(id)
    if not state.open[id] then return false end
    state.depth=state.depth+1; state.current=id; return true
  end
  ui.EndPopup=function() equal(state.depth,1); state.depth=state.depth-1; state.current=nil end
  ui.CloseCurrentPopup=function() check(state.current~=nil); state.open[state.current]=nil end
  ui.InputText=function(label,value,capacity)
    equal(capacity,7)
    if state.text[label] then return state.text[label],true end
    return value,false
  end
  ui.Button=button
  ui.IsItemHovered=function() return state.hover end
  ui.SetTooltip=function(text) state.tooltip=text end
  ui.Text=function(text) check(type(text)=='string') end
  ui.SameLine=function() end
  ui.SetNextItemWidth=function(width) check(type(width)=='number' and width>0) end
  return ui,state
end

test('all byte values roundtrip through normalized RGB with nearest-byte rounding',function()
  for n=0,1023 do
    local value=((n%256)<<16)|(((n*71)%256)<<8)|((n*251)%256)
    local r,g,b=Colors.rgb(value)
    equal(Colors.pack(r,g,b),value)
  end
  equal(Colors.pack(0,0,0),0)
  equal(Colors.pack(1,1,1),0xffffff)
  equal(Colors.pack(0.5,0.5,0.5),0x808080)
  equal(Colors.pack(-1e300,1e300,0.5),0x00ff80)
end)

test('nonfinite and nonnumeric picker output cannot become a saved color',function()
  for _,bad in ipairs({math.huge,-math.huge,0/0,'0.5',false,{}}) do
    equal(Colors.pack(bad,0,0),nil)
    equal(Colors.pack(0,bad,0),nil)
    equal(Colors.pack(0,0,bad),nil)
  end
  equal(Colors.pack(nil,0,0),nil)
end)

test('a closed picker still displays a visible swatch in the correct channel order',function()
  local ui,state=mock_ui()
  local value,changed=Colors.draw(ui,'Health',0x123456)
  equal(value,0x123456); equal(changed,false)
  equal(#state.swatches,1); equal(#state.pickers,0); equal(state.depth,0)
  equal(state.swatches[1].id,'Health##swatch')
  near(state.swatches[1].r,0x12/255); near(state.swatches[1].g,0x34/255); near(state.swatches[1].b,0x56/255)
  equal(state.swatches[1].width,32); equal(state.swatches[1].height,20)
end)

test('clicking a swatch opens its popup and applies a native wheel change immediately',function()
  local ui,state=mock_ui()
  state.buttons['Health##swatch']=true
  state.picker={0.2,0.4,0.6,true}
  local value,changed=Colors.draw(ui,'Health',0x123456)
  equal(value,0x336699); equal(changed,true)
  equal(state.popups[1],'Health##color'); equal(#state.pickers,1)
  near(state.pickers[1].r,0x12/255); near(state.pickers[1].g,0x34/255); near(state.pickers[1].b,0x56/255)
  equal(#state.swatches,9,'popup should show all eight quick colors')
  equal(state.depth,0)
end)

test('unchanged native callbacks leave the current setting alone',function()
  local ui,state=mock_ui()
  state.open['Mana##color']=true
  state.picker={1,0,0,false}
  local value,changed=Colors.draw(ui,'Mana',0x123456)
  equal(value,0x123456); equal(changed,false); equal(state.depth,0)
end)

test('malformed native output is ignored without leaving the popup stack open',function()
  local ui,state=mock_ui()
  state.open['Mana##color']=true
  for _,bad in ipairs({math.huge,-math.huge,0/0,'bad',false}) do
    state.picker={bad,0,0,true}
    local value,changed=Colors.draw(ui,'Mana',0x123456)
    equal(value,0x123456); equal(changed,false); equal(state.depth,0)
  end
end)

test('valid hexadecimal edits apply immediately and invalid text is refused',function()
  local ui,state=mock_ui()
  state.open['Health##color']=true
  state.text.Hex='aBcD12'
  local value,changed=Colors.draw(ui,'Health',0)
  equal(value,0xabcd12); equal(changed,true)
  for _,bad in ipairs({'#ABCDEF','12345','1234567','xyz123','12 456','0xABCD',''}) do
    state.text.Hex=bad
    value,changed=Colors.draw(ui,'Health',0x123456)
    equal(value,0x123456); equal(changed,false)
    equal(state.depth,0)
  end
end)

test('quick palette choices apply immediately while Done closes only its popup',function()
  local palette={0xffffff,0xe7c76b,0xf15b64,0x75da9b,0x5ab9ec,0x9c89df,0x778da7,0x161e2a}
  for index,expected in ipairs(palette) do
    local ui,state=mock_ui()
    state.open['Health##color']=true; state.open['Other##color']=true
    state.buttons['Quick '..index]=true; state.buttons.Done=true
    local value,changed=Colors.draw(ui,'Health',0)
    equal(value,expected); equal(changed,true)
    equal(state.open['Health##color'],nil); equal(state.open['Other##color'],true)
    equal(state.depth,0)
  end
end)

test('separate color labels use separate popup identifiers',function()
  local ui,state=mock_ui()
  state.buttons['Health##swatch']=true
  Colors.draw(ui,'Health',0xff0000)
  local pickers=#state.pickers
  Colors.draw(ui,'Mana',0x0000ff)
  equal(#state.pickers,pickers,'opening Health must not open Mana')
  state.buttons['Mana##swatch']=true
  Colors.draw(ui,'Mana',0x0000ff)
  equal(state.popups[1],'Health##color'); equal(state.popups[2],'Mana##color')
  equal(state.depth,0)
end)

test('an older host retains validated hexadecimal editing without native widgets',function()
  for _,missing in ipairs({'ColorButton','ColorPicker3'}) do
    local ui,state=mock_ui()
    ui[missing]=nil
    state.text.Health='0a1B2c'
    local value,changed=Colors.draw(ui,'Health',0)
    equal(value,0x0a1b2c); equal(changed,true)
    equal(#state.swatches,0); equal(#state.pickers,0); equal(state.depth,0)
    state.text.Health='invalid'
    value,changed=Colors.draw(ui,'Health',0x123456)
    equal(value,0x123456); equal(changed,false)
  end
end)

print(string.format('Color controls: %d tests, %d assertions passed',tests,assertions))
