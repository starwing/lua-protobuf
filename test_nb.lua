local pb = require "pb"
local protoc = require "protoc"
local S = require "serpent"


protoc.new():load [[
message foo {
   optional string name = 1;
   repeated string items = 2;
   optional fixed64 value = 3;
}
]]

for k, v in pb.types() do
   print(k, v)
end

local chunk = assert(pb.encode(
   ".foo", {
      name = "",
      items = { "foo", "bar", "baz", "" },
      value = 3
   }))
print(chunk:gsub(".", function(s)
   return ("%02X "):format(s:byte())
end))
print(S.block(assert(pb.decode(".foo", chunk))))

