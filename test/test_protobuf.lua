local protobuf = require "pb"
t = protobuf.loadfile "addressbook.pb"

proto = t.file[1]

print(proto.name)
print(proto.package)

message = proto.message_type

for _,v in ipairs(message) do
	print(v.name)
	for _,v in ipairs(v.field or {}) do
		print("\t".. v.name .. " ["..v.number.."] " .. v.label)
	end
end

addressbook = {
	name = "Alice",
	id = 12345,
	phone = {
		{ number = "1301234567" },
		{ number = "87654321", type = "WORK" },
	}
}

local Person = protobuf.type "tutorial.Person"
code = protobuf.encode(addressbook, Person)

decode = protobuf.decode(code, Person)

print(decode.name)
print(decode.id)
for _,v in ipairs(decode.phone) do
	print("\t"..v.number, v.type)
end

--[[
phonebuf = protobuf.pack("tutorial.Person.PhoneNumber number","87654321")
buffer = protobuf.pack("tutorial.Person name id phone", "Alice", 123, { phonebuf })
print(protobuf.unpack("tutorial.Person name id phone", buffer))
--]]
