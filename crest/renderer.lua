-- SPDX-License-Identifier: MIT
-- Crest owns every visual and layout decision here. This module has no game, UI,
-- file or network dependency: the host supplies font metrics and opaque textures.
local R = {MAX_COMMANDS=512, MAX_FRAME_COMMANDS=32768, MAX_MODEL_VERTICES=65536, MAX_FRAME_VERTICES=262144,
  MAX_BADGES=8, MAX_STACKED=256, MAX_RECORDS=4096}
local function finite(n) return type(n)=='number' and n==n and math.abs(n)<math.huge end
local function get(t,k) if type(t)=='table' then return rawget(t,k) end end
local function clamp(n,lo,hi,default)
  return finite(n) and math.max(lo,math.min(hi,n)) or default
end
local function number(t,k,lo,hi,default) return clamp(get(t,k),lo,hi,default) end
local function choice(t,k,options,default)
  local v=get(t,k); for _,allowed in ipairs(options) do if v==allowed then return v end end
  return default
end
local function color(v,default) return math.floor(clamp(v,0,0xffffff,default or 0xffffff)) end
local function clean(v,limit)
  if type(v)~='string' then return '' end
  v=v:gsub('[%c]',' ')
  -- Invalid UTF-8 never reaches a font adapter. Bound work before validating it.
  if #v>limit then
    local cut=limit+1
    while cut>1 and (v:byte(cut) or 0)>=128 and (v:byte(cut) or 0)<192 do cut=cut-1 end
    v=v:sub(1,cut-1)
  end
  local ok,n=pcall(utf8.len,v); return ok and n and v or ''
end
local function tone(rgb,factor)
  local function channel(shift) return math.min(255,math.floor(((rgb>>shift)&255)*factor+0.5)) end
  return (channel(16)<<16)|(channel(8)<<8)|channel(0)
end
local function box(x0,y0,x1,y1) return {x=x0,y=y0,w=math.max(0,x1-x0),h=math.max(0,y1-y0)} end
local function translate_y(c,dy)
  if c.y then c.y=c.y+dy else c.y0=c.y0+dy; c.y1=c.y1+dy end
  if c.y2 then c.y2=c.y2+dy end
  if c.bounds then c.bounds.y=c.bounds.y+dy end
end
local function bar(value)
  if type(value)~='table' or not finite(get(value,'value')) then return nil end
  return {value=clamp(get(value,'value'),0,100,0),color=color(get(value,'color')),text=clean(get(value,'text'),256)}
end

-- Conservative public canvas admission weights, not an estimate of GPU timing.
-- Keep dense layouts inside both the host's primitive and geometry budgets.
function R.vertex_cost(commands)
  local count=0
  for _,command in ipairs(commands) do
    local kind=command.kind
    if kind=='Text' then count=count+4*(#command.text+1)
    elseif kind=='Circle' or kind=='CircleFilled' then count=count+4*((command.segments or 32)+1)
    elseif kind=='Rect' then count=count+((command.rounding or 0)>0 and 512 or 24)
    elseif kind=='RectFilled' or kind=='Gradient' then count=count+((command.rounding or 0)>0 and 256 or 4)
    elseif kind=='Image' then count=count+4
    elseif kind=='Triangle' then count=count+36
    else count=count+12 end
  end
  return count
end

-- metrics.measure(text, pixelSize, font) returns advance width/line height plus
-- ink_x0/ink_y0/ink_x1/ink_y1 relative to the text origin. Missing ink metrics
-- use the nominal box; adapters with actual glyph overhang must supply them.
-- metrics.texture(spellIcon) returns {texture,u0,v0,u1,v1}, or nil for vector art.
-- metrics.measurement_cache is an optional shared table for one frame.
-- metrics.omit_command_bounds skips the diagnostic bounds on each primitive.
-- context.scale is the host's distance/UI factor; style.scale is Crest's scale.
-- Commands are relative to the projected head. Positive style.offset_y raises
-- the plate. Bounds include font ink, shadows, strokes and antialias fringes.
function R.prepare(style,context,metrics)
  if style==nil then return nil end -- absence means the ordinary host caption
  if type(style)~='table' then return nil,'invalid style' end
  if get(style,'hide')==true then return {hide=true,bounds=box(0,0,0,0),commands={},vertex_cost=0,clickable=false} end
  context=type(context)=='table' and context or {}
  metrics=type(metrics)=='table' and metrics or {}
  local measure=get(metrics,'measure')
  if type(measure)~='function' then return nil,'font metrics unavailable' end
  local unit=clamp(number(style,'scale',0.1,4,1)*number(context,'scale',0.1,4,1),0.1,4,1)
  local fs=clamp(number(context,'font_size',4,256,14)*unit*number(style,'font_scale',0.5,2,1),4,256,14)
  local tag_font,bar_font=math.max(4,fs*0.8),math.max(4,fs*0.72)
  local font=get(context,'font')
  local alpha=number(style,'alpha',0,1,1)*number(context,'alpha',0,1,1)
  local width=number(style,'width',24,600,142)*unit
  local gap=2*unit
  local frame=choice(style,'frame_style',{'minimal','solid','bevel','glass','runic'},'solid')
  local texture=choice(style,'texture',{'flat','gradient','gloss','segments'},'flat')
  local target_style=choice(style,'target_style',{'none','arrow','chevrons','brackets','glow'},'arrow')
  local target=get(context,'target')==true and get(style,'show_marker')~=false and target_style~='none'
  local name_position=choice(style,'name_position',{'above','inside','below'},'above')
  local text_align=choice(style,'text_align',{'left','center','right'},'center')
  local health_align=choice(style,'health_text_align',{'left','center','right'},'center')
  local cast_align=choice(style,'cast_text_align',{'left','center','right'},'center')
  local badge_position=choice(style,'badge_position',{'above','below','left','right'},'above')
  local out=(frame=='runic' and 4 or (frame=='bevel' or frame=='glass') and 3 or 0)*unit
  local row_gap=gap+out*2
  local border=color(get(style,'border_color'),0x202c3c)
  local background=color(get(style,'background_color'),0x111823)
  local foreground=color(get(style,'text_color'),0xf2f5fa)
  local name_color=color(get(style,'name_color'),foreground)
  local level_color=color(get(style,'level_color'),foreground)
  local health,mana,cast=bar(get(style,'health')),bar(get(style,'mana')),bar(get(style,'cast'))
  local cast_width=number(style,'cast_width',0,600,0)
  cast_width=(cast_width>0 and math.max(24,cast_width) or width/unit)*unit
  local body_width=cast and math.max(width,cast_width) or width
  local chevron_tip,chevron_right
  if target and target_style=='chevrons' and (health or mana or cast) then
    -- Place both chevrons outside the widest bar, including the frame art and stroke fringes.
    local bar_width=(health or mana) and body_width or cast_width
    local frame_fringe=1+out
    if frame=='runic' then frame_fringe=1+15*unit
    elseif frame=='glass' then frame_fringe=frame_fringe+unit/2
    elseif frame=='bevel' then frame_fringe=frame_fringe+0.75*unit
    elseif frame=='minimal' then frame_fringe=1+math.max(0.25,2*unit) end
    local stroke_fringe=1+unit
    chevron_tip=bar_width/2+math.max(5*unit,frame_fringe+stroke_fringe+gap)
    chevron_right=chevron_tip+12*unit+stroke_fringe
  end
  local name=clean(get(style,'name') or get(context,'name'),256)
  local subtitle=clean(get(style,'subtitle'),256)
  local level=clean(get(style,'level'),24)
  local level_below=level~='' and health~=nil and get(style,'level_position')=='below'
  local level_prefix=0
  if level~='' and not level_below then
    subtitle=level..(subtitle~='' and (' | '..subtitle) or '')
    level_prefix=#level
  end
  local show_name=get(style,'show_name')~=false and name~=''
  local inside=show_name and name_position=='inside' and health~=nil
  local error_message
  local shared_cache=get(metrics,'measurement_cache')
  local cache
  if type(shared_cache)=='table' then
    local font_key=font
    if font==nil or font~=font then font_key=false end
    cache=shared_cache[font_key] or {}
    shared_cache[font_key]=cache
  else cache={} end
  local function measured(text,size)
    local by_text=cache[size]
    if not by_text then by_text={}; cache[size]=by_text end
    if by_text[text] then return by_text[text] end
    local ok,m=pcall(measure,text,size,font)
    if not ok or type(m)~='table' then error_message='font measurement failed'; return {width=0,height=size,ink_x0=0,ink_y0=0,ink_x1=0,ink_y1=size} end
    local w,h=get(m,'width'),get(m,'height')
    if not finite(w) or not finite(h) or w<0 or h<0 or w>131072 or h>4096 then
      error_message='invalid font measurement'; return {width=0,height=size,ink_x0=0,ink_y0=0,ink_x1=0,ink_y1=size}
    end
    local result={width=w,height=h,ink_x0=get(m,'ink_x0') or 0,ink_y0=get(m,'ink_y0') or 0,
      ink_x1=get(m,'ink_x1') or w,ink_y1=get(m,'ink_y1') or h}
    for _,keyname in ipairs({'ink_x0','ink_y0','ink_x1','ink_y1'}) do
      if not finite(result[keyname]) or math.abs(result[keyname])>131072 then error_message='invalid font ink'; result[keyname]=0 end
    end
    if result.ink_x1<result.ink_x0 or result.ink_y1<result.ink_y0 then error_message='invalid font ink' end
    if not error_message then by_text[text]=result end
    return result
  end
  local function text_extent(text,size)
    local m=measured(text,size)
    return math.max(m.width,m.ink_x1)-math.min(0,m.ink_x0),m
  end
  local function text_height(text,size)
    local m=measured(text,size)
    return math.max(size,m.height,m.ink_y1)-math.min(0,m.ink_y0),math.min(0,m.ink_y0)
  end
  local function fit(text,size,available,prefix,suffix)
    if text=='' or available<=0 then return '' end
    prefix,suffix=prefix or '',suffix or ''
    local full=prefix..text..suffix
    if text_extent(full,size)<=available then return full end
    if text_extent(prefix..'...'..suffix,size)>available then return '' end
    local offsets={}
    for at in utf8.codes(text) do offsets[#offsets+1]=at end
    offsets[#offsets+1]=#text+1
    local lo,hi,best=0,#offsets-1,''
    while lo<=hi do
      local mid=math.floor((lo+hi)/2)
      local candidate=prefix..text:sub(1,offsets[mid+1]-1)..'...'..suffix
      if text_extent(candidate,size)<=available then best=candidate; lo=mid+1 else hi=mid-1 end
    end
    return best
  end
  local reserved=inside and health.text~='' and math.min(width/2,text_extent(health.text,bar_font)+gap*2) or 0
  local name_width=inside and math.max(0,width-reserved-gap*2) or width
  local prefix,suffix=clean(get(style,'name_prefix'),16),clean(get(style,'name_suffix'),16)
  local fallback_badge
  if show_name and (prefix~='' or suffix~='') then
    local decorated=fit(name,fs,name_width,prefix,suffix)
    if decorated~='' then name=decorated else fallback_badge=get(style,'name_fallback_badge') end
  end
  local commands={}
  local left,top,right,bottom=math.huge,math.huge,-math.huge,-math.huge
  local command_bounds=get(metrics,'omit_command_bounds')~=true
  local function emit(c,x0,y0,x1,y1)
    if (c.alpha or alpha)<=0 then return end
    if #commands>=R.MAX_COMMANDS then error_message='primitive budget exceeded'; return end
    c.color=c.color or foreground; c.alpha=c.alpha or alpha
    if command_bounds then c.bounds=box(x0,y0,x1,y1) end
    commands[#commands+1]=c
    left,top,right,bottom=math.min(left,x0),math.min(top,y0),math.max(right,x1),math.max(bottom,y1)
  end
  local function rect(x0,y0,x1,y1,rgb,opacity,rounding,thickness)
    if x1<=x0 or y1<=y0 then return end
    if thickness then thickness=clamp(thickness,0.01,64,1) end
    local stroke=thickness or 0; local fringe=1+stroke*0.5
    emit({kind=thickness and 'Rect' or 'RectFilled',x0=x0,y0=y0,x1=x1,y1=y1,
      color=rgb,alpha=opacity,rounding=rounding or 0,thickness=thickness},x0-fringe,y0-fringe,x1+fringe,y1+fringe)
  end
  local function line(x0,y0,x1,y1,rgb,thickness,opacity)
    thickness=clamp(thickness,0.01,64,1)
    local fringe=1+thickness*0.5
    emit({kind='Line',x0=x0,y0=y0,x1=x1,y1=y1,color=rgb,alpha=opacity,thickness=thickness},
      math.min(x0,x1)-fringe,math.min(y0,y1)-fringe,math.max(x0,x1)+fringe,math.max(y0,y1)+fringe)
  end
  local function triangle(x0,y0,x1,y1,x2,y2,rgb,opacity)
    emit({kind='TriangleFilled',x0=x0,y0=y0,x1=x1,y1=y1,x2=x2,y2=y2,color=rgb,alpha=opacity},
      math.min(x0,x1,x2)-1,math.min(y0,y1,y2)-1,math.max(x0,x1,x2)+1,math.max(y0,y1,y2)+1)
  end
  local function circle(x,y,r,rgb,thickness,opacity)
    if thickness then thickness=clamp(thickness,0.01,64,1) end
    local fringe=1+(thickness or 0)*0.5
    emit({kind=thickness and 'Circle' or 'CircleFilled',x=x,y=y,radius=r,color=rgb,alpha=opacity,thickness=thickness},
      x-r-fringe,y-r-fringe,x+r+fringe,y+r+fringe)
  end
  local function label(text,x,y,size,available,align,rgb,shadow,prefix_bytes,prefix_color)
    text=fit(text,size,available); if text=='' then return end
    local extent,m=text_extent(text,size)
    local shift=align=='left' and 0 or align=='right' and available-extent or (available-extent)*0.5
    x=x+shift-math.min(0,m.ink_x0)
    local function text_at(piece,ink,tx,ty,c,a)
      emit({kind='Text',x=tx,y=ty,text=piece,size=size,font=font,color=c,alpha=a},
        tx+ink.ink_x0,ty+ink.ink_y0,tx+ink.ink_x1,ty+ink.ink_y1)
    end
    if shadow then text_at(text,m,x+unit,y+unit,0,alpha*0.82) end
    if prefix_bytes and prefix_bytes>0 and prefix_color~=rgb then
      if #text<=prefix_bytes then text_at(text,m,x,y,prefix_color,alpha)
      else
        local head,tail=text:sub(1,prefix_bytes),text:sub(prefix_bytes+1)
        local head_ink=measured(head,size)
        text_at(head,head_ink,x,y,prefix_color,alpha)
        text_at(tail,measured(tail,size),x+head_ink.width,y,rgb,alpha)
      end
    else text_at(text,m,x,y,rgb or foreground,alpha) end
  end
  local function polygon(points,rgb,thickness)
    if thickness then
      for i,p in ipairs(points) do local q=points[i%#points+1]; line(p[1],p[2],q[1],q[2],rgb,thickness) end
    else
      for i=2,#points-1 do triangle(points[1][1],points[1][2],points[i][1],points[i][2],points[i+1][1],points[i+1][2],rgb) end
    end
  end
  local function diamond(cx,cy,r,rgb)
    triangle(cx-r,cy,cx,cy-r,cx+r,cy,rgb); triangle(cx-r,cy,cx+r,cy,cx,cy+r,rgb)
  end
  local function icon(kind,cx,cy,r,rgb)
    local stroke=math.max(0.5,0.18*r)
    if kind=='eye' then
      local lastx,lasttop,lastbottom=cx-r,cy,cy
      for i=1,8 do
        local t=i/8; local x=cx-r+2*r*t; local bulge=2.4*r*t*(1-t)
        line(lastx,lasttop,x,cy-bulge,rgb,stroke); line(lastx,lastbottom,x,cy+bulge,rgb,stroke)
        lastx,lasttop,lastbottom=x,cy-bulge,cy+bulge
      end
      circle(cx,cy,r*0.3,rgb)
    elseif kind=='peace' then
      triangle(cx,cy+r*0.2,cx-r*0.7,cy-r,cx+r*0.15,cy-r*0.3,rgb)
      triangle(cx-r*0.3,cy+r*0.3,cx-r,cy+r*0.6,cx-r*0.6,cy-r*0.2,rgb)
      circle(cx+r*0.15,cy+r*0.1,r*0.45,rgb); circle(cx+r*0.55,cy-r*0.2,r*0.25,rgb)
      triangle(cx+r*0.7,cy-r*0.3,cx+r,cy-r*0.15,cx+r*0.7,cy,rgb)
      line(cx+r*0.6,cy+r*0.15,cx+r*0.9,cy+r*0.8,rgb,stroke)
    elseif kind=='sleep' then
      line(cx-r*0.8,cy-r*0.8,cx+r*0.8,cy-r*0.8,rgb,stroke)
      line(cx+r*0.8,cy-r*0.8,cx-r*0.8,cy+r*0.8,rgb,stroke)
      line(cx-r*0.8,cy+r*0.8,cx+r*0.8,cy+r*0.8,rgb,stroke)
    elseif kind=='star' then
      local points={}
      for i=0,9 do local a=i*math.pi/5-math.pi/2; local radius=i%2==0 and r or r*0.43
        points[i+1]={cx+math.cos(a)*radius,cy+math.sin(a)*radius} end
      for i,p in ipairs(points) do local q=points[i%10+1]; triangle(cx,cy,p[1],p[2],q[1],q[2],rgb) end
    elseif kind=='sun' then
      circle(cx,cy,r*0.5,rgb)
      for i=0,7 do local a=i*math.pi/4; local dx,dy=math.cos(a)*r,math.sin(a)*r
        line(cx+dx*0.7,cy+dy*0.7,cx+dx,cy+dy,rgb,stroke) end
    elseif kind=='moon' then
      circle(cx,cy,r*0.9,rgb); circle(cx+r*0.4,cy-r*0.25,r*0.75,background)
    elseif kind=='diamond' then diamond(cx,cy,r,rgb)
    elseif kind=='triangle' then triangle(cx,cy-r,cx+r,cy+r*0.8,cx-r,cy+r*0.8,rgb)
    elseif kind=='cross' then
      rect(cx-r*0.25,cy-r,cx+r*0.25,cy+r,rgb); rect(cx-r,cy-r*0.25,cx+r,cy+r*0.25,rgb)
    elseif kind=='skull' then
      circle(cx,cy-r*0.2,r*0.8,rgb); rect(cx-r*0.5,cy,cx+r*0.5,cy+r*0.9,rgb)
      circle(cx-r*0.3,cy-r*0.2,r*0.2,background); circle(cx+r*0.3,cy-r*0.2,r*0.2,background)
      line(cx,cy+r*0.5,cx,cy+r*0.9,background,stroke)
    elseif kind=='sword' then
      polygon({{cx-r*0.15,cy+r*0.2},{cx+r*0.75,cy-r*0.85},{cx+r*0.85,cy-r*0.75},{cx+r*0.1,cy+r*0.4}},rgb)
      line(cx-r*0.65,cy+r*0.75,cx-r*0.1,cy+r*0.2,rgb,stroke*2)
      line(cx-r*0.5,cy,cx+r*0.4,cy+r*0.7,rgb,stroke*1.5)
    else circle(cx,cy,r*0.8,rgb,stroke) end
  end
  local badges={}
  local side=badge_position=='left' or badge_position=='right'
  local badge_scale=number(style,'badge_scale',0.5,2.5,1)
  local badge_gap=gap*badge_scale
  local badge_limit=side and math.min(width,140*unit) or width
  local badge_size=math.min(math.max(14*unit,fs*0.8)*badge_scale,math.max(unit,badge_limit-badge_gap*2))
  local badge_font=clamp(tag_font*badge_scale,4,256,14)
  local column_width=0
  for i=1,R.MAX_BADGES do
    local b=i==1 and fallback_badge or get(get(style,'badges'),i-(fallback_badge and 1 or 0))
    if type(b)~='table' then break end
    local text=clean(get(b,'label'),48)
    local icon_width=get(b,'text_only')==true and 0 or badge_size
    local padding=badge_gap*(icon_width>0 and 3 or 2)
    local available=math.max(0,badge_limit-icon_width-padding)
    local extent=text_extent(text,badge_font)
    local size=badge_font
    if icon_width==0 and extent>available then size=math.max(4,badge_font*available/extent) end
    local label_width=math.min(text_extent(text,size),available)
    if text=='' then padding=badge_gap*2 end
    local bw=math.min(badge_limit,icon_width+padding+label_width)
    badges[#badges+1]={data=b,text=text,width=bw,font_size=size,icon_width=icon_width,x=0,y=0}
    column_width=math.max(column_width,bw)
  end
  if not health and not mana and not cast and not show_name and subtitle=='' and #badges==0 then
    return {hide=true,bounds=box(0,0,0,0),commands={},vertex_cost=0,clickable=false}
  end
  local marker=target and target_style=='arrow' and 10*unit or 0
  local y=marker
  local function badge_rows(top)
    local x,row=0,top
    for _,b in ipairs(badges) do
      if x>0 and x+b.width>width then x=0; row=row+badge_size+badge_gap end
      b.x=x-width/2; b.y=row; x=x+b.width+badge_gap
    end
    return #badges>0 and row+badge_size+badge_gap or top
  end
  if badge_position=='above' then y=badge_rows(y) end
  local body_top=y
  local name_y=y
  if show_name and name_position~='below' and not inside then
    local h,above=text_height(name,fs); name_y=y-above; y=y+h+unit+row_gap
  end
  local subtitle_y=y
  if subtitle~='' then local h,above=text_height(subtitle,tag_font); subtitle_y=y-above; y=y+h+unit+row_gap end
  local function height(value,key,default)
    if not value then return 0 end
    local h=number(style,key,1,80,default)*unit
    if value.text~='' then h=math.max(h,text_height(value.text,bar_font)+gap+unit) end
    -- Even extreme border overrides must leave a genuine resource fill interior.
    return math.max(h,unit)
  end
  local hh,mh,ch=height(health,'height',12),height(mana,'mana_height',4),height(cast,'cast_height',9)
  if inside then hh=math.max(hh,text_height(name,fs)+gap+unit) end
  local hy=y; if health then y=y+hh end
  local level_y=y
  if level_below then
    local h,above=text_height(level,tag_font)
    level_y=y+row_gap-above
    y=y+row_gap+h+unit
  end
  if health and mana then y=y+row_gap end
  local my=y; if mana then y=y+mh end
  if cast and (health or mana) then y=y+number(style,'cast_gap',0,24,2)*unit+out*2 end
  local cy=y; if cast then y=y+ch end
  if show_name and name_position=='below' then
    if health or mana or cast then y=y+row_gap end
    local h,above=text_height(name,fs); name_y=y-above; y=y+h+unit
  end
  if side and #badges>0 then
    local column_h=#badges*(badge_size+badge_gap)-badge_gap
    local body_h=y-body_top
    local shift=math.max(0,(column_h-body_h)/2)
    name_y=name_y+shift; subtitle_y=subtitle_y+shift; level_y=level_y+shift; hy=hy+shift; my=my+shift; cy=cy+shift
    local top=body_top+math.max(0,(body_h-column_h)/2)
    local decoration=frame=='runic' and 17*unit or out+2*unit
    if target and target_style=='glow' then decoration=math.max(decoration,9*unit) end
    local side_edge=body_width/2+decoration
    if chevron_right then
      local badge_fringe=frame=='minimal' and 0 or 1+((frame=='bevel' or frame=='runic') and unit/2 or 0)
      side_edge=math.max(side_edge,chevron_right+badge_fringe)
    end
    for i,b in ipairs(badges) do
      b.x=badge_position=='left' and -side_edge-badge_gap-column_width or side_edge+badge_gap
      b.y=top+(i-1)*(badge_size+badge_gap)
    end
  elseif badge_position=='below' and #badges>0 then badge_rows(y+row_gap) end
  if marker>0 then triangle(-3.5*unit,unit,3.5*unit,unit,0,7.3*unit,border) end
  for _,b in ipairs(badges) do
    local rgb=color(get(b.data,'color')); local bx,by=b.x,b.y
    if frame~='minimal' then rect(bx,by,bx+b.width,by+badge_size,background,nil,2*unit) end
    if frame=='bevel' or frame=='runic' then rect(bx,by,bx+b.width,by+badge_size,border,nil,unit,unit) end
    local cx,by_center=bx+badge_gap+badge_size/2,by+badge_size/2
    local drawn=false
    local spell=get(b.data,'spell_icon')
    if finite(spell) and spell>=0 and spell<=65535 and spell%1==0 and type(get(metrics,'texture'))=='function' then
      local ok,t=pcall(metrics.texture,spell)
      if ok and type(t)=='table' and get(t,'texture')~=nil and get(t,'texture')~=false and get(t,'texture')~=0 then
        local u0,v0,u1,v1=number(t,'u0',0,1,0),number(t,'v0',0,1,0),number(t,'u1',0,1,1),number(t,'v1',0,1,1)
        if u1>=u0 and v1>=v0 then
          local radius=badge_size*0.45
          emit({kind='Image',texture=get(t,'texture'),x0=cx-radius,y0=by_center-radius,x1=cx+radius,y1=by_center+radius,
            u0=u0,v0=v0,u1=u1,v1=v1,color=0xffffff},cx-radius,by_center-radius,cx+radius,by_center+radius); drawn=true
        end
      end
    end
    if not drawn and b.icon_width>0 then icon(get(b.data,'icon'),cx,by_center,badge_size*0.35,rgb) end
    local label_x=bx+badge_gap+(b.icon_width>0 and b.icon_width+badge_gap or 0)
    label(b.text,label_x,by+(badge_size-b.font_size)/2,b.font_size,math.max(0,bx+b.width-badge_gap-label_x),'left',rgb,false)
  end
  local function fill(x0,y0,x1,y1,rgb,rounding)
    if x1<=x0 or y1<=y0 then return end
    if texture=='gradient' or texture=='gloss' then
      emit({kind='Gradient',x0=x0,y0=y0,x1=x1,y1=y1,color=tone(rgb,1.15),color2=tone(rgb,0.75),alpha2=alpha,
        rounding=rounding},x0-1,y0-1,x1+1,y1+1)
    else rect(x0,y0,x1,y1,rgb,nil,rounding) end
    if texture=='gloss' then
      local shine=(y1-y0)*0.42
      rect(x0,y0,x1,y0+shine,0xffffff,alpha*0.23,math.min(rounding,shine/2))
      line(x0,y0+shine,x1,y0+shine,0xffffff,math.min(unit,y1-y0),alpha*0.18)
    end
  end
  local function target_trim(x0,y0,x1,y1,rounding)
    if target_style=='arrow' then rect(x0-unit,y0-unit,x1+unit,y1+unit,border,nil,rounding,unit)
    elseif target_style=='chevrons' then
      local middle=(y0+y1)/2
      for _,direction in ipairs({-1,1}) do for _,distance in ipairs({0,7}) do
        local tip=direction*(chevron_tip+distance*unit)
        local tail=tip+direction*5*unit
        line(tail,middle-5*unit,tip,middle,border,2*unit)
        line(tip,middle,tail,middle+5*unit,border,2*unit)
      end end
    elseif target_style=='brackets' then
      for _,direction in ipairs({-1,1}) do for _,row in ipairs({-1,1}) do
        local x=(direction<0 and x0 or x1)+direction*3*unit
        local yy=(row<0 and y0 or y1)+row*3*unit
        line(x,yy-row*5*unit,x,yy,border,1.5*unit)
        line(x,yy,x-direction*7*unit,yy,border,1.5*unit)
      end end
    elseif target_style=='glow' then
      for ring=6,1,-1 do local r=ring*unit
        rect(x0-r,y0-r,x1+r,y1+r,border,alpha*(0.32-ring*0.035),rounding+r,unit)
      end
    end
  end
  local function draw_bar(value,yy,h,w,align,primary,is_health)
    if not value then return end
    local x0,x1=-w/2,w/2
    local y1=yy+h
    local edge=math.min(number(style,'border_width',0,4,1)*unit,math.max(0,(h-unit)/2),math.max(0,(w-unit)/2))
    local rounding=math.min(number(style,'rounding',0,12,2)*unit,h/2)
    if frame=='bevel' then
      rect(x0-out,yy-out,x1+out,y1+out,tone(border,0.45))
      line(x0-out,yy-out,x1+out,yy-out,tone(border,1.85),1.5*unit)
      line(x0-out,yy-out,x0-out,y1+out,tone(border,1.4),1.5*unit)
      line(x0-out,y1+out,x1+out,y1+out,0,1.5*unit)
      line(x1+out,yy-out,x1+out,y1+out,0,1.5*unit)
    elseif frame=='glass' then
      local radius=math.max(rounding,h*0.35)
      rect(x0-out,yy-out,x1+out,y1+out,0x060b13,alpha*0.88,radius)
      rect(x0-out,yy-out,x1+out,y1+out,0xb4d1e8,alpha*0.32,radius,unit)
    elseif frame=='runic' then
      local cut=math.min(h/2+out,7*unit)
      local points={{x0-out+cut,yy-out},{x1+out-cut,yy-out},{x1+out,(yy+y1)/2},
        {x1+out-cut,y1+out},{x0-out+cut,y1+out},{x0-out,(yy+y1)/2}}
      polygon(points,tone(border,0.45)); polygon(points,border,1.4*unit)
      if primary then for _,direction in ipairs({-1,1}) do
        local cx=(direction<0 and x0 or x1)+direction*10*unit
        diamond(cx,(yy+y1)/2,5*unit,border); circle(cx,(yy+y1)/2,1.6*unit,background)
      end end
    end
    rect(x0,yy,x1,y1,background,frame=='minimal' and alpha*0.45 or alpha,rounding)
    local ix,iy,iw,ih=x0+edge,yy+edge,w-edge*2,h-edge*2
    local right=ix+iw*value.value/100
    if texture=='segments' then
      local count=math.max(1,math.min(10,math.floor(iw/math.max(2,unit*3))))
      local segment=iw/count; local segment_gap=math.min(unit*1.5,segment*0.25)
      for i=0,count-1 do local left=ix+i*segment
        fill(left,iy,math.min(right,left+segment-(i+1<count and segment_gap or 0)),iy+ih,value.color,0)
      end
    else fill(ix,iy,right,iy+ih,value.color,math.max(0,rounding-edge)) end
    if frame=='minimal' then line(x0,y1,x1,y1,border,math.max(0.5,edge))
    elseif edge>0 then rect(x0+edge/2,yy+edge/2,x1-edge/2,y1-edge/2,border,nil,rounding,edge) end
    if primary and target then target_trim(x0,yy,x1,y1,rounding) end
    local label_x,label_width=x0+gap,w-gap*2
    local bar_reserved=is_health and reserved or 0
    if bar_reserved>0 then
      label_width=bar_reserved-gap
      label_x=health_align=='left' and x0+gap or x1-bar_reserved
    end
    local th,above=text_height(value.text,bar_font)
    label(value.text,label_x,yy+(h-th)/2-above,bar_font,label_width,align,foreground,true)
    if inside and is_health then
      local nx=health_align=='left' and x0+reserved+gap or x0+gap
      local nh,name_above=text_height(name,fs)
      label(name,nx,yy+(h-nh)/2-name_above,fs,name_width,text_align,name_color,true)
    end
  end
  draw_bar(health,hy,hh,width,health_align,true,true)
  draw_bar(mana,my,mh,width,health_align,not health,false)
  draw_bar(cast,cy,ch,cast_width,cast_align,not health and not mana,false)
  if show_name and not inside then label(name,-width/2,name_y,fs,width,text_align,name_color,true) end
  label(subtitle,-width/2,subtitle_y,tag_font,width,text_align,foreground,true,level_prefix,level_color)
  if level_below then label(level,-width/2,level_y,tag_font,width,text_align,level_color,true) end
  if error_message then return nil,error_message end
  if #commands==0 then return {hide=true,bounds=box(0,0,0,0),commands={},vertex_cost=0,clickable=false} end
  local vertex_cost=R.vertex_cost(commands)
  if vertex_cost>R.MAX_MODEL_VERTICES then return nil,'plate geometry budget exceeded' end
  local total=box(left,top,right,bottom)
  if total.w>4096 or total.h>4096 then return nil,'plate bounds exceeded' end
  local dy=-total.y-total.h-number(style,'offset_y',-200,200,0)*unit
  for _,command in ipairs(commands) do translate_y(command,dy) end
  total.y=total.y+dy
  return {bounds=total,commands=commands,vertex_cost=vertex_cost,stack=get(style,'stack')==true,
    stack_gap=number(style,'stack_gap',0,20,4)*unit,max_stack=number(style,'max_stack',0,200,96)*unit,
    through_walls=get(style,'through_walls')==true,clickable=get(style,'clickable')~=false}
end

-- records: {model,x,y,id,target,depth}. The array order is retained; offset_y is
-- a negative upward displacement added by the adapter to ALL local primitives.
-- Targets and nonstacking plates are pinned. Others use a deterministic target,
-- nearest, spawn-id priority. Work is bounded to 256 stacking candidates and
-- 4096 blockers; crowded overflow is hidden rather than drawn through neighbors.
function R.stack(records,viewport,exhausted)
  if type(records)~='table' then return records end
  viewport=type(viewport)=='table' and viewport or {}
  local vx,vy=number(viewport,'x',-131072,131072,0),number(viewport,'y',-131072,131072,0)
  local vw,vh=number(viewport,'w',0,131072,131072),number(viewport,'h',0,131072,131072)
  local order,blockers={},{}
  local function rect_for(r,offset)
    local b=r.model.bounds
    return {x=r.x+b.x,y=r.y+b.y+offset,w=b.w,h=b.h}
  end
  for i=1,math.min(rawlen(records),R.MAX_RECORDS) do
    local r=records[i]
    if type(r)=='table' then r.offset_y=0; r.hidden=false end
    local model=get(r,'model'); local b=get(model,'bounds')
    if type(r)~='table' or type(b)~='table' or not finite(get(r,'x')) or not finite(get(r,'y')) or
      not finite(get(b,'x')) or not finite(get(b,'y')) or not finite(get(b,'w')) or not finite(get(b,'h')) or b.w<0 or b.h<0 or get(model,'hide') then
      if type(r)=='table' then r.hidden=true end
    elseif not model.stack or r.target then
      blockers[#blockers+1]=rect_for(r,0)
    else order[#order+1]={record=r,index=i} end
  end
  table.sort(order,function(a,b)
    local ad,bd=finite(a.record.depth) and a.record.depth or math.huge,finite(b.record.depth) and b.record.depth or math.huge
    if ad~=bd then return ad<bd end
    local ai,bi=finite(a.record.id) and a.record.id or math.huge,finite(b.record.id) and b.record.id or math.huge
    return ai~=bi and ai<bi or ai==bi and a.index<b.index
  end)
  for index,item in ipairs(order) do
    if exhausted and exhausted() then
      for remaining=index,#order do order[remaining].record.hidden=true end
      break
    end
    local r=item.record; local b=rect_for(r,0)
    if index>R.MAX_STACKED or b.x+b.w<vx or b.x>vx+vw or b.y+b.h<vy or b.y>vy+vh then r.hidden=true
    else
      local limit=clamp(r.model.max_stack,0,800,96); local gap=clamp(r.model.stack_gap,0,80,4)
      -- Forbidden top-edge intervals for horizontally overlapping blockers.
      -- Merging them once avoids repeatedly scanning a crowded cluster after
      -- each upward move: O(blockers log blockers), rather than quadratic scans.
      local intervals={}
      for _,other in ipairs(blockers) do
        if b.x<other.x+other.w+gap and other.x<b.x+b.w+gap then
          intervals[#intervals+1]={lo=other.y-gap-b.h,hi=other.y+other.h+gap}
        end
      end
      table.sort(intervals,function(a,c) return a.lo<c.lo end)
      local merged={}
      for _,interval in ipairs(intervals) do
        local last=merged[#merged]
        if last and interval.lo<last.hi then last.hi=math.max(last.hi,interval.hi)
        else merged[#merged+1]={lo=interval.lo,hi=interval.hi} end
      end
      for _,interval in ipairs(merged) do
        if b.y>interval.lo and b.y<interval.hi then r.offset_y=interval.lo-b.y; b.y=interval.lo; break end
      end
      if -r.offset_y>limit or b.y<vy then r.hidden=true end
      if not r.hidden then blockers[#blockers+1]=b end
    end
  end
  for i=R.MAX_RECORDS+1,rawlen(records) do if type(records[i])=='table' then records[i].offset_y=0; records[i].hidden=true end end
  return records
end

return R
