--[[
    CRT Emulator
    VLC Lua Extension — live CRT simulation controls.
    Install: VLC > Extensions > CRT Emulator > Show Controller
--]]

function descriptor()
    return {
        title = "CRT Emulator",
        version = "4.0",
        author = "Jules Lazaro (original), community contributors",
        description = "Live CRT simulation: scanlines, phosphor, NTSC, overlays.",
        capabilities = {"menu"}
    }
end

function menu() return {"Show Controller"} end
function trigger_menu(id) if id == 1 then create_dialog() end end

-- State
local darkness = 35
local spacing = 2
local blend = true
local blackline = false
local phosphor = 0
local ntsc = 0
local noise = 0
local barrel = 0
local vignette = 0
local bezel = 0
local reflect = 0
local overlay_path = nil
local overlay_zoom = 65
local overlay_x = 0
local overlay_y = -12
local contrast = 100
local saturation = 100
local saved_state = nil

-- Widgets
local dlg, overlay_input, overlay_status, overlay_dropdown = nil, nil, nil, nil
local custom_dropdown, custom_name_input = nil, nil
local labels = {}

local ntsc_names = {[0]="Off", [1]="S-Video", [2]="Composite", [3]="RF"}

-- Descriptors
local function dk_d(v) if v==0 then return "Off" elseif v<=20 then return "Faint" elseif v<=40 then return "Med" elseif v<=60 then return "Strong" else return "Max" end end
local function ph_d(v) if v==0 then return "Off" elseif v<=30 then return "Subtle" elseif v<=60 then return "Warm" else return "Bright" end end
local function ba_d(v) if v==0 then return "Flat" elseif v<=30 then return "Slight" elseif v<=60 then return "CRT" else return "Fish" end end
local function vi_d(v) if v==0 then return "Off" elseif v<=30 then return "Light" elseif v<=60 then return "CRT" else return "Deep" end end
local function bz_d(v) if v==0 then return "Off" elseif v<=25 then return "Thin" elseif v<=50 then return "TV" else return "Thick" end end
local function rf_d(v) if v==0 then return "Off" elseif v<=30 then return "Faint" elseif v<=60 then return "Glass" else return "Bright" end end
local function ns_d(v) if v==0 then return "Off" elseif v<=25 then return "Film" elseif v<=50 then return "Light" else return "Heavy" end end
local function co_d(v) if v<80 then return "Washed" elseif v<100 then return "Soft" elseif v==100 then return "Normal" elseif v<=120 then return "Punchy" else return "Hard" end end
local function sa_d(v) if v==0 then return "B&W" elseif v<80 then return "Muted" elseif v==100 then return "Normal" elseif v<=150 then return "Vivid" else return "Over" end end
local function zm_d(v) if v<=40 then return "Small" elseif v<=60 then return "Medium" elseif v<=80 then return "Large" else return "Full" end end

local function fmt(val, desc_fn) return "<b>" .. tostring(val) .. "</b> " .. desc_fn(val) end
-- Cable type: name + description combined in one label
local ntsc_full = {
    [0] = "<b>Off</b> — clean, no artifacts",
    [1] = "<b>S-Video</b> — slight color smear",
    [2] = "<b>Composite</b> — rainbow + dot crawl",
    [3] = "<b>RF</b> — heavy color bleed",
}
local function fmt_ntsc() return ntsc_full[ntsc] end

-- === Custom Presets (stored in VLC config as serialized string) ===
local CUSTOM_KEY = "crtemulator-custom-presets"
local custom_presets = {}

local function serialize_presets()
    local parts = {}
    for _, p in ipairs(custom_presets) do
        local vals = {}
        for k, v in pairs(p.data) do
            table.insert(vals, k .. "=" .. tostring(v))
        end
        table.insert(parts, p.name .. ":" .. table.concat(vals, ","))
    end
    return table.concat(parts, ";")
end

local function deserialize_presets(str)
    custom_presets = {}
    if not str or str == "" then return end
    for entry in str:gmatch("[^;]+") do
        local name, vals_str = entry:match("^(.-):(.*)")
        if name and vals_str then
            local data = {}
            for kv in vals_str:gmatch("[^,]+") do
                local k, v = kv:match("^(.-)=(.+)")
                if k and v then
                    if v == "true" then data[k] = true
                    elseif v == "false" then data[k] = false
                    else data[k] = tonumber(v) or v end
                end
            end
            table.insert(custom_presets, {name=name, data=data})
        end
    end
end

local function save_presets_to_config()
    vlc.config.set(CUSTOM_KEY, serialize_presets())
end

local function load_presets_from_config()
    local str = vlc.config.get(CUSTOM_KEY)
    deserialize_presets(str)
end

local function fmt_custom_list()
    local names = {}
    for _, p in ipairs(custom_presets) do
        table.insert(names, p.name)
    end
    if #names == 0 then return "<i>No saved presets</i>" end
    return "<i>Saved: " .. table.concat(names, " | ") .. "</i>"
end

local function refresh_custom_label()
    if labels and labels.custom_list then
        labels.custom_list:set_text(fmt_custom_list())
    end
end

local function list_custom_presets()
    local names = {}
    for _, p in ipairs(custom_presets) do
        table.insert(names, p.name)
    end
    return names
end

-- Lifecycle
function activate()
    darkness = vlc.config.get("crtemulator-darkness") or 35
    spacing = vlc.config.get("crtemulator-spacing") or 2
    phosphor = vlc.config.get("crtemulator-phosphor") or 0
    ntsc = vlc.config.get("crtemulator-ntsc") or 0
    noise = vlc.config.get("crtemulator-noise") or 0
    barrel = vlc.config.get("crtemulator-barrel") or 0
    bezel = vlc.config.get("crtemulator-bezel") or 0
    reflect = vlc.config.get("crtemulator-reflect") or 0
    overlay_path = vlc.config.get("crtemulator-overlay") or nil
    if overlay_path == "" then overlay_path = nil end
    overlay_zoom = vlc.config.get("crtemulator-overlay-zoom") or 65
    overlay_x = vlc.config.get("crtemulator-overlay-x") or 0
    overlay_y = vlc.config.get("crtemulator-overlay-y") or 0
    vignette = vlc.config.get("crtemulator-vignette") or 0
    contrast = vlc.config.get("crtemulator-contrast") or 100
    saturation = vlc.config.get("crtemulator-saturation") or 100
    local v = vlc.config.get("crtemulator-blend")
    if v == nil then blend = true else blend = v end
    v = vlc.config.get("crtemulator-blackline")
    if v == nil then blackline = false else blackline = v end
    load_presets_from_config()
    create_dialog()
end

function deactivate()
    darkness=0; phosphor=0; ntsc=0; noise=0; barrel=0; vignette=0
    bezel=0; reflect=0; contrast=100; saturation=100
    vlc.config.set("crtemulator-darkness", 0)
    vlc.config.set("crtemulator-phosphor", 0)
    vlc.config.set("crtemulator-ntsc", 0)
    vlc.config.set("crtemulator-bezel", 0)
    vlc.config.set("crtemulator-reflect", 0)
    vlc.config.set("crtemulator-barrel", 0)
    vlc.config.set("crtemulator-vignette", 0)
    vlc.config.set("crtemulator-noise", 0)
    vlc.config.set("crtemulator-contrast", 100)
    vlc.config.set("crtemulator-saturation", 100)
    if dlg then dlg:delete(); dlg = nil end
end

function close() if dlg then dlg:delete(); dlg = nil end end

-- Dialog layout: clean 4-column grid, one control per row.
-- Pattern: [Label] [-] [+] [Value]
-- This ensures consistent alignment across all controls.
function create_dialog()
    if dlg then dlg:delete() end
    dlg = vlc.dialog("CRT Emulator")
    labels = {}

    local r = 1

    -- POWER
    dlg:add_button("  ON  ", click_enable, 1, r, 1, 1)
    dlg:add_button("  OFF  ", click_disable, 2, r, 1, 1)
    labels.preset = dlg:add_label("", 3, r, 2, 1)
    r = r + 1

    -- === SCANLINES ===
    dlg:add_label("<b>--- SCANLINES ---</b>", 1, r, 4, 1); r = r + 1
    dlg:add_label("<i>Dark horizontal lines across the screen, like a real CRT</i>", 1, r, 4, 1); r = r + 1

    dlg:add_label("Darkness:", 1, r, 1, 1)
    dlg:add_button(" - ", function() adj("darkness", -5, 0, 100, dk_d) end, 2, r, 1, 1)
    dlg:add_button(" + ", function() adj("darkness", 5, 0, 100, dk_d) end, 3, r, 1, 1)
    labels.darkness = dlg:add_label(fmt(darkness, dk_d), 4, r, 1, 1)
    r = r + 1

    dlg:add_label("Line Gap:", 1, r, 1, 1)
    dlg:add_button(" - ", function() adj("spacing", -1, 1, 20) end, 2, r, 1, 1)
    dlg:add_button(" + ", function() adj("spacing", 1, 1, 20) end, 3, r, 1, 1)
    labels.spacing = dlg:add_label("<b>" .. spacing .. "px</b>", 4, r, 1, 1)
    r = r + 1

    dlg:add_label("Line Shape:", 1, r, 1, 1)
    dlg:add_button("Smooth", click_blend_on, 2, r, 1, 1)
    dlg:add_button("Hard", click_blend_off, 3, r, 1, 1)
    dlg:add_button("Black", click_blackline, 4, r, 1, 1)
    r = r + 1

    -- === CRT SCREEN ===
    dlg:add_label("<b>--- CRT SCREEN ---</b>", 1, r, 4, 1); r = r + 1
    dlg:add_label("<i>Phosphor glow, curvature, shadow, and glass reflection</i>", 1, r, 4, 1); r = r + 1

    dlg:add_label("Phosphor Glow:", 1, r, 1, 1)
    dlg:add_button(" - ", function() adj("phosphor", -10, 0, 100, ph_d) end, 2, r, 1, 1)
    dlg:add_button(" + ", function() adj("phosphor", 10, 0, 100, ph_d) end, 3, r, 1, 1)
    labels.phosphor = dlg:add_label(fmt(phosphor, ph_d), 4, r, 1, 1)
    r = r + 1

    dlg:add_label("Screen Curve:", 1, r, 1, 1)
    dlg:add_button(" - ", function() adj("barrel", -10, 0, 100, ba_d) end, 2, r, 1, 1)
    dlg:add_button(" + ", function() adj("barrel", 10, 0, 100, ba_d) end, 3, r, 1, 1)
    labels.barrel = dlg:add_label(fmt(barrel, ba_d), 4, r, 1, 1)
    r = r + 1

    dlg:add_label("Edge Shadow:", 1, r, 1, 1)
    dlg:add_button(" - ", function() adj("vignette", -10, 0, 100, vi_d) end, 2, r, 1, 1)
    dlg:add_button(" + ", function() adj("vignette", 10, 0, 100, vi_d) end, 3, r, 1, 1)
    labels.vignette = dlg:add_label(fmt(vignette, vi_d), 4, r, 1, 1)
    r = r + 1

    dlg:add_label("Glass Shine:", 1, r, 1, 1)
    dlg:add_button(" - ", function() adj("reflect", -10, 0, 100, rf_d) end, 2, r, 1, 1)
    dlg:add_button(" + ", function() adj("reflect", 10, 0, 100, rf_d) end, 3, r, 1, 1)
    labels.reflect = dlg:add_label(fmt(reflect, rf_d), 4, r, 1, 1)
    r = r + 1

    dlg:add_label("Contrast:", 1, r, 1, 1)
    dlg:add_button(" - ", function() adj("contrast", -10, 50, 150, co_d) end, 2, r, 1, 1)
    dlg:add_button(" + ", function() adj("contrast", 10, 50, 150, co_d) end, 3, r, 1, 1)
    labels.contrast = dlg:add_label(fmt(contrast, co_d), 4, r, 1, 1)
    r = r + 1

    dlg:add_label("Color Intensity:", 1, r, 1, 1)
    dlg:add_button(" - ", function() adj("saturation", -10, 0, 200, sa_d) end, 2, r, 1, 1)
    dlg:add_button(" + ", function() adj("saturation", 10, 0, 200, sa_d) end, 3, r, 1, 1)
    labels.saturation = dlg:add_label(fmt(saturation, sa_d), 4, r, 1, 1)
    r = r + 1

    dlg:add_label("Bezel Frame:", 1, r, 1, 1)
    dlg:add_button(" - ", function() adj("bezel", -10, 0, 100, bz_d) end, 2, r, 1, 1)
    dlg:add_button(" + ", function() adj("bezel", 10, 0, 100, bz_d) end, 3, r, 1, 1)
    labels.bezel = dlg:add_label(fmt(bezel, bz_d), 4, r, 1, 1)
    r = r + 1

    dlg:add_label("Static Noise:", 1, r, 1, 1)
    dlg:add_button(" - ", function() adj("noise", -10, 0, 100, ns_d) end, 2, r, 1, 1)
    dlg:add_button(" + ", function() adj("noise", 10, 0, 100, ns_d) end, 3, r, 1, 1)
    labels.noise = dlg:add_label(fmt(noise, ns_d), 4, r, 1, 1)
    r = r + 1

    -- === VIDEO SIGNAL ===
    dlg:add_label("<b>--- VIDEO SIGNAL ---</b>", 1, r, 4, 1); r = r + 1
    dlg:add_label("<i>Color bleed — colors smear sideways like a bad cable</i>", 1, r, 4, 1); r = r + 1

    dlg:add_label("Cable Type:", 1, r, 1, 1)
    dlg:add_button(" << ", function() adj("ntsc", -1, 0, 3) end, 2, r, 1, 1)
    dlg:add_button(" >> ", function() adj("ntsc", 1, 0, 3) end, 3, r, 1, 1)
    labels.ntsc = dlg:add_label(fmt_ntsc(), 4, r, 1, 1)
    r = r + 1

    -- === TV OVERLAY ===
    dlg:add_label("<b>--- TV OVERLAY ---</b>", 1, r, 4, 1); r = r + 1
    dlg:add_label("<i>Load a TV image — video shows inside the screen</i>", 1, r, 4, 1); r = r + 1

    overlay_dropdown = dlg:add_dropdown(1, r, 2, 1)
    overlay_dropdown:add_value("-- Select TV --", 0)
    overlay_dropdown:add_value("Generic Gray TV", 1)
    overlay_dropdown:add_value("Generic White TV", 2)
    overlay_dropdown:add_value("Generic CRT Monitor", 3)
    overlay_dropdown:add_value("Generic Flat TV", 4)
    overlay_dropdown:add_value("Nintendo NES", 5)
    overlay_dropdown:add_value("Nintendo SNES", 6)
    overlay_dropdown:add_value("Nintendo N64", 7)
    overlay_dropdown:add_value("Nintendo Famicom", 8)
    overlay_dropdown:add_value("Sega Genesis", 9)
    overlay_dropdown:add_value("Sega Master System", 10)
    overlay_dropdown:add_value("Sega Dreamcast", 11)
    overlay_dropdown:add_value("SNK Neo Geo", 12)
    overlay_dropdown:add_value("Sony PlayStation", 13)
    overlay_dropdown:add_value("Sony PS2", 14)
    dlg:add_button("Apply", click_apply_overlay, 3, r, 1, 1)
    dlg:add_button("Clear", click_clear_overlay, 4, r, 1, 1)
    r = r + 1

    dlg:add_label("Zoom:", 1, r, 1, 1)
    dlg:add_button(" - ", function() adj("overlay_zoom", -5, 20, 100, zm_d) end, 2, r, 1, 1)
    dlg:add_button(" + ", function() adj("overlay_zoom", 5, 20, 100, zm_d) end, 3, r, 1, 1)
    labels.overlay_zoom = dlg:add_label(fmt(overlay_zoom, zm_d), 4, r, 1, 1)
    r = r + 1

    dlg:add_label("X Position:", 1, r, 1, 1)
    dlg:add_button(" < ", function() adj("overlay_x", -3, -50, 50) end, 2, r, 1, 1)
    dlg:add_button(" > ", function() adj("overlay_x", 3, -50, 50) end, 3, r, 1, 1)
    labels.overlay_x = dlg:add_label("<b>" .. overlay_x .. "</b>", 4, r, 1, 1)
    r = r + 1

    dlg:add_label("Y Position:", 1, r, 1, 1)
    dlg:add_button(" Up ", function() adj("overlay_y", -3, -50, 50) end, 2, r, 1, 1)
    dlg:add_button(" Down ", function() adj("overlay_y", 3, -50, 50) end, 3, r, 1, 1)
    labels.overlay_y = dlg:add_label("<b>" .. overlay_y .. "</b>", 4, r, 1, 1)
    r = r + 1

    overlay_input = dlg:add_text_input(overlay_path or "", 1, r, 3, 1)
    dlg:add_button("Load", click_load_overlay, 4, r, 1, 1)
    r = r + 1

    overlay_status = dlg:add_label(overlay_path and ("<i>" .. (overlay_path:match("[^/\\]+$") or "") .. "</i>") or "", 1, r, 4, 1)
    r = r + 1

    -- === PRESETS ===
    dlg:add_label("<b>--- PRESETS ---</b>", 1, r, 4, 1); r = r + 1

    dlg:add_button("Subtle", click_preset_subtle, 1, r, 1, 1)
    dlg:add_button("Classic", click_preset_classic, 2, r, 1, 1)
    dlg:add_button("Heavy", click_preset_heavy, 3, r, 1, 1)
    dlg:add_button("Anime", click_preset_anime, 4, r, 1, 1)
    r = r + 1

    dlg:add_button("VHS", click_preset_vhs, 1, r, 1, 1)
    dlg:add_button("Arcade", click_preset_arcade, 2, r, 1, 1)
    dlg:add_button("RESET", click_reset_all, 3, r, 2, 1)
    r = r + 1

    -- === CUSTOM PRESETS ===
    dlg:add_label("<b>--- CUSTOM PRESETS ---</b>", 1, r, 4, 1); r = r + 1

    custom_dropdown = dlg:add_dropdown(1, r, 2, 1)
    custom_dropdown:add_value("-- Select --", 0)
    local cnames = list_custom_presets()
    for i, cname in ipairs(cnames) do
        custom_dropdown:add_value(cname, i)
    end
    dlg:add_button("Load", click_load_custom, 3, r, 1, 1)
    dlg:add_button("Delete", click_delete_custom, 4, r, 1, 1)
    r = r + 1

    custom_name_input = dlg:add_text_input("", 1, r, 2, 1)
    dlg:add_button("Save Current", click_save_custom, 3, r, 2, 1)
end

-- Universal adjust function
function adj(param, delta, min_v, max_v, desc_fn)
    local vals = {darkness=darkness, spacing=spacing, phosphor=phosphor,
        ntsc=ntsc, noise=noise, barrel=barrel, vignette=vignette,
        bezel=bezel, reflect=reflect, contrast=contrast,
        saturation=saturation, overlay_zoom=overlay_zoom,
        overlay_x=overlay_x, overlay_y=overlay_y}
    local v = math.max(min_v, math.min(max_v, vals[param] + delta))

    -- Update state
    if param == "darkness" then darkness = v
    elseif param == "spacing" then spacing = v
    elseif param == "phosphor" then phosphor = v
    elseif param == "ntsc" then ntsc = v
    elseif param == "noise" then noise = v
    elseif param == "barrel" then barrel = v
    elseif param == "vignette" then vignette = v
    elseif param == "bezel" then bezel = v
    elseif param == "reflect" then reflect = v
    elseif param == "contrast" then contrast = v
    elseif param == "saturation" then saturation = v
    elseif param == "overlay_zoom" then overlay_zoom = v
    elseif param == "overlay_x" then overlay_x = v
    elseif param == "overlay_y" then overlay_y = v
    end

    -- Write to VLC config
    local cfg_name = param:gsub("_", "-")
    vlc.config.set("crtemulator-" .. cfg_name, v)

    -- Update label
    if labels[param] then
        if param == "ntsc" then
            labels[param]:set_text(fmt_ntsc())
        elseif param == "spacing" then
            labels[param]:set_text("<b>" .. v .. "px</b>")
        elseif desc_fn then
            labels[param]:set_text(fmt(v, desc_fn))
        else
            labels[param]:set_text("<b>" .. v .. "</b>")
        end
    end
    if labels.preset then labels.preset:set_text("") end
end

-- Overlay
local function find_ov_dir()
    local paths = {
        "/Applications/VLC.app/Contents/MacOS/share/crt-overlays/",
        (os.getenv("HOME") or "") .. "/Documents/GitHub/VLC-CRT-Filter-Effect/overlays/",
    }
    for _, p in ipairs(paths) do
        local f = io.open(p .. "Generic_Gray_TV.png", "r")
        if f then f:close(); return p end
    end
    return paths[1]
end

-- Per-overlay screen position presets (zoom, x, y)
-- These are tuned for each Soqueroeu TV model
local ov_presets = {
    ["Generic_Gray_TV.png"]     = { zoom=63, x=0,  y=-12 },
    ["Generic_CRT_Monitor.png"] = { zoom=60, x=0,  y=-10 },
    ["Generic_Flat_TV.png"]     = { zoom=62, x=0,  y=-8  },
    ["Generic_White_TV.png"]    = { zoom=63, x=0,  y=-12 },
    ["G02_Generic_TV.png"]      = { zoom=62, x=0,  y=-10 },
    ["Nintendo_NES.png"]        = { zoom=60, x=0,  y=-10 },
    ["Nintendo_SNES.png"]       = { zoom=60, x=0,  y=-10 },
    ["Nintendo_N64.png"]        = { zoom=62, x=0,  y=-12 },
    ["Nintendo_Famicom.png"]    = { zoom=60, x=0,  y=-10 },
    ["Sega_Genesis.png"]        = { zoom=60, x=0,  y=-8  },
    ["Sega_MasterSystem.png"]   = { zoom=60, x=0,  y=-10 },
    ["Sega_Dreamcast.png"]      = { zoom=62, x=0,  y=-12 },
    ["SNK_NeoGeo.png"]          = { zoom=60, x=0,  y=-8  },
    ["Sony_PlayStation.png"]    = { zoom=62, x=0,  y=-12 },
    ["Sony_PS2.png"]            = { zoom=62, x=0,  y=-12 },
}

function load_ov(filename)
    local path = find_ov_dir() .. filename
    overlay_path = path
    vlc.config.set("crtemulator-overlay", path)
    if overlay_input then overlay_input:set_text(path) end
    if overlay_status then overlay_status:set_text("<i>" .. filename .. "</i>") end

    -- Apply per-overlay position preset + curvature
    local p = ov_presets[filename]
    if p then
        overlay_zoom = p.zoom; overlay_x = p.x; overlay_y = p.y
        vlc.config.set("crtemulator-overlay-zoom", p.zoom)
        vlc.config.set("crtemulator-overlay-x", p.x)
        vlc.config.set("crtemulator-overlay-y", p.y)
        if labels.overlay_zoom then labels.overlay_zoom:set_text(fmt(p.zoom, zm_d)) end
        if labels.overlay_x then labels.overlay_x:set_text("<b>" .. p.x .. "</b>") end
        if labels.overlay_y then labels.overlay_y:set_text("<b>" .. p.y .. "</b>") end
    end

    -- Set barrel curvature for all overlays (CRT TVs have curved screens)
    barrel = 55
    vlc.config.set("crtemulator-barrel", 55)
    if labels.barrel then labels.barrel:set_text(fmt(55, ba_d)) end
end

-- Dropdown ID → filename mapping
local ov_files = {
    [1]  = "Generic_Gray_TV.png",
    [2]  = "Generic_White_TV.png",
    [3]  = "Generic_CRT_Monitor.png",
    [4]  = "Generic_Flat_TV.png",
    [5]  = "Nintendo_NES.png",
    [6]  = "Nintendo_SNES.png",
    [7]  = "Nintendo_N64.png",
    [8]  = "Nintendo_Famicom.png",
    [9]  = "Sega_Genesis.png",
    [10] = "Sega_MasterSystem.png",
    [11] = "Sega_Dreamcast.png",
    [12] = "SNK_NeoGeo.png",
    [13] = "Sony_PlayStation.png",
    [14] = "Sony_PS2.png",
}

function click_apply_overlay()
    if overlay_dropdown then
        local id = overlay_dropdown:get_value()
        if id and ov_files[id] then
            load_ov(ov_files[id])
        end
    end
end

function click_load_overlay()
    if overlay_input then
        local path = overlay_input:get_text()
        if path and path ~= "" then
            overlay_path = path
            vlc.config.set("crtemulator-overlay", path)
            if overlay_status then
                overlay_status:set_text("<i>" .. (path:match("[^/\\]+$") or path) .. "</i>")
            end
        end
    end
end

function click_clear_overlay()
    overlay_path = nil
    vlc.config.set("crtemulator-overlay", "")
    if overlay_status then overlay_status:set_text("") end
    if overlay_input then overlay_input:set_text("") end
end

-- Power
function click_enable()
    if saved_state then
        darkness = saved_state.darkness or 35
        phosphor = saved_state.phosphor or 0
        ntsc = saved_state.ntsc or 0
        noise = saved_state.noise or 0
        barrel = saved_state.barrel or 0
        vignette = saved_state.vignette or 0
        bezel = saved_state.bezel or 0
        reflect = saved_state.reflect or 0
        contrast = saved_state.contrast or 100
        saturation = saved_state.saturation or 100
    else
        darkness = 35
    end
    apply_all()
end

function click_disable()
    saved_state = {darkness=darkness, phosphor=phosphor, ntsc=ntsc,
        noise=noise, barrel=barrel, vignette=vignette, bezel=bezel,
        reflect=reflect, contrast=contrast, saturation=saturation}
    darkness=0; phosphor=0; ntsc=0; noise=0; barrel=0; vignette=0
    bezel=0; reflect=0; contrast=100; saturation=100
    apply_all()
end

function apply_all()
    local params = {"darkness","spacing","phosphor","ntsc","noise","barrel",
        "vignette","bezel","reflect","contrast","saturation"}
    local vals = {darkness,spacing,phosphor,ntsc,noise,barrel,
        vignette,bezel,reflect,contrast,saturation}
    for i, p in ipairs(params) do
        vlc.config.set("crtemulator-" .. p, vals[i])
    end
    vlc.config.set("crtemulator-blend", blend)
    vlc.config.set("crtemulator-blackline", blackline)
end

-- Scanline style
function click_blend_on() blend=true; blackline=false; vlc.config.set("crtemulator-blend",true); vlc.config.set("crtemulator-blackline",false) end
function click_blend_off() blend=false; blackline=false; vlc.config.set("crtemulator-blend",false); vlc.config.set("crtemulator-blackline",false) end
function click_blackline() blackline=true; vlc.config.set("crtemulator-blackline",true) end

-- Reset all to defaults
function click_reset_all()
    darkness=35; spacing=2; blend=true; blackline=false
    phosphor=0; ntsc=0; noise=0; barrel=0; vignette=0
    bezel=0; reflect=0; contrast=100; saturation=100
    overlay_zoom=65; overlay_x=0; overlay_y=-12
    apply_all()
    vlc.config.set("crtemulator-overlay-zoom", 65)
    vlc.config.set("crtemulator-overlay-x", 0)
    vlc.config.set("crtemulator-overlay-y", -12)

    if labels.darkness then labels.darkness:set_text(fmt(35, dk_d)) end
    if labels.spacing then labels.spacing:set_text("<b>2px</b>") end
    if labels.phosphor then labels.phosphor:set_text(fmt(0, ph_d)) end
    if labels.barrel then labels.barrel:set_text(fmt(0, ba_d)) end
    if labels.vignette then labels.vignette:set_text(fmt(0, vi_d)) end
    if labels.reflect then labels.reflect:set_text(fmt(0, rf_d)) end
    if labels.contrast then labels.contrast:set_text(fmt(100, co_d)) end
    if labels.saturation then labels.saturation:set_text(fmt(100, sa_d)) end
    if labels.bezel then labels.bezel:set_text(fmt(0, bz_d)) end
    if labels.noise then labels.noise:set_text(fmt(0, ns_d)) end
    if labels.ntsc then labels.ntsc:set_text(fmt_ntsc()) end
    if labels.overlay_zoom then labels.overlay_zoom:set_text(fmt(65, zm_d)) end
    if labels.overlay_x then labels.overlay_x:set_text("<b>0</b>") end
    if labels.overlay_y then labels.overlay_y:set_text("<b>-12</b>") end
    if labels.preset then labels.preset:set_text("<i>Reset</i>") end
end

-- Presets
local function preset(d,s,bl,bk,ph,nt,ns,ba,vi,bz,rf,co,sa,name)
    darkness=d; spacing=s; blend=bl; blackline=bk
    phosphor=ph; ntsc=nt; noise=ns; barrel=ba; vignette=vi
    bezel=bz; reflect=rf; contrast=co; saturation=sa
    apply_all()

    -- Update ALL GUI labels to reflect the new values
    if labels.darkness then labels.darkness:set_text(fmt(d, dk_d)) end
    if labels.spacing then labels.spacing:set_text("<b>" .. s .. "px</b>") end
    if labels.phosphor then labels.phosphor:set_text(fmt(ph, ph_d)) end
    if labels.barrel then labels.barrel:set_text(fmt(ba, ba_d)) end
    if labels.vignette then labels.vignette:set_text(fmt(vi, vi_d)) end
    if labels.reflect then labels.reflect:set_text(fmt(rf, rf_d)) end
    if labels.contrast then labels.contrast:set_text(fmt(co, co_d)) end
    if labels.saturation then labels.saturation:set_text(fmt(sa, sa_d)) end
    if labels.bezel then labels.bezel:set_text(fmt(bz, bz_d)) end
    if labels.noise then labels.noise:set_text(fmt(ns, ns_d)) end
    if labels.ntsc then labels.ntsc:set_text(fmt_ntsc()) end
    if labels.preset then labels.preset:set_text("<i>" .. name .. "</i>") end
end

function click_preset_subtle()  preset(15,2,true,false, 0,0,0, 0,0,0,0, 100,100, "Subtle") end
function click_preset_classic() preset(35,2,true,false, 10,0,0, 0,0,0,0, 105,100, "Classic") end
function click_preset_heavy()   preset(55,3,false,false, 20,0,0, 0,0,0,0, 110,95, "Heavy") end
function click_preset_anime()   preset(30,2,true,false, 45,1,0, 15,25,0,15, 105,110, "90s Anime") end
function click_preset_vhs()     preset(20,2,true,false, 35,3,30, 10,30,0,0, 95,85, "VHS Tape") end
function click_preset_arcade()  preset(65,2,false,true, 70,0,0, 30,50,50,35, 120,115, "Arcade") end


function click_save_custom()
    if not custom_name_input then return end
    local name = custom_name_input:get_text()
    if not name or name == "" then return end
    name = name:gsub("[;:,=]", ""):gsub("^%s+", ""):gsub("%s+$", "")
    if name == "" then return end

    local data = {
        darkness=darkness, spacing=spacing, blend=blend, blackline=blackline,
        phosphor=phosphor, ntsc=ntsc, noise=noise, barrel=barrel,
        vignette=vignette, bezel=bezel, reflect=reflect,
        contrast=contrast, saturation=saturation
    }

    -- Replace if exists, otherwise append
    local found = false
    for i, p in ipairs(custom_presets) do
        if p.name == name then
            custom_presets[i].data = data
            found = true
            break
        end
    end
    if not found then
        table.insert(custom_presets, {name=name, data=data})
    end

    save_presets_to_config()
    -- Recreate dialog to refresh dropdown with new preset
    create_dialog()
end

function click_load_custom()
    -- Get name from dropdown
    local name = nil
    if custom_dropdown then
        local id = custom_dropdown:get_value()
        if id and id > 0 then
            local names = list_custom_presets()
            name = names[id]
        end
    end
    if not name then return end

    local found = false
    for _, p in ipairs(custom_presets) do
        if p.name == name then
            local d = p.data
            darkness = d.darkness or 35
            spacing = d.spacing or 2
            blend = d.blend ~= false
            blackline = d.blackline or false
            phosphor = d.phosphor or 0
            ntsc = d.ntsc or 0
            noise = d.noise or 0
            barrel = d.barrel or 0
            vignette = d.vignette or 0
            bezel = d.bezel or 0
            reflect = d.reflect or 0
            contrast = d.contrast or 100
            saturation = d.saturation or 100
            found = true
            break
        end
    end

    if not found then
        if labels.preset then labels.preset:set_text("<i>Not found</i>") end
        return
    end

    apply_all()
    if labels.darkness then labels.darkness:set_text(fmt(darkness, dk_d)) end
    if labels.spacing then labels.spacing:set_text("<b>" .. spacing .. "px</b>") end
    if labels.phosphor then labels.phosphor:set_text(fmt(phosphor, ph_d)) end
    if labels.barrel then labels.barrel:set_text(fmt(barrel, ba_d)) end
    if labels.vignette then labels.vignette:set_text(fmt(vignette, vi_d)) end
    if labels.reflect then labels.reflect:set_text(fmt(reflect, rf_d)) end
    if labels.contrast then labels.contrast:set_text(fmt(contrast, co_d)) end
    if labels.saturation then labels.saturation:set_text(fmt(saturation, sa_d)) end
    if labels.bezel then labels.bezel:set_text(fmt(bezel, bz_d)) end
    if labels.noise then labels.noise:set_text(fmt(noise, ns_d)) end
    if labels.ntsc then labels.ntsc:set_text(fmt_ntsc()) end
    if labels.preset then labels.preset:set_text("<i>" .. name .. "</i>") end
end

function click_delete_custom()
    local name = nil
    if custom_dropdown then
        local id = custom_dropdown:get_value()
        if id and id > 0 then
            local names = list_custom_presets()
            name = names[id]
        end
    end
    if not name then return end

    for i, p in ipairs(custom_presets) do
        if p.name == name then
            table.remove(custom_presets, i)
            break
        end
    end

    save_presets_to_config()
    -- Recreate dialog to refresh dropdown
    create_dialog()
end
