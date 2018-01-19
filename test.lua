local lu = require "luaunit"

local pb     = require "pb"
local pbio   = require "pb.io"
local protoc = require "protoc"

for name, a, b in pb.types() do
   print(name, a, b)
end

local assert_not = lu.assertEvalToFalse
local assert_eq  = lu.assertEquals

local function check_msg(name, data)
   local chunk2 = assert(pb.encode(name, data))
   local data2 = assert(pb.decode(name, chunk2))
   assert_eq(data2, data)
end

-- luacheck: globals test_io
test_io = {}
function test_io.setup()
   pbio.dump("address.proto", [[
   message Phone {
      optional string name        = 1;
      optional int64  phonenumber = 2;
   }
   message Person {
      optional string name     = 1;
      optional int32  age      = 2;
      optional string address  = 3;
      repeated Phone  contacts = 4;
   } ]])
end

function test_io.teardown()
   os.remove "address.proto"
   os.remove "address.pb"
end

function test_io.test()
   local chunk = assert(protoc.new():compile(pbio.read "address.proto",
                                             "address.proto"))
   assert(pbio.dump("address.pb", chunk))
   assert(pb.loadfile "address.pb")

   local data = {
      name = "ilse",
      age  = 18,
      contacts = {
         { name = "alice", phonenumber = 12312341234 },
         { name = "bob",   phonenumber = 45645674567 }
      }
   }
   check_msg(".Person", data)
end

-- luacheck: globals test_type
function test_type()
   assert(protoc.new():load [[
   message TestTypes {
      optional double   dv    = 1;
      optional float    fv    = 2;
      optional int64    i64v  = 3;
      optional uint64   u64v  = 4;
      optional int32    i32v  = 5;
      optional uint32   u32v  = 13;
      optional fixed64  f64v  = 6;
      optional fixed32  f32v  = 7;
      optional bool     bv    = 8;
      optional string   sv    = 9;
      optional bytes    btv   = 12;
      optional sfixed32 sf32v = 15;
      optional sfixed64 sf64v = 16;
      optional sint32   s32v  = 17;
      optional sint64   s64v  = 18;
   } ]])

   local data = {
      dv    = 0.125;
      fv    = 0.5;
      i64v  = -12345678901234567;
      u64v  = 12345678901234567;
      i32v  = -2101112222;
      u32v  = 2101112222;
      f64v  = 12345678901234567;
      f32v  = 1231231234;
      bv    = true;
      sv    = "foo";
      btv   = "bar";
      sf32v = -123;
      sf64v = -456;
      s32v  = -1234;
      s64v  = -4321;
   }

   check_msg(".TestTypes", data)
end

-- luacheck: globals test_packed
function test_packed()
   assert(protoc.new():load [[
   message TestPacked {
      repeated int64 packs = 1 [packed=true];
   } ]])

   local data = {
      packs = { 1,2,3,4,-1,-2,3 }
   }

   check_msg(".TestPacked", data)
end



os.exit(lu.LuaUnit.run(), true)
