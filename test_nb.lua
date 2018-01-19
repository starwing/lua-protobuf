local pb = require "pb"
local protoc = require "protoc"
local S = require "serpent"


protoc.new():load [[
message foo {
   optional string name = 1;
   repeated string items = 2;
}
]]

for k, v in pb.types() do
   print(k, v)
end

local chunk = assert(pb.encode(
   ".foo", {
      name = "",
      items = { "foo", "bar", "baz", "" }
   }))
print(chunk:gsub(".", function(s)
   return ("%02X "):format(s:byte())
end))
print(S.block(assert(pb.decode(".foo", chunk))))

