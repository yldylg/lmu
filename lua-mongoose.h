#ifndef LUA_MONGOOSE
#define LUA_MONGOOSE

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include "mongoose/mongoose.h"

#include "lua/lua.h"
#include "lua/lualib.h"
#include "lua/lauxlib.h"

#define MONGOOSE_LUA "lua-mongoose"

struct mg_context
{
    struct mg_server *server;
    lua_State *vm;
    int callbackweb;
    int callbackws;
};

int luaopen_mongoose(lua_State *L);

#endif // LUA_MONGOOSE
