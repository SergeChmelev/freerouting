-- ===========================================================================
-- ui/board_view.lua
-- OpenGL view of a board snapshot produced by the C++ engine (frcore).
--
-- The engine hands over geometry as binary float32 xy strings (triangles for
-- copper/areas/keepouts/vias, segment pairs for the outline and the airlines),
-- so a new snapshot is uploaded with one gl.buffer_data per category and
-- nothing is converted in Lua. Board coordinates go straight into the VBOs;
-- the vertex shader applies pan/zoom (board y points up, like GL clip space).
--
-- Colors follow the defaults of the Java freerouting GUI
-- (gui/rendering/ItemColorTableModel.java, OtherColorTableModel.java).
-- ===========================================================================

local gl = require('moongl')

local M = {}

local VS = [[
#version 330 core
layout(location = 0) in vec2 aPos;
uniform vec4 uXform;   // scale x/y, offset x/y (board -> clip)
void main() { gl_Position = vec4(aPos * uXform.xy + uXform.zw, 0.0, 1.0); }
]]

local FS = [[
#version 330 core
uniform vec4 uColor;
out vec4 fragColor;
void main() { fragColor = uColor; }
]]

local function rgb(r, g, b, a) return {r / 255, g / 255, b / 255, a or 1} end

-- Java GUI default palette
M.BACKGROUND = rgb(0, 16, 35)
M.OUTLINE = rgb(100, 150, 255)
M.AIRLINE = rgb(255, 255, 255, 0.85)
M.VIA = rgb(227, 183, 46)
M.KEEPOUT = rgb(26, 196, 210, 0.35)
local INNER_TRACES = {rgb(40, 204, 217), rgb(127, 200, 127), rgb(206, 125, 44),
                      rgb(79, 203, 203), rgb(219, 98, 139), rgb(167, 165, 198)}

-- default colors of layer i (1-based) of n layers
function M.layer_colors(i, n)
   if i == 1 then
      return {traces = rgb(200, 52, 52), pads = rgb(227, 183, 46), areas = rgb(0, 150, 0, 0.55)}
   elseif i == n then
      return {traces = rgb(77, 127, 196), pads = rgb(227, 183, 46), areas = rgb(100, 100, 0, 0.55)}
   end
   return {traces = INNER_TRACES[(i - 1) % 6 + 1], pads = rgb(255, 150, 0),
           areas = rgb(0, 200, 60, 0.55)}
end

-- ----------------------------------------------------------------- GL objects

local View = {}
View.__index = View

local function make_mesh(sData, ePrim)
   if not sData or #sData == 0 then return nil end
   local vao = gl.gen_vertex_arrays()
   local vbo = gl.gen_buffers(1)
   gl.bind_vertex_array(vao)
   gl.bind_buffer('array', vbo)
   gl.buffer_data('array', sData, 'static draw')
   gl.vertex_attrib_pointer(0, 2, 'float', false, 2 * gl.sizeof('float'), 0)
   gl.enable_vertex_attrib_array(0)
   gl.unbind_vertex_array()
   return {vao = vao, vbo = vbo, count = #sData // 8, prim = ePrim}
end

local function free_mesh(m)
   if not m then return end
   gl.delete_vertex_arrays(m.vao)
   gl.delete_buffers(m.vbo)
end

function M.new()
   local self = setmetatable({}, View)
   local prog = gl.make_program_s({vertex = VS, fragment = FS})
   self.prog = prog
   self.uXform = gl.get_uniform_location(prog, 'uXform')
   self.uColor = gl.get_uniform_location(prog, 'uColor')
   self.meshes = nil          -- built from the current snapshot
   self.version = 0
   self.layers = {}           -- {name, visible, colors}
   self.active_layer = 1
   self.show = {traces = true, pads = true, vias = true, areas = true,
                keepouts = true, outline = true, airlines = true}
   -- view: center in board units and pixels per board unit
   self.cx, self.cy, self.scale = 0, 0, 1
   self.fitted = false
   self.bbox = nil
   self.drag = nil
   return self
end

-- Upload a new snapshot (from eng:snapshot()).
function View:set_snapshot(snap)
   self:release()
   local ms = {layers = {}}
   for i, l in ipairs(snap.layers) do
      ms.layers[i] = {
         traces = make_mesh(l.traces, 'triangles'),
         pads = make_mesh(l.pads, 'triangles'),
         areas = make_mesh(l.areas, 'triangles'),
         keepouts = make_mesh(l.keepouts, 'triangles'),
      }
      -- keep user choices (visibility/colors) across snapshots of the same board
      local old = self.layers[i]
      if not old or old.name ~= l.name then
         self.layers[i] = {name = l.name, signal = l.signal, visible = true,
                           colors = M.layer_colors(i, #snap.layers)}
      end
   end
   for i = #snap.layers + 1, #self.layers do self.layers[i] = nil end
   ms.vias = make_mesh(snap.vias, 'triangles')
   ms.outline = make_mesh(snap.outline, 'lines')
   ms.airlines = make_mesh(snap.airlines, 'lines')
   self.meshes = ms
   self.version = snap.version
   self.bbox = snap.bbox
   self.unit, self.resolution = snap.unit, snap.resolution
   if self.active_layer > #self.layers then self.active_layer = 1 end
   if not self.fitted and snap.bbox[3] > snap.bbox[1] then
      self.fit_pending = true
      self.fitted = true
   end
end

function View:release()
   if not self.meshes then return end
   for _, l in ipairs(self.meshes.layers) do
      free_mesh(l.traces); free_mesh(l.pads); free_mesh(l.areas); free_mesh(l.keepouts)
   end
   free_mesh(self.meshes.vias); free_mesh(self.meshes.outline); free_mesh(self.meshes.airlines)
   self.meshes = nil
end

-- A new design: forget view and layer settings.
function View:reset()
   self:release()
   self.layers = {}
   self.version = 0
   self.fitted = false
   self.bbox = nil
end

-- ----------------------------------------------------------------- navigation

function View:fit(w, h)
   local b = self.bbox
   if not b or b[3] <= b[1] or w <= 0 or h <= 0 then return end
   self.cx, self.cy = (b[1] + b[3]) / 2, (b[2] + b[4]) / 2
   self.scale = 0.92 * math.min(w / (b[3] - b[1]), h / math.max(b[4] - b[2], 1e-9))
end

-- screen (window pixels) -> board units, for the rect r = {x, y, w, h}
function View:to_board(r, mx, my)
   return self.cx + (mx - (r[1] + r[3] / 2)) / self.scale,
          self.cy - (my - (r[2] + r[4] / 2)) / self.scale
end

-- Mouse handling for the canvas rect r. bOver: the mouse is over the canvas
-- and not over a UI window. Returns the board coordinates under the cursor.
function View:input(ctx, nk, r, bOver, bLeftPans)
   local mx, my = ctx:mouse_pos()
   if self.fit_pending then self:fit(r[3], r[4]); self.fit_pending = false end
   -- pan: right or middle drag (and left drag in select mode) that started on the canvas
   local bDown = ctx:is_mouse_down(nk.BUTTON_RIGHT) or ctx:is_mouse_down(nk.BUTTON_MIDDLE)
              or (bLeftPans and ctx:is_mouse_down(nk.BUTTON_LEFT))
   if bDown and (self.drag or bOver) then
      if not self.drag then self.drag = {x = mx, y = my, cx = self.cx, cy = self.cy}
      else
         self.cx = self.drag.cx - (mx - self.drag.x) / self.scale
         self.cy = self.drag.cy + (my - self.drag.y) / self.scale
      end
   elseif not bDown then
      self.drag = nil
   end
   -- zoom around the cursor
   if bOver then
      local _, wy = ctx:mouse_scroll_delta()
      if wy and wy ~= 0 then
         local bx, by = self:to_board(r, mx, my)
         self.scale = self.scale * (1.2 ^ wy)
         local nx, ny = self:to_board(r, mx, my)
         self.cx, self.cy = self.cx + (bx - nx), self.cy + (by - ny)
      end
   end
   return self:to_board(r, mx, my)
end

-- ------------------------------------------------------------------ drawing

-- Draw into the window-space rect r; fbScale = framebuffer px per window px.
function View:draw(r, i32WndH, fbScale)
   local x, y = math.floor(r[1] * fbScale), math.floor((i32WndH - r[2] - r[4]) * fbScale)
   local w, h = math.floor(r[3] * fbScale), math.floor(r[4] * fbScale)
   if w <= 0 or h <= 0 then return end
   gl.viewport(x, y, w, h)
   gl.enable('scissor test')
   gl.scissor(x, y, w, h)
   local bg = M.BACKGROUND
   gl.clear_color(bg[1], bg[2], bg[3], 1)
   gl.clear('color')
   local ms = self.meshes
   if ms then
      gl.use_program(self.prog)
      gl.enable('blend')
      gl.blend_func('src alpha', 'one minus src alpha')
      local sx = 2 * self.scale / r[3]
      local sy = 2 * self.scale / r[4]
      gl.uniformf(self.uXform, sx, sy, -self.cx * sx, -self.cy * sy)
      local function draw(m, c, alphaMul)
         if not m then return end
         gl.uniformf(self.uColor, c[1], c[2], c[3], (c[4] or 1) * (alphaMul or 1))
         gl.bind_vertex_array(m.vao)
         gl.draw_arrays(m.prim, 0, m.count)
      end
      -- inactive layers first (bottom-most first), the active layer on top
      local order = {}
      for i = #ms.layers, 1, -1 do if i ~= self.active_layer then order[#order + 1] = i end end
      order[#order + 1] = self.active_layer
      for _, i in ipairs(order) do
         local L, lm = self.layers[i], ms.layers[i]
         if L and lm and L.visible then
            local a = (i == self.active_layer) and 1 or 0.55
            if self.show.areas then draw(lm.areas, L.colors.areas, a) end
            if self.show.keepouts then draw(lm.keepouts, M.KEEPOUT, a) end
            if self.show.traces then draw(lm.traces, L.colors.traces, a) end
            if self.show.pads then draw(lm.pads, L.colors.pads, a) end
         end
      end
      if self.show.vias then draw(ms.vias, M.VIA) end
      if self.show.outline then draw(ms.outline, M.OUTLINE) end
      if self.show.airlines then draw(ms.airlines, M.AIRLINE) end
      -- interactive overlay (route in progress): rebuilt every frame, a few vertices only
      if self.overlay and #self.overlay.lines > 0 then
         local m = make_mesh(string.pack(("<" .. ("f"):rep(#self.overlay.lines)), table.unpack(self.overlay.lines)), "lines")
         draw(m, self.overlay.color or M.AIRLINE)
         free_mesh(m)
      end
      gl.unbind_vertex_array()
      gl.disable('blend')
   end
   gl.disable('scissor test')
end

-- Overlay segments in board units: {x0, y0, x1, y1, ...}
function View:set_overlay(lines, color)
   self.overlay = lines and {lines = lines, color = color} or nil
end

-- board units -> the given display unit ('mm', 'mil', 'um', 'in')
local MM_PER = {um = 0.001, mm = 1, mil = 0.0254, inch = 25.4}
function View:convert(v, sTo)
   local fromMM = (MM_PER[self.unit or 'um'] or 1) / (self.resolution or 1)
   local mm = v * fromMM
   if sTo == 'in' then return mm / 25.4 end
   return mm / (MM_PER[sTo] or 1)
end

return M
