-- SPDX-License-Identifier: MIT
local Config = dofile("crest/config.lua")
local Panel = dofile("crest/panel.lua")
local Personal = dofile("crest/personal.lua")

-- The shared widgets take MQ's Begin return order. Hailstone's pack UI reports visibility first.
local panel_ui = setmetatable({
    Begin = function(...)
        local visible, open = ui.Begin(...)
        return open, visible
    end,
    SetNextWindowSize = function(width, height, condition)
        local viewport = ui.GetIO()
        ui.SetNextWindowSize(math.min(width, math.max(1, viewport.width - 24)),
            math.min(height, math.max(0, viewport.height - 24)), condition)
    end,
}, {__index = ui})

local crest, state, revision
local settings, saved_settings
local profiles, saved_profiles = {}, {}
local settings_changed = false
local notice = ""

local function mark_changed()
    settings_changed = true
end

local function refresh_settings()
    if settings and revision == state.revision then return end
    revision = state.revision
    local decoded, error_message = Config.import(crest.get_config())
    settings = decoded or Config.repair()
    saved_settings = Config.export(settings)
    if not decoded then notice = error_message or "Crest settings could not be read." end

    profiles, saved_profiles = {}, {}
    for key, text in pairs(crest.get_profiles() or {}) do
        if type(key) == "string" and key:sub(1, 1) == ":" then
            local profile = Config.import(text)
            if profile then
                local name = key:sub(2)
                profiles[name] = profile
                saved_profiles[name] = profile
            end
        end
    end
end

local function apply_settings()
    if not settings_changed then return true end
    settings_changed = false
    local text = Config.export(settings)
    if text == saved_settings then return true end
    if not crest.replace_settings(text) then
        settings = Config.import(saved_settings)
        notice = "Crest could not apply these settings."
        return false
    end
    saved_settings = text
    notice = ""
    return true
end

local function save_profile(name)
    name = Config.text(name, 32):match("^%s*(.-)%s*$")
    if name == "" then return "Enter a profile name." end
    local count = 0
    for _ in pairs(profiles) do count = count + 1 end
    if not profiles[name] and count >= Config.MAX_PROFILES then return "Profile limit reached." end
    if not state.has_character then return "Enter a zone to save a named profile." end

    local profile = Config.repair(settings)
    if not crest.save_profile(name, Config.export(profile)) then return "Crest could not save this profile." end
    profiles[name] = profile
    saved_profiles[name] = profile
    return "Saved " .. name .. "."
end

local function status_lines()
    local lines = {"Crest native nameplates",
        state.has_character and "Character settings loaded." or "Waiting for a character in a zone.",
        state.server_data and "Optional indicators are receiving supported data." or
            "Optional indicators are waiting for supported data."}
    if type(state.storage_error) == "string" and state.storage_error ~= "" then
        lines[#lines + 1] = state.storage_error
    end
    if notice ~= "" then lines[#lines + 1] = notice end
    return lines
end

local panel = Panel.new(panel_ui, {
    labels = {short_names = "Use short names for NPCs and corpses"},
    settings = function() return settings end,
    profiles = function() return profiles end,
    api_version = function() return state.available and 1 or 0 end,
    changed = mark_changed,
    replace = function(replacement)
        settings = Config.repair(replacement)
        settings_changed = true
    end,
    save = function()
        if not apply_settings() then return false end
        local saved = crest.save_settings()
        notice = saved and "Settings saved." or "Crest could not save these settings."
        return saved
    end,
    profile_save = save_profile,
    status = status_lines,
})

ui.window("Crest settings", function()
    crest = hs.Plugin("crest")
    state = crest and crest.settings()
    if not state then
        panel.open = false
        revision = nil
        return
    end
    refresh_settings()

    if state.open and not panel.open then ui.SetNextWindowFocus() end
    panel.open = state.open == true
    if panel.open then
        panel:draw()
        if not panel.open then crest.set_setting("open", false) end
    end

    if state.has_character and state.enabled and settings.personal_bar then
        local player = crest.personal()
        local spell = player and player.cast_name and player.cast_name ~= "" and {name = player.cast_name} or nil
        Personal.draw(panel_ui, settings, player, mark_changed, spell)
    end

    apply_settings()
    -- The shared panel removes profiles directly from its table. Mirror those removals in native storage.
    for name, profile in pairs(saved_profiles) do
        if not profiles[name] then
            if crest.delete_profile(name) then
                saved_profiles[name] = nil
            else
                profiles[name] = profile
                panel.message = "Crest could not delete this profile."
            end
        end
    end
end)
