local unqlite = require("unqlite")
local mongoose = require("mongoose")

----------

db=unqlite.open("a.db")
--unqlite.store(db,"a","abc")
s=unqlite.fetch(db,"a")
print(s)
unqlite.close(db)

----------

local function web_handle(ctx)
	for k,v in pairs(ctx) do
		print(k,v)
	end
	print()
	return "hello mongoose"
end

local function ws_handle(obj)
	local key = obj["key"]
	local ip = obj["ip"]
	local event = obj["event"]
	local data = obj["data"]
	if event == "open" then
		print(key .. " openning...")
		mongoose.ws_send(server1, key, "{key:'" .. key .. "'}")
	elseif event == "close" then
		print(key .. " closed")
	else
		mongoose.ws_send(server1, key, "hello websocket")
	end
end

----------

server1 = mongoose.create(80, web_handle, ws_handle)
while true do
	server1:poll()
end
