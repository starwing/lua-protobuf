local pb = require "pb"
local pbio = require "pb.io"
local serpent = require "serpent"

local data = pbio.read "descriptor.pb"
print(#data)

local result = pb.decode(data,
   "google.protobuf.FileDescriptorSet")
--print(serpent.block(result))

local data2 = pb.encode(result,
   "google.protobuf.FileDescriptorSet")
print(#data2)
pbio.dump("descriptor_out.pb", data2)

local result2 = pb.decode(data2,
   "google.protobuf.FileDescriptorSet")

local function sorted_keys(t)
   local keys = {}
   for k,v in pairs(t) do
      keys[#keys+1] = k
   end
   table.sort(keys)
   return keys
end

local function dfs(t1, t2)
   local k1 = sorted_keys(t1)
   local k2 = sorted_keys(t2)
   assert(#k1 == #k2)
   for i = 1, #k1 do
      assert(k1[i] == k2[i])
      local v1, v2 = t1[k1[i]], t2[k2[i]]
      assert(type(v1) == type(v2))
      if type(v1) == "table" then
         dfs(v1, v2)
      else
         assert(type(v1) == "number" or
                type(v1) == "string" or
                type(v1) == "boolean")
         assert(v1 == v2)
      end
   end
end
dfs(result, result2)

print "ok"
