-- Headless test of interactive editing through frcore:
--   lua5.3 ui/test_edit.lua <board.dsn>
-- Routes the first airline by "mouse" (route mode), checks the incomplete count drops, then
-- undoes it and checks the count comes back.
package.cpath = './ui/?.so;' .. package.cpath
local fr = require('frcore')
local socket = require('socket')

local eng = fr.engine()
local function settle()
   for _ = 1, 400 do
      local st = eng:status()
      if not st.busy and st.state ~= 'loading' and st.state ~= 'routing' then
         socket.sleep(0.15)  -- let queued edit events drain
         return eng:status()
      end
      socket.sleep(0.05)
   end
end
local function snap() return eng:snapshot(0) end

eng:load(arg[1])
settle()
local s0 = snap()
print(('loaded: %d incompletes, %d traces'):format(s0.incompletes, s0.trace_count))
local x0, y0, x1, y1 = string.unpack('<ffff', s0.airlines, 1)
print(('first airline: (%.0f, %.0f) -> (%.0f, %.0f)'):format(x0, y0, x1, y1))

eng:edit_mode('route')
eng:mouse('click', x0, y0)            -- start at the pin
local st = settle()
print('after start: routing=', st.edit.routing, st.edit.message)
for i = 1, 20 do                      -- follow the airline with the mouse
   local t = i / 20
   eng:mouse('move', x0 + (x1 - x0) * t, y0 + (y1 - y0) * t)
   socket.sleep(0.02)
end
st = settle()
print('after moves: routing=', st.edit.routing, 'msg=', st.edit.message, 'corner=',
      st.edit.corner_x, st.edit.corner_y)
if st.edit.routing then eng:key('complete') st = settle() end
local s1 = snap()
print(('after route: %d incompletes, %d traces (state %s)'):format(s1.incompletes, s1.trace_count, st.state))

eng:undo()
settle()
local s2 = snap()
print(('after undo: %d incompletes, %d traces'):format(s2.incompletes, s2.trace_count))
eng:redo()
settle()
local s3 = snap()
print(('after redo: %d incompletes, %d traces'):format(s3.incompletes, s3.trace_count))

-- drag mode smoke: press on the start pin, drag a bit, release
eng:edit_mode('drag')
eng:mouse('press', x0, y0)
for i = 1, 5 do eng:mouse('drag', x0 + i * 50, y0) socket.sleep(0.02) end
eng:mouse('release')
st = settle()
local s4 = snap()
print(('after drag: %d incompletes, %d traces, msg=%s'):format(s4.incompletes, s4.trace_count, st.edit.message))

eng:route{}
st = settle()
print(('autoroute after edits: state=%s unrouted=%d violations=%d'):format(st.state, st.unrouted, st.violations))
eng:close()
