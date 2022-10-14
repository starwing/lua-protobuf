local lu = require "luaunit"

local pb     = require "pb"
local pbio   = require "pb.io"
local buffer = require "pb.buffer"
local slice  = require "pb.slice"
local conv   = require "pb.conv"
local protoc = require "protoc"

-- local assert_not = lu.assertEvalToFalse
local eq       = lu.assertEquals
local table_eq = lu.assertItemsEquals
local fail     = lu.assertErrorMsgContains
local is_true  = lu.assertIsTrue

local types = 0
for _ in pb.types() do
   types = types + 1
end
pbio.write("pb predefined types: ", tostring(types), "\n")

local function check_load(chunk, name)
   local pbdata = protoc.new():compile(chunk, name)
   local ret, offset = pb.load(pbdata)
   if not ret then
      error("load error at "..offset..
            "\nproto: "..chunk..
            "\ndata: "..buffer(pbdata):tohex())
   end
end

local function check_msg(name, data, r)
   local chunk2, _ = pb.encode(name, data)
   local data2 = pb.decode(name, chunk2)
   --print("msg: ", name, "<"..chunk2:gsub(".", function(s)
      --return ("%02X "):format(s:byte())
   --end)..">")
   eq(data2, r or data)
end

local function withstate(f)
   local old = pb.state(nil)
   local ok, res = pcall(f, old)
   pb.state(old)
   assert(ok, res)
end

_G.test_io = {} do

function _G.test_io.setup()
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

function _G.test_io.teardown()
   os.remove "address.proto"
   os.remove "address.pb"
end

function _G.test_io.test()
   local code = "assert(io.write(require 'pb.io'.read()))"
   assert(pbio.dump("t.lua", code))
   local fh = assert(io.popen(arg[-1].." t.lua < t.lua", "r"))
   eq(fh:read "*a", code)
   fh:close()
   assert(os.remove "t.lua")
   fail("-not-exists-", function() assert(pbio.read "-not-exists-") end)

   local chunk = protoc.new():compile(pbio.read "address.proto",
                                             "address.proto")
   assert(pbio.dump("address.pb", chunk))
   assert(pb.loadfile "address.pb")
   assert(pb.type "Person")
   eq(pb.type "-not-exists-", nil)
   local ft = {}
   for name in pb.fields "Person" do
      ft[name] = true
   end
   table_eq(ft, { name=true, age=true,address=true,contacts=true })

   eq(pb.decode("Person", "\240\255\255\255\255\255\255\255\255\1\1"), {})
   eq(pb.decode("Person", "\240\255\255\255\255\255\255\255\255\255"), {})
   eq(pb.decode("Person", "\240\255\255\255\255"), {})
   eq(pb.decode("Person", "\242\255\255\255\255\1\255"), {})
   eq(pb.decode("Person", "\x71"), {})
   eq(pb.decode("Person", "\x33\1\2\3\4\x34"), {})
   eq(pb.decode("Person", "\x33\1\2\3\4\x44"), {})
   eq(pb.decode("Person", "\x33\1\2\3\4"), {})
   eq(pb.decode("Person", "\x75"), {})
   eq(pb.decode("Person", "\x75\1\1\1\1"), {})

   fail("type 'Foo' does not exists", function() pb.encode("Foo", {}) end)
   fail("type 'Foo' does not exists", function() pb.decode("Foo", "") end)

   fail("string expected for field 'name', got boolean",
        function() pb.encode("Person", { name = true }) end)

   fail("type mismatch for field 'name' at offset 2, bytes expected for type string, got varint",
        function() pb.decode("Person", "\8\1") end)

   fail("invalid varint value at offset 2",
        function() pb.decode("Person", "\16\255") end)

   fail("invalid bytes length: 0 (at offset 2)",
        function() pb.decode("Person", "\10\255") end)

   fail("unfinished bytes (len 10 at offset 3)",
        function() pb.decode("Person", "\10\10") end)

   local data = {
      name = "ilse",
      age  = 18,
      contacts = {
         { name = "alice", phonenumber = 12312341234 },
         { name = "bob",   phonenumber = 45645674567 }
      }
   }
   check_msg(".Person", data)

   pb.clear()
   protoc.reload()

   fail("-not-exists-", function() assert(pb.loadfile "-not-exists-") end)
   assert(pb.type ".google.protobuf.FileDescriptorSet")
end

end

_G.test_depend = {} do

function _G.test_depend.setup()
   pbio.dump("depend1.proto", [[
      syntax = "proto2";

      message Depend1Msg {
          optional int32  id   = 1;
          optional string name = 2;
      } ]])
end

function _G.test_depend.teardown()
   os.remove "depend1.proto"
end

function _G.test_depend.test()
   local function load_depend(p)
      p:load [[
         syntax = "proto2";

         import "depend1.proto";

         message Depend2Msg {
             optional Depend1Msg dep1  = 1;
             optional int32      other = 2;
         } ]]
   end

   load_depend(protoc.new())
   local t = { dep1 = { id = 1, name = "foo" }, other = 2 }
   check_msg("Depend2Msg", t, { dep1 = {}, other = 2 })

   eq(protoc.new():loadfile "depend1.proto", true)
   local chunk = pb.encode("Depend2Msg", t)
   check_msg("Depend2Msg", t)

   pb.clear "Depend1Msg"
   check_msg("Depend2Msg", t, { other = 2 })
   table_eq(pb.decode("Depend2Msg", chunk), { other = 2 })

   eq(protoc.new():loadfile "depend1.proto", true)
   check_msg("Depend2Msg", t)

   pb.clear "Depend1Msg"
   pb.clear "Depend2Msg"

   local p = protoc.new()
   p.include_imports = true
   load_depend(p)
   check_msg("Depend2Msg", t)
   assert(pb.type ".google.protobuf.FileDescriptorSet")
end

end

function _G.test_extend()
   local P = protoc.new()

   P.unknown_import = true
   P.unknown_type = true
   assert(P:load([[
      syntax = "proto2";
      import "descriptor.proto";

      extend google.protobuf.EnumValueOptions {
         optional string name = 51000;
      }

      message Extendable {
         extend NestExtend {
            optional string ext_name = 100;
         }
         optional int32 id = 1;
         extensions 100 to max;
      } ]], "extend1.proto"))
   eq(pb.type "Extendable", ".Extendable")

   P.unknown_import = nil
   P.unknown_type = nil
   local chunk = assert(P:compile [[
      syntax = "proto2";
      import "extend1.proto"

      enum MyEnum {
         First  = 1 [(name) = "first"];
         Second = 2 [(name) = "second"];
         Third  = 3 [(name) = "third"];
      }

      message NestExtend {
         optional uint32 id = 1;
         extensions 100 to max;
      }

      extend Extendable {
         optional string ext_name = 100;
      } ]])
   assert(pb.load(chunk))
   assert(pb.type "MyEnum")

   local t = { ext_name = "foo", id = 10 }
   check_msg("Extendable", t)

   local t2 = { ext_name = "foo", id = 10 }
   check_msg("NestExtend", t2)

   local data = pb.decode("google.protobuf.FileDescriptorSet", chunk)
   eq(data.file[1].enum_type[1].value[1].options.name, "first")
   eq(data.file[1].enum_type[1].value[2].options.name, "second")
   eq(data.file[1].enum_type[1].value[3].options.name, "third")
   assert(pb.type ".google.protobuf.FileDescriptorSet")
end

function _G.test_type()
   pb.clear "not-exists"
   check_load [[
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
   } ]]

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
   pb.clear "TestTypes"

   check_load [[
      message test_type {
         optional uint32 r = 1;
         repeated uint64 r64 = 2;
      }
      message test2 {
        optional test_type test_type = 1;
      } ]]

   pb.option "int64_as_string"
   local data = {
      r64 = {
         1231234123,
         "#45645674567",
         "#18446744073709551615"
      }
   }
   check_msg(".test_type", data)
   pb.option "int64_as_number"

   check_msg("test_type", {r = 1})
   pb.clear "test_type"
   pb.clear "test2"
   assert(pb.type ".google.protobuf.FileDescriptorSet")
end

function _G.test_default()
   withstate(function()
   protoc.reload()
   check_load [[
      enum TestDefaultColor {
         RED = 0;
         GREEN = 1;
         BLUE = 2;
      }
      message TestDefault {
         // some fields here
         optional int32 foo = 1;

         optional uint32 defaulted_uint = 18 [ default = 666 ];
         optional int32 defaulted_int = 10 [ default = 777 ];
         optional bool defaulted_bool = 11 [ default = true ];
         optional string defaulted_str = 12 [ default = "foo" ];
         optional float defaulted_num = 13 [ default = 0.125 ];

         optional TestDefaultColor color = 14 [default = RED];
         optional bool bool1 = 15 [default=false];
         optional bool bool2 = 16 [default=foo];
         repeated int32 array = 17;
      } ]]
   check_msg("TestDefault", { foo = 1 })
   local function copy_no_meta(t)
      local r = {}
      for k, v in pairs(t) do
         if k ~= "__index" then
            r[k] = v
         end
      end
      return r
   end
   pb.option "enum_as_value"
   table_eq(copy_no_meta(pb.defaults "TestDefault"), {
            defaulted_uint = 666,
            defaulted_int = 777,
            defaulted_bool = true,
            defaulted_str = "foo",
            defaulted_num = 0.125,
            color = 0,
            bool1 = false,
            bool2 = nil
         })
   pb.option "enum_as_name"
   pb.defaults("TestDefault", "clear")
   table_eq(copy_no_meta(pb.defaults "TestDefault"), {
            defaulted_uint = 666,
            defaulted_int = 777,
            defaulted_bool = true,
            defaulted_str = "foo",
            defaulted_num = 0.125,
            color = "RED",
            bool1 = false,
            bool2 = nil
         })
   pb.clear "TestDefault"
   pb.clear "TestDefaultColor"
   fail("type not found",
      function() pb.defaults "-invalid-type-" end)

   check_load [[
      syntax = "proto3";
      enum TestDefaultColor {
         RED = 0;
         GREEN = 1;
         BLUE = 2;
      }
      message TestNest{}
      message TestNest1 {
         TestNest nest = 1;
      }
      message TestNest2 {
         TestNest1 nest = 1;
      }
      message TestNest3 {
         TestNest2 nest = 1;
      }
      message TestDefault {
         // some fields here
         int32 foo = 1;

         int32 defaulted_int = 10;
         bool defaulted_bool = 11;
         string defaulted_str = 12;
         float defaulted_num = 13;

         TestDefaultColor color = 14;
         bool bool1 = 15;
         bool bool2 = 16;
         TestNest nest = 17;
         repeated int32 array = 18;
      } ]]

   pb.option "decode_default_message"
   local dt = pb.decode("TestNest3", "")
   table_eq(dt, {nest={nest={nest={}}}})
   pb.option "no_decode_default_message"

   local _, _, _, _, rep = pb.field("TestDefault", "foo")
   eq(rep, "optional")
   table_eq(copy_no_meta(pb.defaults "TestDefault"), {
            defaulted_int = 0,
            defaulted_bool = false,
            defaulted_str = "",
            defaulted_num = 0.0,
            color = "RED",
            bool1 = false,
            bool2 = false,
         })
   pb.option "enum_as_value"
   pb.defaults("TestDefault", "clear")
   table_eq(copy_no_meta(pb.defaults "TestDefault"), {
            defaulted_int = 0,
            defaulted_bool = false,
            defaulted_str = "",
            defaulted_num = 0.0,
            color = 0,
            bool1 = false,
            bool2 = false,
         })

   pb.option "use_default_metatable"
   local dt = pb.decode("TestDefault", "")
   eq(getmetatable(dt), pb.defaults "TestDefault")
   eq(dt.defaulted_int, 0)
   eq(dt.defaulted_bool, false)
   eq(dt.defaulted_str, "")
   eq(dt.defaulted_num, 0.0)
   eq(dt.color, 0)
   eq(dt.bool1, false)
   eq(dt.bool2, false)
   table_eq(dt.array, {})
   table_eq(pb.decode "TestDefault", pb.decode("TestDefault", ""))

   pb.option "use_default_values"
   dt = pb.decode("TestDefault", "")
   eq(getmetatable(dt), nil)
   table_eq(dt, {
            defaulted_int = 0,
            defaulted_bool = false,
            defaulted_str = "",
            defaulted_num = 0.0,
            color = 0,
            bool1 = false,
            bool2 = false,
            array = {}
         })
   eq(dt.defaulted_int, 0)
   eq(dt.defaulted_bool, false)
   eq(dt.defaulted_str, "")
   eq(dt.defaulted_num, 0.0)
   eq(dt.color, 0)
   eq(dt.bool1, false)
   eq(dt.bool2, false)
   table_eq(dt.array, {})

   pb.option "no_default_values"
   pb.option "encode_default_values"
   pb.option "decode_default_array"
   local dt = pb.decode("TestDefault", "")
   eq(getmetatable(dt), nil)
   table_eq(dt,{ array = {} })
   local chunk2, _ = pb.encode("TestDefault", {defaulted_int = 0,defaulted_bool = true})
   local dt = pb.decode("TestDefault", chunk2)
   eq(dt.defaulted_int, 0)
   eq(dt.defaulted_bool, true)
   eq(dt.defaulted_str, nil)
   eq(dt.defaulted_num, nil)
   eq(dt.color, nil)
   eq(dt.bool1, nil)
   eq(dt.bool2, nil)
   table_eq(dt.array, {})

   pb.option "no_encode_default_values"
   pb.option "no_decode_default_array"
   pb.option "no_default_values"

   pb.option "enum_as_name"
   pb.clear "TestDefault"
   pb.clear "TestNest"
   pb.option "auto_default_values"
   end)
end

function _G.test_enum()
   check_load [[
      enum Color {
         Red = 0;
         Green = 1;
         Blue = 2;
      }
      message TestEnum {
         optional Color color  = 1;
      } ]]
   eq(pb.enum("Color", 0), "Red")
   eq(pb.enum("Color", "Red"), 0)
   eq(pb.enum("Color", 1), "Green")
   eq(pb.enum("Color", "Green"), 1)
   local t = {}
   for name, number in pb.fields "Color" do
      t[name] = number
   end
   table_eq(t, {Red=0, Green=1, Blue=2})
   eq({pb.field("TestEnum", "color")}, {"color", 1, ".Color", nil, "optional"})

   local data = { color = "Red" }
   check_msg("TestEnum", data)

   local data2 = { color = 123 }
   check_msg("TestEnum", data2)

   pb.option "enum_as_value"
   check_msg("TestEnum", data, { color = 0 })

   pb.option "int64_as_string"
   check_msg("TestEnum", { color = "#18446744073709551615" })
   pb.option "int64_as_number"

   pb.option "enum_as_name"
   check_msg("TestEnum", data, { color = "Red" })

   fail("invalid varint value at offset 2",
        function() pb.decode("TestEnum", "\8\255") end)
   fail("number/string expected at field 'color', got boolean",
        function() pb.encode("TestEnum", { color = true }) end)
   fail("can not encode unknown enum 'foo' at field 'color'",
        function() pb.encode("TestEnum", { color = "foo" }) end)

   check_load [[
      message TestAlias {
        enum AliasedEnum {
          option allow_alias = true;
          ZERO = 0;
          ONE = 1;
          TWO = 2;
          FIRST = 1;
        }
        repeated AliasedEnum aliased_enumf = 2;
      } ]]
   check_msg("TestAlias",
             { aliased_enumf = { "ZERO", "FIRST", "TWO", 23, "ONE" } },
             { aliased_enumf = { "ZERO", "FIRST", "TWO", 23, "FIRST" } })
   assert(pb.type ".google.protobuf.FileDescriptorSet")
end

function _G.test_packed()
   check_load [[
   message Empty {}
   message TestPacked {
      repeated int64 packs = 1 [packed=true];
   } ]]

   local data = {
      packs = { 1,2,3,4,-1,-2,3 }
   }
   check_msg(".TestPacked", data)
   fail("table expected at field 'packs', got boolean",
        function() pb.encode("TestPacked", { packs = true }) end)

   local data = {packs = {}}
   check_msg(".TestPacked", data, {})

   local hasEmpty
   for _, name in pb.types() do
      if name == "Empty" then
         hasEmpty = true
         break
      end
   end
   assert(hasEmpty)

   pb.clear "TestPacked"
   pb.clear "Empty"
   eq(pb.type("TestPacked"), nil)
   for _, name in pb.types() do
      assert(name ~= "TestPacked", name)
      assert(name ~= "Empty", name)
   end
   eq(pb.types()(nil, "not-exists"), nil)

   check_load [[
      syntax="proto3";

      message MyMessage
      {
          repeated int32 intList = 1;
      } ]]
   local b = pb.encode("MyMessage", { intList = { 1,2,3 } })
   eq(pb.tohex(b), "0A 03 01 02 03")

   check_load [[
      syntax="proto3";

      message MessageA
      {
          int32 intValue = 1;
      }
      message MessageB
      {
          repeated MessageA messageValue = 1;
      } ]]
   pb.option "use_default_values"
   check_msg("MessageB", { messageValue = { { intValue = 0 } } })
   pb.option "no_default_values"
   check_msg("MessageB", { messageValue = { {} } })
   check_msg("MessageB", { messageValue = { { intValue = 1 } } })
   eq(pb.tohex(pb.encode(
      "MessageB", { messageValue = { {} } })), "0A 00")
   eq(pb.tohex(pb.encode(
      "MessageB", { messageValue = { { intValue = 1 } } })), "0A 02 08 01")
   pb.clear "MessageA"
   pb.clear "MessageB"

   check_load [[
      syntax="proto3";
      message Message3
      {
          repeated int32 v1 = 1;
      } ]]
   check_load [[
      message Message2
      {
          repeated int32 v1 = 1;
      } ]]
   local t = { v1 = {1,2,3,4,5} }
   local bytes = pb.encode("Message2", t)
   eq(pb.decode("Message3", bytes), t)
   bytes = pb.encode("Message3", t)
   eq(pb.decode("Message2", bytes), t)
   pb.clear "Message2"
   pb.clear "Message3"
   pb.option "auto_default_values"
   assert(pb.type ".google.protobuf.FileDescriptorSet")
end

function _G.test_map()
   check_load [[
   syntax = "proto3";
   message TestEmpty {}
   message TestNum {
      int32 f = 1;
   }
   message TestMap {
       map<string, int32> map = 1;
       map<string, int32> packed_map = 2 [packed=true];
       map<string, TestEmpty> msg_map = 3;
   } ]]

   check_msg("TestMap", { map = {}, packed_map = {}, msg_map = {} })

   check_msg(".TestMap", {
             map = { one = 1, two = 2, three = 3 };
             packed_map = { one = 1, two = 2, three = 3 }
          }, {
             map = { one = 1, two = 2, three = 3 };
             packed_map = { one = 1, two = 2, three = 3 };
             msg_map = {}
          })

   local data2 = { map = { one = 1, [1]=1 } }
   fail("string expected for field 'key', got number", function()
      local chunk = pb.encode("TestMap", data2)
      table_eq(pb.decode("TestMap", chunk), {
               map = {one = 1},
               packed_map = {},
               msg_map = {},
            })
   end)
   fail("type mismatch for repeated field 'map' at offset 2,"..
        " bytes expected for type message, got varint", function()
      local chunk = pb.encode("TestNum", {f = 123})
      pb.decode("TestMap", chunk)
   end)
   eq(pb.decode("TestMap", "\10\4\3\10\1\1"), {
      map = {["\1"] = 0},
      packed_map = {},
      msg_map = {},
   })
   eq(pb.decode("TestMap", "\10\0"), {
      map = { [""] = 0 },
      packed_map = {},
      msg_map = {}
   })
   eq(pb.decode("TestMap", "\26\0"), {
      map = {},
      packed_map = {},
      msg_map = {[""] = {}}
   })

   check_load [[
   syntax = "proto2";
   message TestMap2 {
       map<string, int32> map = 1;
   } ]]
   check_msg("TestMap2", { map = { one = 1, two = 2, three = 3 } })
end

function _G.test_oneof()
   check_load [[
   syntax = "proto3";
   message TO_M1 {
   }
   message TO_M2 {
   }
   message TO_M3 {
       int32 value = 1;
   }
   message TestOneof {
       oneof body_oneof {
           TO_M1 m1 = 100;
           TO_M2 m2 = 200;
           TO_M3 m3 = 300;
       }
   } ]]
   check_msg("TestOneof", {})
   check_msg("TestOneof", { m1 = {}, body_oneof = "m1" })
   check_msg("TestOneof", { m2 = {}, body_oneof = "m2" })
   check_msg("TestOneof", { m3 = { value = 0 }, body_oneof = "m3" })
   check_msg("TestOneof", { m3 = { value = 10 }, body_oneof = "m3" })
   pb.clear "TestOneof"

   check_load [[
   syntax = "proto3";
   message TestOneof {
      oneof body {
         uint32 foo = 1;
         string bar = 2;
      }
   }
   message Outter {
      TestOneof msg = 1;
   }
   ]]

   check_msg("TestOneof", { foo = 0, body = "foo" })
   check_msg("TestOneof", { bar = "", body = "bar" })
   local chunk = pb.encode("TestOneof", { foo = 0, bar = "" })
   local data = pb.decode("TestOneof", chunk)
   is_true(data.body == "foo" or data.body == "bar")
   check_msg("Outter", { msg = { foo = 0, body = "foo" }})
   check_msg("Outter", { msg = { bar = "", body = "bar" }})
   local chunk = pb.encode("Outter", {msg = { foo = 0, bar = "" }})
   local data = pb.decode("Outter", chunk)
   is_true(data.msg.body == "foo" or data.msg.body == "bar")
   pb.clear "TestOneof"
   pb.clear "Outter"

   check_load [[
   syntax = "proto3";
   message TestOneof {
       oneof test_oneof {
         string name = 4;
         int32  value = 5;
       }
   } ]]

   check_msg("TestOneof", { name = "foo", test_oneof = "name" })
   check_msg("TestOneof", { value = 0, test_oneof = "value" })
   local chunk = pb.encode("TestOneof", { name = "foo", value = 0 })
   local data = pb.decode("TestOneof", chunk)
   is_true(data.test_oneof == "name" or data.test_oneof == "value")

   eq(pb.field("TestOneof", "name"), "name")
   pb.clear("TestOneof", "name")
   eq(pb.field("TestOneof", "name"), nil)
   eq(pb.type "TestOneof", ".TestOneof")
   pb.clear "TestOneof"
   eq(pb.type "TestOneof", nil)
   pb.option "auto_default_values"
   assert(pb.type ".google.protobuf.FileDescriptorSet")
end

function _G.test_conv()
   eq(conv.encode_uint32(-1), 0xFFFFFFFF)
   eq(conv.decode_uint32(0xFFFFFFFF), 0xFFFFFFFF)
   eq(conv.decode_uint32(conv.encode_uint32(-1)), 0xFFFFFFFF)

   pb.option "int64_as_string"
   eq(conv.encode_int32(-1), "#18446744073709551615")
   pb.option "int64_as_number"

   eq(conv.encode_int32(0x12300000123), 0x123)
   eq(conv.encode_int32(0xFFFFFFFF), -1)
   eq(conv.encode_int32(0x123FFFFFFFF), -1)
   eq(conv.encode_int32(0x123FFFFFFFE), -2)
   eq(conv.decode_int32(0x12300000123), 0x123)
   eq(conv.decode_int32(0xFFFFFFFF), -1)
   eq(conv.decode_int32(0x123FFFFFFFF), -1)
   eq(conv.decode_int32(0x123FFFFFFFE), -2)
   eq(conv.decode_int32(conv.encode_int32(-1)), -1)

   eq(conv.encode_sint32(0), 0)
   eq(conv.encode_sint32(-1), 1)
   eq(conv.encode_sint32(1), 2)
   eq(conv.encode_sint32(-2), 3)
   eq(conv.encode_sint32(2), 4)
   eq(conv.encode_sint32(-3), 5)
   eq(conv.encode_sint32(-123), 245)
   eq(conv.encode_sint32(123), 246)
   eq(conv.encode_sint64(-123), 245)
   eq(conv.encode_sint64(123), 246)

   eq(conv.decode_sint32(0), 0)
   eq(conv.decode_sint32(1), -1)
   eq(conv.decode_sint32(2), 1)
   eq(conv.decode_sint32(3), -2)
   eq(conv.decode_sint32(4), 2)
   eq(conv.decode_sint32(5), -3)
   eq(conv.decode_sint32(245), -123)
   eq(conv.decode_sint32(246), 123)
   eq(conv.decode_sint32(0xFFFFFFFF), -0x80000000)
   eq(conv.decode_sint32(0xFFFFFFFE), 0x7FFFFFFF)
   eq(conv.decode_sint64(0xFFFFFFFF), -0x80000000)
   eq(conv.decode_sint64(0xFFFFFFFE), 0x7FFFFFFF)

   eq(conv.decode_float(conv.encode_float(123.125)), 123.125)
   eq(conv.decode_double(conv.encode_double(123.125)), 123.125)

   pb.option "int64_as_string"
   eq(conv.decode_sint64(conv.encode_sint64("#1311768467294899695")), "#1311768467294899695")
   pb.option "int64_as_hexstring"
   eq(conv.decode_sint64(conv.encode_sint64("#0x1234567890ABCDEF")), "#0x1234567890ABCDEF")
   pb.option "int64_as_number"
   if _VERSION == "Lua 5.3" then
      eq(conv.decode_sint64(conv.encode_sint64("#0x1234567890ABCDEF")), 0x1234567890ABCDEF)
   else
      assert(conv.decode_sint64(conv.encode_sint64("#0x1234567890ABCDEF")))
   end

   eq(conv.encode_sint32('---1'), 1)
   fail("number/string expected, got boolean", function() conv.encode_sint64(true) end)
   fail("integer format error: '@xyz'", function() conv.encode_sint64('@xyz') end)
   assert(pb.type ".google.protobuf.FileDescriptorSet")
end

function _G.test_buffer()
   eq(buffer.pack("vvv", 1,2,3), "\1\2\3")
   eq(buffer.tohex(buffer.pack("d", 4294967295)), "FF FF FF FF")
   if _VERSION == "Lua 5.3" then
      eq(buffer.tohex(buffer.pack("q", 9223372036854775807)), "FF FF FF FF FF FF FF 7F")
   else
      eq(buffer.tohex(buffer.pack("q", "#9223372036854775807")), "FF FF FF FF FF FF FF 7F")
   end
   eq(buffer.pack("s", "foo"), "\3foo")
   eq(buffer.pack("cc", "foo", "bar"), "foobar")
   eq(buffer():pack("vvv", 1,2,3):result(), "\1\2\3")

   eq(buffer("foo", "bar"):result(), "foobar")
   eq(buffer.new("foo", "bar"):result(), "foobar")

   eq(pb.fromhex"01 23 456789ABCDEF", "\1\35\69\103\137\171\205\239")

   local b = buffer.new()
   b:pack("b", true);       eq(b:tohex(-1), "01")
   b:pack("f", 0.125);      eq(b:tohex(-4), "00 00 00 3E")
   b:pack("F", 0.125);      eq(b:tohex(-8), "00 00 00 00 00 00 C0 3F")
   b:pack("i", 4294967295); eq(b:tohex(-10), "FF FF FF FF FF FF FF FF FF 01")
   b:pack("j", 4294967295); eq(b:tohex(-1), "01")
   b:pack("u", 4294967295); eq(b:tohex(-5), "FF FF FF FF 0F")
   b:pack("x", 4294967295); eq(b:tohex(-4), "FF FF FF FF")
   b:pack("y", 4294967295); eq(b:tohex(-4), "FF FF FF FF")
   if _VERSION == "Lua 5.3" then
      b:pack("I", 9223372036854775807); eq(b:tohex(-9), "FF FF FF FF FF FF FF FF 7F")
      b:pack("J", 9223372036854775807); eq(b:tohex(-10), "FE FF FF FF FF FF FF FF FF 01")
      b:pack("U", 9223372036854775807); eq(b:tohex(-9), "FF FF FF FF FF FF FF FF 7F")
      b:pack("X", 9223372036854775807); eq(b:tohex(-8), "FF FF FF FF FF FF FF 7F")
      b:pack("Y", 9223372036854775807); eq(b:tohex(-8), "FF FF FF FF FF FF FF 7F")
   else
      b:pack("I", "#9223372036854775807"); eq(b:tohex(-9), "FF FF FF FF FF FF FF FF 7F")
      b:pack("J", "#9223372036854775807"); eq(b:tohex(-10), "FE FF FF FF FF FF FF FF FF 01")
      b:pack("U", "#9223372036854775807"); eq(b:tohex(-9), "FF FF FF FF FF FF FF FF 7F")
      b:pack("X", "#9223372036854775807"); eq(b:tohex(-8), "FF FF FF FF FF FF FF 7F")
      b:pack("Y", "#9223372036854775807"); eq(b:tohex(-8), "FF FF FF FF FF FF FF 7F")
   end
   assert(#b ~= 0)
   assert(#b:reset() == 0)
   assert(tostring(b):match 'pb.Buffer')

   b = buffer.new "foo"
   assert(#b == 3)
   b:delete()
   assert(#b == 0)
   b:pack("vvv", 1,2,3)
   assert(#b == 3)

   b = buffer.new()
   eq(b:pack("(vvv)", 1,2,3):tohex(-4), "03 01 02 03")
   eq(b:pack("((vvv))", 1,2,3):tohex(-5), "04 03 01 02 03")
   fail("unmatch '(' in format", function() buffer.pack "(" end)
   fail("unexpected ')' in format", function() buffer.pack ")" end)
   fail("number expected for type 'int32', got string", function() buffer.pack("i", "foo") end)
   fail("number expected for type 'int32', got boolean", function() buffer.pack("i", true) end)
   fail("invalid formater: '!'", function() buffer.pack '!' end)

   b = buffer.new()
   eq(b:pack("c", ("a"):rep(1025)):result(), ("a"):rep(1025))
   eq(b:pack("c", ("b"):rep(1025)):result(), ("a"):rep(1025)..("b"):rep(1025))
   eq(#b, 2050)
   b:reset("foo", "bar")
   eq(#b, 6)

   fail("integer format error: 'foo'", function() buffer.pack("v", "foo") end)
   if _VERSION == "Lua 5.3" or _VERSION == "Lua 5.4" then
      fail("integer format error", function() buffer.pack("v", 1e308) end)
   else
      fail("number has no integer representation", function() buffer.pack("v", 1e308) end)
   end

   b = buffer.new()
   fail("encode bytes fail", function() b:pack("#", 10) end)
   check_load [[
   message Test { optional int32 value = 1 }
   ]]
   local len = #b
   eq(#b, 0)
   eq(pb.encode("Test", { value = 1 }, b), b)
   eq(#b, 2)
   b:pack("#", len)
   eq(b:tohex(), "02 08 01")

   b = buffer.new()
   eq(b:pack("i", -1):tohex(), "FF FF FF FF FF FF FF FF FF 01")
   assert(pb.type ".google.protobuf.FileDescriptorSet")
end

function _G.test_slice()
   local s = slice.new "\3\1\2\3"
   eq(#s, 4)
   eq(s:level(), 1)
   eq(s:level(1), 1)
   eq(s:level(2), nil)
   eq({s:level(-1)}, {1,1,4})
   eq(s:enter(), s)
   eq(s:level(), 2)
   eq({s:level(-1)}, {2,2,4})
   eq({s:level(1)}, {5,1,4})
   eq({s:unpack "vvv"}, {1,2,3})
   eq(s:unpack "v", nil)
   eq(s:leave(), s)
   eq(s:unpack("+s", -4), "\1\2\3")

   s = slice "\3\1\2\3"
   for i = 2, 20 do
      s:enter(1, 4); eq(s:level(), i)
   end
   for i = 19, 1 do
      s:leave(); eq(s:level(), i)
   end
   eq(s:tohex(), "03 01 02 03")
   eq(s:result(-3), "\1\2\3")
   eq(#s, 4)
   eq(#s:reset(), 0)
   eq(#s:reset"foo", 3)

   eq({slice.unpack("\255\1", "v@")}, { 255, 3 })
   eq({slice.unpack("\1", "v*v", 1)}, { 1, 1 })
   fail("invalid formater: '!'", function() slice.unpack("\1", '!') end)

   table_eq({slice.unpack("\1\2\3", "vvv")}, {1,2,3})
   eq(slice.unpack("\255\255\255\255", "d"), 4294967295)
   if _VERSION == "Lua 5.3" then
      eq(slice.unpack("\255\255\255\255\255\255\255\127", "q"), 9223372036854775807)
   else
      pb.option 'int64_as_string'
      eq(slice.unpack("\255\255\255\255\255\255\255\127", "q"), '#9223372036854775807')
      pb.option 'int64_as_number'
   end
   eq(slice.unpack("\3foo", "s"), "foo")
   eq({slice.unpack("foobar", "cc", 3, 3)}, {"foo", "bar"})

   eq(slice.unpack("\255\255\255\127\255", "v"), 0xFFFFFFF)
   fail("invalid varint value at offset 1", function()
      slice.unpack(("\255"):rep(10), "v") end)
   fail("invalid varint value at offset 1", function() slice.unpack("\255\255\255", "v") end)
   fail("invalid varint value at offset 1", function() slice.unpack("\255\255\255", "v") end)
   fail("invalid bytes value at offset 1", function() slice.unpack("\3\1\2", "s") end)
   fail("invalid fixed32 value at offset 1", function() slice.unpack("\1\2\3", "d") end)
   fail("invalid fixed64 value at offset 1", function() slice.unpack("\1\2\3", "q") end)
   fail("invalid sub string at offset 1", function() slice.unpack("\3\1\2", "c", 5) end)
   fail("invalid varint value at offset 1", function() slice.unpack("\255\255\255", "i") end)
   fail("invalid fixed32 value at offset 1", function() slice.unpack("\255\255\255", "x") end)
   fail("invalid fixed64 value at offset 1", function() slice.unpack("\255\255\255", "X") end)
   fail("string/buffer/slice expected, got boolean", function() slice.unpack(true, "v") end)
   fail("bytes wireformat expected at offset 1", function() slice"\1":enter() end)

   fail("level (3) exceed max level 2", function()
      local s1 = slice.new "\1\2\3"
      s1:enter()
      s1:leave(3)
   end)

   s:reset "\1\2\3"
   eq({s:leave()}, {s, 1})
   eq(s:level(), 1)
   eq({s:level(-1)}, {1,1,3})


   assert(tostring(s):match 'pb.Slice')
   assert(pb.type ".google.protobuf.FileDescriptorSet")
end

function _G.test_typefmt()
   -- load schema from text
   assert(protoc:load [[
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

   -- lua table data
   local data = {
      name = "ilse",
      age  = 18,
      contacts = {
         { name = "alice", phonenumber = 12312341234 },
         { name = "bob",   phonenumber = 45645674567 }
      }
   }

   local bytes = assert(pb.encode("Person", data))
   local s = require "pb.slice".new(bytes)
   local function decode(type, str, d)
      while #str > 0 do
         local _, tag = str:unpack"@v"
         local name, _, pbtype = pb.field(type, math.floor(tag / 8))
         local fmt = pb.typefmt(pbtype)
         if fmt == "message" then
            str:enter()
            if d[name][1] then
               decode(pbtype, str, d[name][1])
               table.remove(d[name], 1)
            end
            str:leave()
         else
            assert(d[name] == str:unpack(fmt))
         end
      end
   end
   decode("Person", s, data)

   assert(pb.typefmt'F' == "double"  )
   assert(pb.typefmt'f' == "float"   )
   assert(pb.typefmt'I' == "int64"   )
   assert(pb.typefmt'U' == "uint64"  )
   assert(pb.typefmt'i' == "int32"   )
   assert(pb.typefmt'X' == "fixed64" )
   assert(pb.typefmt'x' == "fixed32" )
   assert(pb.typefmt'b' == "bool"    )
   assert(pb.typefmt't' == "string"  )
   assert(pb.typefmt'g' == "group"   )
   assert(pb.typefmt'S' == "message" )
   assert(pb.typefmt's' == "bytes"   )
   assert(pb.typefmt'u' == "uint32"  )
   assert(pb.typefmt'v' == "enum"    )
   assert(pb.typefmt'y' == "sfixed32")
   assert(pb.typefmt'Y' == "sfixed64")
   assert(pb.typefmt'j' == "sint32"  )
   assert(pb.typefmt'J' == "sint64"  )

   assert(pb.typefmt"varint"   == 'v')
   assert(pb.typefmt"64bit"    == 'q')
   assert(pb.typefmt"bytes"    == 's')
   assert(pb.typefmt"gstart"   == '!')
   assert(pb.typefmt"gend"     == '!')
   assert(pb.typefmt"32bit"    == 'd')
   assert(pb.typefmt"double"   == 'F')
   assert(pb.typefmt"float"    == 'f')
   assert(pb.typefmt"int64"    == 'I')
   assert(pb.typefmt"uint64"   == 'U')
   assert(pb.typefmt"int32"    == 'i')
   assert(pb.typefmt"fixed64"  == 'X')
   assert(pb.typefmt"fixed32"  == 'x')
   assert(pb.typefmt"bool"     == 'b')
   assert(pb.typefmt"string"   == 't')
   assert(pb.typefmt"group"    == 'g')
   assert(pb.typefmt"bytes"    == 's')
   assert(pb.typefmt"uint32"   == 'u')
   assert(pb.typefmt"enum"     == 'v')
   assert(pb.typefmt"sfixed32" == 'y')
   assert(pb.typefmt"sfixed64" == 'Y')
   assert(pb.typefmt"sint32"   == 'j')
   assert(pb.typefmt"sint64"   == 'J')

   assert(pb.typefmt "whatever" == '!')
end

function _G.test_load()
   withstate(function()
      protoc.reload()
      assert(protoc:load [[ message Test_Load1 { optional int32 t = 1; } ]])
      assert(pb.type "Test_Load1")
      assert(protoc:load [[ message Test_Load2 { optional int32 t = 2; } ]])
      assert(pb.type "Test_Load2")
      protoc.reload()
      local p = protoc.new()
      assert(p:load [[ message Test_Load1 { optional int32 t = 1; } ]])
      assert(pb.type "Test_Load1")
      assert(p:load [[ message Test_Load2 { optional int32 t = 2; } ]])
      assert(pb.type "Test_Load2")
   end)

   withstate(function(old)
   assert(old.setdefault)
   eq(pb.type ".google.protobuf.FileDescriptorSet", nil)
   eq({pb.load "\16\255\255\1\10\2\18\3"}, {false, 8})
   pb.state(nil) -- discard previous one
   eq(pb.type ".google.protobuf.FileDescriptorSet", nil)

   local buf = buffer.new()
   local function v(n) return n*8 + 0 end
   local function s(n) return n*8 + 2 end
   buf:pack("v(v(vsv(vsvvvvv(vvvv)vv)vvv(vv)v(vv)))vv",
            s(1), -- FileDescriptorSet.file
            s(4), -- FileDescriptorProto.message_type
            s(1), -- DescriptorProto.name
            "load_test",
            s(2), -- DescriptorProto.field
            s(1), -- FieldDescriptorProto.name
            "test_unknown",
            v(3), -- FieldDescriptorProto.number
            1,
            v(4), -- FieldDescriptorProto.label
            1,
            s(8), -- FieldDescriptorProto.options
            v(2), -- FieldOptions.packed
            1,
            v(100), 0, -- unknown field options
            v(100), 0, -- unknown field entry
            v(100), 0, -- unknown type entry
            s(8), -- DescriptorProto.oneof_decl
            v(100), 0, -- unknown oneof entry
            s(7), -- DescriptorProto.options
            v(100), 0, -- unknown type options
            v(100), 0 -- unknown file options
            )
   eq(pb.load(buf:result()), true)
   fail("unknown type <unknown>", function() pb.encode("load_test", { test_unknown = 1 }) end)
   fail("<unknown> expected for type <unknown>, got varint", function() pb.decode("load_test", "\8\1") end)
   fail("unknown type <unknown> (0)", function() pb.decode("load_test", "\14\1") end)

   buf:reset()
   buf:pack("v(v(vsv(vsvvvv)))",
            s(1), s(4), s(1), "load_test",
            s(2), s(1), "test_unknown", v(3), 1, v(4), 1)
   eq(pb.load(buf:result()), true)
   fail("type mismatch for field 'test_unknown' at offset 2, <unknown> expected for type <unknown>, got varint",
            function() pb.decode("load_test", "\8\1") end)
   fail("type mismatch for field 'test_unknown' at offset 2, <unknown> expected for type <unknown>, got 64bit",
            function() pb.decode("load_test", "\9\1") end)
   fail("type mismatch for field 'test_unknown' at offset 2, <unknown> expected for type <unknown>, got bytes",
            function() pb.decode("load_test", "\10\1") end)
   fail("type mismatch for field 'test_unknown' at offset 2, <unknown> expected for type <unknown>, got gstart",
            function() pb.decode("load_test", "\11\1") end)
   fail("type mismatch for field 'test_unknown' at offset 2, <unknown> expected for type <unknown>, got gend",
            function() pb.decode("load_test", "\12\1") end)
   fail("type mismatch for field 'test_unknown' at offset 2, <unknown> expected for type <unknown>, got 32bit",
            function() pb.decode("load_test", "\13\1") end)

   buf:reset()
   buf:pack("v(v(vsv(vsvvvv)))",
            s(1), s(4), s(1), "load_test",
            s(2), s(1), "test_unknown", v(3), 2, v(4), 1)
   eq(pb.load(buf:result()), true)

   buf:reset()
   buf:pack("v(v(vsv(vsvvvv)))",
            s(1), s(4), s(1), "load_test",
            s(2), s(1), "test_unknown", v(3), 1, v(4), 1)
   eq(pb.load(buf:result()), true)

   buf:reset()
   buf:pack("v(v(vsv(vsvvvv)))",
            s(1), s(4), s(1), "load_test",
            s(2), s(1), "test_unknown2", v(3), 1, v(4), 1)
   eq(pb.load(buf:result()), true)

   buf:reset()
   buf:pack("v(v(vsv(vsvvvv)))",
            s(1), s(4), s(1), "load_test",
            s(2), s(1), "test_unknown", v(3), 1, v(4), 1)
   eq(pb.load(buf:result()), true)

   buf:reset()
   buf:pack("v(v(vsv(vvvv)))",
            s(1), s(4), s(1), "load_test",
            s(2), v(3), 1, v(4), 1)
   eq(pb.load(buf:result()), false)

   buf:reset()
   buf:pack("v(v(vsv(vvvvvv)))",
            s(1), s(4), s(1), "load_test",
            s(2), v(3), 1, v(4), 1, v(5), 11)
   eq(pb.load(buf:result()), false)

   buf:reset()
   buf:pack("v(v(vsv(vvvv)))",
            s(1), s(4), s(1), "load_test",
            s(6), v(3), 1, v(4), 1)
   eq(pb.load(buf:result()), false)

   buf:reset()
   buf:pack("v(v(v(vx)))", s(1), s(4), s(6), v(3), -1)
   eq({pb.load(buf:result())}, { false, 8 })
   end)
   assert(pb.type ".google.protobuf.FileDescriptorSet")
end

function _G.test_hook()
   withstate(function()
   protoc.reload()
   check_load [[
      enum Type {
         HOME = 1;
         WORK = 2;
      }
      message Phone {
         optional string name        = 1;
         optional int64  phonenumber = 2;
         optional Type   type        = 3;
      }
      message Person {
         optional string name     = 1;
         optional int32  age      = 2;
         optional string address  = 3;
         repeated Phone  contacts = 4;
      } ]]
   pb.option "enable_hooks"
   assert(pb.hook "Phone" == nil)
   fail("function expected, got boolean",
        function() pb.hook("Phone", true) end)
   fail("type not found",
      function() pb.hook "-invalid-type-" end)
   local function make_hook(name, func)
      local fetch = pb.hook(name)
      local function helper(t)
         return func(name, t)
      end
      local oldh = pb.hook(name, helper)
      assert(fetch == oldh)
      assert(pb.hook(name) == helper)
   end
   local s = {}
   make_hook("Person", function(name, t)
      s[#s+1] = ("(%s|%s)"):format(name, t.name)
      t.hooked = true
   end)
   make_hook("Phone", function(name, t)
      s[#s+1] = ("(%s|%s|%s)"):format(name, t.name, t.phonenumber)
      t.hooked = true
      return t
   end)
   make_hook("Type", function(name, t)
      s[#s+1] = ("(%s|%s)"):format(name, t)
      return { type = name, value = t }
   end)
   local data = {
      name = "ilse",
      age  = 18,
      contacts = {
         { name = "alice", type = "HOME", phonenumber = 12312341234 },
         { name = "bob",   type = "WORK", phonenumber = 45645674567 }
      }
   }
   local res = pb.decode("Person", pb.encode("Person", data))
   s = table.concat(s)
   assert(s == "(Type|HOME)(Phone|alice|12312341234)"..
      "(Type|WORK)(Phone|bob|45645674567)"..
      "(Person|ilse)")
   assert(res.hooked)
   assert(res.contacts[1].hooked)
   assert(res.contacts[2].hooked)
   assert(type(res.contacts[1].type) == "table")
   assert(type(res.contacts[2].type) == "table")
   end)
end

function _G.test_encode_hook()
   withstate(function()
   protoc.reload()
   check_load [[
      enum Type {
         HOME = 1;
         WORK = 2;
      }
      message Phone {
         optional string name        = 1;
         optional int64  phonenumber = 2;
         optional Type   type        = 3;
      }
      message Person {
         optional string name     = 1;
         optional int32  age      = 2;
         optional string address  = 3;
         repeated Phone  contacts = 4;
      } ]]
   pb.option "enable_hooks"
   pb.option "enable_enchooks"
   assert(pb.encode_hook "Phone" == nil)
   fail("function expected, got boolean",
        function() pb.encode_hook("Phone", true) end)
   fail("type not found",
      function() pb.encode_hook "-invalid-type-" end)
   local function make_encode_hook(name, func)
      local fetch = pb.encode_hook(name)
      local function helper(t)
         return func(name, t)
      end
      local oldh = pb.encode_hook(name, helper)
      assert(fetch == oldh)
      assert(pb.encode_hook(name) == helper)
   end
   local s = {}
   make_encode_hook("Person", function(name, t)
      s[#s+1] = ("(%s|%s)"):format(name, t.name)
      return t
   end)
   make_encode_hook("Phone", function(name, ph)
      ph_name, ty, num = ph:match("(%w+)|(%w+)|(%d+)")
      t = {
         name = ph_name,
         type = ty,
         phonenumber = tonumber(num),
      }
      s[#s+1] = ("(%s|%s|%s)"):format(name, t.name, t.phonenumber)
      return t
   end)
   make_encode_hook("Type", function(name, v)
      local t = v:lower() == v and "HOME" or "WORK"
      s[#s+1] = ("(%s|(%s)%s)"):format(name, v, t)
      return t
   end)
   local data = {
      name = "ilse",
      age  = 18,
      contacts = {
         "alice|zzz|12312341234",
         "bob|Grr|45645674567",
      }
   }
   local res = pb.decode("Person", pb.encode("Person", data))
   s = table.concat(s)
   assert(s == "(Person|ilse)(Phone|alice|12312341234)"..
          "(Type|(zzz)HOME)(Phone|bob|45645674567)"..
         "(Type|(Grr)WORK)")
   end)
end

function _G.test_unsafe()
   local unsafe = require "pb.unsafe"
   assert(type(unsafe.decode) == "function")
   assert(type(unsafe.use) == "function")
   fail("userdata expected, got boolean",
      function() unsafe.decode("", true, 1)
   end)
   fail("userdata expected, got boolean",
      function() unsafe.slice(true, 1)
   end)
   local s, len = unsafe.touserdata(io.stdin, 0)
   -- s is a null pointer!
   fail("userdata expected, got userdata",
      function() unsafe.slice(s, len)
   end)
   check_load [[
   message TestType {
   }
   ]]
   s, len = unsafe.touserdata("", 0)
   eq(type(s), "userdata")
   eq(len, 0)
   table_eq(unsafe.decode("TestType", s, len), {})
   table_eq(pb.decode("TestType", unsafe.slice(s, len)), {})
   pb.clear "TestType"
   eq((unsafe.use "global"), true)
   eq((unsafe.use "local"), true)
end

function _G.test_order()
   withstate(function()
   protoc.reload()
   check_load [[
      enum Type {
         HOME = 1;
         WORK = 2;
      }
      message Phone {
         optional string name        = 1;
         optional int64  phonenumber = 2;
         optional Type   type        = 3;
      }
      message Person {
         optional string name     = 1;
         optional int32  age      = 2;
         optional string address  = 3;
         repeated Phone  contacts = 4;
      } ]]
   pb.option "encode_order"
   local data = {
      name = "ilse",
      age  = 18,
      contacts = {
         { name = "alice", phonenumber = 12312341234 },
         { name = "bob",   phonenumber = 45645674567 }
      }
   }
   local b1 = pb.encode("Person", data)
   local b2 = pb.encode("Person", data)
   eq(b1, b2)
   end)
end

function _G.test_pack_unpack()
   withstate(function()
   protoc.reload()
   check_load [[
      syntax = "proto3";
      enum Type {
         HOME = 1;
         WORK = 2;
      }
      message Phone {
         string name        = 1;
         int64  phonenumber = 2;
         Type   type        = 3;
      }
      message Friend {
         string name = 1;
         repeated Friend friends = 2;
      }
      message Person {
         // will be sorted by field number

         repeated Friend friends = 200;
         map<string, Phone> map = 100;

         string name     = 1;
         int32  age      = 2;
         string address  = 3;
         repeated Phone  contacts = 4;
      } ]]

   local function __copy(src)
      if "table" ~= type(src) then return src end

      local dst = {}
      for k, v in pairs(src) do
         dst[k] = __copy(v)
      end

      return dst
   end

   local name = "ilse"
   local age  = 18
   local address = "earth"
   local contacts = {
      { name = "alice", phonenumber = 12312341234 },
      { name = "bob",   phonenumber = 45645674567 }
   }
   local map = {
      ["m111111111"] = contacts[1],
      ["m222222222"] = contacts[2]
   }
   local friends = {
      { name = "f10", friends = {
         { name = "f11"}, { name = "f12" }
      }},
      { name = "f20", friends = {
         { name = "f21"}, { name = "f22" }
      }}
   }

   local person = {
      name = name,
      age = age,
      address = address,
      contacts = __copy(contacts),
      map = __copy(map),
      friends = __copy(friends)
   }
   -- fill default value for eq
   for _, m in pairs(person.map) do
      m.type = 0
   end
   for _, m in pairs(person.contacts) do
      m.type = 0
   end
   for _, f in pairs(person.friends) do
      for _, f2 in pairs(f.friends) do
         f2.friends = {}
      end
   end
   local default_contacts = {name = "", phonenumber=0, type=0}
   local default_map = {key = ""}
   local default_friend = {friends = {}, name = ""}

   local b1 = pb.pack("Person", name, age, address, contacts, map, friends)

   local p = pb.decode("Person", b1)
   eq(person, p)

   local n1, a1, e1, c1, m1, f1 = pb.unpack("Person", b1)
   eq(n1, person.name)
   eq(a1, person.age)
   eq(e1, person.address)
   eq(c1, person.contacts)
   eq(m1, person.map)
   eq(f1, person.friends)

   local b2 = pb.pack("Person")
   local n2, a2, e2, c2, m2, f2 = pb.unpack("Person", b2)
   eq(n2, "")
   eq(a2, 0)
   eq(e2, "")
   eq(c2, default_contacts)
   eq(m2, default_map)
   eq(f2, default_friend)

   local b3 = pb.pack("Person", nil, age, nil, contacts, nil)
   local n3, a3, e3, c3, m3, f3 = pb.unpack("Person", b3)
   eq(n3, "")
   eq(a3, person.age)
   eq(e3, "")
   eq(c3, person.contacts)
   eq(m3, default_map)
   eq(f3, default_friend)

   fail("number expected for field 'age', got string",
            function() pb.pack("Person", nil, "abc") end)
   fail("bad argument #2 to 'pack' (string expected for field 'name', got number)",
            function() pb.pack("Person", 100, "abc") end)
   fail("type mismatch for field 'name' at offset 2, bytes expected for type string, got 32bit",
            function() pb.unpack("Person", "\13\1") end)

   local ub = buffer.new()
   pb.pack("Person", ub, nil, age, nil, contacts, nil)

   local us = slice.new(ub:result())
   local n5, a5, e5, c5, m5, f5 = pb.unpack("Person", us)
   eq(n5, "")
   eq(a5, person.age)
   eq(e5, "")
   eq(c5, person.contacts)
   eq(m5, default_map)
   eq(f5, default_friend)

   pb.option "no_default_values"
   local n4, a4, e4, c4, m4, f4 = pb.unpack("Person", b2)
   eq(n4, nil)
   eq(a4, nil)
   eq(e4, nil)
   eq(c4, nil)
   eq(m4, nil)
   eq(f4, nil)

   pb.option "enable_hooks"
   pb.option "enable_enchooks"

   local hook_contacts = {
      { name = "alice", phonenumber = 123456789 },
   }
   local hook_count = 0
   pb.encode_hook("Person", function(v)
      hook_count = hook_count + 1
      eq(true, false) -- won't be called
   end)
   pb.encode_hook("Phone", function(v)
      hook_count = hook_count + 1
      eq(v, hook_contacts[1])
   end)
   pb.hook("Person", function(v)
      hook_count = hook_count + 1
      eq(true, false) -- won't be called
   end)
   pb.hook("Phone", function(v)
      hook_count = hook_count + 1
      eq(v, hook_contacts[1])
   end)
   local b5 = pb.pack("Person", nil, age, nil, hook_contacts)
   local n5, a5 = pb.unpack("Person", b5)

   eq(hook_count, 2)
   pb.option "disable_hooks"
   pb.option "disable_enchooks"

   pb.option "auto_default_values"
   pb.clear()
   protoc.reload()
   check_load [[
      syntax = "proto3";
      enum Type {
         HOME = 1;
         WORK = 2;
      }
      message Phone {
         string name        = 1;
         int64  phonenumber = 2;
         Type   type        = 3;
      }
      message Friend {
         string name = 1;
         repeated Friend friends = 2;
      }
      message Person {
         // will be sorted by field number

         map<string, Phone> map = 100;

         string name     = 1;
         int32  age      = 2;
         repeated Phone  contacts = 4;
      } ]]

      local n1, a1, c1, m1, f1 = pb.unpack("Person", b1)
      eq(n1, person.name)
      eq(a1, person.age)
      -- eq(e1, person.address) -- no address field anymore
      eq(c1, person.contacts)
      eq(m1, person.map)
      -- eq(f1, person.friends) -- no friend field anymore
   end)
end

function _G.test_extend_pack()
   local P = protoc.new()

   assert(P:load([[
      syntax = "proto3";
      message ExtendPackTest {
         int32 id = 200;
         extensions 100 to 199;
      }
   ]], "extend_pack_test.proto"))

   local i = 123456789
   local s = "abcdefghijklmn"
   local b = pb.pack("ExtendPackTest", i)

   assert(P:load([[
      syntax = "proto3";
      import "extend_pack_test.proto"

      extend ExtendPackTest {
         string ext_name = 100;
      }
   ]]))
   local s1, i1 = pb.unpack("ExtendPackTest", b)
   eq(s1, "")
   eq(i1, i)

   local b2 = pb.pack("ExtendPackTest", s, i)
   pb.clear("ExtendPackTest", "ext_name")
   local v1, v2 = pb.unpack("ExtendPackTest", b2)
   eq(v1, i)
   eq(v2, nil)
end

if _VERSION == "Lua 5.1" and not _G.jit then
   lu.LuaUnit.run()
else
   os.exit(lu.LuaUnit.run(), true)
end

-- unixcc: run='rm -f *.gcda; lua test.lua; gcov pb.c'
-- win32cc: run='del *.gcda & lua test.lua & gcov pb.c'
