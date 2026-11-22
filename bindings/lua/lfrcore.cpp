// Lua 5.3 binding of the freerouting C++ routing core.
//
//   local fr = require('frcore')
//   local eng = fr.engine()             -- starts the C++ engine thread
//   eng:load('board.dsn')               -- all commands are asynchronous
//   eng:route{max_passes=100, optimize=true, vias=true, timeout='PT10M', drc=true}
//   eng:cancel()
//   eng:save('board.ses')
//   local st = eng:status()             -- {state=, stage=, pass=, unrouted=, violations=, ...}
//   local snap = eng:snapshot(have)     -- nil if snapshot version == have, else geometry table
//   local lines, nxt = eng:logs(from)   -- {{level=, text=}, ...}, index to continue from
//   eng:close()                         -- stops the engine thread (also done by __gc)
//
// No Lua code ever runs on the engine thread and the engine never calls back into Lua, so the
// interpreter needs no lock and computation does not depend on it.

#include <lua.hpp>

#include <string>
#include <vector>

#include "engine.h"
#include "jrt/threads.h"

namespace {

constexpr const char* kEngineMeta = "frcore.Engine";

struct EngineBox {
  frui::Engine* engine;
};

frui::Engine* checkEngine(lua_State* L) {
  auto* box = static_cast<EngineBox*>(luaL_checkudata(L, 1, kEngineMeta));
  if (box->engine == nullptr) luaL_error(L, "engine is closed");
  return box->engine;
}

void setStr(lua_State* L, const char* key, const std::string& v) {
  lua_pushlstring(L, v.data(), v.size());
  lua_setfield(L, -2, key);
}
void setInt(lua_State* L, const char* key, lua_Integer v) {
  lua_pushinteger(L, v);
  lua_setfield(L, -2, key);
}
void setBool(lua_State* L, const char* key, bool v) {
  lua_pushboolean(L, v);
  lua_setfield(L, -2, key);
}
void setNum(lua_State* L, const char* key, double v) {
  lua_pushnumber(L, v);
  lua_setfield(L, -2, key);
}

int l_engine(lua_State* L) {
  auto* box = static_cast<EngineBox*>(lua_newuserdata(L, sizeof(EngineBox)));
  box->engine = nullptr;
  luaL_setmetatable(L, kEngineMeta);
  std::string err;
  try {
    // The engine lives in uncollectable memory: Lua's userdata (malloc) is not scanned by the
    // collector, so the engine is a GC root of its own.
    box->engine = jrt::new_root<frui::Engine>();
  } catch (const std::exception& e) {
    err = e.what();
  }
  if (!err.empty()) return luaL_error(L, "frcore.engine: %s", err.c_str());
  return 1;
}

int l_close(lua_State* L) {
  auto* box = static_cast<EngineBox*>(luaL_checkudata(L, 1, kEngineMeta));
  if (box->engine != nullptr) {
    jrt::destroy_root(box->engine);  // joins the engine thread
    box->engine = nullptr;
  }
  return 0;
}

int l_load(lua_State* L) {
  frui::Engine* e = checkEngine(L);
  e->load(luaL_checkstring(L, 2));
  return 0;
}

int l_save(lua_State* L) {
  frui::Engine* e = checkEngine(L);
  e->save(luaL_checkstring(L, 2));
  return 0;
}

int l_cancel(lua_State* L) {
  checkEngine(L)->cancel();
  return 0;
}

int l_route(lua_State* L) {
  frui::Engine* e = checkEngine(L);
  frui::RouteOptions o;
  if (lua_istable(L, 2)) {
    if (lua_getfield(L, 2, "max_passes") == LUA_TNUMBER) o.maxPasses = static_cast<int32_t>(lua_tointeger(L, -1));
    lua_pop(L, 1);
    if (lua_getfield(L, 2, "optimize") == LUA_TBOOLEAN) o.optimize = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    if (lua_getfield(L, 2, "vias") == LUA_TBOOLEAN) o.vias = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    if (lua_getfield(L, 2, "timeout") == LUA_TSTRING) o.timeout = std::string(lua_tostring(L, -1));
    lua_pop(L, 1);
    if (lua_getfield(L, 2, "drc") == LUA_TBOOLEAN) o.drc = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
  }
  e->route(o);
  return 0;
}

// eng:edit_mode("select" | "route" | "drag")
int l_edit_mode(lua_State* L) {
  frui::Engine* e = checkEngine(L);
  static const char* const kModes[] = {"select", "route", "drag", nullptr};
  frui::EditEvent ev{frui::EditEvent::Mode};
  ev.value = luaL_checkoption(L, 2, nullptr, kModes);
  e->edit(ev);
  return 0;
}

// eng:mouse("press" | "drag" | "release" | "move" | "click", x, y)  -- board coordinates
int l_mouse(lua_State* L) {
  frui::Engine* e = checkEngine(L);
  static const char* const kKinds[] = {"press", "drag", "release", "move", "click", nullptr};
  static const frui::EditEvent::Kind kMap[] = {frui::EditEvent::Press, frui::EditEvent::Drag,
                                               frui::EditEvent::Release, frui::EditEvent::Move,
                                               frui::EditEvent::Click};
  frui::EditEvent ev{kMap[luaL_checkoption(L, 2, nullptr, kKinds)]};
  ev.x = luaL_optnumber(L, 3, 0);
  ev.y = luaL_optnumber(L, 4, 0);
  e->edit(ev);
  return 0;
}

// eng:key("complete" | "cancel")
int l_key(lua_State* L) {
  frui::Engine* e = checkEngine(L);
  static const char* const kKeys[] = {"complete", "cancel", nullptr};
  int k = luaL_checkoption(L, 2, nullptr, kKeys);
  e->edit(frui::EditEvent{k == 0 ? frui::EditEvent::Complete : frui::EditEvent::Cancel});
  return 0;
}

// eng:set_layer(index)  -- 1-based like the snapshot layer list
int l_set_layer(lua_State* L) {
  frui::Engine* e = checkEngine(L);
  frui::EditEvent ev{frui::EditEvent::Layer};
  ev.value = static_cast<int32_t>(luaL_checkinteger(L, 2)) - 1;
  e->edit(ev);
  return 0;
}

int l_undo(lua_State* L) {
  checkEngine(L)->edit(frui::EditEvent{frui::EditEvent::Undo});
  return 0;
}

int l_redo(lua_State* L) {
  checkEngine(L)->edit(frui::EditEvent{frui::EditEvent::Redo});
  return 0;
}

int l_status(lua_State* L) {
  frui::Status s = checkEngine(L)->status();
  lua_createtable(L, 0, 16);
  setStr(L, "state", s.state);
  setStr(L, "stage", s.stage);
  setStr(L, "message", s.message);
  setStr(L, "design", s.design);
  setStr(L, "error", s.error);
  setInt(L, "pass", s.pass);
  setInt(L, "unrouted", s.unrouted);
  setInt(L, "violations", s.violations);
  setNum(L, "elapsed", s.elapsedMs / 1000.0);
  setInt(L, "version", static_cast<lua_Integer>(s.snapshotVersion));
  setBool(L, "busy", s.busy);
  setBool(L, "board_loaded", s.boardLoaded);
  setBool(L, "routed", s.routed);
  lua_createtable(L, 0, 12);
  setStr(L, "mode", s.editMode);
  setStr(L, "message", s.editMessage);
  setInt(L, "layer", s.editLayer + 1);
  setBool(L, "routing", s.editRouting);
  setBool(L, "dragging", s.editDragging);
  if (s.hasCorner) {
    setNum(L, "corner_x", s.cornerX);
    setNum(L, "corner_y", s.cornerY);
    setInt(L, "pen_half_width", s.penHalfWidth);
  }
  if (s.hasTarget) {
    setNum(L, "target_x", s.targetX);
    setNum(L, "target_y", s.targetY);
    setInt(L, "target_layer", s.targetLayer + 1);
  }
  lua_setfield(L, -2, "edit");
  return 1;
}

int l_snapshot(lua_State* L) {
  frui::Engine* e = checkEngine(L);
  auto have = static_cast<uint64_t>(luaL_optinteger(L, 2, 0));
  // The pointer stays valid while on this (GC-registered) thread's stack: snapshots are
  // immutable and collected only once unreachable.
  const frui::Snapshot* snap = e->snapshotIfNewer(have);
  if (snap == nullptr) {
    lua_pushnil(L);
    return 1;
  }
  lua_createtable(L, 0, 12);
  setInt(L, "version", static_cast<lua_Integer>(snap->version));
  lua_createtable(L, 4, 0);
  lua_pushnumber(L, snap->x0);
  lua_rawseti(L, -2, 1);
  lua_pushnumber(L, snap->y0);
  lua_rawseti(L, -2, 2);
  lua_pushnumber(L, snap->x1);
  lua_rawseti(L, -2, 3);
  lua_pushnumber(L, snap->y1);
  lua_rawseti(L, -2, 4);
  lua_setfield(L, -2, "bbox");
  lua_createtable(L, static_cast<int>(snap->layers.size()), 0);
  for (size_t i = 0; i < snap->layers.size(); ++i) {
    const frui::LayerGeometry& g = snap->layers[i];
    lua_createtable(L, 0, 6);
    setStr(L, "name", g.name);
    setBool(L, "signal", g.signal);
    setStr(L, "traces", g.traces);
    setStr(L, "pads", g.pads);
    setStr(L, "areas", g.areas);
    setStr(L, "keepouts", g.keepouts);
    lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1));
  }
  lua_setfield(L, -2, "layers");
  setStr(L, "vias", snap->vias);
  setStr(L, "outline", snap->outline);
  setStr(L, "airlines", snap->airlines);
  setInt(L, "items", snap->items);
  setInt(L, "trace_count", snap->traces);
  setInt(L, "via_count", snap->viaCount);
  setInt(L, "pin_count", snap->pins);
  setInt(L, "incompletes", snap->incompletes);
  setStr(L, "unit", snap->unit);
  setInt(L, "resolution", snap->resolution);
  return 1;
}

int l_logs(lua_State* L) {
  frui::Engine* e = checkEngine(L);
  auto from = static_cast<uint64_t>(luaL_optinteger(L, 2, 0));
  std::vector<frui::LogLine> lines;
  uint64_t next = e->logs(from, lines);
  lua_createtable(L, static_cast<int>(lines.size()), 0);
  for (size_t i = 0; i < lines.size(); ++i) {
    lua_createtable(L, 0, 2);
    setInt(L, "level", lines[i].level);
    setStr(L, "text", lines[i].text);
    lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1));
  }
  lua_pushinteger(L, static_cast<lua_Integer>(next));
  return 2;
}

const luaL_Reg kEngineMethods[] = {
    {"load", l_load},     {"route", l_route}, {"save", l_save},
    {"cancel", l_cancel}, {"status", l_status}, {"snapshot", l_snapshot},
    {"logs", l_logs},     {"close", l_close},
    {"edit_mode", l_edit_mode}, {"mouse", l_mouse}, {"key", l_key},
    {"set_layer", l_set_layer}, {"undo", l_undo}, {"redo", l_redo}, {nullptr, nullptr},
};

const luaL_Reg kModuleFunctions[] = {
    {"engine", l_engine},
    {nullptr, nullptr},
};

}  // namespace

extern "C" __attribute__((visibility("default"))) int luaopen_frcore(lua_State* L) {
  luaL_newmetatable(L, kEngineMeta);
  lua_newtable(L);
  luaL_setfuncs(L, kEngineMethods, 0);
  lua_setfield(L, -2, "__index");
  lua_pushcfunction(L, l_close);
  lua_setfield(L, -2, "__gc");
  lua_pop(L, 1);

  luaL_newlib(L, kModuleFunctions);
  lua_pushliteral(L, "freerouting-cpp 2.5.1-SNAPSHOT (C++ port)");
  lua_setfield(L, -2, "version");
  return 1;
}
