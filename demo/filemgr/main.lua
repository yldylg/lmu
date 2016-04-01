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
		local ret = {}
		local fst = {}
		local op = data["op"]
		
		if op == "listdir" then
			local path = data["path"]
			local dirs = os.listdir(path)
			for k, v in pairs(dirs) do
				if path ~= "/" then
					v2 = path .. "\\" .. v
				else
					v2 = v
				end
				print(v2)
				local st = os.stat(v2)
				if st ~= nil then
					table.insert(fst, {
						name = v,
						mtime = st["mtime"],
						type =st["isdir"],
						size =st["size"],
						path = v2
					})
				else
					table.insert(fst, {
						name = v,
						mtime = "",
						type = "",
						size = "",
						path = v2
					})
				end
			end
		end
		ret["op"] = op
		ret["data"] = fst
		mongoose.ws_send(server1, key, json.encode(ret))
	end
end

----------

server1 = mongoose.create(8080, web_handle, ws_handle)
while true do
	server1:poll()
end
