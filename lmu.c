#include <stdio.h>
#include <stdlib.h>

#include "lua-mongoose.h"
#include "lua-unqlite.h"
#include "lua-cjson/lua-cjson.h"

#define MAX_BUF_SIZE 4096

void luaL_module(lua_State *L, const char *name, lua_CFunction f)
{
    luaL_getsubtable(L, LUA_REGISTRYINDEX, "_PRELOAD");
    lua_pushcfunction(L, f);
    lua_setfield(L, -2, name);
}

int main(int argc, char* argv[])
{
    lua_State *L = luaL_newstate();
    luaL_openlibs(L);

    luaL_module(L, "mongoose", luaopen_mongoose);
    luaL_module(L, "unqlite", luaopen_unqlite);
    luaL_module(L, "json", luaopen_cjson);

    if(argv[1] != NULL)
    {
        if(luaL_dofile(L, argv[1]) != 0)
        {
            printf("%s\n", lua_tostring(L, -1));
        }
    }
    else
    {
        printf("Lmu Shell. (lua + mongoose + unqlite)\n");
        char buf[MAX_BUF_SIZE];
        strcpy(buf, "print(");
        int rc = 0;
        while(1)
        {
            printf("lmu> ");
            gets(buf + 6);
            if(buf[0] == 0x04 || buf[0] == 0x1a)
            {
                printf("quit.\n");
                break;
            }
            rc = luaL_dostring(L, buf + 6);
            if(rc != 0)
            {
                strcat(buf, ")");
                luaL_dostring(L, buf);
            }
        }
    }

    lua_close(L);
    return 0;
}
