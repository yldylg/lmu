#include "lua-mongoose.h"

#define FLAG_WS_DATA 0
#define FLAG_WS_OPEN 1
#define FLAG_WS_CLOSE 2

static void web_param(lua_State *L, struct mg_connection *conn)
{
    lua_newtable(L);

    lua_pushstring(L, conn->request_method);
    lua_setfield(L, -2, "method");

    lua_pushstring(L, conn->remote_ip);
    lua_setfield(L, -2, "ip");

    lua_pushstring(L, conn->uri);
    lua_setfield(L, -2, "uri");

    lua_pushstring(L, conn->query_string);
    lua_setfield(L, -2, "qs");

    if(conn->content_len > 0)
    {
        lua_pushlstring(L, conn->content, conn->content_len);
        lua_setfield(L, -2, "data");
    }
}

static void ws_param(lua_State *L, struct mg_connection *conn, int flag)
{
    lua_newtable(L);

    lua_pushstring(L, conn->remote_ip);
    lua_setfield(L, -2, "ip");

    lua_pushstring(L, conn->connection_param);
    lua_setfield(L, -2, "key");

    if(flag == FLAG_WS_OPEN)
    {
        lua_pushstring(L, "open");
        lua_setfield(L, -2, "event");
    }
    else if(flag == FLAG_WS_CLOSE)
    {
        lua_pushstring(L, "close");
        lua_setfield(L, -2, "event");
    }
    else if(flag == FLAG_WS_DATA && conn->content_len > 0)
    {
        lua_pushlstring(L, conn->content, conn->content_len);
        lua_setfield(L, -2, "data");
    }
}

static void ws_handler(struct mg_connection *conn, int flag)
{
    if(conn->content_len <= 0
            && flag != FLAG_WS_OPEN
            && flag != FLAG_WS_CLOSE)
        return;

    struct mg_context *ctx = conn->server_param;
    lua_State *L = ctx->vm;

    lua_rawgeti(L, LUA_REGISTRYINDEX , ctx->callbackws);
    ws_param(L, conn, flag);

    lua_pcall(L, 1, 1, 0);
}

static int web_handler(struct mg_connection *conn)
{
    struct mg_context *ctx = conn->server_param;
    lua_State *L = ctx->vm;

    lua_rawgeti(L, LUA_REGISTRYINDEX , ctx->callbackweb);
    web_param(L, conn);

    lua_pcall(L, 1, 1, 0);
    if(lua_isnoneornil(L, -1))
    {
        return -1;
    }

    size_t len;
    const char* ret = luaL_tolstring(L, -1, &len);
    mg_send_data(conn,ret,len);
    return 0;
}

static int event_handler(struct mg_connection *conn, enum mg_event ev)
{
    if(ev == MG_REQUEST)
    {
        if(conn->is_websocket)
        {
            ws_handler(conn, FLAG_WS_DATA);
            return MG_TRUE;
        }
        else
        {
            int ret = web_handler(conn);
            if(ret < 0)
            {
                const char* srvfilepath = strlen(conn->uri) <= 1 ? "index.html" : conn->uri;
                if(srvfilepath[0] == '/')
                    srvfilepath++;
                mg_send_file(conn, srvfilepath, NULL);
                return MG_MORE;
            }
            return MG_TRUE;
        }
    }
    else if(ev == MG_WS_CONNECT)
    {
        conn->connection_param = malloc(25);
        const char* key = mg_get_header(conn, "Sec-WebSocket-Key");
        strncpy(conn->connection_param, key, 24);
        char *p = (char*)(conn->connection_param);
        p[24] = '\0';
        ws_handler(conn, FLAG_WS_OPEN);
        return MG_TRUE;
    }
    else if(ev == MG_CLOSE)
    {
        if(conn->is_websocket)
        {
            ws_handler(conn, FLAG_WS_CLOSE);
        }
        free(conn->connection_param);
        return MG_TRUE;
    }
    else if(ev == MG_AUTH)
    {
        return MG_TRUE;
    }
    return MG_FALSE;
}

static int mg_poll(lua_State *L)
{
    struct mg_context *ctx = luaL_checkudata(L, 1, MONGOOSE_LUA);
    mg_poll_server(ctx->server, 10);
    return MG_FALSE;
}

static int mg_destroy(lua_State *L)
{
    struct mg_context *ctx = luaL_checkudata(L, 1, MONGOOSE_LUA);
    mg_destroy_server(&ctx->server);
    return MG_FALSE;
}

static int ws_send(lua_State *L)
{
    struct mg_context *ctx = luaL_checkudata(L, 1, MONGOOSE_LUA);
    const char* key = luaL_checkstring(L, 2);
    size_t len;
    const char* data = luaL_tolstring(L, 3, &len);

    struct mg_connection *c = NULL;
    for (c = mg_next(ctx->server, NULL); c != NULL; c = mg_next(ctx->server, c))
    {
        if (c->is_websocket && !strncmp(c->connection_param, key, 24))
        {
            mg_websocket_printf(c, WEBSOCKET_OPCODE_TEXT, data);
            break;
        }
    }
    return MG_TRUE;
}

static int mg_create(lua_State *L)
{
    const char* port = luaL_checkstring(L, 1);

    luaL_checktype(L, 2, LUA_TFUNCTION);
    int refws = luaL_ref(L, LUA_REGISTRYINDEX);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    int refweb = luaL_ref(L, LUA_REGISTRYINDEX);

    struct mg_context *ctx = lua_newuserdata(L, sizeof(struct mg_context) );

    struct mg_server *server = mg_create_server(ctx, event_handler);
    const char* err = mg_set_option(server, "listening_port", port);
    if(err)
    {
        luaL_error(L, "%s %s", err, port);
    }

    luaL_getmetatable(L, MONGOOSE_LUA);
    lua_setmetatable(L, -2);

    ctx->server = server;
    ctx->vm = L;
    ctx->callbackweb = refweb;
    ctx->callbackws = refws;

    return MG_TRUE;
}

int luaopen_mongoose(lua_State *L)
{
    static const luaL_Reg mongoose_meta[] =
    {
        {"poll", mg_poll},
        {"__gc", mg_destroy},
        {NULL, NULL}
    };

    static const luaL_Reg mongoose[] =
    {
        {"ws_send", ws_send},
        {"create", mg_create},
        {NULL, NULL}
    };

    luaL_newmetatable(L, MONGOOSE_LUA);
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");
    luaL_setfuncs(L, mongoose_meta, 0);

    luaL_newlib(L, mongoose);
    return MG_TRUE;
}
