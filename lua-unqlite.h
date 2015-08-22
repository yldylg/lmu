#ifndef LUA_UNQLITE
#define LUA_UNQLITE

#include "lua/lua.h"
#include "lua/lualib.h"
#include "lua/lauxlib.h"
#include "unqlite/unqlite.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#define UNQLITE_LUA "lua-unqlite"

int luaopen_unqlite(lua_State* L);

#endif // LUA_UNQLITE
