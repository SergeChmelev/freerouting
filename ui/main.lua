-- ===========================================================================
-- ui/main.lua
-- Freerouting C++ front end: Nuklear (MoonNuklear) + OpenGL (MoonGL/MoonGLFW).
--
--   lua5.3 ui/main.lua [board.dsn]          (FRUI_SCALE=1.5 to scale the UI)
--
-- The routing runs in the C++ engine thread of the frcore module; this Lua
-- loop only queues commands, polls plain status/log data and uploads render
-- snapshots when their version changes. Layout and controls follow the Java
-- freerouting GUI (menu, toolbar, board canvas, status bar); the docking
-- panels, theme and file dialog are reused from the Demopaja timeline port.
-- ===========================================================================

GLOBAL_SCALE = GLOBAL_SCALE or tonumber(os.getenv('FRUI_SCALE') or '') or 1.5

local HERE = (debug.getinfo(1, 'S').source:match('^@(.*)/[^/]*$')) or '.'
package.path = HERE .. '/?.lua;' .. package.path
package.cpath = HERE .. '/?.so;' .. package.cpath

local gl = require('moongl')
local glfw = require('moonglfw')
local backend = require('moonnuklear.glbackend')
local nk = require('moonnuklear')
local fr = require('frcore')

local Theme = require('lib.theme')
local Dock = require('lib.dock')
local FileDialog = require('lib.filedialog')
local BoardView = require('board_view')

local function S(v) return math.floor(v * GLOBAL_SCALE + 0.5) end

local MENU_H, TOOL_H, STATUS_H = S(28), S(38), S(26)
local WIN_W, WIN_H = S(1100), S(720)
local UNITS = {'mm', 'mil', 'um', 'in'}

-- ------------------------------------------------------------------- window

glfw.init_hint('platform', 'x11')
glfw.init()
glfw.window_hint('context creation api', 'native')
glfw.version_hint(3, 3, 'core')

-- The window must fit the monitor (WSLg does not present windows larger than the desktop well);
-- like the Demopaja port, clamp to the primary monitor's video mode.
do
   local ok, mode = pcall(function() return glfw.get_video_mode(glfw.get_primary_monitor()) end)
   if ok and type(mode) == 'table' then
      local mw = mode.width or mode[1]
      local mh = mode.height or mode[2]
      if mw then WIN_W = math.min(WIN_W, mw - S(40)) end
      if mh then WIN_H = math.min(WIN_H, mh - S(90)) end
   end
end

local window = glfw.create_window(WIN_W, WIN_H, 'Freerouting C++')
glfw.make_context_current(window)
gl.init()
glfw.swap_interval(1)

local ctx = backend.init(window, {
   vbo_size = 2 * 1024 * 1024, ebo_size = 1024 * 1024,
   anti_aliasing = true, clipboard = false, callbacks = true,
})
local atlas = backend.font_stash_begin()
local fonts = {
   normal = atlas:add(14 * GLOBAL_SCALE, HERE .. '/fonts/DroidSans.ttf',
                      {ranges = nk.font_cyrillic_glyph_ranges()}),
   bold = atlas:add(14 * GLOBAL_SCALE, HERE .. '/fonts/Roboto-Bold.ttf'),
}
backend.font_stash_end(ctx, fonts.normal)
Theme.apply('dark')
Theme.apply_chrome(ctx)

Dock.init{left = S(230), right = S(260), bottom = S(170)}
Dock.register('layers', 'Layers', 'left', {'left', 'right'})
Dock.register('router', 'Router', 'right', {'right', 'left'})
Dock.register('log', 'Log', 'bottom', {'bottom'})

-- -------------------------------------------------------------------- state

local eng = fr.engine()
local view = BoardView.new()
local st = eng:status()
local snap_info = nil          -- counters of the current snapshot
local logs, log_next, log_follow = {}, 0, true
local opts = {passes_override = false, max_passes = 100, optimize = true, vias = true,
              drc = true, timeout_min = 0}
local unit_idx = 1
local cursor = {x = 0, y = 0, valid = false}
local design_path = nil
local initial_incompletes = nil
local click_guard = 0          -- frames to ignore canvas clicks after menus/dialogs
local edit_mode = 'select'     -- select | route | drag (Java GUI: Mode Route/Drag)
local mouse_state = {down = false, bx = nil, by = nil}

local function set_edit_mode(m)
   if edit_mode == m then return end
   edit_mode = m
   eng:edit_mode(m)
end

local LOG_COLORS = {
   [1] = {1.0, 0.35, 0.35, 1}, [2] = {1.0, 0.8, 0.3, 1}, [3] = {0.85, 0.85, 0.85, 1},
   [4] = {0.6, 0.7, 0.8, 1}, [5] = {0.5, 0.55, 0.6, 1},
}

local function busy() return st.busy or st.state == 'loading' or st.state == 'routing' or st.state == 'saving' end
local function routing() return st.state == 'routing' end
local function editable() return st.board_loaded and not busy() end

local function open_design(path)
   design_path = path
   view:reset()
   initial_incompletes = nil
   eng:load(path)
end

local function start_routing()
   if not st.board_loaded or busy() then return end
   initial_incompletes = snap_info and snap_info.incompletes or nil
   eng:route{
      max_passes = opts.passes_override and opts.max_passes or nil,
      optimize = opts.optimize, vias = opts.vias, drc = opts.drc,
      timeout = opts.timeout_min > 0 and ('PT%dM'):format(opts.timeout_min) or nil,
   }
end

local function dialog_open()
   FileDialog.open{title = 'Open Specctra design', mode = 'open',
                   dir = design_path and design_path:match('^(.*)/[^/]*$') or os.getenv('PWD'),
                   exts = {'dsn'},
                   on_ok = function(p) open_design(p) return true end}
end

local function dialog_save()
   if not st.board_loaded then return end
   local dir, name = (design_path or ''):match('^(.*)/([^/]*)%.dsn$')
   FileDialog.open{title = 'Save routed session', mode = 'save', dir = dir or os.getenv('PWD'),
                   name = (name or st.design or 'board') .. '.ses', exts = {'ses'},
                   on_ok = function(p) eng:save(p) return true end}
end

local function poll()
   st = eng:status()
   local s = eng:snapshot(view.version)
   if s then
      view:set_snapshot(s)
      snap_info = {items = s.items, traces = s.trace_count, vias = s.via_count, pins = s.pin_count,
                   incompletes = s.incompletes, layers = #s.layers}
   end
   local lines, nxt = eng:logs(log_next)
   log_next = nxt
   for _, l in ipairs(lines) do logs[#logs + 1] = l end
   if #logs > 3000 then
      local trimmed = {}
      for i = #logs - 2000, #logs do trimmed[#trimmed + 1] = logs[i] end
      logs = trimmed
   end
end

-- ------------------------------------------------------------- menu/toolbar

local function draw_menubar(w)
   if nk.window_begin(ctx, 'menubar', {0, 0, w, MENU_H}, nk.WINDOW_NO_SCROLLBAR) then
      nk.menubar_begin(ctx)
      nk.layout_row(ctx, 'static', MENU_H - S(8), {S(50), S(50), S(60), S(50), S(50)})
      if nk.menu_begin(ctx, nil, 'File', nk.TEXT_LEFT, {S(200), S(160)}) then
         nk.layout_row_dynamic(ctx, S(24), 1)
         if nk.menu_item(ctx, nil, 'Open design...', nk.TEXT_LEFT) then click_guard = 2 dialog_open() end
         if st.board_loaded and nk.menu_item(ctx, nil, 'Save session (.ses)...', nk.TEXT_LEFT) then
            click_guard = 2 dialog_save()
         end
         if nk.menu_item(ctx, nil, 'Exit', nk.TEXT_LEFT) then glfw.set_window_should_close(window, true) end
         nk.menu_end(ctx)
      end
      if nk.menu_begin(ctx, nil, 'Edit', nk.TEXT_LEFT, {S(260), S(200)}) then
         nk.layout_row_dynamic(ctx, S(24), 1)
         if nk.menu_item(ctx, nil, 'Undo  (Ctrl+Z)', nk.TEXT_LEFT) then eng:undo() end
         if nk.menu_item(ctx, nil, 'Redo  (Ctrl+Y)', nk.TEXT_LEFT) then eng:redo() end
         if nk.menu_item(ctx, nil, 'Select / pan mode', nk.TEXT_LEFT) then set_edit_mode('select') end
         if nk.menu_item(ctx, nil, 'Interactive route mode', nk.TEXT_LEFT) then set_edit_mode('route') end
         if nk.menu_item(ctx, nil, 'Drag mode', nk.TEXT_LEFT) then set_edit_mode('drag') end
         nk.menu_end(ctx)
      end
      if nk.menu_begin(ctx, nil, 'Route', nk.TEXT_LEFT, {S(200), S(120)}) then
         nk.layout_row_dynamic(ctx, S(24), 1)
         if nk.menu_item(ctx, nil, 'Autoroute', nk.TEXT_LEFT) then click_guard = 2 start_routing() end
         if nk.menu_item(ctx, nil, 'Stop', nk.TEXT_LEFT) then eng:cancel() end
         nk.menu_end(ctx)
      end
      if nk.menu_begin(ctx, nil, 'View', nk.TEXT_LEFT, {S(200), S(280)}) then
         nk.layout_row_dynamic(ctx, S(24), 1)
         if nk.menu_item(ctx, nil, 'Fit board', nk.TEXT_LEFT) then view.fit_pending = true end
         for _, k in ipairs({'traces', 'pads', 'vias', 'areas', 'keepouts', 'outline', 'airlines'}) do
            view.show[k] = nk.checkbox(ctx, k:sub(1, 1):upper() .. k:sub(2), view.show[k])
         end
         nk.menu_end(ctx)
      end
      if nk.menu_begin(ctx, nil, 'Help', nk.TEXT_LEFT, {S(320), S(90)}) then
         nk.layout_row_dynamic(ctx, S(22), 1)
         nk.label(ctx, fr.version, nk.TEXT_LEFT)
         nk.label(ctx, 'Routing core ported from freerouting (Java).', nk.TEXT_LEFT)
         nk.menu_end(ctx)
      end
      nk.menubar_end(ctx)
   end
   nk.window_end(ctx)
end

local function draw_toolbar(w)
   if nk.window_begin(ctx, 'toolbar', {0, MENU_H, w, TOOL_H}, nk.WINDOW_NO_SCROLLBAR) then
      nk.layout_row(ctx, 'static', TOOL_H - S(12),
                    {S(60), S(48), S(60), S(56), S(12), S(56), S(56), S(12), S(96), S(52), S(80),
                     S(40), S(12), S(40), S(70), math.max(S(40), w - S(810))})
      if nk.button(ctx, nil, 'Open') then click_guard = 2 dialog_open() end
      -- Mode (Java GUI toolbar: Route / Drag); deselecting returns to select/pan
      nk.label(ctx, 'Mode', nk.TEXT_RIGHT)
      for _, m in ipairs({{'route', 'Route'}, {'drag', 'Drag'}}) do
         local sel = nk.selectable(ctx, nil, m[2], nk.TEXT_CENTERED, edit_mode == m[1])
         if sel and edit_mode ~= m[1] then set_edit_mode(m[1])
         elseif not sel and edit_mode == m[1] then set_edit_mode('select') end
      end
      nk.spacing(ctx, 1)
      if nk.button(ctx, nil, 'Undo') then eng:undo() end
      if nk.button(ctx, nil, 'Redo') then eng:redo() end
      nk.spacing(ctx, 1)
      if routing() then
         nk.label(ctx, 'Routing...', nk.TEXT_CENTERED, {1, 0.8, 0.3, 1})
      elseif nk.button(ctx, nil, 'Autoroute') then
         start_routing()
      end
      if nk.button(ctx, nil, 'Stop') then eng:cancel() end
      if nk.button(ctx, nil, 'Save SES') then click_guard = 2 dialog_save() end
      if nk.button(ctx, nil, 'Fit') then view.fit_pending = true end
      nk.spacing(ctx, 1)
      nk.label(ctx, 'Unit', nk.TEXT_RIGHT)
      unit_idx = nk.combo(ctx, UNITS, unit_idx, S(22), {S(70), S(130)})
      local title = st.design ~= '' and st.design or 'no design loaded'
      nk.label(ctx, '  ' .. title, nk.TEXT_LEFT)
   end
   nk.window_end(ctx)
end

-- ------------------------------------------------------------------- panels

local function panel(id, title, fn)
   local r, extra = Dock.panel_bounds(id)
   if nk.window_begin(ctx, title, r, nk.WINDOW_BORDER | nk.WINDOW_TITLE | extra) then
      Dock.panel_inside(ctx, id)
      fn()
   end
   nk.window_end(ctx)
   Dock.panel_after(ctx, id)
end

local function kv(k, v)
   nk.layout_row_dynamic(ctx, S(20), 2)
   nk.label(ctx, k, nk.TEXT_LEFT)
   nk.label(ctx, tostring(v), nk.TEXT_RIGHT)
end

local function draw_layers()
   if #view.layers == 0 then
      nk.layout_row_dynamic(ctx, S(22), 1)
      nk.label(ctx, 'Open a .dsn design.', nk.TEXT_LEFT)
      return
   end
   nk.layout_row_dynamic(ctx, S(20), 1)
   nk.label(ctx, 'Active / visible layer', nk.TEXT_LEFT)
   for i, L in ipairs(view.layers) do
      nk.layout_row(ctx, 'dynamic', S(22), {0.15, 0.6, 0.25})
      local active, changed = nk.option(ctx, '', view.active_layer == i)
      if changed and active then
         view.active_layer = i
         eng:set_layer(i)     -- routing layer (during a route this inserts a via, like Java)
      end
      nk.label(ctx, L.name, nk.TEXT_LEFT, L.colors.traces)
      L.visible = nk.checkbox(ctx, '', L.visible)
   end
   nk.layout_row_dynamic(ctx, S(8), 1)
   nk.layout_row_dynamic(ctx, S(20), 1)
   nk.label(ctx, 'Show', nk.TEXT_LEFT)
   nk.layout_row_dynamic(ctx, S(22), 2)
   for _, k in ipairs({'traces', 'pads', 'vias', 'areas', 'keepouts', 'outline', 'airlines'}) do
      view.show[k] = nk.checkbox(ctx, k, view.show[k])
   end
   if snap_info then
      nk.layout_row_dynamic(ctx, S(8), 1)
      kv('Items', snap_info.items)
      kv('Pins', snap_info.pins)
      kv('Traces', snap_info.traces)
      kv('Vias', snap_info.vias)
      kv('Incomplete', snap_info.incompletes)
   end
end

local function draw_router()
   nk.layout_row_dynamic(ctx, S(22), 1)
   opts.passes_override = nk.checkbox(ctx, 'Limit passes', opts.passes_override)
   if opts.passes_override then
      nk.layout_row_dynamic(ctx, S(24), 1)
      opts.max_passes = nk.property(ctx, 'Max passes', 1, opts.max_passes, 9999, 1, 0.5)
   end
   nk.layout_row_dynamic(ctx, S(22), 1)
   opts.vias = nk.checkbox(ctx, 'Vias allowed', opts.vias)
   opts.optimize = nk.checkbox(ctx, 'Optimize after routing', opts.optimize)
   opts.drc = nk.checkbox(ctx, 'Final design rules check', opts.drc)
   nk.layout_row_dynamic(ctx, S(24), 1)
   opts.timeout_min = nk.property(ctx, 'Timeout, min (0=none)', 0, opts.timeout_min, 600, 1, 0.2)

   nk.layout_row_dynamic(ctx, S(30), 2)
   if routing() then
      nk.label(ctx, 'Routing...', nk.TEXT_CENTERED, {1, 0.8, 0.3, 1})
   elseif nk.button(ctx, nil, 'Autoroute') then
      start_routing()
   end
   if nk.button(ctx, nil, 'Stop') then eng:cancel() end

   nk.layout_row_dynamic(ctx, S(8), 1)
   kv('State', st.state)
   if st.stage ~= '' then kv('Stage', st.stage) end
   if st.pass > 0 then kv('Pass', st.pass) end
   kv('Unrouted', st.unrouted >= 0 and st.unrouted or '-')
   kv('Violations', st.violations >= 0 and st.violations or '-')
   kv('Time', ('%.1f s'):format(st.elapsed))
   if routing() and initial_incompletes and initial_incompletes > 0 and st.unrouted >= 0 then
      nk.layout_row_dynamic(ctx, S(18), 1)
      nk.progress(ctx, math.floor(initial_incompletes - math.min(st.unrouted, initial_incompletes)),
                  math.floor(initial_incompletes), "fixed")
   end
   if st.error ~= '' then
      nk.layout_row_dynamic(ctx, S(40), 1)
      nk.label_wrap(ctx, st.error, {1, 0.4, 0.4, 1})
   end
end

local log_shown = 0
local function draw_log()
   nk.layout_row_dynamic(ctx, S(18), 1)
   local first = math.max(1, #logs - 400)
   for i = first, #logs do
      local l = logs[i]
      nk.label(ctx, l.text, nk.TEXT_LEFT, LOG_COLORS[l.level] or LOG_COLORS[3])
   end
   -- follow the tail when new lines arrive
   if log_follow and #logs ~= log_shown then
      pcall(nk.window_set_scroll, ctx, 0, 1000000)
      log_shown = #logs
   end
end

local function draw_statusbar(w, h)
   if nk.window_begin(ctx, 'statusbar', {0, h - STATUS_H, w, STATUS_H}, nk.WINDOW_NO_SCROLLBAR) then
      nk.layout_row(ctx, 'static', STATUS_H - S(8), {S(110), S(110), w - S(520), S(280)})
      local v = st.violations >= 0 and st.violations or 0
      local u = st.unrouted >= 0 and st.unrouted or 0
      nk.label(ctx, ('Violations: %d'):format(v), nk.TEXT_LEFT, v > 0 and {1, 0.35, 1, 1} or nil)
      nk.label(ctx, ('Unrouted: %d'):format(u), nk.TEXT_LEFT, u > 0 and {1, 0.8, 0.3, 1} or nil)
      local em = st.edit and st.edit.message or ''
      local mode = edit_mode ~= 'select' and ('[' .. edit_mode .. ' L' .. (st.edit and st.edit.layer or 1) .. '] ') or ''
      nk.label(ctx, mode .. (em ~= '' and em or st.message), nk.TEXT_LEFT)
      if cursor.valid then
         local un = UNITS[unit_idx]
         nk.label(ctx, ('X %.4f  Y %.4f %s'):format(view:convert(cursor.x, un),
                                                     view:convert(cursor.y, un), un), nk.TEXT_RIGHT)
      else
         nk.label(ctx, '', nk.TEXT_RIGHT)
      end
   end
   nk.window_end(ctx)
end

-- ---------------------------------------------------- test autopilot (CI) ---
-- FRUI_SHOT=/tmp/prefix lua5.3 ui/main.lua board.dsn : load, screenshot, route,
-- screenshot while routing and when done, then quit.

local SHOT = os.getenv('FRUI_SHOT')
local autopilot = SHOT and {phase = 'load', frames = 0, t0 = 0} or nil

local function screenshot(path, fbw, fbh)
   -- moongl sizes the read_pixels buffer as one byte per pixel (overflow for rgb/rgba), so read
   -- the channels one by one and interleave them here.
   gl.pixel_store('pack alignment', 1)
   local ch = {gl.read_pixels(0, 0, fbw, fbh, 'red', 'ubyte'),
               gl.read_pixels(0, 0, fbw, fbh, 'green', 'ubyte'),
               gl.read_pixels(0, 0, fbw, fbh, 'blue', 'ubyte')}
   local rows, char, byte = {}, string.char, string.byte
   for y = fbh - 1, 0, -1 do
      local o = y * fbw
      local r = {byte(ch[1], o + 1, o + fbw)}
      local g = {byte(ch[2], o + 1, o + fbw)}
      local b = {byte(ch[3], o + 1, o + fbw)}
      local px = {}
      for x = 1, fbw do px[x] = char(r[x], g[x], b[x]) end
      rows[#rows + 1] = table.concat(px)
   end
   require('moonimage').write_png(path, fbw, fbh, 'rgb', table.concat(rows))
   print('screenshot: ' .. path)
end

local function autopilot_step(fbw, fbh)
   local a = autopilot
   a.frames = a.frames + 1
   if a.phase == 'load' and st.board_loaded and view.version > 0 and a.frames > 20 then
      screenshot(SHOT .. '_loaded.png', fbw, fbh)
      if os.getenv('FRUI_EDIT') then
         -- interactive route along the first airline, through the same engine calls the mouse uses
         local s = eng:snapshot(0)
         a.ax0, a.ay0, a.ax1, a.ay1 = string.unpack('<ffff', s.airlines, 1)
         set_edit_mode('route')
         eng:mouse('click', a.ax0, a.ay0)
         a.phase, a.frames, a.step = 'edit', 0, 0
         return
      end
      start_routing()
      a.phase, a.frames, a.t0 = 'route', 0, glfw.get_time()
   elseif a.phase == 'edit' then
      if a.frames % 3 == 0 and a.step < 10 then
         a.step = a.step + 1
         local t = a.step / 20
         eng:mouse('move', a.ax0 + (a.ax1 - a.ax0) * t, a.ay0 + (a.ay1 - a.ay0) * t)
      elseif a.step >= 10 and a.frames > 45 and not a.mid then
         screenshot(SHOT .. '_editing.png', fbw, fbh)
         a.mid = true
         for i = 11, 20 do
            local t = i / 20
            eng:mouse('move', a.ax0 + (a.ax1 - a.ax0) * t, a.ay0 + (a.ay1 - a.ay0) * t)
         end
         eng:key('complete')
      elseif a.mid and a.frames > 80 then
         a.phase, a.frames = 'done', 0
      end
   elseif a.phase == 'route' then
      if routing() and not a.mid and glfw.get_time() - a.t0 > 1.5 then
         screenshot(SHOT .. '_routing.png', fbw, fbh)
         a.mid = true
      end
      if a.frames > 10 and not busy() then a.phase, a.frames = 'done', 0 end
   elseif a.phase == 'done' and a.frames > 15 then
      screenshot(SHOT .. '_routed.png', fbw, fbh)
      print(('autopilot: %s unrouted=%d violations=%d time=%.1fs'):format(
         st.state, st.unrouted, st.violations, st.elapsed))
      glfw.set_window_should_close(window, true)
   end
end

-- --------------------------------------------------------------- main loop

-- ------------------------------------------------------------------ keyboard
-- Esc cancels the route/drag, Enter/Space completes it, Ctrl+Z / Ctrl+Y undo/redo,
-- 1..9 / +/- change the routing layer (inserting a via while routing), r/d/s switch mode.

local function signal_layers()
   local r = {}
   for i, L in ipairs(view.layers) do if L.signal then r[#r + 1] = i end end
   return r
end

local function step_layer(dir)
   local sig = signal_layers()
   local cur = st.edit and st.edit.layer or view.active_layer
   for k = 1, #sig do
      if sig[k] == cur then
         local n = sig[k + dir]
         if n then view.active_layer = n eng:set_layer(n) end
         return
      end
   end
end

glfw.set_key_callback(window, function(wnd, key, scancode, action, shift, control, alt, super)
   if action == 'press' and not FileDialog.active() and not nk.item_is_any_active(ctx) then
      if key == 'escape' then eng:key('cancel') return
      elseif (key == 'enter' or key == 'space') and edit_mode ~= 'select' then eng:key('complete') return
      elseif key == 'z' and control then eng:undo() return
      elseif key == 'y' and control then eng:redo() return
      elseif key == 'r' and not control then set_edit_mode('route') return
      elseif key == 'd' and not control then set_edit_mode('drag') return
      elseif key == 's' and not control then set_edit_mode('select') return
      elseif key == 'f' and not control then view.fit_pending = true return
      end
   end
   backend.key_callback(wnd, key, scancode, action, shift, control, alt, super)
end)

glfw.set_char_callback(window, function(wnd, cp)
   if not FileDialog.active() and not nk.item_is_any_active(ctx) then
      local ch = utf8.char(cp)
      if ch:match('%d') and ch ~= '0' then
         local sig = signal_layers()
         local n = sig[math.min(tonumber(ch), #sig)]
         if n then view.active_layer = n eng:set_layer(n) end
         return
      elseif ch == '+' then step_layer(1) return
      elseif ch == '-' then step_layer(-1) return
      end
   end
   if backend.char_callback then backend.char_callback(wnd, cp) end
end)

-- Canvas mouse -> engine (board coordinates). Route: click starts/finishes, moves follow the
-- cursor while a route is open. Drag: press/drag/release on the item under the cursor.
local function canvas_edit_input(over, bx, by)
   if not editable() or edit_mode == 'select' then
      mouse_state.down = false
      return
   end
   local pressed = over and ctx:is_mouse_pressed(nk.BUTTON_LEFT)
   local down = ctx:is_mouse_down(nk.BUTTON_LEFT)
   local moved = bx ~= mouse_state.bx or by ~= mouse_state.by
   mouse_state.bx, mouse_state.by = bx, by
   if edit_mode == 'route' then
      if pressed then
         eng:mouse('click', bx, by)
      elseif moved and over and st.edit and st.edit.routing then
         eng:mouse('move', bx, by)
      end
   elseif edit_mode == 'drag' then
      if pressed then
         mouse_state.down = true
         eng:mouse('press', bx, by)
      elseif mouse_state.down and down and moved then
         eng:mouse('drag', bx, by)
      elseif mouse_state.down and not down then
         mouse_state.down = false
         eng:mouse('release')
      end
   end
end

-- overlay of the route being drawn: a line from its last corner to the nearest target
local function update_overlay()
   local e = st.edit
   if e and e.routing and e.corner_x then
      local L = {}
      local s = (e.pen_half_width or 0) * 2 + 1
      -- cross at the route end
      L[#L + 1], L[#L + 2], L[#L + 3], L[#L + 4] = e.corner_x - s, e.corner_y, e.corner_x + s, e.corner_y
      L[#L + 1], L[#L + 2], L[#L + 3], L[#L + 4] = e.corner_x, e.corner_y - s, e.corner_x, e.corner_y + s
      if e.target_x then
         L[#L + 1], L[#L + 2], L[#L + 3], L[#L + 4] = e.corner_x, e.corner_y, e.target_x, e.target_y
      end
      view:set_overlay(L, {1, 1, 1, 1})
   else
      view:set_overlay(nil)
   end
   -- the routing layer may change through via insertion: follow it in the layer panel
   if e and e.routing and e.layer and view.layers[e.layer] then view.active_layer = e.layer end
end

if arg[1] then open_design(arg[1]) end

while not glfw.window_should_close(window) do
   glfw.wait_events_timeout(1 / 30)
   backend.new_frame()
   poll()

   local w, h = glfw.get_window_size(window)
   local fbw, fbh = glfw.get_framebuffer_size(window)
   if click_guard > 0 then click_guard = click_guard - 1 end

   draw_menubar(w)
   draw_toolbar(w)
   Dock.frame_begin(ctx, 0, MENU_H + TOOL_H, w, h - MENU_H - TOOL_H - STATUS_H)
   panel('layers', 'Layers', draw_layers)
   panel('router', 'Router', draw_router)
   panel('log', 'Log', draw_log)
   Dock.draw_hints(ctx)
   Dock.frame_end(ctx)
   draw_statusbar(w, h)
   FileDialog.draw(ctx, w, h)

   -- board canvas in the free center area
   local cx, cy, cw, ch = Dock.center_rect()
   local rect = {cx, cy, cw, ch}
   local mx, my = ctx:mouse_pos()
   local inside = mx >= cx and my >= cy and mx < cx + cw and my < cy + ch
   local over = inside and not nk.window_is_any_hovered(ctx) and not FileDialog.active()
                and click_guard == 0 and not Dock.dragging()
   cursor.x, cursor.y = view:input(ctx, nk, rect, over, edit_mode == 'select' or not editable())
   cursor.valid = inside and view.bbox ~= nil
   if view.bbox then canvas_edit_input(over, cursor.x, cursor.y) end
   update_overlay()

   gl.viewport(0, 0, fbw, fbh)
   gl.disable('scissor test')
   local bg = Theme.bgcolor()
   gl.clear_color(bg[1], bg[2], bg[3], 1)
   gl.clear('color')
   view:draw(rect, h, w > 0 and fbw / w or 1)
   gl.viewport(0, 0, fbw, fbh)
   backend.render()
   if autopilot then autopilot_step(fbw, fbh) end
   glfw.swap_buffers(window)
   collectgarbage('step')
end

eng:close()
backend.shutdown()
