-- SPDX-License-Identifier: MIT
-- Run from the repository root: lua tests/server_indicators_test.lua
local Server = require("crest.server_indicators")
local tests, assertions = 0, 0

local function check(condition, message)
    assertions = assertions + 1
    assert(condition, message or "check failed")
end

local function equal(actual, expected, message)
    check(actual == expected, (message or "unexpected value") .. ": " .. tostring(actual) .. " ~= " .. tostring(expected))
end

local function test(name, run)
    local ok, failure = pcall(run)
    assert(ok, name .. ": " .. tostring(failure))
    tests = tests + 1
end

local function le16(n)
    return string.char(n % 256, math.floor(n / 256) % 256)
end

local function le32(n)
    return le16(n % 65536) .. le16(math.floor(n / 65536))
end

local function snapshot(magic, generation, index, chunks, entries, options)
    options = options or {}
    local parts = {
        le32(magic), string.char(options.version or 1, options.flags or 1),
        le16(options.count or #entries), le32(generation), le16(index), le16(chunks),
    }
    for _, entry in ipairs(entries) do
        parts[#parts + 1] = le16(entry[1]) .. string.char(entry[2], entry[3] or 0)
    end
    return table.concat(parts)
end

local A, S, P, C = 0x4e504131, 0x4e505331, 0x4e504d31, 0x4e504331
local AO, MO = 0x7d1d, 0x7d1c
local function mark(action, icon, scope, target, options)
    options = options or {}
    return le32(options.magic or P) .. string.char(options.version or 1, action, icon, scope)
        .. le32(target) .. le32(options.reserved or 0)
end

local function badge(decoder, id, now, token, in_raid)
    for _, item in ipairs(decoder:badges(id, now, in_raid)) do
        if item.icon == token then return item end
    end
    return nil
end

test("unsupported servers and untrusted argument types stay empty", function()
    local d = Server.new()
    for _, opcode in ipairs({ 0, 0x7d1b, 0x7d1e, "0x7d1d" }) do
        equal(d:receive(opcode, snapshot(A, 1, 0, 1, { { 1, 1 } }), 0), false)
    end
    for _, payload in ipairs({ false, 42, {} }) do equal(d:receive(AO, payload, 0), false) end
    equal(d:receive(AO, nil, 0), false)
    equal(#d:badges(1, 0), 0)
end)

test("independent streams and all awareness/status flags", function()
    local d = Server.new()
    check(d:receive(AO, snapshot(A, 12, 0, 1, { { 1, 1 }, { 2, 2 }, { 3, 3 } }), 0))
    check(d:receive(AO, snapshot(S, 3, 0, 1, { { 1, 1 }, { 2, 2 }, { 3, 3 } }), 0))
    equal(badge(d, 1, 0, "eye").label, "Detects you")
    equal(badge(d, 2, 0, "eye").label, "Engaged")
    equal(badge(d, 3, 0, "eye").label, "Detects you")
    equal(#d:badges(1, 0), 2)
    equal(#d:badges(2, 0), 2)
    equal(#d:badges(3, 0), 3)
    equal(#d:badges(4, 0), 0)
    check(badge(d, 3, 0, "sleep"))
    check(badge(d, 3, 0, "peace"))
    for _, item in ipairs(d:badges(3, 0)) do
        check(type(item.color) == "number" and item.color >= 0 and item.color <= 0xffffff)
        check(#item.label <= 32)
    end
end)

test("empty full snapshots clear only their stream", function()
    local d = Server.new()
    check(d:receive(AO, snapshot(A, 1, 0, 1, { { 1, 1 } }), 0))
    check(d:receive(AO, snapshot(S, 1, 0, 1, { { 1, 3 } }), 0))
    check(d:receive(AO, snapshot(A, 2, 0, 1, {}), 1))
    equal(#d:badges(1, 1), 2)
    check(not badge(d, 1, 1, "eye"))
    check(d:receive(AO, snapshot(S, 2, 0, 1, {}), 2))
    equal(#d:badges(1, 2), 0)
end)

test("concealment snapshot is atomic, independent, and expires", function()
    local d = Server.new()
    check(d:receive(AO, snapshot(A, 2, 0, 1, { { 1, 1 } }), 1))
    check(d:receive(AO, snapshot(C, 2, 1, 2, { { 1, 3 } }), 2))
    equal(d:concealment(1, 2), nil)
    check(d:receive(AO, snapshot(C, 2, 0, 2, { { 2, 1 }, { 3, 2 } }), 3))
    equal(d:concealment(1, 3), 3)
    equal(d:concealment(2, 3), 1)
    equal(d:concealment(3, 3), 2)
    check(badge(d, 1, 3, "eye"))
    d:remove(1)
    equal(d:concealment(1, 3), nil)
    equal(d:concealment(2, 5004), nil)
end)

test("malformed snapshot headers never publish", function()
    local cases = {
        snapshot(0, 1, 0, 1, {}), snapshot(A, 1, 0, 1, {}, { version = 2 }),
        snapshot(A, 1, 0, 1, {}, { flags = 0 }), snapshot(A, 1, 0, 1, {}, { flags = 3 }),
        snapshot(A, 1, 0, 0, {}), snapshot(A, 1, 0, 10, {}), snapshot(A, 1, 1, 1, { { 1, 1 } }),
        snapshot(A, 1, 0, 2, {}), snapshot(A, 1, 1, 2, {}),
        snapshot(A, 1, 0, 1, { { 1, 1 } }, { count = 0 }),
        snapshot(A, 1, 0, 1, { { 1, 1 } }, { count = 2 }),
        snapshot(A, 1, 0, 1, { { 1, 1 } }) .. "x",
    }
    for _, bytes in ipairs(cases) do
        local d = Server.new()
        equal(d:receive(AO, bytes, 0), false)
        equal(#d:badges(1, 0), 0)
    end
    local valid = snapshot(A, 1, 0, 1, { { 1, 1 } })
    for length = 0, #valid - 1 do
        equal(Server.new():receive(AO, valid:sub(1, length), 0), false)
    end
end)

test("malformed entries and oversized chunks are rejected", function()
    for _, entries in ipairs({ { { 0, 1 } }, { { 1, 0 } }, { { 1, 4 } }, { { 1, 255 } },
        { { 1, 1, 1 } }, { { 1, 1 }, { 1, 2 } } }) do
        equal(Server.new():receive(AO, snapshot(A, 1, 0, 1, entries), 0), false)
    end
    local entries = {}
    for id = 1, 123 do entries[#entries + 1] = { id, 1 } end
    equal(Server.new():receive(AO, snapshot(S, 1, 0, 1, entries), 0), false)
end)

test("reordered chunks publish atomically and preserve the prior snapshot", function()
    local d = Server.new()
    check(d:receive(AO, snapshot(A, 1, 0, 1, { { 99, 1 } }), 0))
    check(d:receive(AO, snapshot(A, 2, 2, 3, { { 3, 3 } }), 1))
    check(d:receive(AO, snapshot(A, 2, 0, 3, { { 1, 1 } }), 2))
    equal(#d:badges(3, 2), 0)
    check(badge(d, 99, 2, "eye"))
    equal(d:diagnostics(2).awareness.pending_chunks, 2)
    check(d:receive(AO, snapshot(A, 2, 1, 3, { { 2, 2 } }), 3))
    for id = 1, 3 do check(badge(d, id, 3, "eye")) end
    equal(#d:badges(99, 3), 0)
    equal(d:diagnostics(3).awareness.pending_chunks, 0)
end)

test("identical retransmissions are harmless and do not extend expiry", function()
    local d, bytes = Server.new(), snapshot(A, 7, 0, 1, { { 1, 1 } })
    check(d:receive(AO, bytes, 0))
    check(d:receive(AO, bytes, 4000))
    check(badge(d, 1, 5000, "eye"))
    equal(#d:badges(1, 5001), 0)
    equal(d:diagnostics(5001).awareness.entries, 0)
    local first = snapshot(A, 8, 0, 2, { { 1, 1 } })
    check(d:receive(AO, first, 6000))
    check(d:receive(AO, first, 10000))
    equal(d:diagnostics(11001).awareness.pending_chunks, 0)
end)

test("conflicting retransmissions and chunk counts discard staging", function()
    local d = Server.new()
    check(d:receive(AO, snapshot(A, 1, 0, 2, { { 1, 1 } }), 0))
    equal(d:receive(AO, snapshot(A, 1, 0, 2, { { 1, 2 } }), 1), false)
    equal(d:diagnostics(1).awareness.pending_chunks, 0)
    check(d:receive(AO, snapshot(A, 2, 0, 2, { { 1, 1 } }), 2))
    equal(d:receive(AO, snapshot(A, 2, 1, 3, { { 2, 1 } }), 3), false)
    equal(d:diagnostics(3).awareness.pending_chunks, 0)
    equal(#d:badges(1, 3), 0)
end)

test("cross-chunk duplicates never publish", function()
    local d = Server.new()
    check(d:receive(AO, snapshot(S, 1, 0, 2, { { 1, 1 } }), 0))
    equal(d:receive(AO, snapshot(S, 1, 1, 2, { { 1, 2 } }), 1), false)
    equal(#d:badges(1, 1), 0)
    equal(d:diagnostics(1).status.pending_chunks, 0)
end)

test("generation ordering rejects older published and staged generations", function()
    local d = Server.new()
    check(d:receive(AO, snapshot(A, 10, 0, 1, { { 1, 1 } }), 0))
    equal(d:receive(AO, snapshot(A, 9, 0, 1, { { 2, 1 } }), 1), false)
    check(d:receive(AO, snapshot(A, 12, 0, 2, { { 3, 1 } }), 2))
    equal(d:receive(AO, snapshot(A, 11, 0, 1, { { 4, 1 } }), 3), false)
    check(d:receive(AO, snapshot(A, 13, 0, 1, { { 5, 1 } }), 4))
    equal(d:receive(AO, snapshot(A, 12, 1, 2, { { 6, 1 } }), 5), false)
    check(badge(d, 5, 5, "eye"))
    equal(#d:badges(1, 5), 0)
end)

test("generation wraparound and ambiguous half-range ordering", function()
    local d = Server.new()
    check(d:receive(AO, snapshot(A, 4294967295, 0, 1, { { 1, 1 } }), 0))
    check(d:receive(AO, snapshot(A, 1, 0, 1, { { 2, 1 } }), 1))
    equal(d:receive(AO, snapshot(A, 4294967295, 0, 1, { { 3, 1 } }), 2), false)
    equal(d:receive(AO, snapshot(A, 2147483649, 0, 1, { { 3, 1 } }), 3), false)
    check(badge(d, 2, 3, "eye"))
end)

test("maximum snapshot is bounded and excess aggregate entries reject", function()
    local d = Server.new()
    for chunk = 0, 8 do
        local entries = {}
        local count = chunk == 8 and 48 or 122
        for offset = 1, count do entries[#entries + 1] = { chunk * 122 + offset, 1 } end
        check(d:receive(AO, snapshot(S, 1, chunk, 9, entries), chunk))
    end
    equal(d:diagnostics(8).status.entries, 1024)
    check(badge(d, 1024, 8, "sleep"))
    for chunk = 0, 8 do
        local entries = {}
        for offset = 1, 122 do entries[#entries + 1] = { chunk * 122 + offset, 1 } end
        equal(d:receive(AO, snapshot(S, 2, chunk, 9, entries), chunk + 10), chunk ~= 8)
    end
    equal(d:diagnostics(18).status.entries, 1024)
    equal(d:diagnostics(18).status.pending_chunks, 0)
end)

test("each snapshot stream expires independently", function()
    local d = Server.new()
    check(d:receive(AO, snapshot(A, 1, 0, 1, { { 1, 1 } }), 0))
    check(d:receive(AO, snapshot(S, 1, 0, 1, { { 1, 3 } }), 1000))
    equal(#d:badges(1, 5001), 2)
    equal(#d:badges(1, 6001), 0)
end)

test("all custom symbols map to supported renderer tokens", function()
    local d = Server.new()
    local tokens = { "star", "sun", "moon", "diamond", "triangle", "cross", "skull", "sword" }
    for icon = 1, 8 do
        check(d:receive(MO, mark(1, icon, 1, icon), 0))
        local items = d:badges(icon, 0)
        equal(#items, 1)
        equal(items[1].icon, tokens[icon])
    end
    equal(d:diagnostics(0).group_marks, 8)
end)

test("custom marks remain scope isolated without a guessed TTL", function()
    local d = Server.new()
    check(d:receive(MO, mark(1, 1, 1, 1), 0))
    check(d:receive(MO, mark(1, 2, 2, 2), 0))
    equal(#d:badges(1, 1000000, true), 0)
    check(badge(d, 1, 1000000, "star"))
    equal(#d:badges(2, 1000000), 0)
    check(badge(d, 2, 1000000, "sun", true))
end)

test("custom mark uniqueness and every clear action", function()
    local d = Server.new()
    check(d:receive(MO, mark(1, 1, 1, 1), 0))
    check(d:receive(MO, mark(1, 2, 1, 1), 0))
    equal(#d:badges(1, 0), 1)
    check(badge(d, 1, 0, "sun"))
    check(d:receive(MO, mark(1, 2, 1, 2), 0))
    equal(#d:badges(1, 0), 0)
    check(d:receive(MO, mark(2, 2, 1, 0), 0))
    equal(#d:badges(2, 0), 0)
    check(d:receive(MO, mark(1, 3, 1, 3), 0))
    check(d:receive(MO, mark(5, 0, 1, 3), 0))
    equal(#d:badges(3, 0), 0)
    check(d:receive(MO, mark(1, 3, 1, 3), 0))
    check(d:receive(MO, mark(1, 4, 2, 4), 0))
    check(d:receive(MO, mark(3, 0, 1, 0), 0))
    equal(#d:badges(3, 0), 0)
    check(badge(d, 4, 0, "diamond", true))
    check(d:receive(MO, mark(4, 0, 2, 0), 0))
    equal(#d:badges(4, 0, true), 0)
end)

test("all malformed custom marker shapes are ignored", function()
    local cases = {
        mark(1, 1, 1, 1, { magic = 0 }), mark(1, 1, 1, 1, { version = 2 }),
        mark(1, 1, 1, 1, { reserved = 1 }), mark(1, 1, 0, 1), mark(1, 1, 3, 1),
        mark(0, 1, 1, 1), mark(6, 1, 1, 1), mark(1, 0, 1, 1), mark(1, 9, 1, 1),
        mark(1, 1, 1, 0), mark(2, 0, 1, 0), mark(2, 9, 1, 0), mark(2, 1, 1, 1),
        mark(3, 1, 1, 0), mark(3, 0, 1, 1), mark(4, 1, 1, 0), mark(4, 0, 1, 1),
        mark(5, 1, 1, 1), mark(5, 0, 1, 0), mark(1, 1, 1, 1) .. "x",
    }
    for _, bytes in ipairs(cases) do
        local d = Server.new()
        equal(d:receive(MO, bytes, 0), false)
        equal(d:diagnostics(0).group_marks, 0)
    end
    local bytes = mark(1, 1, 1, 1)
    for length = 0, 15 do equal(Server.new():receive(MO, bytes:sub(1, length), 0), false) end
end)

test("remove clears scopes and snapshots, retiring pending entity state", function()
    local d = Server.new()
    check(d:receive(AO, snapshot(A, 1, 0, 1, { { 1, 1 } }), 0))
    check(d:receive(AO, snapshot(S, 1, 0, 1, { { 1, 3 } }), 0))
    check(d:receive(MO, mark(1, 1, 1, 1), 0))
    check(d:receive(MO, mark(1, 2, 2, 1), 0))
    check(d:receive(AO, snapshot(A, 2, 0, 2, { { 1, 1 } }), 0))
    d:remove(1)
    equal(#d:badges(1, 0), 0)
    equal(#d:badges(1, 0, true), 0)
    equal(d:receive(AO, snapshot(A, 2, 1, 2, { { 2, 1 } }), 1), false)
    check(d:receive(AO, snapshot(A, 3, 0, 1, { { 2, 1 } }), 2))
    check(badge(d, 2, 2, "eye"))
end)

test("zone reset clears all state and permits a fresh generation", function()
    local d = Server.new()
    check(d:receive(AO, snapshot(A, 500, 0, 1, { { 1, 1 } }), 0))
    check(d:receive(AO, snapshot(S, 500, 0, 2, { { 1, 3 } }), 0))
    check(d:receive(MO, mark(1, 1, 1, 1), 0))
    d:reset()
    equal(#d:badges(1, 0), 0)
    equal(d:diagnostics(0).status.pending_chunks, 0)
    check(d:receive(AO, snapshot(A, 1, 0, 1, { { 1, 2 } }), 0))
    equal(badge(d, 1, 0, "eye").label, "Engaged")
end)

test("invalid time and backwards clocks fail closed", function()
    for _, now in ipairs({ -1, math.huge, -math.huge, 0 / 0, "0", false }) do
        local d = Server.new()
        equal(d:receive(AO, snapshot(A, 1, 0, 1, { { 1, 1 } }), now), false)
        equal(#d:badges(1, now), 0)
        equal(d:diagnostics(now), nil)
    end
    local d = Server.new()
    check(d:receive(MO, mark(1, 1, 1, 1), 1000))
    equal(#d:badges(1, 999), 0)
end)

test("returned badges cannot mutate decoder state", function()
    local d = Server.new()
    check(d:receive(AO, snapshot(A, 1, 0, 1, { { 1, 1 } }), 0))
    local items = d:badges(1, 0)
    items[1].icon, items[1].label = "skull", "changed"
    equal(badge(d, 1, 0, "eye").label, "Detects you")
    for _, id in ipairs({ 0, -1, 1.5, 4294967296, "1", false }) do
        equal(#d:badges(id, 0), 0)
        d:remove(id)
    end
    check(badge(d, 1, 0, "eye"))
end)

test("deterministic malformed traffic never raises or allocates state", function()
    local d = Server.new()
    for length = 0, 550 do
        local bytes = string.rep(string.char((length * 37) % 256), length)
        local ok1, accepted1 = pcall(d.receive, d, AO, bytes, length)
        local ok2, accepted2 = pcall(d.receive, d, MO, bytes, length)
        check(ok1 and ok2)
        equal(accepted1, false)
        equal(accepted2, false)
    end
    equal(d:diagnostics(551).awareness.entries, 0)
    equal(d:diagnostics(551).status.entries, 0)
    equal(d:diagnostics(551).group_marks, 0)
    equal(d:diagnostics(551).raid_marks, 0)
end)

print(string.format("Server protocol: %d tests, %d assertions passed", tests, assertions))
