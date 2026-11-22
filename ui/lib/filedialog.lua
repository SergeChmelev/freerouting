-- Reused from the Demopaja timeline port (~/timeline_ai), see its README for credits.
-- ===========================================================================
-- editor/filedialog.lua
-- A modal file browser for the editor: directory navigation, an optional
-- extension filter (only matching files are listed), a type-in filename
-- field and a native close button. Nuklear has no OS dialogs; directories
-- are listed with `ls -1p` (Linux/WSLg — the project's home turf), so no
-- extra Lua modules are needed.
--
--   FileDialog.open{title=, mode='open'|'save', dir=, name=, exts={'dmo'},
--                   on_ok = function(sPath) return bOk, sErr end}
--
-- A single click selects a file, a DOUBLE click opens it and closes the
-- dialog; a click on a directory enters it.
--   FileDialog.active()          -- modal: the host mutes other input
--   FileDialog.draw(ctx, w, h)   -- call every frame from the UI pass
--
-- on_ok returning false + message keeps the dialog open with the error
-- shown (bad file, failed import, ...).
-- Part of the Demopaja full port; see port_notes/architecture.md.
-- ===========================================================================

local nk = require('moonnuklear')

local M = {}

local function S(v) return math.floor(v * (GLOBAL_SCALE or 1) + 0.5) end

-- ------------------------------------------------------------------ listing ---

local function shell_quote(s)
   return "'" .. s:gsub("'", "'\\''") .. "'"
end

local function normalize(sDir)
   if sDir == '' then sDir = '.' end
   -- collapse trailing slashes (keep the root's one)
   sDir = sDir:gsub('/+$', '')
   return sDir == '' and '/' or sDir
end

local function parent_dir(sDir)
   sDir = normalize(sDir)
   if sDir == '/' then return '/' end
   return sDir:match('^(.*)/[^/]+$') or (sDir:sub(1, 1) == '/' and '/' or '.')
end

-- List a directory: dirs and files (sorted by ls, dotfiles skipped), files
-- optionally filtered to the given extensions (case-insensitive).
-- rExts: array like {'tga', 'png'} or nil = everything.
function M.list_dir(sDir, rExts)
   local rExtSet = nil
   if rExts and #rExts > 0 then
      rExtSet = {}
      for _, e in ipairs(rExts) do rExtSet[e:lower()] = true end
   end
   local rDirs, rFiles = {}, {}
   local pPipe = io.popen('ls -1p -- ' .. shell_quote(normalize(sDir)) .. ' 2>/dev/null')
   if not pPipe then return rDirs, rFiles end
   for sLine in pPipe:lines() do
      if sLine:sub(1, 1) ~= '.' then
         if sLine:sub(-1) == '/' then
            rDirs[#rDirs + 1] = sLine:sub(1, -2)
         else
            local sExt = sLine:match('%.([^.]+)$')
            if not rExtSet or (sExt and rExtSet[sExt:lower()]) then
               rFiles[#rFiles + 1] = sLine
            end
         end
      end
   end
   pPipe:close()
   return rDirs, rFiles
end

-- ------------------------------------------------------------------- state ---

local rState = nil            -- nil = no dialog

local function refresh()
   rState.dirs, rState.files = M.list_dir(rState.dir, rState.exts)
end

function M.open(rOpts)
   rState = {
      title = rOpts.title or 'Select file',
      mode = rOpts.mode or 'open',
      dir = normalize(rOpts.dir or os.getenv('PWD') or '.'),
      name = rOpts.name or '',
      exts = rOpts.exts,
      on_ok = rOpts.on_ok or function() return true end,
      error = nil,
      show_pending = true,     -- un-hide the nk window after a previous close
   }
   -- One nk window PER TITLE: nuklear keeps the creation-time title of a
   -- window name forever, so sharing one name across dialog modes left a
   -- stale header (an "Import file" that was really File->Open).
   rState.window = 'filedialog:' .. rState.title
   refresh()
end

-- The host dialog modes live here so the filter invariants are testable
-- headless: mode -> {title, mode, exts (nil = caller supplies)}.
M.MODES = {
   open   = {title = 'Open project',     mode = 'open', exts = {'dmo'}},
   saveas = {title = 'Save project as',  mode = 'save', exts = {'dmo'}},
   import = {title = 'Import file',      mode = 'open', exts = nil},
   music  = {title = 'Load music (wav)', mode = 'open', exts = {'wav'}},
}

-- rOpts: {dir=, name=, exts= (import only), on_ok=}
function M.open_mode(sMode, rOpts)
   local rDef = assert(M.MODES[sMode], 'unknown dialog mode: ' .. tostring(sMode))
   M.open{
      title = rDef.title, mode = rDef.mode,
      exts = rDef.exts or rOpts.exts,
      dir = rOpts.dir, name = rOpts.name, on_ok = rOpts.on_ok,
   }
end

-- the current dialog state, for tests and status displays
function M.state()
   if not rState then return nil end
   -- sel and name are in here so a test can see what a click did, the way
   -- synth4k/ui.lua exposes its probes
   return {title = rState.title, mode = rState.mode, exts = rState.exts,
           dir = rState.dir, window = rState.window, sel = rState.sel,
           name = rState.name, error = rState.error}
end

function M.active() return rState ~= nil end
function M.close() rState = nil end

-- ------------------------------------------------------------------- draw ---

local function confirm()
   local sName = rState.name
   if sName == '' then return end
   -- saving without an extension gets the filter's first one
   if rState.mode == 'save' and rState.exts and rState.exts[1] and
      not sName:match('%.[^.]+$') then
      sName = sName .. '.' .. rState.exts[1]
   end
   local sPath = (rState.dir == '/' and '' or rState.dir) .. '/' .. sName
   local bOk, sErr = rState.on_ok(sPath)
   if bOk then
      rState = nil
   else
      rState.error = sErr or 'failed'
   end
end

function M.draw(ctx, i32WndW, i32WndH)
   if not rState then return end
   local w, h = S(620), S(460)
   local rBounds = {(i32WndW - w) // 2, (i32WndH - h) // 2, w, h}

   if rState.show_pending then
      -- a window closed with the X stays hidden under the same name
      pcall(nk.window_show, ctx, rState.window, 'shown')
      rState.show_pending = nil
   end

   if nk.window_begin(ctx, rState.title, rBounds,
                      nk.WINDOW_TITLE | nk.WINDOW_BORDER | nk.WINDOW_CLOSABLE |
                      nk.WINDOW_NO_SCROLLBAR, rState.window) then
      -- current directory + Up
      nk.layout_row(ctx, 'static', S(26), {S(40), w - S(70)}, 2)
      if nk.button(ctx, nil, 'Up') then
         rState.dir = parent_dir(rState.dir)
         rState.sel = nil
         refresh()
      end
      nk.label(ctx, rState.dir, nk.TEXT_LEFT)

      -- the entry list (scrollable)
      local i32ListH = h - S(26 + 30 + 30 + 30 + 26) - (rState.error and S(24) or 0)
      nk.layout_row_dynamic(ctx, i32ListH, 1)
      if nk.group_begin(ctx, 'fd_list', nk.WINDOW_BORDER) then
         for _, sDir in ipairs(rState.dirs) do
            nk.layout_row_dynamic(ctx, S(20), 1)
            if nk.selectable(ctx, nil, '[' .. sDir .. ']', nk.TEXT_LEFT, false) then
               rState.dir = (rState.dir == '/' and '' or rState.dir) .. '/' .. sDir
               rState.sel = nil
               refresh()
            end
         end
         for _, sFile in ipairs(rState.files) do
            nk.layout_row_dynamic(ctx, S(20), 1)
            local rRow = nk.widget_bounds(ctx)
            local bSel = (rState.sel == sFile)
            local bNow = nk.selectable(ctx, nil, sFile, nk.TEXT_LEFT, bSel)
            -- A DOUBLE CLICK opens it and closes the dialog, which is what
            -- every file dialog does and what this one did not. Nuklear keeps
            -- the double itself (the GL backend feeds the 'double' button), so
            -- there is no timer here. It only counts when the row was ALREADY
            -- the selection -- that is, when the first click of the double
            -- landed on this very row: entering a directory clears the
            -- selection, so the second click of a double cannot reach through
            -- a refreshed list and open whatever slid under the cursor.
            if rState.sel == sFile
               and ctx:is_mouse_click_in_rect('double', rRow) then
               rState.name = sFile
               confirm()
               if not rState then break end
            elseif bNow and not bSel then
               rState.sel = sFile
               rState.name = sFile
            elseif bNow and rState.sel == nil and rState.name == sFile then
               -- clicking the row whose name was typed in confirms it too
               confirm()
               if not rState then break end
            end
         end
         nk.group_end(ctx)
      end
      if not rState then nk.window_end(ctx) return end

      -- filename + the active filter
      nk.layout_row(ctx, 'static', S(26), {S(50), w - S(220), S(130)}, 3)
      nk.label(ctx, 'File:', nk.TEXT_LEFT)
      local sTyped = nk.edit_string(ctx, nk.EDIT_FIELD, rState.name, 256, 'default')
      if sTyped ~= rState.name then
         rState.name = sTyped
         rState.sel = nil
      end
      nk.label(ctx, rState.exts and ('*.' .. table.concat(rState.exts, ' *.')) or '*.*',
               nk.TEXT_RIGHT)

      if rState.error then
         nk.layout_row_dynamic(ctx, S(20), 1)
         nk.label(ctx, rState.error, nk.TEXT_LEFT)
      end

      nk.layout_row_dynamic(ctx, S(26), 2)
      if nk.button(ctx, nil, rState.mode == 'save' and 'Save' or 'Open') then
         confirm()
      end
      if rState and nk.button(ctx, nil, 'Cancel') then rState = nil end
   end
   nk.window_end(ctx)

   -- the title-bar X
   if rState and nk.window_is_hidden(ctx, rState.window) then
      rState = nil
   end
end

return M
