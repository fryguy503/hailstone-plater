-- SPDX-License-Identifier: MIT
-- lua tests/resources_test.lua, from the repository root.
local Resources=require('crest.resources')
local tests,assertions=0,0
local function check(value,message) assertions=assertions+1; assert(value,message or 'check failed') end
local function equal(actual,expected,message)
  check(actual==expected,(message or 'unexpected value')..': '..tostring(actual)..' ~= '..tostring(expected))
end
local function test(name,fn)
  local ok,why=pcall(fn); assert(ok,name..': '..tostring(why)); tests=tests+1
end
local MANA,ENDURANCE=0x3791,0x5f42
local function update(current,maximum,id) return string.pack('<I4I4I2',current,maximum,id) end
local function values(d,id,name,mana,endurance)
  local m,e=d:values(id,name); equal(m,mana); equal(e,endurance)
end

test('tracked group and extended targets receive independent supplied resource pools',function()
  local d=Resources.new()
  local tracked={[7]='Groupie',[9]='XTarget'}
  check(d:receive(MANA,update(25,100,7),tracked))
  check(d:receive(ENDURANCE,update(90,200,7),tracked))
  check(d:receive(MANA,update(21,84,9),tracked))
  values(d,7,'Groupie',25,45); values(d,9,'XTarget',25,nil)
  equal(d.count,2)
end)

test('authoritative zero and full pools remain available without a fabricated expiry',function()
  local d=Resources.new(); local tracked={[7]='Groupie'}
  check(d:receive(MANA,update(0,100,7),tracked))
  check(d:receive(ENDURANCE,update(2147483647,2147483647,7),tracked))
  for _=1,20 do d:prune(tracked); values(d,7,'Groupie',0,100) end
end)

test('scope or raw-name changes remove former occupants before values can be reused',function()
  local d=Resources.new(); local tracked={[7]='Former01'}
  check(d:receive(MANA,update(50,100,7),tracked))
  values(d,7,'Different01',nil,nil)
  tracked[7]='Different01'; d:prune(tracked)
  values(d,7,'Former01',nil,nil); equal(d.count,0)
  check(d:receive(ENDURANCE,update(25,100,7),tracked))
  values(d,7,'Different01',nil,25)
  tracked[7]=nil; d:prune(tracked); equal(d.count,0)
end)

test('invalid numeric or extra records clear only the identified resource pool',function()
  local tracked={[7]='Groupie'}
  for _,record in ipairs({update(0x80000000,0x80000000,7),update(1,0x80000000,7),
      update(0xffffffff,100,7),update(10,0,7),update(101,100,7),update(10,100,7)..'\0'}) do
    local d=Resources.new()
    check(d:receive(MANA,update(50,100,7),tracked))
    check(d:receive(ENDURANCE,update(75,100,7),tracked))
    equal(d:receive(MANA,record,tracked),false)
    values(d,7,'Groupie',nil,75)
    equal(d.count,1)
  end
  local d=Resources.new(); check(d:receive(MANA,update(50,100,7),tracked))
  equal(d:receive(MANA,update(1,0,7),tracked),false); equal(d.count,0)
end)

test('unsupported opcodes truncation and untracked ids cannot establish observations',function()
  local d=Resources.new(); local tracked={[7]='Groupie'}
  local record=update(50,100,7)
  for n=0,9 do equal(d:receive(MANA,record:sub(1,n),tracked),false) end
  for _,opcode in ipairs({0,0x3792,0x5f43,'0x3791',false}) do equal(d:receive(opcode,record,tracked),false) end
  for _,bytes in ipairs({false,42,{}}) do equal(d:receive(MANA,bytes,tracked),false) end
  equal(d:receive(MANA,update(50,100,0),{[0]='Not valid'}),false)
  equal(d:receive(MANA,update(50,100,8),tracked),false)
  equal(d.count,0)
end)

test('despawn and reset forget both pools and do not retain caller-owned tracking tables',function()
  local d=Resources.new(); local tracked={[7]='Groupie'}
  check(d:receive(MANA,update(50,100,7),tracked))
  check(d:receive(ENDURANCE,update(75,100,7),tracked))
  tracked[7]='Changed'; values(d,7,'Groupie',50,75) -- values hold copied scalar identity until explicit prune
  d:remove(7); d:remove(7); equal(d.count,0); values(d,7,'Groupie',nil,nil)
  check(d:receive(MANA,update(50,100,7),tracked))
  d:reset(); equal(d.count,0); values(d,7,'Changed',nil,nil)
end)

test('identity pruning is also performed before a new packet is accepted',function()
  local d=Resources.new(); local tracked={[7]='Former',[8]='Other'}
  check(d:receive(MANA,update(50,100,7),tracked))
  check(d:receive(ENDURANCE,update(75,100,7),tracked))
  tracked[7]='Replacement'
  check(d:receive(MANA,update(20,100,7),tracked))
  values(d,7,'Replacement',20,nil); equal(d.count,1)
  tracked[7]=nil
  check(d:receive(MANA,update(30,100,8),tracked))
  values(d,7,'Replacement',nil,nil); equal(d.count,1)
end)

test('storage remains bounded even with an oversized caller tracking scope',function()
  local d=Resources.new(); local tracked={}
  for id=1,Resources.MAX_ENTRIES+50 do tracked[id]='Actor'..id end
  for id=1,Resources.MAX_ENTRIES+50 do
    equal(d:receive(MANA,update(25,100,id),tracked),id<=Resources.MAX_ENTRIES)
  end
  equal(d.count,Resources.MAX_ENTRIES)
  local count=0; for _ in pairs(d.entries) do count=count+1 end; equal(count,Resources.MAX_ENTRIES)
  tracked[1]=nil; d:prune(tracked); equal(d.count,Resources.MAX_ENTRIES-1)
  check(d:receive(ENDURANCE,update(40,100,Resources.MAX_ENTRIES+1),tracked))
  equal(d.count,Resources.MAX_ENTRIES)
end)

test('malformed caller scopes and ids fail closed without invoking metatables',function()
  local d=Resources.new()
  local tracked=setmetatable({[7]='Groupie'},{__index=function() error('must not run') end})
  check(d:receive(MANA,update(50,100,7),tracked))
  equal(d:receive(MANA,update(50,100,8),tracked),false)
  for _,id in ipairs({0,-1,65536,1.5,0/0,math.huge,'7',false,{}}) do
    values(d,id,'Groupie',nil,nil); d:remove(id)
  end
  d:prune(false); equal(d.count,0)
  equal(d:receive(MANA,update(50,100,7),{[7]=string.rep('x',257)}),false)
end)

test('random and cut resource payloads never throw or exceed bounds',function()
  local d=Resources.new(); local tracked={}
  for id=1,128 do tracked[id]='Actor'..id end
  local random=73421
  local function byte() random=(random*1664525+1013904223)%4294967296; return random%256 end
  for _=1,2500 do
    local bytes={}; for _=1,byte()%32 do bytes[#bytes+1]=string.char(byte()) end
    local ok=pcall(d.receive,d,byte()%2==0 and MANA or ENDURANCE,table.concat(bytes),tracked)
    check(ok); check(d.count>=0 and d.count<=Resources.MAX_ENTRIES)
  end
end)

print(string.format('Resource observations: %d tests, %d assertions passed',tests,assertions))
