-- Reused from the Demopaja timeline port (~/timeline_ai), see its README for credits.
-- ===========================================================================
-- timeline/theme.lua
-- Light/dark palettes, active colors, marker colors.
-- Part of the Demopaja Timeline port; see timelinebar.lua (entry point).
-- ===========================================================================

local RGB = require('moonnuklear').color_from_bytes

-- Colors: TimeLineBar::GetColor() + Windows system colors.
-- Two palettes; the active one is selected via M.apply(); M.current is the scheme name.
local THEMES = {
   light = {
      TIMELINE_TEXT         = RGB(16, 16, 16),
      TIMELINE_BLACK        = RGB(16, 16, 16),
      TIMELINE_BACK         = RGB(255, 255, 255),
      TIMELINE_BACK_SEL     = RGB(250, 250, 250),
      TIMELINE_DARK_TICK    = RGB(240, 210, 210),
      TIMELINE_LIGHT_TICK   = RGB(240, 240, 240),
      TIMELINE_GRID         = RGB(190, 190, 190),
      TIMELINE_VALUE_GRID   = RGB(196, 196, 196),
      TIMELINE_VALUE_TEXT   = RGB(80, 80, 80),
      TIMELINE_PARAM_FACE   = RGB(196, 196, 196),
      TIMELINE_PARAM_LIGHT  = RGB(225, 225, 225),
      TIMELINE_PARAM_SHADOW = RGB(128, 128, 128),

      -- ::GetSysColor(...) of the standard Windows theme
      BTNFACE       = RGB(212, 208, 200),
      BTNSHADOW     = RGB(128, 128, 128),
      BTNHILIGHT    = RGB(255, 255, 255),
      BTNTEXT       = RGB(0, 0, 0),
      HIGHLIGHT     = RGB(10, 36, 106),
      HIGHLIGHTTEXT = RGB(255, 255, 255),
      WINDOW        = RGB(255, 255, 255),

      TSEG_LAYER_HILIGHT  = RGB(196, 128, 160),   -- DrawTimeSegment: layer
      TSEG_EFFECT_HILIGHT = RGB(128, 160, 196),   -- DrawTimeSegment: effect
      TIMECURSOR_RED      = RGB(255, 15, 0),
   },

   dark = {
      TIMELINE_TEXT         = RGB(224, 226, 230),
      TIMELINE_BLACK        = RGB(208, 210, 214),  -- outlines/curves: light
      TIMELINE_BACK         = RGB(32, 34, 37),
      TIMELINE_BACK_SEL     = RGB(38, 40, 44),
      TIMELINE_DARK_TICK    = RGB(74, 48, 52),     -- measure highlight
      TIMELINE_LIGHT_TICK   = RGB(44, 46, 50),     -- beat highlight / panel background
      TIMELINE_GRID         = RGB(80, 84, 90),
      TIMELINE_VALUE_GRID   = RGB(70, 74, 80),
      TIMELINE_VALUE_TEXT   = RGB(168, 173, 178),
      TIMELINE_PARAM_FACE   = RGB(88, 92, 98),
      TIMELINE_PARAM_LIGHT  = RGB(52, 55, 60),
      TIMELINE_PARAM_SHADOW = RGB(22, 24, 27),

      BTNFACE       = RGB(56, 59, 64),
      BTNSHADOW     = RGB(24, 26, 29),
      BTNHILIGHT    = RGB(98, 103, 110),
      BTNTEXT       = RGB(222, 224, 228),
      HIGHLIGHT     = RGB(56, 98, 180),
      HIGHLIGHTTEXT = RGB(255, 255, 255),
      WINDOW        = RGB(28, 30, 33),

      TSEG_LAYER_HILIGHT  = RGB(158, 96, 128),
      TSEG_EFFECT_HILIGHT = RGB(92, 128, 164),
      TIMECURSOR_RED      = RGB(255, 70, 52),
   },
}

-- The active palette is a separate copy (not a reference into THEMES[...]!):
-- apply() overwrites its contents; the source palettes stay intact.
local Colors = {}
for k, v in pairs(THEMES.light) do Colors[k] = v end

-- Waveform marker colors ([0] is substituted with the palette base on the fly)
local MARKER_COLORS = {
   [0] = Colors.TIMELINE_LIGHT_TICK,     -- base (substituted on the fly)
   [1] = RGB(255, 255, 255),
   [2] = RGB(255, 0, 0),
   [3] = RGB(255, 255, 0),
   [4] = RGB(0, 255, 0),
   [5] = RGB(0, 255, 255),
   [6] = RGB(0, 0, 255),
   [7] = RGB(255, 0, 255),
   [8] = RGB(0, 0, 0),
}

-- Nuklear chrome styles (windows/buttons/menus around the timeline), one
-- per palette, in nk.style_from_table() format. The dark one mirrors the
-- stock nuklear look tuned to the dark timeline palette; the light one is
-- the classic Windows-gray to match the timeline's original colors.
local CHROME = {
   dark = {
      text = RGB(222, 224, 228),
      window = RGB(40, 42, 46),
      header = RGB(32, 34, 38),
      border = RGB(60, 63, 68),
      button = RGB(56, 59, 64),
      button_hover = RGB(66, 70, 76),
      button_active = RGB(76, 80, 87),
      toggle = RGB(56, 59, 64),
      toggle_hover = RGB(66, 70, 76),
      toggle_cursor = RGB(227, 183, 46),   -- freerouting: accent so checked boxes stand out
      select = RGB(50, 53, 58),
      select_active = RGB(56, 98, 180),
      slider = RGB(50, 53, 58),
      slider_cursor = RGB(100, 105, 112),
      slider_cursor_hover = RGB(115, 120, 128),
      slider_cursor_active = RGB(130, 136, 144),
      property = RGB(50, 53, 58),
      edit = RGB(50, 53, 58),
      edit_cursor = RGB(222, 224, 228),
      combo = RGB(50, 53, 58),
      chart = RGB(50, 53, 58),
      chart_color = RGB(56, 98, 180),
      chart_color_highlight = RGB(255, 70, 52),
      scrollbar = RGB(36, 38, 42),
      scrollbar_cursor = RGB(70, 74, 80),
      scrollbar_cursor_hover = RGB(85, 90, 97),
      scrollbar_cursor_active = RGB(100, 105, 112),
      tab_header = RGB(46, 48, 53),
   },
   light = {
      text = RGB(16, 16, 16),
      window = RGB(212, 208, 200),
      header = RGB(185, 182, 175),
      border = RGB(128, 128, 128),
      button = RGB(212, 208, 200),
      button_hover = RGB(228, 225, 218),
      button_active = RGB(200, 196, 188),
      toggle = RGB(190, 186, 178),
      toggle_hover = RGB(228, 225, 218),
      toggle_cursor = RGB(10, 36, 106),
      select = RGB(255, 255, 255),
      select_active = RGB(10, 36, 106),
      slider = RGB(190, 186, 178),
      slider_cursor = RGB(128, 128, 128),
      slider_cursor_hover = RGB(110, 110, 110),
      slider_cursor_active = RGB(90, 90, 90),
      property = RGB(228, 225, 218),
      edit = RGB(255, 255, 255),
      edit_cursor = RGB(16, 16, 16),
      combo = RGB(255, 255, 255),
      chart = RGB(255, 255, 255),
      chart_color = RGB(10, 36, 106),
      chart_color_highlight = RGB(255, 15, 0),
      scrollbar = RGB(236, 233, 226),
      scrollbar_cursor = RGB(190, 186, 178),
      scrollbar_cursor_hover = RGB(170, 166, 158),
      scrollbar_cursor_active = RGB(150, 146, 138),
      tab_header = RGB(200, 196, 188),
   },
}

local M = {
   themes = THEMES,
   colors = Colors,
   marker_colors = MARKER_COLORS,
   chrome = CHROME,
   current = nil,
}

-- Restyle the nuklear chrome (everything outside the custom-drawn
-- timeline) to the active palette. Call once after context creation and
-- again whenever the theme switches.
function M.apply_chrome(ctx)
   local nk = require('moonnuklear')
   nk.style_from_table(ctx, CHROME[M.current] or CHROME.light)
end

-- In-place palette switch: all references to M.colors stay valid.
function M.apply(sTheme)
   M.current = sTheme
   local rSrc = THEMES[sTheme] or THEMES.light
   for k, v in pairs(rSrc) do Colors[k] = v end
end

-- GL clear color behind the window (the preview surround in the editor):
-- a neutral tone dimmer than the chrome, so the layout content is framed.
function M.bgcolor()
   if M.current == 'dark' then
      return {0.08, 0.09, 0.10, 1.0}
   end
   return {0.52, 0.51, 0.49, 1.0}
end

return M
