-- Headless smoke test of the frcore Lua module (no window needed).
--   lua5.3 ui/test_engine.lua <board.dsn> <out.ses>
package.cpath = './ui/?.so;' .. package.cpath
local fr = require('frcore')
local socket_ok, socket = pcall(require, 'socket')
local function now() return socket_ok and socket.gettime() or os.clock() end
local function sleep(s) if socket_ok then socket.sleep(s) end end

local dsn, ses = arg[1], arg[2]
print(fr.version)
local eng = fr.engine()

local function wait_idle(label)
   local t0 = now()
   local last = ''
   while true do
      local st = eng:status()
      local line = string.format('%-8s stage=%-12s pass=%d unrouted=%d viol=%d %s',
         st.state, st.stage, st.pass, st.unrouted, st.violations, st.message)
      if line ~= last then print(string.format('[%6.2fs] %s', now() - t0, line)); last = line end
      if not st.busy and st.state ~= 'loading' and st.state ~= 'routing' and st.state ~= 'saving' then
         return st
      end
      sleep(0.05)
   end
end

eng:load(dsn)
local st = wait_idle('load')
assert(st.board_loaded, 'load failed: ' .. st.error)
local snap = eng:snapshot(0)
print(('snapshot v%d: %d items, %d layers, %d bytes outline, %d incompletes, bbox %.0f %.0f %.0f %.0f'):format(
   snap.version, snap.items, #snap.layers, #snap.outline, snap.incompletes,
   snap.bbox[1], snap.bbox[2], snap.bbox[3], snap.bbox[4]))
for i, l in ipairs(snap.layers) do
   print(('  layer %d %-10s traces=%d pads=%d areas=%d keepouts=%d (floats)'):format(
      i, l.name, #l.traces // 4, #l.pads // 4, #l.areas // 4, #l.keepouts // 4))
end

eng:route{}
local seen = {}
local t0 = now()
while true do
   local s = eng:snapshot(snap.version)
   if s then snap = s; seen[#seen + 1] = s.version end
   st = eng:status()
   if not st.busy and st.state ~= 'routing' then break end
   sleep(0.05)
end
print(('routed in %.2fs: state=%s unrouted=%d violations=%d, %d live snapshots, last v%d (%d traces, %d vias)'):format(
   now() - t0, st.state, st.unrouted, st.violations, #seen, snap.version, snap.trace_count, snap.via_count))

eng:save(ses)
st = wait_idle('save')
print('save:', st.state, st.message, st.error)

local lines, nxt = eng:logs(0)
print(('%d log lines, e.g.: %s'):format(#lines, lines[#lines] and lines[#lines].text or ''))
eng:close()
print('closed')
