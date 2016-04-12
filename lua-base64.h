#ifndef LUA_BASE64
#define LUA_BASE64

#include <stdlib.h>

#include "lua/lua.h"
#include "lua/lualib.h"
#include "lua/lauxlib.h"

#define BASE64_LUA "lua-base64"

int luaopen_base64(lua_State *L);

#endif // LUA_BASE64
