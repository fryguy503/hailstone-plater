-- SPDX-License-Identifier: MIT
local modes = {"Parentheses + tags", "Parentheses only", "Tags", "Name suffix", "Off"}
local was_open = false

ui.window("Crest settings", function()
    local crest = hs.Plugin("crest")
    local settings = crest and crest.settings()
    if not settings or not settings.open then
        was_open = false
        return
    end

    if not was_open then ui.SetNextWindowFocus() end
    was_open = true
    local visible, open = ui.Begin("Crest###CrestSettings", true,
        ui.WindowFlags.NoCollapse | ui.WindowFlags.AlwaysAutoResize)
    if not open then
        crest.set_setting("open", false)
        was_open = false
    elseif visible then
        if not settings.available then
            ui.Text("Crest needs a compatible native nameplate canvas.")
        else
            local enabled, changed = ui.Checkbox("Enable Crest nameplates", settings.enabled)
            if changed then crest.set_setting("enabled", enabled) end

            local selected = (settings.mode or 0) + 1
            ui.SetNextItemWidth(184)
            if ui.BeginCombo("Concealment display", modes[selected] or modes[1]) then
                for i, label in ipairs(modes) do
                    if ui.Selectable(label, i == selected) then crest.set_setting("mode", i - 1) end
                end
                ui.EndCombo()
            end
            ui.TextDisabled("Changes apply to this session.")
        end
    end
    ui.End()
end)
