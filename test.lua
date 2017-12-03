local pb = require "pb"

pb.loadfile 'descriptor.pb'

for name, a, b in pb.types() do
   print(name, a, b)
end

for name, a, b, c, d in pb.fields "google.protobuf.DescriptorProto" do
   print(name, a, b, c ,d)
end

local pbio = require "pb.io"
local data = assert(pb.decode(".google.protobuf.FileDescriptorSet", pbio.read "descriptor.pb"))
local byte = assert(pb.encode(".google.protobuf.FileDescriptorSet", data))
print(byte:gsub(".", function(ch)
   return ("%02X "):format(string.byte(ch))
end))
local data2 = assert(pb.decode(".google.protobuf.FileDescriptorSet", byte))

print(require "serpent".block(data))
print(require "serpent".block(data2))
