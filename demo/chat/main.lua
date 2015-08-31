local unqlite = require("unqlite")
local mongoose = require("mongoose")
local json = require("json")

----------

local db = unqlite.open("chat-msgs.db")
local usermap = {}

----------

local function load_msgs()
	local count = unqlite.fetch(db, "count")
	count = tonumber(count) or 0;
	local msgs = {}
	for i = 0, count, 1 do
		local msg = unqlite.fetch(db, i)
		table.insert(msgs, msg)
	end
	return msgs
end

local function save_msg(strmsg)
	local count = unqlite.fetch(db, "count")
	count = tonumber(count) or 0;
	unqlite.begin(db)
	unqlite.store(db, count, strmsg)
	unqlite.store(db, "count", count + 1)
	unqlite.commit(db)
end

local function web_handle(ctx)
	if ctx["uri"] == "/abc" then
		return "hello mongoose"
	end
end

local function ws_handle(obj)
	local key = obj["key"]
	local ip = obj["ip"]
	local event = obj["event"]
	if event == "open" then
		print(key .. " openning...")
		local msgs = load_msgs()
		mongoose.ws_send(server1, key, json.encode(msgs))
	elseif event == "close" then
		print(key .. " closed")
		for name, k in pairs(usermap) do
			if key == k then
				usermap[name] = nil
			end
		end
	else
		local data = json.decode(obj["data"])
		local msg = {}
		local op = data["op"]
		msg["op"] = op
		if op == "join" then
			local name = data["name"]
			if usermap[name] == nil then
				msg["ok"] = true
				usermap[name] = key
			end
			mongoose.ws_send(server1, key, json.encode(msg))
		elseif op == "p2p" then
			local keyto = usermap[data["to"]]
			if keyto == nil then
				mongoose.ws_send(server1, key, json.encode(msg))
			else
				msg["from"] = data["from"]
				msg["msg"] = data["msg"]
				strmsg = json.encode(msg)
				mongoose.ws_send(server1, keyto, strmsg)
				save_msg(strmsg)
			end
		elseif op == "all" then
			msg["from"] = data["from"]
			msg["msg"] = data["msg"]
			strmsg = json.encode(msg)
			for name, keyto in pairs(usermap) do
				mongoose.ws_send(server1, keyto, strmsg)
			end
			save_msg(strmsg)
		end
	end
end

----------

server1 = mongoose.create(80, web_handle, ws_handle)
while true do
	server1:poll()
end

unqlite.close(db)

