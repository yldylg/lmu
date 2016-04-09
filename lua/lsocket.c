// lsocket.c

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>

#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#include <process.h>
#include <ws2tcpip.h>
#define MSG_DONTWAIT 0
#define EWOULDBLOCK WSAEWOULDBLOCK
#else
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <sys/select.h>
#include <dirent.h>
#endif

#include "lua.h"
#include "lauxlib.h"

#define LSOCKET "socket"
#define TOSTRING_BUFSIZ 64
#define READER_BUFSIZ 131072
#define LSOCKET_EMPTY "lsocket_empty_table"
#define LSOCKET_TCP "tcp"
#define LSOCKET_UDP "udp"
#define LSOCKET_MCAST "mcast"

typedef struct _lsocket_socket
{
	int sockfd;
	int type;
	int mcast;
	int protocol;
	int listening;
} lSocket;

static lSocket* lsocket_checklSocket(lua_State *L, int index)
{
	lSocket *sock = (lSocket*) luaL_checkudata(L, index, LSOCKET);
	return sock;
}

static int lsocket_islSocket(lua_State *L, int index)
{
	if (lua_isuserdata(L, index))
	{
		if (lua_getmetatable(L, index))
		{
			luaL_getmetatable(L, LSOCKET);
			if (lua_rawequal(L, -1, -2))
			{
				lua_pop(L, 2);
				return 1;
			}
			lua_pop(L, 2);
		}
	}
	return 0;
}

static lSocket* lsocket_pushlSocket(lua_State *L)
{
	lSocket *sock = (lSocket*) lua_newuserdata(L, sizeof(lSocket));
	luaL_getmetatable(L, LSOCKET);
	lua_setmetatable(L, -2);
	return sock;
}

static int lsocket_sock__gc(lua_State *L)
{
	lSocket *sock = (lSocket*) lua_touserdata(L, 1);
	if (sock->sockfd > 0)
		close(sock->sockfd);
	sock->sockfd = -1;
	return 0;
}

static int lsocket_sock__toString(lua_State *L)
{
	lSocket *sock = lsocket_checklSocket(L, 1);
	char buf[TOSTRING_BUFSIZ];
	if (strlen(LSOCKET) + (sizeof(void*) * 2) + 2 + 3 > TOSTRING_BUFSIZ)
		return luaL_error(L, "Whoopsie... the string representation seems to be too long.");
	sprintf(buf, "%s: %p", LSOCKET, sock);
	lua_pushstring(L, buf);
	return 1;
}

static const luaL_Reg lSocket_meta[] =
{
	{"__gc", lsocket_sock__gc},
	{"__tostring", lsocket_sock__toString},
	{0, 0}
};

static int lsocket_error(lua_State *L, const char *msg)
{
	lua_pushnil(L);
	lua_pushstring(L, msg);
	return 2;
}

static int _initsocket(lSocket *sock, int type, int mcast, int protocol, int listening)
{
	if (sock->sockfd == -1)
		return 0;

	int on = 1;
	setsockopt(sock->sockfd, SOL_SOCKET, SO_REUSEADDR, (void*)&on, sizeof(on));
	sock->type = type;
	sock->mcast = mcast;
	sock->protocol = protocol;
	sock->listening = listening;

	return 0;
}

static const char *_addr2string(struct sockaddr *sa)
{
	const char *s = inet_ntoa(*(struct in_addr*)sa);
	return s;
}

static uint16_t _portnumber(struct sockaddr *sa)
{
	uint16_t port = ((struct sockaddr_in*)sa)->sin_port;
	return ntohs(port);
}

static int _needsnolookup(const char *addr)
{
	int len = strlen(addr);
	int pfx = strspn(addr, "0123456789.");
	if (pfx != len)
	{
		pfx = strspn(addr, "0123456789abcdefABCDEF:");
		if (addr[pfx] == '.')
			pfx += strspn(addr + pfx, "0123456789.");
	}
	return pfx == len;
}

static int _gethostaddr(lua_State *L, const char *addr, int type, int port, int *protocol, struct sockaddr *sa, socklen_t *slen)
{
	struct addrinfo hint, *info =0;
	memset(&hint, 0, sizeof(hint));
	hint.ai_family = AF_UNSPEC;
	hint.ai_socktype = type;
	if (_needsnolookup(addr))
		hint.ai_flags = AI_NUMERICHOST;

	int err = getaddrinfo(addr, 0, &hint, &info);
	if (err != 0)
	{
		if (info) freeaddrinfo(info);
		return lsocket_error(L, gai_strerror(err));
	}
	else if (info->ai_family != AF_INET)
	{
		if (info) freeaddrinfo(info);
		return lsocket_error(L, "unknown address family");
	}

	*slen = info->ai_addrlen;
	*protocol = info->ai_protocol;
	memcpy(sa, info->ai_addr, *slen);
	((struct sockaddr_in*) sa)->sin_port = htons(port);

	freeaddrinfo(info);
	return 0;
}

static int lsocket_bind(lua_State *L)
{
	char sabuf[sizeof(struct sockaddr)];
	struct sockaddr *sa = (struct sockaddr*) sabuf;
	socklen_t slen = sizeof(sabuf);
	int protocol = 0;
	int mcast = 0;
	const char *addr = NULL;
	int type = SOCK_STREAM;
	int top = 1;

	if (lua_type(L, top) == LUA_TSTRING)
	{
		const char *str = lua_tostring(L, top);
		if (!strcasecmp(str, LSOCKET_TCP))
		{
			top += 1;
		}
		else if (!strcasecmp(str, LSOCKET_UDP))
		{
			type = SOCK_DGRAM;
			top += 1;
		}
		else if (!strcasecmp(str, LSOCKET_MCAST))
		{
			type = SOCK_DGRAM;
			mcast = 1;
			top += 1;
		}
	}

	if (lua_type(L, top) == LUA_TSTRING)
	{
		addr = lua_tostring(L, top++);
	}

	int port = luaL_checknumber(L, top++);
	int backlog = luaL_optnumber(L, top, 5);

	if (addr)
	{
		int err = _gethostaddr(L, addr, type, port, &protocol, sa, &slen);
		if (err) return err;
	}
	else
	{
		memset(&sabuf, 0, sizeof(sabuf));
		struct sockaddr_in *si = (struct sockaddr_in*) sa;
		sa->sa_family = AF_INET;
		si->sin_addr.s_addr = INADDR_ANY;
		si->sin_port = htons(port);
		slen = sizeof(struct sockaddr_in);
	}

	lSocket *sock = lsocket_pushlSocket(L);
	sock->sockfd = socket(AF_INET, type, protocol);
	_initsocket(sock, type, mcast, protocol, 1);

	if (mcast)
	{
		if (setsockopt(sock->sockfd, SOL_SOCKET, SO_BROADCAST, (void*)&mcast, sizeof(mcast)) < 0)
			return lsocket_error(L, strerror(errno));
	}

	if (bind(sock->sockfd, sa, slen) < 0)
		return lsocket_error(L, strerror(errno));

	if (type == SOCK_STREAM)
		if (listen(sock->sockfd, backlog) < 0)
			return lsocket_error(L, strerror(errno));

	return 1;
}

static int lsocket_connect(lua_State *L)
{
	int type = SOCK_STREAM;
	int top = 1;
	char sabuf[sizeof(struct sockaddr_in6)];
	struct sockaddr *sa = (struct sockaddr*) sabuf;
	socklen_t slen = sizeof(sabuf);
	int protocol = 0;
	int mcast = 0;

	if (lua_type(L, top) == LUA_TSTRING)
	{
		const char *str = lua_tostring(L, 1);
		if (!strcasecmp(str, LSOCKET_TCP))
			top += 1;
		else if (!strcasecmp(str, LSOCKET_UDP))
		{
			type = SOCK_DGRAM;
			top += 1;
		}
		else if (!strcasecmp(str, LSOCKET_MCAST))
		{
			type = SOCK_DGRAM;
			mcast = 1;
			top += 1;
		}
	}

	const char *addr = luaL_checkstring(L, top);
	int port = luaL_checknumber(L, top + 1);
	int ttl = luaL_optnumber(L, top + 2, 1);

	int err = _gethostaddr(L, addr, type, port, &protocol, sa, &slen);
	if (err) return err;

	lSocket *sock = lsocket_pushlSocket(L);
	sock->sockfd = socket(AF_INET, type, protocol);
	_initsocket(sock, type, mcast, protocol, 0);
	if (mcast)
	{
		int ok;
		if (setsockopt(sock->sockfd, SOL_SOCKET, SO_BROADCAST, (void*)&mcast, sizeof(mcast)) < 0)
			return lsocket_error(L, strerror(errno));

		ok = setsockopt(sock->sockfd, IPPROTO_IP, IP_MULTICAST_TTL, (void*)&ttl, sizeof(ttl));
		if (ok < 0)
			return lsocket_error(L, strerror(errno));
	}

	if (connect(sock->sockfd, sa, slen) < 0)
		return lsocket_error(L, strerror(errno));

	return 1;
}

static void _push_sockname(lua_State *L, struct sockaddr *sa)
{
	const char *s;
	lua_pushliteral(L, "port");
	lua_pushnumber(L, _portnumber(sa));
	lua_rawset(L, -3);
	lua_pushliteral(L, "addr");
	s = _addr2string(sa);
	if (s)
	{
		lua_pushstring(L, s);
		lua_rawset(L, -3);
	}
	else
	{
		lua_pop(L, 1);
	}
}

static int lsocket_sock_info(lua_State *L)
{
	lSocket *sock = lsocket_checklSocket(L, 1);
	const char *which = luaL_optstring(L, 2, NULL);
	char sabuf [sizeof(struct sockaddr)];
	struct sockaddr *sa = (struct sockaddr*) sabuf;
	socklen_t slen = sizeof(sabuf);

	lua_newtable(L);

	if (which == NULL)
	{
		lua_pushliteral(L, "fd");
		lua_pushnumber(L, sock->sockfd);
		lua_rawset(L, -3);

		lua_pushliteral(L, "type");
		switch (sock->type)
		{
		case SOCK_STREAM:
			lua_pushliteral(L, LSOCKET_TCP);
			break;
		case SOCK_DGRAM:
			lua_pushliteral(L, LSOCKET_UDP);
			break;
		default:
			lua_pushliteral(L, "unknown");
		}
		lua_rawset(L, -3);

		lua_pushliteral(L, "listening");
		lua_pushboolean(L, sock->listening);
		lua_rawset(L, -3);

		lua_pushliteral(L, "multicast");
		lua_pushboolean(L, sock->mcast);
		lua_rawset(L, -3);
	}
	else if (!strcasecmp(which, "peer"))
	{
		if (getpeername(sock->sockfd, sa, &slen) >= 0)
			_push_sockname(L, sa);
		else
		{
			lua_pop(L, 1);
			lua_pushnil(L);
		}
	}
	else if (!strcasecmp(which, "socket"))
	{
		if (getsockname(sock->sockfd, sa, &slen) >= 0)
			_push_sockname(L, sa);
		else
		{
			lua_pop(L, 1);
			lua_pushnil(L);
		}
	}
	else
	{
		lua_pop(L, 1);
		lua_pushnil(L);
	}
	return 1;
}

static int _canacceptdata(int fd, int send)
{
	fd_set rfds;
	struct timeval tv;
	int ok = -1;

	FD_ZERO(&rfds);
	FD_SET(fd, &rfds);
	tv.tv_sec = 0;
	tv.tv_usec = 0;

	if (send == 0)
		ok = select(fd + 1, &rfds, NULL, NULL, &tv);
	else
		ok = select(fd + 1, NULL, &rfds, NULL, &tv);

	return ok;
}

static int lsocket_sock_accept(lua_State *L)
{
	lSocket *sock = lsocket_checklSocket(L, 1);
	if (!_canacceptdata(sock->sockfd, 0))
	{
		lua_pushboolean(L, 0);
		return 1;
	}

	char sabuf[sizeof(struct sockaddr)];
	struct sockaddr *sa = (struct sockaddr*) sabuf;
	socklen_t slen = sizeof(sabuf);
	int newfd = accept(sock->sockfd, sa, &slen);

	if (newfd < 0)
		return lsocket_error(L, strerror(errno));

	lSocket *nsock = lsocket_pushlSocket(L);
	nsock->sockfd = newfd;
	if (_initsocket(nsock, sock->type, sock->mcast, sock->protocol, 0) == -1)
		return lsocket_error(L, strerror(errno));
	lua_pushstring(L, _addr2string(sa));
	lua_pushnumber(L, _portnumber(sa));
	return 3;
}

static int lsocket_sock_recv(lua_State *L)
{
	lSocket *sock = lsocket_checklSocket(L, 1);

	uint32_t howmuch = luaL_optnumber(L, 2, READER_BUFSIZ);
	if (lua_tonumber(L, 2) > UINT_MAX)
		return luaL_error(L, "bad argument #1 to 'recv' (invalid number)");

	char *buf = malloc(howmuch);
	int nrd = recv(sock->sockfd, buf, howmuch, MSG_DONTWAIT);
	if (nrd < 0)
	{
		free(buf);
		if (errno == EAGAIN || errno == EWOULDBLOCK)
			lua_pushboolean(L, 0);
		else
			return lsocket_error(L, strerror(errno));
	}
	else if (nrd == 0)
		lua_pushnil(L);
	else
	{
		lua_pushlstring(L, buf, nrd);
		free(buf);
	}
	return 1;
}

static int lsocket_sock_recvfrom(lua_State *L)
{
	lSocket *sock = lsocket_checklSocket(L, 1);
	uint32_t howmuch = luaL_optnumber(L, 2, READER_BUFSIZ);
	if (lua_tonumber(L, 2) > UINT_MAX)
		return luaL_error(L, "bad argument #1 to 'recvfrom' (invalid number)");

	char sabuf[sizeof(struct sockaddr_in6)];
	struct sockaddr *sa = (struct sockaddr*) sabuf;
	socklen_t slen = sizeof(sabuf);
	char *buf = malloc(howmuch);
	int nrd = recvfrom(sock->sockfd, buf, howmuch, MSG_DONTWAIT, sa, &slen);
	if (nrd < 0)
	{
		free(buf);
		if (errno == EAGAIN || errno == EWOULDBLOCK)
			lua_pushboolean(L, 0);
		else
			return lsocket_error(L, strerror(errno));
	}
	else if (nrd == 0)
		lua_pushnil(L); /* not possible for udp, so should not get here */
	else
	{
		lua_pushlstring(L, buf, nrd);
		free(buf);
		const char *s = _addr2string(sa);
		if (s)
			lua_pushstring(L, s);
		else
			return lsocket_error(L, strerror(errno)); /* should not happen */
		lua_pushnumber(L, _portnumber(sa));
		return 3;
	}
	return 1;
}

static int lsocket_sock_send(lua_State *L)
{
	lSocket *sock = lsocket_checklSocket(L, 1);
	size_t len;
	const char *data = luaL_checklstring(L, 2, &len);

	int nwr = send(sock->sockfd, data, len, MSG_DONTWAIT);

	if (nwr < 0)
	{
		if (errno == EAGAIN || errno == EWOULDBLOCK)
			lua_pushboolean(L, 0);
		else
			return lsocket_error(L, strerror(errno));
	}
	lua_pushnumber(L, nwr);
	return 1;
}

static int lsocket_sock_sendto(lua_State *L)
{
	lSocket *sock = lsocket_checklSocket(L, 1);
	size_t len;
	const char *data = luaL_checklstring(L, 2, &len);
	const char *addr = luaL_checkstring(L, 3);
	int port = luaL_checknumber(L, 4);
	char sabuf[sizeof(struct sockaddr_in6)];
	struct sockaddr *sa = (struct sockaddr*) sabuf;
	socklen_t slen = sizeof(sabuf);
	int protocol;

	int err = _gethostaddr(L, addr, sock->type, port, &protocol, sa, &slen);
	if (err) return err;

	int nwr = sendto(sock->sockfd, data, len, MSG_DONTWAIT, sa, slen);

	if (nwr < 0)
	{
		if (errno == EAGAIN || errno == EWOULDBLOCK)
			lua_pushboolean(L, 0);
		else
			return lsocket_error(L, strerror(errno));
	}
	lua_pushnumber(L, nwr);
	return 1;
}

static int lsocket_sock_close(lua_State *L)
{
	lSocket *sock = lsocket_checklSocket(L, 1);
	int err = 0;
	if (sock->sockfd >= 0)
		err = close(sock->sockfd);
	sock->sockfd = -1;
	sock->type = -1;
	sock->listening = 0;
	if (err)
		return lsocket_error(L, strerror(errno));
	lua_pushboolean(L, 1);
	return 1;
}

static const struct luaL_Reg lSocket_methods [] =
{
	{"info", lsocket_sock_info},
	{"accept", lsocket_sock_accept},
	{"recv", lsocket_sock_recv},
	{"recvfrom", lsocket_sock_recvfrom},
	{"send", lsocket_sock_send},
	{"sendto", lsocket_sock_sendto},
	{"close", lsocket_sock_close},
	{NULL, NULL}
};

static int _table2fd_set(lua_State *L, int idx, fd_set *s)
{
	int i = 1;
	int maxfd = -1;
	lua_rawgeti(L, idx, i++);
	while (lsocket_islSocket(L, -1))
	{
		lSocket *sock = lsocket_checklSocket(L, -1);
		if (sock->sockfd >= 0)
		{
			FD_SET(sock->sockfd, s);
			if (sock->sockfd > maxfd) maxfd = sock->sockfd;
		}
		lua_pop(L, 1);
		lua_rawgeti(L, idx, i++);
	}

	if (!lua_isnil(L, -1) && !lsocket_islSocket(L, -1))
	{
		lua_pop(L, 1);
		return luaL_error(L, "bad argument to 'select' (tables can only contain sockets)");
	}

	lua_pop(L, 1);
	return maxfd;
}

static int _push_sock_for_fd(lua_State *L, int idx, int fd)
{
	int i = 1;
	lua_rawgeti(L, idx, i++);
	while (lsocket_islSocket(L, -1))
	{
		lSocket *sock = lsocket_checklSocket(L, -1);
		if (sock->sockfd == fd) return 1;
		lua_pop(L, 1);
		lua_rawgeti(L, idx, i++);
	}

	if (!lua_isnil(L, -1) && !lsocket_islSocket(L, -1))
		return luaL_error(L, "bad argument to 'select' (tables can only contain sockets)");

	return 0;
}

static int _fd_set2table(lua_State *L, int idx, fd_set *s, int maxfd)
{
	int i, n = 1;
	lua_newtable(L);
	for (i = 0; i <= maxfd; ++i)
	{
		if (FD_ISSET(i, s))
		{
			if (_push_sock_for_fd(L, idx, i))
				lua_rawseti(L, -2, n++);
			else
				return luaL_error(L, "unexpected file descriptor returned from select");
		}
	}
	return 1;
}

static int lsocket_select(lua_State *L)
{
	fd_set readfd, writefd;
	int hasrd = 0, haswr = 0;
	double timeo = 0;
	struct timeval timeout, *timeop = &timeout;
	int top = 1;
	int maxfd = -1, mfd;
	int nargs = lua_gettop(L);

	FD_ZERO(&readfd);
	FD_ZERO(&writefd);

	if (lua_istable(L, 1))
	{
		mfd = _table2fd_set(L, 1, &readfd);
		if (mfd > maxfd) maxfd = mfd;
		hasrd = 1;
		top = 2;
	}
	if (lua_istable(L, 2))
	{
		mfd = _table2fd_set(L, 2, &writefd);
		if (mfd > maxfd) maxfd = mfd;
		haswr = 1;
		top = 3;
	}

	timeo = luaL_optnumber(L, top, -1);

	if (maxfd < 0 && timeo == -1) return lsocket_error(L, "no open sockets to check and no timeout set");
	if (top < nargs) luaL_error(L, "bad argument to 'select' (invalid option)");

	if (timeo < 0)
	{
		timeop = NULL;
	}
	else
	{
		timeout.tv_sec = (long) timeo;
		timeout.tv_usec = (timeo - timeout.tv_sec) * 1000000;
	}

	int ok = select(maxfd+1, hasrd ? &readfd : NULL, haswr ? &writefd : NULL, NULL, timeop);

	if (ok == 0)
	{
		lua_pushboolean(L, 0);
		return 1;
	}
	else if (ok < 0)
		return lsocket_error(L, strerror(errno));

	int nret = 0;
	if (hasrd)
	{
		_fd_set2table(L, 1, &readfd, maxfd);
		nret = 1;
	}
	if (haswr)
	{
		if (!hasrd)
		{
			lua_pushliteral(L, LSOCKET_EMPTY);
			lua_gettable(L, LUA_REGISTRYINDEX);
		}
		_fd_set2table(L, 2, &writefd, maxfd);
		nret = 2;
	}

	return nret;
}

static int lsocket_resolve(lua_State *L)
{
	const char *name = luaL_checkstring(L, 1);
	struct addrinfo hint, *info =0;
	memset(&hint, 0, sizeof(hint));
	hint.ai_family = AF_UNSPEC;
	if (_needsnolookup(name))
		hint.ai_flags = AI_NUMERICHOST;

	int err = getaddrinfo(name, 0, &hint, &info);
	if (err != 0)
	{
		if (info) freeaddrinfo(info);
		return lsocket_error(L, gai_strerror(err));
	}

	int i = 1;
	lua_newtable(L);
	while (info)
	{
		if (info->ai_family == AF_INET)
		{
			lua_newtable(L);
			lua_pushliteral(L, "addr");
			lua_pushstring(L, _addr2string(info->ai_addr));
			lua_rawset(L, -3);
			lua_rawseti(L, -2, i++);
			info = info->ai_next;
		}
	}

	freeaddrinfo(info);
	return 1;
}

static const struct luaL_Reg lsocket [] =
{
	{"connect", lsocket_connect},
	{"bind", lsocket_bind},
	{"select", lsocket_select},
	{"resolve", lsocket_resolve},
	{NULL, NULL}
};

int luaopen_socket(lua_State *L)
{
#ifdef _WIN32
	WSADATA wd;
	if (WSAStartup(MAKEWORD(2,2), &wd) != 0)
	{
		return 0;
	}
#endif

	luaL_newlib(L, lsocket);

	lua_pushliteral(L, "INADDR_ANY");
	lua_pushliteral(L, "0.0.0.0");
	lua_rawset(L, -3);

	luaL_newmetatable(L, LSOCKET);
	luaL_setfuncs(L, lSocket_meta, 0);

	lua_pushliteral(L, "__index");
	luaL_newlib(L, lSocket_methods);
	lua_rawset(L, -3);

	lua_pushliteral(L, "__type");
	lua_pushstring(L, LSOCKET);
	lua_rawset(L, -3);

	lua_pop(L, 1);

	lua_newtable(L);	/* empty table */
	lua_pushliteral(L, LSOCKET_EMPTY);
	lua_pushvalue(L, -2);
	lua_settable(L, LUA_REGISTRYINDEX);
	lua_pop(L, 1);

	return 1;
}
