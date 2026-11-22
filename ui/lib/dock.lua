-- Reused from the Demopaja timeline port (~/timeline_ai), see its README for credits.
-- ===========================================================================
-- editor/dock.lua
-- A small docking layer over nuklear windows (nuklear has none of its own):
-- panels live either DOCKED in a slot ('left' | 'bottom') around the
-- central preview, or FLOATING as movable/scalable nuklear windows.
--
--   * dragging a docked panel's title bar past a threshold tears it off
--     into a floating window (nuklear picks up the drag mid-press: it moves
--     a movable window while the mouse is down over its header)
--   * dragging a floating panel lights up the free slots' drop zones;
--     releasing inside one docks it there
--   * slot edges are draggable splitters
--
-- The trick that makes this cheap: non-movable nuklear windows take their
-- bounds from window_begin() every frame (docked), movable ones own their
-- bounds (floating) — the manager just decides which mode a panel is in.
-- Part of the Demopaja full port; see port_notes/architecture.md.
-- ===========================================================================

local nk = require('moonnuklear')
local Theme = require("lib.theme")

local M = {}

local function S(v) return math.floor(v * (GLOBAL_SCALE or 1) + 0.5) end

local HEADER_H = function() return S(26) end
local SPLITTER = function() return S(6) end
local ZONE = function() return S(52) end
local TEAR_DIST = function() return S(10) end

-- slot -> {size}: left/right hold widths, top/bottom hold heights
local rSlots = {left = {size = 0}, right = {size = 0},
                top = {size = 0}, bottom = {size = 0}}
local rPanels = {}            -- id -> panel record
local rOrder = {}             -- registration order
local rWork = {x = 0, y = 0, w = 0, h = 0}
local pDragged = nil          -- panel id being dragged (floating, mouse down)
local sHoverZone = nil        -- slot name under the dragged panel
local sSplitDrag = nil        -- active splitter ('left' | 'bottom')
local rPending = {}           -- id -> rect to force via window_set_bounds

function M.init(rSizes)
   rSlots.left.size = rSizes.left or S(250)
   rSlots.right.size = rSizes.right or S(250)
   rSlots.top.size = rSizes.top or S(340)
   rSlots.bottom.size = rSizes.bottom or S(340)
end

-- sSlot: the starting slot; rAllowed: the slots this panel may dock into
-- (e.g. {'bottom', 'top'} for the timeline) — defaults to just sSlot
function M.register(sId, sTitle, sSlot, rAllowed)
   rPanels[sId] = {
      id = sId, title = sTitle, slot = sSlot,
      allowed = rAllowed or {sSlot},
      floating = nil,          -- {x, y, w, h} when torn off
      rect = {0, 0, 0, 0},     -- last laid-out / observed bounds
      press = nil,             -- header-press tracking for the tear-off
   }
   rOrder[#rOrder + 1] = sId
end

local function slot_panel(sSlot)
   for _, sId in ipairs(rOrder) do
      local p = rPanels[sId]
      if p.slot == sSlot and not p.floating then return p end
   end
   return nil
end

local function in_rect(x, y, r)
   return x >= r[1] and x < r[1] + r[3] and y >= r[2] and y < r[2] + r[4]
end

-- the drop zones (only for free slots)
local function zone_rect(sSlot)
   if sSlot == 'left' then
      return {rWork.x, rWork.y, ZONE(), rWork.h}
   elseif sSlot == 'right' then
      return {rWork.x + rWork.w - ZONE(), rWork.y, ZONE(), rWork.h}
   elseif sSlot == 'top' then
      return {rWork.x, rWork.y, rWork.w, ZONE()}
   end
   return {rWork.x, rWork.y + rWork.h - ZONE(), rWork.w, ZONE()}
end

-- the carve order: top and bottom strips span the full width, left and
-- right columns live in the band between them, the center is the rest
local function band()
   local i32Top = slot_panel('top') and (rSlots.top.size + SPLITTER()) or 0
   local i32Bot = slot_panel('bottom') and (rSlots.bottom.size + SPLITTER()) or 0
   return rWork.y + i32Top, rWork.h - i32Top - i32Bot
end

-- ------------------------------------------------------------------ layout ---

-- Call once per frame BEFORE drawing panels; handles splitters and applies
-- any pending explicit bounds.
function M.frame_begin(ctx, i32X, i32Y, i32W, i32H)
   rWork.x, rWork.y, rWork.w, rWork.h = i32X, i32Y, i32W, i32H

   for sId, rRect in pairs(rPending) do
      pcall(nk.window_set_bounds, ctx, rPanels[sId].title, rRect)
      rPending[sId] = nil
   end

   local mx, my = ctx:mouse_pos()
   local bDown = ctx:is_mouse_down(nk.BUTTON_LEFT)
   local bPressed = ctx:is_mouse_pressed(nk.BUTTON_LEFT)

   -- splitters: the strip between a docked slot and the center
   local i32BandY, i32BandH = band()
   if sSplitDrag and not bDown then sSplitDrag = nil end
   if not sSplitDrag and bPressed and not pDragged then
      if slot_panel('left') and in_rect(mx, my,
            {rWork.x + rSlots.left.size, i32BandY, SPLITTER(), i32BandH}) then
         sSplitDrag = 'left'
      elseif slot_panel('right') and in_rect(mx, my,
            {rWork.x + rWork.w - rSlots.right.size - SPLITTER(), i32BandY,
             SPLITTER(), i32BandH}) then
         sSplitDrag = 'right'
      elseif slot_panel('top') and in_rect(mx, my,
            {rWork.x, rWork.y + rSlots.top.size, rWork.w, SPLITTER()}) then
         sSplitDrag = 'top'
      elseif slot_panel('bottom') and in_rect(mx, my,
            {rWork.x, rWork.y + rWork.h - rSlots.bottom.size - SPLITTER(),
             rWork.w, SPLITTER()}) then
         sSplitDrag = 'bottom'
      end
   end
   if sSplitDrag == 'left' then
      rSlots.left.size = math.max(S(120), math.min(mx - rWork.x, rWork.w - S(200)))
   elseif sSplitDrag == 'right' then
      rSlots.right.size = math.max(S(120),
         math.min(rWork.x + rWork.w - mx, rWork.w - S(200)))
   elseif sSplitDrag == 'top' then
      rSlots.top.size = math.max(S(120), math.min(my - rWork.y, rWork.h - S(150)))
   elseif sSplitDrag == 'bottom' then
      rSlots.bottom.size = math.max(S(120),
         math.min(rWork.y + rWork.h - my, rWork.h - S(150)))
   end
end

-- the layout rect of a slot's docked panel
local function slot_rect(sSlot)
   local i32BandY, i32BandH = band()
   if sSlot == 'top' then
      return {rWork.x, rWork.y, rWork.w, rSlots.top.size}
   elseif sSlot == 'bottom' then
      return {rWork.x, rWork.y + rWork.h - rSlots.bottom.size, rWork.w, rSlots.bottom.size}
   elseif sSlot == 'left' then
      return {rWork.x, i32BandY, rSlots.left.size, i32BandH}
   end
   return {rWork.x + rWork.w - rSlots.right.size, i32BandY, rSlots.right.size, i32BandH}
end

-- what's left for the preview
function M.center_rect()
   local i32BandY, i32BandH = band()
   local i32LX = slot_panel('left') and (rSlots.left.size + SPLITTER()) or 0
   local i32RX = slot_panel('right') and (rSlots.right.size + SPLITTER()) or 0
   return rWork.x + i32LX, i32BandY, rWork.w - i32LX - i32RX, i32BandH
end

-- Bounds + extra nk flags for the panel's window_begin. Docked panels are
-- not movable (begin() bounds re-apply every frame); floating ones are.
function M.panel_bounds(sId)
   local p = rPanels[sId]
   if p.floating then
      p.rect = {p.floating[1], p.floating[2], p.floating[3], p.floating[4]}
      return p.rect, nk.WINDOW_MOVABLE | nk.WINDOW_SCALABLE, false
   end
   local r = slot_rect(p.slot)
   p.rect = r
   return r, 0, true
end

-- ------------------------------------------------------- tear-off / drop ---

-- Call between window_begin and window_end (nuklear only reports the
-- bounds of the CURRENT window): captures where a floating panel really is.
function M.panel_inside(ctx, sId)
   local p = rPanels[sId]
   if not p.floating then return end
   local bOk, rB = pcall(nk.window_get_bounds, ctx)
   if bOk and type(rB) == 'table' then
      if ctx:is_mouse_down(nk.BUTTON_LEFT) and
         (rB[1] ~= p.floating[1] or rB[2] ~= p.floating[2]) then
         pDragged = sId             -- it moved under a held button = dragging
      end
      p.floating = {rB[1], rB[2], rB[3], rB[4]}
   end
end

-- Call right after the panel's window_end.
function M.panel_after(ctx, sId)
   local p = rPanels[sId]
   local mx, my = ctx:mouse_pos()
   local bDown = ctx:is_mouse_down(nk.BUTTON_LEFT)
   local bPressed = ctx:is_mouse_pressed(nk.BUTTON_LEFT)

   if not p.floating then
      -- tear-off: press in the title bar, drag past the threshold
      local rHeader = {p.rect[1], p.rect[2], p.rect[3], HEADER_H()}
      if bPressed and in_rect(mx, my, rHeader) then
         p.press = {x = mx, y = my}
      elseif p.press and bDown then
         local dx, dy = mx - p.press.x, my - p.press.y
         if dx * dx + dy * dy >= TEAR_DIST() * TEAR_DIST() then
            local i32W = math.min(p.rect[3], S(720))
            local i32H = math.min(p.rect[4], S(420))
            p.floating = {mx - i32W // 4, my - HEADER_H() // 2, i32W, i32H}
            rPending[sId] = p.floating
            p.press = nil
            pDragged = sId          -- nuklear continues the drag mid-press
         end
      elseif not bDown then
         p.press = nil
      end
      return
   end

   -- floating: bounds were captured by panel_inside; here we run drop logic
   if pDragged == sId then
      sHoverZone = nil
      for _, sSlot in ipairs(p.allowed) do
         if not slot_panel(sSlot) and in_rect(mx, my, zone_rect(sSlot)) then
            sHoverZone = sSlot
         end
      end
      if not bDown then
         if sHoverZone then
            p.slot = sHoverZone
            p.floating = nil        -- back to slot bounds next frame
         end
         pDragged = nil
         sHoverZone = nil
      end
   end
end

-- ------------------------------------------------------------- drop hints ---

-- Highlight the free slots while a panel is dragged. WINDOW_BACKGROUND
-- keeps the hints under the panels (the zones overlap the GL preview only).
function M.draw_hints(ctx)
   if not pDragged then return end
   local rHi = Theme.colors.HIGHLIGHT
   for _, sSlot in ipairs(rPanels[pDragged].allowed) do
      if not slot_panel(sSlot) then
         local r = zone_rect(sSlot)
         local sName = 'dockhint_' .. sSlot
         if nk.window_begin(ctx, sName, r,
                            nk.WINDOW_BACKGROUND | nk.WINDOW_NO_SCROLLBAR |
                            nk.WINDOW_NO_INPUT) then
            local canvas = nk.window_get_canvas(ctx)
            local f32A = (sHoverZone == sSlot) and 0.55 or 0.25
            canvas:fill_rect({r[1], r[2], r[3], r[4]}, 0,
                             {rHi[1], rHi[2], rHi[3], f32A})
         end
         nk.window_end(ctx)
      end
   end
end

-- hide stale hint windows when the drag ends (nuklear keeps them alive)
function M.frame_end(ctx)
   if pDragged then return end
   for sSlot in pairs(rSlots) do
      pcall(nk.window_close, ctx, 'dockhint_' .. sSlot)
   end
end

-- programmatic control (smoke tests, menu items)
function M.undock(sId, rRect)
   local p = rPanels[sId]
   if p.floating then return end
   p.floating = rRect or {rWork.x + S(60), rWork.y + S(60),
                          math.min(p.rect[3], S(720)), math.min(p.rect[4], S(420))}
   rPending[sId] = p.floating
end

function M.dock(sId, sSlot)
   local p = rPanels[sId]
   if sSlot then p.slot = sSlot end
   p.floating = nil
end

function M.is_floating(sId) return rPanels[sId].floating ~= nil end
function M.dragging() return pDragged end

return M
