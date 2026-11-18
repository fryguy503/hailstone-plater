-- SPDX-License-Identifier: MIT
-- Passive decoder for optional server-supplied nameplate indicators.
-- Protocol references: eq-core-dll/src/nameplates.h and
-- Server-side nameplate_{awareness,markers}.h. No host state or I/O.
local M = {}

local AWARENESS_OPCODE, MARKER_OPCODE = 0x7d1d, 0x7d1c
local MARKER_MAGIC = 0x4e504d31
local snapshot_streams = { [0x4e504131] = 'awareness', [0x4e505331] = 'status', [0x4e504331] = 'concealment' }
local TTL, MAX_ENTRIES, MAX_CHUNKS, MAX_CHUNK_ENTRIES = 5000, 1024, 9, 122
local U32 = 4294967296
local icons = { "star", "sun", "moon", "diamond", "triangle", "cross", "skull", "sword" }
local labels = { "Star", "Sun", "Moon", "Diamond", "Triangle", "Cross", "Skull", "Sword" }
local colors = { 0xffd250, 0xffa845, 0xc4a6ff, 0x65e1ff, 0x66df91, 0xff7575, 0xf1edf8, 0xe2e8ef }

local function integer(value, minimum, maximum)
    return type(value) == "number" and value == value and value >= minimum
        and value <= maximum and value % 1 == 0
end

local function valid_time(now)
    return type(now) == "number" and now == now and now >= 0 and now < math.huge
end

local function u16(bytes, at)
    local a, b = bytes:byte(at, at + 1)
    return a + b * 256
end

local function u32(bytes, at)
    local a, b, c, d = bytes:byte(at, at + 3)
    return a + b * 256 + c * 65536 + d * 16777216
end

local function newer(candidate, previous)
    local delta = (candidate - previous) % U32
    return delta > 0 and delta < U32 / 2
end

local function stream()
    return { entries = {}, count = 0 }
end

local Decoder = {}
Decoder.__index = Decoder

function M.new()
    local self = setmetatable({}, Decoder)
    self:reset()
    return self
end

function Decoder:reset()
    self.snapshots = {}
    for _, name in pairs(snapshot_streams) do self.snapshots[name] = stream() end
    self.marks = { {}, {} }
    self.last_time = nil
end

-- Callers supply their monotonic clock. A backwards jump fails closed rather
-- than extending a stale badge indefinitely.
function Decoder:_advance(now)
    if not valid_time(now) then return false end
    if now == self.last_time then return true end
    if self.last_time and now < self.last_time then self:reset() end
    self.last_time = now
    for _, state in pairs(self.snapshots) do
        if state.received_at and now - state.received_at > TTL then
            state.entries, state.count = {}, 0
            state.generation, state.received_at = nil, nil
        end
        if state.pending and now - state.pending.started_at > TTL then
            state.pending = nil
        end
    end
    return true
end

local function parse_snapshot(bytes)
    if #bytes < 16 or #bytes > 504 then return nil end
    local name = snapshot_streams[u32(bytes, 1)]
    if not name then return nil end
    local version, flags = bytes:byte(5, 6)
    local count, generation = u16(bytes, 7), u32(bytes, 9)
    local index, chunks = u16(bytes, 13), u16(bytes, 15)
    if version ~= 1 or flags ~= 1 or count > MAX_CHUNK_ENTRIES
        or chunks < 1 or chunks > MAX_CHUNKS or index >= chunks
        or #bytes ~= 16 + count * 4
        or (count == 0 and (chunks ~= 1 or index ~= 0)) then
        return nil
    end
    local entries = {}
    for offset = 17, #bytes, 4 do
        local id, entry_flags = u16(bytes, offset), bytes:byte(offset + 2)
        if id == 0 or entry_flags < 1 or entry_flags > 3
            or bytes:byte(offset + 3) ~= 0 or entries[id] then
            return nil
        end
        entries[id] = entry_flags
    end
    return {
        stream = name, count = count, generation = generation,
        index = index, chunks = chunks, entries = entries,
    }
end

local function equal_entries(a, b)
    for id, flags in pairs(a) do if b[id] ~= flags then return false end end
    for id, flags in pairs(b) do if a[id] ~= flags then return false end end
    return true
end

local function accept_snapshot(state, chunk, now)
    local generation = chunk.generation
    if state.removed_generation and not newer(generation, state.removed_generation) then return false end
    if state.generation ~= nil then
        -- Replays do not refresh the snapshot's expiry time.
        if generation == state.generation then return true end
        if not newer(generation, state.generation) then return false end
    end
    local pending = state.pending
    if pending and generation ~= pending.generation then
        if not newer(generation, pending.generation) then return false end
        pending = nil
        state.pending = nil
    end
    if not pending then
        pending = {
            generation = generation, chunks = chunk.chunks, started_at = now,
            received = {}, entries = {}, total = 0, received_count = 0,
        }
        state.pending = pending
    elseif pending.chunks ~= chunk.chunks then
        state.pending = nil
        return false
    end
    local prior = pending.received[chunk.index]
    if prior then
        if equal_entries(prior, chunk.entries) then return true end
        state.pending = nil
        return false
    end
    if pending.total + chunk.count > MAX_ENTRIES then
        state.pending = nil
        return false
    end
    for id in pairs(chunk.entries) do
        if pending.entries[id] then
            state.pending = nil
            return false
        end
    end
    pending.received[chunk.index] = chunk.entries
    pending.received_count = pending.received_count + 1
    pending.total = pending.total + chunk.count
    for id, flags in pairs(chunk.entries) do pending.entries[id] = flags end
    if pending.received_count == pending.chunks then
        state.entries, state.count = pending.entries, pending.total
        state.generation, state.received_at = generation, now
        state.pending = nil
        state.removed_generation = nil
    end
    return true
end

local function parse_marker(bytes)
    if #bytes ~= 16 or u32(bytes, 1) ~= MARKER_MAGIC then return nil end
    local version, action, icon, scope = bytes:byte(5, 8)
    local target = u32(bytes, 9)
    if version ~= 1 or (scope ~= 1 and scope ~= 2) or u32(bytes, 13) ~= 0 then return nil end
    local valid = (action == 1 and icon >= 1 and icon <= 8 and target ~= 0)
        or (action == 2 and icon >= 1 and icon <= 8 and target == 0)
        or ((action == 3 or action == 4) and icon == 0 and target == 0)
        or (action == 5 and icon == 0 and target ~= 0)
    if not valid then return nil end
    return { action = action, icon = icon, scope = scope, target = target }
end

-- True means a valid contract was accepted (including a harmless duplicate).
-- False means malformed, unsupported or older data. This return value is NOT
-- a host packet-filter decision; host receive callbacks must return nil.
function Decoder:receive(opcode, bytes, now_ms)
    if type(bytes) ~= "string" then return false end
    if opcode == AWARENESS_OPCODE then
        local chunk = parse_snapshot(bytes)
        if not chunk or not self:_advance(now_ms) then return false end
        return accept_snapshot(self.snapshots[chunk.stream], chunk, now_ms)
    elseif opcode == MARKER_OPCODE then
        local marker = parse_marker(bytes)
        if not marker or not self:_advance(now_ms) then return false end
        local marks = self.marks[marker.scope]
        if marker.action == 1 then
            for icon = 1, 8 do if marks[icon] == marker.target then marks[icon] = nil end end
            marks[marker.icon] = marker.target
        elseif marker.action == 2 then
            marks[marker.icon] = nil
        elseif marker.action == 3 or marker.action == 4 then
            self.marks[marker.scope] = {}
        elseif marker.action == 5 then
            for icon = 1, 8 do if marks[icon] == marker.target then marks[icon] = nil end end
        end
        return true
    end
    return false
end

function Decoder:badges(spawn_id, now_ms, in_raid)
    if not self:_advance(now_ms) or not integer(spawn_id, 1, U32 - 1) then return {} end
    local result, marks = {}, self.marks[in_raid == true and 2 or 1]
    for icon = 1, 8 do
        if marks[icon] == spawn_id then
            result[#result + 1] = { icon = icons[icon], label = labels[icon], color = colors[icon] }
            break
        end
    end
    local awareness = self.snapshots.awareness.entries[spawn_id]
    if awareness then
        result[#result + 1] = {
            icon = "eye", label = awareness == 2 and "Engaged" or "Detects you", color = 0xffc857,
        }
    end
    local status = self.snapshots.status.entries[spawn_id]
    if status == 1 or status == 3 then
        result[#result + 1] = { icon = "sleep", label = "Mezzed", color = 0xd89bff }
    end
    if status == 2 or status == 3 then
        result[#result + 1] = { icon = "peace", label = "Pacified", color = 0x74deff }
    end
    return result
end

function Decoder:concealment(spawn_id, now_ms)
    if not self:_advance(now_ms) or not integer(spawn_id, 1, 65535) then return nil end
    return self.snapshots.concealment.entries[spawn_id]
end

function Decoder:remove(id)
    if not integer(id, 1, U32 - 1) then return end
    for _, state in pairs(self.snapshots) do
        if state.entries[id] then
            state.entries[id] = nil
            state.count = state.count - 1
        end
        -- Retire a staged generation so late chunks from a despawned entity
        -- cannot republish it. A later server generation can establish state.
        if state.pending then
            state.removed_generation = state.pending.generation
            state.pending = nil
        end
    end
    for scope = 1, 2 do
        for icon = 1, 8 do if self.marks[scope][icon] == id then self.marks[scope][icon] = nil end end
    end
end

function Decoder:diagnostics(now_ms)
    if not self:_advance(now_ms) then return nil end
    local function describe(state)
        return {
            entries = state.count, generation = state.generation,
            age_ms = state.received_at and now_ms - state.received_at or nil,
            pending_chunks = state.pending and state.pending.received_count or 0,
            expected_chunks = state.pending and state.pending.chunks or 0,
        }
    end
    local marks = { 0, 0 }
    for scope = 1, 2 do
        for icon = 1, 8 do if self.marks[scope][icon] then marks[scope] = marks[scope] + 1 end end
    end
    return { awareness = describe(self.snapshots.awareness), status = describe(self.snapshots.status),
        concealment = describe(self.snapshots.concealment), group_marks = marks[1], raid_marks = marks[2] }
end

return M
