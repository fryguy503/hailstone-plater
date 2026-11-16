-- SPDX-License-Identifier: MIT
-- Passive RoF2 exact resource observations (docs/protocol/rof2-packets.md, PointsUpdate).
-- Only identities in the caller's current group / extended-target scope can establish a reading.
local M={MAX_ENTRIES=128}
local MANA,ENDURANCE=0x3791,0x5f42
local SIGNED_MAX=2147483647
local Decoder={}
Decoder.__index=Decoder

local function valid_id(id)
  return type(id)=='number' and id==id and id>=1 and id<=65535 and id%1==0
end

local function tracked_name(tracked,id)
  if type(tracked)~='table' or not valid_id(id) then return nil end
  local name=rawget(tracked,id)
  return type(name)=='string' and #name>0 and #name<=256 and name or nil
end

function M.new()
  local self=setmetatable({},Decoder)
  self:reset()
  return self
end

function Decoder:reset() self.entries={}; self.count=0 end

function Decoder:remove(id)
  if valid_id(id) and self.entries[id] then
    self.entries[id]=nil; self.count=self.count-1
  end
end

function Decoder:prune(tracked)
  for id,entry in pairs(self.entries) do
    if tracked_name(tracked,id)~=entry.name then self:remove(id) end
  end
end

local function clear_pool(self,id,pool)
  local entry=self.entries[id]
  if not entry then return end
  entry[pool]=nil
  if entry.mana==nil and entry.endurance==nil then self:remove(id) end
end

-- The return value describes accepted data only, never a host packet-filter decision.
-- An invalid update with an identifiable tracked id clears that pool's earlier reading.
function Decoder:receive(opcode,bytes,tracked)
  local pool=opcode==MANA and 'mana' or opcode==ENDURANCE and 'endurance' or nil
  if not pool or type(bytes)~='string' then return false end
  self:prune(tracked)
  if #bytes<10 then return false end -- no complete entity id is available in a truncated record
  local current,maximum,id=string.unpack('<I4I4I2',bytes)
  local name=tracked_name(tracked,id)
  if not name then return false end
  if #bytes~=10 or current>SIGNED_MAX or maximum>SIGNED_MAX or maximum==0 or current>maximum then
    clear_pool(self,id,pool)
    return false
  end
  local entry=self.entries[id]
  if not entry then
    if self.count>=M.MAX_ENTRIES then return false end
    entry={name=name}; self.entries[id]=entry; self.count=self.count+1
  end
  entry[pool]=100*current/maximum
  return true
end

-- There is no guessed time-to-live. Reliable resource updates remain meaningful until tracking scope or
-- identity ends, a despawn is observed, or an explicit invalid update invalidates that particular pool.
function Decoder:values(id,name)
  if not valid_id(id) or type(name)~='string' then return nil,nil end
  local entry=self.entries[id]
  if not entry or entry.name~=name then return nil,nil end
  return entry.mana,entry.endurance
end

return M
