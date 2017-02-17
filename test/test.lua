local lu = require "luaunit"
local eq       = lu.assertEquals
local table_eq = lu.assertItemsEquals
local is_true  = lu.assertTrue
local fail     = lu.assertErrorMsgMatches

package.cpath = "../?.dll;"..package.cpath
local conv   = require "pb.conv"
local buffer = require "pb.buffer"
local slice  = require "pb.slice"
local pbio   = require "pb.io"
local pb     = require "pb"

function testConv()
   eq(conv.encode_uint32(-1), 0xFFFFFFFF)
   eq(conv.decode_uint32(0xFFFFFFFF), 0xFFFFFFFF)
   eq(conv.decode_uint32(conv.encode_uint32(-1)), 0xFFFFFFFF)

   local uint32 = conv.encode_uint32
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

   eq(conv.decode_float(conv.encode_float(123.125)), 123.125)
   eq(conv.decode_double(conv.encode_double(123.125)), 123.125)
end

function testIo()
   assert(pbio.dump("tmp", "a\nb\0c"))
   eq(pbio.read("tmp"), "a\nb\0c")
   assert(pbio.write "\0")
   assert(os.remove "tmp")
end

function testBuffer()
   
end

function testSlice()
   
end

function testLoad()
   assert(pb.loadfile "protos/descriptor.pb")
   assert(pb.loadfile "protos/addressbook.pb")
   assert(pb.loadfile "protos/addressbook.pb")

   local data = assert(pb.decode("google.protobuf.FileDescriptorSet",
                                 pbio.read "protos/descriptor.pb"))
   local str = assert(pb.encode("google.protobuf.FileDescriptorSet", data))
   table_eq(data, pb.decode("google.protobuf.FileDescriptorSet", str))

   local data = assert(pb.decode("google.protobuf.FileDescriptorSet",
                                 pbio.read "protos/addressbook.pb"))
   local str = assert(pb.encode("google.protobuf.FileDescriptorSet", data))
   table_eq(data, pb.decode("google.protobuf.FileDescriptorSet", str))
end

function testPb()
   assert(pb.loadfile "protos/addressbook.pb")
   local addressbook = {
      name = "Alice",
      id = 12345,
      phone = {
         { number = "1301234567" },
         { number = "87654321", type = "WORK" },
      }
   }
   local code = assert(pb.encode("tutorial.Person", addressbook))
   table_eq(addressbook, assert(pb.decode("tutorial.Person", code)))
end

function testDepend()
   assert(pb.loadfile "protos/depend2.pb")
   local t = { dep1 = { id = 1, name = "foo" }, other = 2 }
   local code = assert(pb.encode("Depend2Msg", t))
   table_eq(assert(pb.decode("Depend2Msg", code)), { other = 2 })

   assert(pb.loadfile "protos/depend1.pb")
   local code = assert(pb.encode("Depend2Msg", t))
   table_eq(assert(pb.decode("Depend2Msg", code)), t)
end

function testZExtend()
   pb.clear "google.protobuf.EnumValueOptions"
   assert(pb.loadfile "protos/extend2.pb")
   local t = { ext_name = "foo", id = 10 }
   local code = assert(pb.encode("Extendable", t))
   table_eq(assert(pb.decode("Extendable", code)), { ext_name = "foo" })

   assert(pb.loadfile "protos/extend1.pb")
   local code = assert(pb.encode("Extendable", t))
   table_eq(assert(pb.decode("Extendable", code)), t)

   assert(pb.loadfile "protos/descriptor.pb")
   local data = assert(pb.decode("google.protobuf.FileDescriptorSet",
                                 pbio.read "protos/extend2.pb"))
   eq(data.file[1].enum_type[1].value[1].options.name, "first")
   eq(data.file[1].enum_type[1].value[2].options.name, "second")
   eq(data.file[1].enum_type[1].value[3].options.name, "third")
end

os.exit(lu.LuaUnit.run(), true)

