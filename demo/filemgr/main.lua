local mongoose = require("mongoose")
local json = require("json")

----------

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
	elseif event == "close" then
		print(key .. " closed")
	else
		local data = json.decode(obj["data"])
		local msg = {}
		local op = data["op"]
		msg["op"] = op
		mongoose.ws_send(server1, key, json.encode(msgs))
	end
end

----------

server1 = mongoose.create(80, web_handle, ws_handle)
while true do
	server1:poll()
end
