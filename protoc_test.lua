-- protoc_test.lua -- unit tests for protoc.lua, the pure-Lua .proto compiler.
--
-- Run:  lua protoc_test.lua
--
-- Tests whose name ends in `_known_issue` assert behaviour the compiler does
-- not implement yet (currently the schema validation protoc performs, such as
-- rejecting `required` in proto3 or out-of-range field numbers).  They are
-- expected to fail until implemented, and are kept separate so the healthy
-- part of the suite can be run on its own:
--
--   lua protoc_test.lua -x known_issue
--
-- When an issue is fixed, drop the suffix from its test name.

local lu = require "luaunit"

local pb     = require "pb"
local protoc = require "protoc"

local eq        = lu.assertEquals
local fail      = lu.assertErrorMsgContains
local error_any = lu.assertError
local is_true   = lu.assertIsTrue
local is_nil    = lu.assertIsNil

-- every test runs against a fresh pb state with the standard descriptors
-- reloaded, so tests do not leak types/options into one another
local function withstate(f)
   local old = pb.state(nil)
   protoc.reload()
   local ok, err = pcall(f, old)
   pb.state(old)
   assert(ok, err)
end

local function compile_set(p, src, name)
   local bytes = p:compile(src, name)
   return pb.decode(".google.protobuf.FileDescriptorSet", bytes), bytes
end

local function first_message(p, src, name)
   local set = compile_set(p, src, name)
   return set.file[1].message_type[1], set.file[1]
end

local function check_msg(name, data, expected)
   local bytes = pb.encode(name, data)
   eq(pb.decode(name, bytes), expected or data)
end

local function copy_no_meta(t)
   local r = {}
   for k, v in pairs(t) do
      if k ~= "__index" then r[k] = v end
   end
   return r
end

local function withfiles(files, f)
   local names = {}
   for name, content in pairs(files) do
      local fh = assert(io.open(name, "w"))
      assert(fh:write(content))
      fh:close()
      names[#names + 1] = name
   end
   local ok, err = pcall(f)
   for _, name in ipairs(names) do os.remove(name) end
   assert(ok, err)
end

-- ------------------------------------------------------------------ lexer

function _G.test_protoc_lexer_comments()
   withstate(function()
      local m = first_message(protoc.new(), [[
// line comment before syntax
syntax = "proto2"; /* block
                      comment */
message M { // trailing comment
   optional int32 x = 1; /* inline */ optional string y = 2;
} ]], "lexer_comments.proto")
      eq(m.name, "M")
      eq(#m.field, 2)
      eq(m.field[1].name, "x")
      eq(m.field[2].name, "y")
   end)
end

function _G.test_protoc_lexer_integer_literals()
   withstate(function()
      local set = compile_set(protoc.new(), [[
         syntax = "proto2";
         enum E { OCT = 010; HEX = 0x11; NEG = -1; }
         message M { optional int32 a = 1; optional int32 b = 16; }
      ]], "lexer_int.proto")
      local e = set.file[1].enum_type[1]
      eq(e.value[1].number, 8)
      eq(e.value[2].number, 17)
      eq(e.value[3].number, -1)
      local m = set.file[1].message_type[1]
      eq(m.field[1].number, 1)
      eq(m.field[2].number, 16)
   end)
end

function _G.test_protoc_lexer_float_literals()
   withstate(function()
      local m = first_message(protoc.new(), [[
         syntax = "proto2";
         message M {
            optional float  a = 1 [default = 0.125];
            optional float  b = 2 [default = -2.5];
            optional double c = 3 [default = 1000.25];
         }
      ]], "lexer_float.proto")
      eq(m.field[1].default_value, "0.125")
      eq(m.field[2].default_value, "-2.5")
      eq(tonumber(m.field[3].default_value), 1000.25)
   end)
end

-- BUG: the exponent sub-pattern starts with `^` inside a capture, so it
-- matches a literal '^' instead of anchoring: any float written with an
-- exponent (1e3, 1.5e-3) fails to parse.
function _G.test_protoc_lexer_float_exponent()
   withstate(function()
      local m = first_message(protoc.new(), [[
         syntax = "proto2";
         message M {
            optional double a = 1 [default = 1e3];
            optional double b = 2 [default = 1.5e-3];
         }
      ]], "lexer_float_exp.proto")
      eq(tonumber(m.field[1].default_value), 1000)
      eq(tonumber(m.field[2].default_value), 0.0015)
   end)
end

function _G.test_protoc_lexer_string_escapes()
   withstate(function()
      local m = first_message(protoc.new(), [[
         syntax = "proto2";
         message M { optional string s = 1 [default = "tab\there\nnl \\ \'q"]; }
      ]], "lexer_str.proto")
      eq(m.field[1].default_value, "tab\there\nnl \\ 'q")
   end)
end

-- BUG: \xNN and \NNN escapes are decoded with string.byte(number), which
-- stringifies the number first, so "\x41" becomes the text "54", not "A".
function _G.test_protoc_lexer_hex_octal_escapes()
   withstate(function()
      local m = first_message(protoc.new(), [[
         syntax = "proto2";
         message M { optional string s = 1 [default = "\x41\101"]; }
      ]], "lexer_hex.proto")
      eq(m.field[1].default_value, "AA")
   end)
end

-- BUG: the escape patterns are greedy (`%x+`, `%d+`) but protoc reads at most
-- two hex digits and three octal digits: "\x4142" is "A42", "\1011" is "A1".
function _G.test_protoc_lexer_escape_digit_limit()
   withstate(function()
      local m = first_message(protoc.new(), [[
         syntax = "proto2";
         message M {
            optional string a = 1 [default = "\x4142"];
            optional string b = 2 [default = "\1011"];
         }
      ]], "lexer_hex2.proto")
      eq(m.field[1].default_value, "A42")
      eq(m.field[2].default_value, "A1")
   end)
end

-- three octal digits may exceed 0xff: protoc keeps the low byte ("\400" is
-- a NUL, "\777" is 0xff), not an out-of-range string.char() error
function _G.test_protoc_lexer_octal_overflow()
   withstate(function()
      local m = first_message(protoc.new(), [[
         syntax = "proto2";
         message M {
            optional string a = 1 [default = "\400"];
            optional string b = 2 [default = "\777"];
         }
      ]], "lexer_oct.proto")
      eq(m.field[1].default_value, "\0")
      eq(m.field[2].default_value, "\255")
   end)
end

function _G.test_protoc_lexer_error_location()
   withstate(function()
      local p = protoc.new()
      fail("lexer_err.proto:3:", function()
         p:parse('syntax = "proto2";\nmessage M {\n   optional int32 x = ;\n}',
                 "lexer_err.proto")
      end)
   end)
end

-- ------------------------------------------------------- parser structure

function _G.test_protoc_parse_returns_file_descriptor()
   withstate(function()
      local info = protoc.new():parse([[
         syntax = "proto2";
         package demo.pkg;
         message M { optional int32 i = 1; }
      ]], "parse_shape.proto")
      eq(info.name, "parse_shape.proto")
      eq(info.syntax, "proto2")
      eq(info.package, "demo.pkg")
      eq(info.message_type[1].name, "M")
   end)
end

function _G.test_protoc_package_and_type_names()
   withstate(function()
      local p = protoc.new()
      local set, bytes = compile_set(p, [[
         syntax = "proto3";
         package foo.bar;
         message M { int32 x = 1; }
         message N { M m = 1; }
         enum E { ZERO = 0; }
      ]], "pkg.proto")
      local f = set.file[1]
      eq(f.package, "foo.bar")
      eq(f.message_type[1].name, "M")
      eq(f.message_type[2].field[1].type_name, ".foo.bar.M")
      eq(f.enum_type[1].name, "E")
      assert(pb.load(bytes))
      eq(pb.type(".foo.bar.M"), ".foo.bar.M")
      eq(pb.field(".foo.bar.M", "x"), "x")
      check_msg(".foo.bar.N", { m = { x = 1 } })
   end)
end

function _G.test_protoc_nested_types()
   withstate(function()
      local p = protoc.new()
      local set, bytes = compile_set(p, [[
         syntax = "proto2";
         message Outer {
            message Inner { optional int32 i = 1; }
            enum Kind { A = 0; B = 1; }
            optional Inner inner = 1;
            optional Kind kind = 2;
         }
      ]], "nested.proto")
      local outer = set.file[1].message_type[1]
      eq(outer.nested_type[1].name, "Inner")
      eq(outer.enum_type[1].name, "Kind")
      eq(outer.field[1].type_name, ".Outer.Inner")
      eq(outer.field[2].type_name, ".Outer.Kind")
      assert(pb.load(bytes))
      eq(pb.type(".Outer.Inner"), ".Outer.Inner")
      check_msg("Outer", { inner = { i = 1 }, kind = "B" })
   end)
end

function _G.test_protoc_map()
   withstate(function()
      local p = protoc.new()
      local set, bytes = compile_set(p, [[
         syntax = "proto3";
         message M {
            map<string, int32> m = 1;
            map<int32, N> n = 2;
         }
         message N { int32 x = 1; }
      ]], "map.proto")
      local m = set.file[1].message_type[1]
      local entry = m.nested_type[1]
      eq(entry.name, "MEntry")
      eq(entry.options.map_entry, true)
      eq(entry.field[1].name, "key")
      eq(entry.field[1].type, "TYPE_STRING")
      eq(entry.field[2].name, "value")
      eq(entry.field[2].type, "TYPE_INT32")
      eq(m.field[1].type, "TYPE_MESSAGE")
      eq(m.field[1].type_name, ".M.MEntry")
      eq(m.field[1].label, "LABEL_REPEATED")
      eq(m.field[2].type_name, ".M.NEntry")
      eq(m.nested_type[2].field[2].type_name, ".N")
      assert(pb.load(bytes))
      check_msg("M", { m = { a = 1 }, n = { [7] = { x = 2 } } })
      local _, _, kind = pb.type(".M.MEntry")
      eq(kind, "map")
   end)
end

function _G.test_protoc_oneof()
   withstate(function()
      local p = protoc.new()
      local set, bytes = compile_set(p, [[
         syntax = "proto3";
         message M {
            oneof body { int32 a = 1; string b = 2; }
            oneof other { bool c = 3; }
         }
      ]], "oneof.proto")
      local m = set.file[1].message_type[1]
      eq(#m.oneof_decl, 2)
      eq(m.oneof_decl[1].name, "body")
      eq(m.oneof_decl[2].name, "other")
      eq(m.field[1].oneof_index, 0)
      eq(m.field[2].oneof_index, 0)
      eq(m.field[3].oneof_index, 1)
      assert(pb.load(bytes))
      eq(select("#", pb.field("M", "a")), 7)
      local _, _, _, _, _, oname, oidx = pb.field("M", "a")
      eq(oname, "body")
      eq(oidx, 0)
      check_msg("M", { a = 1, body = "a" })
      local data = pb.decode("M", pb.encode("M", { a = 0 }))
      eq(data.a, 0)
      eq(data.body, "a")
   end)
end

function _G.test_protoc_enum()
   withstate(function()
      local set, bytes = compile_set(protoc.new(), [[
         syntax = "proto2";
         enum E {
            option allow_alias = true;
            A = 0;
            B = 1;
            ALIAS = 1;
            reserved 5;
            reserved "OLD";
         }
      ]], "enum.proto")
      local e = set.file[1].enum_type[1]
      eq(#e.value, 3)
      eq(e.value[1].name, "A")
      eq(e.value[2].number, 1)
      eq(e.value[3].name, "ALIAS")
      eq(e.options.allow_alias, true)
      eq(e.reserved_range[1].start, 5)
      eq(e.reserved_name[1], "OLD")
      assert(pb.load(bytes))
      eq(pb.type(".E"), ".E")
   end)
end

function _G.test_protoc_enum_duplicates()
   withstate(function()
      local p = protoc.new()
      fail("enum name 'A' exists", function()
         p:parse([[syntax = "proto2"; enum E { A = 0; A = 1; }]], "enum_dup.proto")
      end)
      local p2 = protoc.new()
      fail("enum number '0' exists", function()
         p2:parse([[syntax = "proto2"; enum E { A = 0; B = 0; }]], "enum_dup2.proto")
      end)
   end)
end

-- BUG: nested enums never reach check_enum(), so duplicate names/numbers
-- inside a message are accepted (protoc rejects both).
function _G.test_protoc_nested_enum_duplicates()
   withstate(function()
      local p = protoc.new()
      error_any(function()
         p:parse([[syntax = "proto2"; message M { enum E { A = 0; A = 1; } }]],
                 "nested_enum_dup.proto")
      end)
      local p2 = protoc.new()
      error_any(function()
         p2:parse([[syntax = "proto2"; message M { enum E { A = 0; B = 0; } }]],
                  "nested_enum_dup2.proto")
      end)
   end)
end

function _G.test_protoc_extensions()
   withstate(function()
      local p = protoc.new()
      local set, bytes = compile_set(p, [[
         syntax = "proto2";
         message Base {
            optional int32 id = 1;
            extensions 100 to 199;
         }
         extend Base {
            optional string foo = 100;
         }
      ]], "ext.proto")
      local f = set.file[1]
      eq(#f.extension, 1)
      eq(f.extension[1].extendee, ".Base")
      eq(f.extension[1].name, "foo")
      eq(f.extension[1].number, 100)
      eq(f.message_type[1].extension_range[1].start, 100)
      assert(pb.load(bytes))
      check_msg("Base", { id = 1, foo = "bar" })
      eq(select("#", pb.field("Base", "foo")), 5)
      -- extension presence is explicit: an empty string is still encoded
      eq(pb.encode("Base", { foo = "" }), "\162\6\0")
   end)
end

function _G.test_protoc_nested_extend()
   withstate(function()
      local set, bytes = compile_set(protoc.new(), [[
         syntax = "proto2";
         message Nest {
            optional uint32 id = 1;
            extensions 100 to max;
         }
         message Holder {
            extend Nest { optional string name = 100; }
         }
      ]], "nested_ext.proto")
      local holder = set.file[1].message_type[2]
      eq(holder.name, "Holder")
      eq(holder.extension[1].extendee, ".Nest")
      is_nil(set.file[1].extension)
      assert(pb.load(bytes))
      check_msg("Nest", { id = 2, name = "x" })
   end)
end

-- BUG: ExtensionRange.end is exclusive in descriptor.proto; "100 to 200"
-- must be written as 201, "300" as 301 (protoc 36.x does this).
function _G.test_protoc_extension_range_end()
   withstate(function()
      local set = compile_set(protoc.new(), [[
         syntax = "proto2";
         message M {
            extensions 100 to 200;
            extensions 300;
            extensions 400 to max;
         }
      ]], "ext_range.proto")
      local r = set.file[1].message_type[1].extension_range
      eq(r[1]["end"], 201)
      eq(r[2]["end"], 301)
      eq(r[3]["end"], 536870912)
   end)
end

-- BUG: DescriptorProto.ReservedRange.end is exclusive: "300 to 310" must be
-- 311, "320" 321 and "400 to max" 536870912.  EnumReservedRange.end is
-- inclusive instead, and "to max" is INT32_MAX there (2147483647), so the enum
-- case needs a different `max` value than the message case.
function _G.test_protoc_reserved_range_end()
   withstate(function()
      local set = compile_set(protoc.new(), [[
         syntax = "proto2";
         message M {
            reserved 300 to 310;
            reserved 320;
            reserved 400 to max;
         }
         enum E {
            A = 0;
            reserved 5 to 7;
            reserved 400 to max;
         }
      ]], "reserved_range.proto")
      local r = set.file[1].message_type[1].reserved_range
      eq(r[1]["end"], 311)
      eq(r[2]["end"], 321)
      eq(r[3]["end"], 536870912)
      local er = set.file[1].enum_type[1].reserved_range
      eq(er[1]["end"], 7)
      eq(er[2]["end"], 2147483647)
   end)
end

-- BUG: extension numbers are not checked against the declared extension
-- ranges of the extendee (protoc rejects both cases).
function _G.test_protoc_extension_number_range_known_issue()
   withstate(function()
      local p = protoc.new()
      error_any(function()
         p:parse([[
            syntax = "proto2";
            message Base { extensions 100 to 199; }
            extend Base { optional int32 x = 300; }
         ]], "ext_num.proto")
      end)
      local p2 = protoc.new()
      error_any(function()
         p2:parse([[
            syntax = "proto2";
            message Base { optional int32 id = 1; }
            extend Base { optional int32 x = 100; }
         ]], "ext_num2.proto")
      end)
   end)
end

function _G.test_protoc_extendee_not_message()
   withstate(function()
      local p = protoc.new()
      fail("message type expected in extension", function()
         p:parse([[
            syntax = "proto2";
            enum E { A = 0; }
            extend E { optional int32 x = 1; }
         ]], "ext_enum.proto")
      end)
   end)
end

function _G.test_protoc_service()
   withstate(function()
      local set, bytes = compile_set(protoc.new(), [[
         syntax = "proto3";
         message In { int32 i = 1; }
         message Out { int32 o = 1; }
         service S {
            rpc Unary(In) returns (Out);
            rpc ClientStream(stream In) returns (Out);
            rpc ServerStream(In) returns (stream Out);
            rpc Bidi(stream In) returns (stream Out) {
               option deprecated = true;
            }
         }
      ]], "svc.proto")
      local s = set.file[1].service[1]
      eq(s.name, "S")
      eq(#s.method, 4)
      eq(s.method[1].name, "Unary")
      eq(s.method[1].input_type, ".In")
      eq(s.method[1].output_type, ".Out")
      is_nil(s.method[1].client_streaming)
      is_nil(s.method[1].server_streaming)
      eq(s.method[2].client_streaming, true)
      eq(s.method[3].server_streaming, true)
      eq(s.method[4].client_streaming, true)
      eq(s.method[4].server_streaming, true)
      eq(s.method[4].options.deprecated, true)
      assert(pb.load(bytes))
   end)
end

function _G.test_protoc_service_errors()
   withstate(function()
      local p = protoc.new()
      fail("unknown type 'Nope'", function()
         p:parse([[syntax = "proto3"; service S { rpc R(Nope) returns (Nope); }]],
                 "svc_err.proto")
      end)
      local p2 = protoc.new()
      fail("message type expected in parameter", function()
         p2:parse([[
            syntax = "proto3";
            enum E { Z = 0; }
            message M { int32 i = 1; }
            service S { rpc R(E) returns (M); }
         ]], "svc_err2.proto")
      end)
      local p3 = protoc.new()
      fail("message type expected in return", function()
         p3:parse([[
            syntax = "proto3";
            enum E { Z = 0; }
            message M { int32 i = 1; }
            service S { rpc R(M) returns (E); }
         ]], "svc_err3.proto")
      end)
   end)
end

function _G.test_protoc_options()
   withstate(function()
      local set = compile_set(protoc.new(), [[
         syntax = "proto2";
         option java_package = "com.example";
         option optimize_for = SPEED;
         option deprecated = true;
         message M {
            option message_set_wire_format = true;
            optional int32 x = 1 [deprecated = true, json_name = "xx"];
            repeated int32 r = 2 [packed = true];
            oneof body {
               option deprecated = true;
               int32 y = 3;
            }
         }
      ]], "options.proto")
      local f = set.file[1]
      eq(f.options.java_package, "com.example")
      eq(f.options.optimize_for, "SPEED")
      eq(f.options.deprecated, true)
      local m = f.message_type[1]
      eq(m.options.message_set_wire_format, true)
      eq(m.field[1].options.deprecated, true)
      eq(m.field[1].json_name, "xx")
      eq(m.field[2].options.packed, true)
   end)
end

function _G.test_protoc_defaults()
   withstate(function()
      local set, bytes = compile_set(protoc.new(), [[
         syntax = "proto2";
         enum Color { RED = 0; GREEN = 1; }
         message M {
            optional int32  i = 1 [default = 42];
            optional uint32 u = 2 [default = 7];
            optional bool   b = 3 [default = true];
            optional string s = 4 [default = "hi"];
            optional float  f = 5 [default = 0.5];
            optional Color  c = 6 [default = GREEN];
         }
      ]], "defaults.proto")
      local m = set.file[1].message_type[1]
      is_nil(m.field[1].options)
      eq(m.field[1].default_value, "42")
      eq(m.field[3].default_value, "true")
      eq(m.field[4].default_value, "hi")
      eq(m.field[5].default_value, "0.5")
      eq(m.field[6].default_value, "GREEN")
      assert(pb.load(bytes))
      pb.option "enum_as_name"
      local d = copy_no_meta(pb.defaults "M")
      eq(d.i, 42)
      eq(d.u, 7)
      eq(d.b, true)
      eq(d.s, "hi")
      eq(d.f, 0.5)
      eq(d.c, "GREEN")
   end)
end

-- BUG: any inline option sets default_value to the string "nil", because
-- `tostring(options.default)` runs even when the option is absent.  This
-- corrupts the descriptor and makes pb.defaults() return "nil" for strings.
function _G.test_protoc_default_without_default_option()
   withstate(function()
      local set, bytes = compile_set(protoc.new(), [[
         syntax = "proto2";
         message M {
            optional string s = 1 [deprecated = true];
            repeated int32  r = 2 [packed = true];
         }
      ]], "default_pollution.proto")
      local m = set.file[1].message_type[1]
      is_nil(m.field[1].default_value)
      is_nil(m.field[2].default_value)
      assert(pb.load(bytes))
      local _, _, _, def = pb.field("M", "s")
      is_nil(def)
      is_nil(pb.defaults("M").s)
   end)
end

-- ------------------------------------------------ imports and unknown names

function _G.test_protoc_include_imports()
   withfiles({
      ["protoc_test_dep_lib.proto"] = [[
         syntax = "proto2";
         message DepLib { optional int32 i = 1; }
      ]],
      ["protoc_test_user.proto"] = [[
         syntax = "proto2";
         import "protoc_test_dep_lib.proto";
         message User { optional DepLib dep = 1; }
      ]],
   }, function()
      withstate(function()
         local p = protoc.new()
         p:addpath(".")
         p.include_imports = true
         local bytes = p:compilefile("protoc_test_user.proto")
         local set = pb.decode(".google.protobuf.FileDescriptorSet", bytes)
         eq(#set.file, 2)
         eq(set.file[1].name, "protoc_test_dep_lib.proto")
         eq(set.file[2].name, "protoc_test_user.proto")
         eq(set.file[2].dependency[1], "protoc_test_dep_lib.proto")
         assert(pb.load(bytes))
         check_msg("User", { dep = { i = 1 } })
      end)
   end)
end

function _G.test_protoc_include_imports_unknown_import()
   withstate(function()
      local p = protoc.new()
      p.include_imports = true
      p.unknown_import = true
      p.unknown_type = true
      local set = pb.decode(".google.protobuf.FileDescriptorSet", p:compile([[
         syntax = "proto2";
         import "missing.proto";
         message M { optional int32 i = 1; }
      ]], "include_missing.proto"))
      eq(#set.file, 1)
      eq(set.file[1].dependency[1], "missing.proto")
   end)
end

function _G.test_protoc_include_imports_restores_on_import()
   withstate(function()
      local p = protoc.new()
      local orig = function() end
      p.on_import = orig
      p.include_imports = true
      p:compile([[syntax = "proto2"; message M { optional int32 i = 1; }]],
                "restore_on_import.proto")
      is_true(p.on_import == orig)
      p.on_import = orig
      error_any(function()
         p:compile("message", "restore_on_import_bad.proto")
      end)
      is_true(p.on_import == orig)
   end)
end

function _G.test_protoc_on_import()
   withfiles({
      ["protoc_test_dep_cb.proto"] = [[
         syntax = "proto2";
         message DepCb { optional int32 i = 1; }
      ]],
      ["protoc_test_cb_user.proto"] = [[
         syntax = "proto2";
         import "protoc_test_dep_cb.proto";
         message CbUser { optional DepCb dep = 1; }
      ]],
   }, function()
      withstate(function()
         local p = protoc.new()
         p:addpath(".")
         local seen = {}
         p.on_import = function(info) seen[#seen + 1] = info.name end
         p:compilefile("protoc_test_cb_user.proto")
         eq(seen, { "protoc_test_dep_cb.proto" })
         eq(p.loaded["protoc_test_dep_cb.proto"].name, "protoc_test_dep_cb.proto")
      end)
   end)
end

-- BUG: a plain `import` is recorded as a public_dependency; descriptor.proto
-- only lists `import public` there.
function _G.test_protoc_import_plain_not_public()
   withfiles({
      ["protoc_test_imp_a.proto"] = [[
         syntax = "proto2";
         message ImpA { optional int32 i = 1; }
      ]],
      ["protoc_test_imp_b.proto"] = [[
         syntax = "proto2";
         import "protoc_test_imp_a.proto";
         import public "protoc_test_imp_c.proto";
         message ImpB { optional ImpA a = 1; }
      ]],
      ["protoc_test_imp_c.proto"] = [[
         syntax = "proto2";
         message ImpC { optional int32 i = 1; }
      ]],
   }, function()
      withstate(function()
         local p = protoc.new()
         p:addpath(".")
         local set = pb.decode(".google.protobuf.FileDescriptorSet",
                               p:compilefile("protoc_test_imp_b.proto"))
         local f = set.file[1]
         eq(f.dependency, { "protoc_test_imp_a.proto", "protoc_test_imp_c.proto" })
         eq(f.public_dependency, { 1 })
      end)
   end)
end

-- BUG: include_imports emits the same file once per import statement instead
-- of once per file (a diamond dependency duplicates the shared file).
function _G.test_protoc_include_imports_dedup()
   withfiles({
      ["protoc_test_leaf.proto"] = [[
         syntax = "proto2";
         message Leaf { optional int32 i = 1; }
      ]],
      ["protoc_test_mid.proto"] = [[
         syntax = "proto2";
         import "protoc_test_leaf.proto";
         message Mid { optional Leaf leaf = 1; }
      ]],
      ["protoc_test_top.proto"] = [[
         syntax = "proto2";
         import "protoc_test_mid.proto";
         import "protoc_test_leaf.proto";
         message Top { optional Mid mid = 1; }
      ]],
   }, function()
      withstate(function()
         local p = protoc.new()
         p:addpath(".")
         p.include_imports = true
         local set = pb.decode(".google.protobuf.FileDescriptorSet",
                               p:compilefile("protoc_test_top.proto"))
         local names = {}
         for i, f in ipairs(set.file) do names[i] = f.name end
         eq(names, {
            "protoc_test_leaf.proto",
            "protoc_test_mid.proto",
            "protoc_test_top.proto",
         })
      end)
   end)
end

-- BUG: import cycles are silently accepted without include_imports, and blow
-- up with a confusing pb.encode error with include_imports.  protoc rejects
-- the cycle at parse time.
function _G.test_protoc_import_cycle()
   withfiles({
      ["protoc_test_cyc_a.proto"] = [[
         syntax = "proto2";
         import "protoc_test_cyc_b.proto";
         message CycA { optional int32 i = 1; }
      ]],
      ["protoc_test_cyc_b.proto"] = [[
         syntax = "proto2";
         import "protoc_test_cyc_a.proto";
         message CycB { optional int32 i = 1; }
      ]],
   }, function()
      withstate(function()
         local p = protoc.new()
         p:addpath(".")
         error_any(function() p:compilefile("protoc_test_cyc_a.proto") end)
      end)
   end)
end

-- BUG: a file may import itself; parsefile() returns the in-progress guard
-- value `true` instead of reporting the recursion.
function _G.test_protoc_import_self()
   withfiles({
      ["protoc_test_self.proto"] = [[
         syntax = "proto2";
         import "protoc_test_self.proto";
         message SelfImp { optional int32 i = 1; }
      ]],
   }, function()
      withstate(function()
         local p = protoc.new()
         p:addpath(".")
         error_any(function() p:compilefile("protoc_test_self.proto") end)
      end)
   end)
end

-- BUG: README documents unknown_import as accepting a Lua pattern string, but
-- the string is called as a function.
function _G.test_protoc_unknown_import_string()
   withstate(function()
      local p = protoc.new()
      p.unknown_import = "foo%."
      local info = p:parse([[syntax = "proto2"; import "foo.proto";]],
                           "unknown_import_str.proto")
      eq(info.dependency[1], "foo.proto")
      local p2 = protoc.new()
      p2.unknown_import = "foo%."
      fail("module load error", function()
         p2:parse([[syntax = "proto2"; import "bar.proto";]],
                  "unknown_import_str2.proto")
      end)
   end)
end

function _G.test_protoc_unknown_import_handlers()
   withstate(function()
      local p = protoc.new()
      p.unknown_import = true
      local info = p:parse([[
         syntax = "proto2";
         import "no_such_file.proto";
         message M { optional int32 i = 1; }
      ]], "unknown_import_true.proto")
      eq(info.dependency[1], "no_such_file.proto")

      local p2 = protoc.new()
      local called
      p2.unknown_import = function(self, name)
         called = name
         return p2:parse("syntax = 'proto2'; message Fake { optional int32 i = 1; }",
                         name)
      end
      local info2 = p2:parse([[
         syntax = "proto2";
         import "virtual.proto";
         message M { optional int32 i = 1; }
      ]], "unknown_import_fn.proto")
      eq(called, "virtual.proto")
      eq(info2.dependency[1], "virtual.proto")
   end)
end

function _G.test_protoc_unknown_type_handlers()
   withstate(function()
      local p = protoc.new()
      p.unknown_type = true
      local set = pb.decode(".google.protobuf.FileDescriptorSet", p:compile([[
         syntax = "proto2";
         message M { optional Foo f = 1; }
      ]], "unknown_type_true.proto"))
      local f = set.file[1].message_type[1].field[1]
      eq(f.type, "TYPE_MESSAGE")
      eq(f.type_name, ".Foo")

      local p2 = protoc.new()
      p2.unknown_type = "Foo.*"
      local bytes = p2:compile([[
         syntax = "proto2";
         message M { optional FooBar f = 1; }
      ]], "unknown_type_pat.proto")
      is_true(type(bytes) == "string")
      local p3 = protoc.new()
      p3.unknown_type = "Foo.*"
      fail("unknown type 'BarBar'", function()
         p3:parse([[syntax = "proto2"; message M { optional BarBar f = 1; }]],
                  "unknown_type_pat2.proto")
      end)
   end)
end

-- BUG: the unknown_type callback may return a type name *and* a type
-- ("message"/"enum") per README, but only the first return value is used, so
-- enums fall back to message.
function _G.test_protoc_unknown_type_callback_enum()
   withstate(function()
      local p = protoc.new()
      p.unknown_type = function(self, name) return ".My" .. name, "enum" end
      local set = pb.decode(".google.protobuf.FileDescriptorSet", p:compile([[
         syntax = "proto2";
         message M { optional Foo f = 1; }
      ]], "unknown_type_cb.proto"))
      local f = set.file[1].message_type[1].field[1]
      eq(f.type, "TYPE_ENUM")
      eq(f.type_name, ".MyFoo")
   end)
end

function _G.test_protoc_unknown_type_callback_default()
   withstate(function()
      local p = protoc.new()
      p.unknown_type = function(self, name) return ".One" .. name end
      local set = pb.decode(".google.protobuf.FileDescriptorSet", p:compile([[
         syntax = "proto2";
         message M { optional Foo f = 1; }
      ]], "unknown_type_cb2.proto"))
      local f = set.file[1].message_type[1].field[1]
      eq(f.type, "TYPE_MESSAGE")
      eq(f.type_name, ".OneFoo")
   end)
end

function _G.test_protoc_unknown_type_invalid_kind()
   withstate(function()
      local p = protoc.new()
      p.unknown_type = function(self, name) return ".X" .. name, "bogus" end
      fail("invalid type 'bogus'", function()
         p:parse([[syntax = "proto2"; message M { optional Foo f = 1; }]],
                 "unknown_type_bad.proto")
      end)
   end)
end

-- BUG: the unknown_type handler is bypassed for absolute type names.
function _G.test_protoc_unknown_type_absolute()
   withstate(function()
      local p = protoc.new()
      p.unknown_type = true
      local set = pb.decode(".google.protobuf.FileDescriptorSet", p:compile([[
         syntax = "proto2";
         message M { optional .Foo f = 1; }
      ]], "unknown_type_abs.proto"))
      local f = set.file[1].message_type[1].field[1]
      eq(f.type, "TYPE_MESSAGE")
      eq(f.type_name, ".Foo")
   end)
end

-- ------------------------------------------------ proto3 optional (WIP)

-- REGRESSION: proto3 `optional` no longer gets a synthetic oneof, so pb has no
-- presence flag and drops/zero-fills the field.
function _G.test_protoc_proto3_optional_presence()
   withstate(function()
      local p = protoc.new()
      p.proto3_optional = true
      local _, bytes = compile_set(p, [[
         syntax = "proto3";
         message M {
            optional int32  x = 1;
            optional string s = 2;
            optional bool   b = 3;
         }
      ]], "opt_presence.proto")
      assert(pb.load(bytes))
      eq(pb.encode("M", { x = 0 }), "\8\0")
      eq(pb.encode("M", { s = "" }), "\18\0")
      eq(pb.encode("M", { b = false }), "\24\0")
      local d = pb.decode("M", "")
      is_nil(d.x)
      is_nil(d.s)
      is_nil(d.b)
   end)
end

-- REGRESSION: descriptor.proto requires a synthetic oneof for every
-- proto3_optional field; the compiler writes proto3_optional without it.
function _G.test_protoc_proto3_optional_descriptor()
   withstate(function()
      local set = compile_set((function()
         local p = protoc.new()
         p.proto3_optional = true
         return p
      end)(), [[
         syntax = "proto3";
         message N { int32 i = 1; }
         message M {
            optional int32 x = 1;
            optional N n = 2;
         }
      ]], "opt_desc.proto")
      local m = set.file[1].message_type[2]
      eq(m.field[1].proto3_optional, true)
      eq(m.field[2].proto3_optional, true)
      eq(m.oneof_decl and #m.oneof_decl or 0, 2)
      eq(m.oneof_decl and m.oneof_decl[1].name, "_x")
      eq(m.oneof_decl and m.oneof_decl[2].name, "_n")
      eq(m.field[1].oneof_index or -1, 0)
      eq(m.field[2].oneof_index or -1, 1)
   end)
end

-- synthetic oneof names follow protoc: prepend "_" unless the field already
-- starts with one, then prepend "X" while the name collides with a field/oneof
function _G.test_protoc_proto3_optional_oneof_name()
   withstate(function()
      local set = compile_set(protoc.new(), [[
         syntax = "proto3";
         message M {
            optional int32 _x = 1;
            oneof _y { int32 a = 2; }
            optional int32 y = 3;
         }
      ]], "opt_name.proto")
      local m = set.file[1].message_type[1]
      local names = {}
      for i, o in ipairs(m.oneof_decl or {}) do names[i] = o.name end
      eq(names, { "_y", "X_x", "X_y" })
      eq(m.field[1].oneof_index, 1)
      eq(m.field[3].oneof_index, 2)
   end)
end

-- REGRESSION/BUG: synthetic oneofs must be ordered after all real oneofs
-- (protoc puts "o" first, then "_a", then "_b" in declaration order).
function _G.test_protoc_proto3_optional_oneof_order()
   withstate(function()
      local p = protoc.new()
      p.proto3_optional = true
      local set = compile_set(p, [[
         syntax = "proto3";
         message M {
            optional int32 a = 1;
            oneof o { int32 c = 3; }
            optional int32 b = 2;
         }
      ]], "opt_order.proto")
      local m = set.file[1].message_type[1]
      local names = {}
      for i, o in ipairs(m.oneof_decl or {}) do names[i] = o.name end
      eq(names, { "o", "_a", "_b" })
      eq(m.field[1].oneof_index or -1, 1)
      eq(m.field[2].oneof_index or -1, 0)
      eq(m.field[3].oneof_index or -1, 2)
   end)
end

-- REGRESSION: the compiler writes oneof_index for a proto3 optional extension
-- (off-by-one, 1-based) and pollutes the extendee with a fake oneof, so pb
-- fails to decode the extension with "table index is nil".
function _G.test_protoc_proto3_optional_extension()
   withstate(function()
      local p = protoc.new()
      p.unknown_type = true
      p.proto3_optional = true
      local bytes = p:compile([[
         syntax = "proto3";
         extend google.protobuf.MessageOptions {
            optional string foo = 50000;
         }
      ]], "opt_ext.proto")
      local set = pb.decode(".google.protobuf.FileDescriptorSet", bytes)
      local ext = set.file[1].extension[1]
      eq(ext.proto3_optional, true)
      is_nil(ext.oneof_index)
      eq(#(set.file[1].message_type or {}), 0)
      assert(pb.load(bytes))
      local encoded = pb.encode(".google.protobuf.MessageOptions", { foo = "bar" })
      eq(pb.tohex(encoded), "82 B5 18 03 62 61 72")
      eq(pb.decode(".google.protobuf.MessageOptions", encoded).foo, "bar")
   end)
end

-- REGRESSION: the same pollution/failure happens when the extendee comes from
-- an imported file and include_imports embeds that file in the output.
function _G.test_protoc_proto3_optional_extension_imported()
   withfiles({
      ["protoc_test_opt_ext_base.proto"] = [[
         syntax = "proto2";
         message OptBase {
            optional int32 id = 1;
            extensions 100 to max;
         }
      ]],
   }, function()
      withstate(function()
         local p = protoc.new()
         p:addpath(".")
         p.include_imports = true
         p.proto3_optional = true
         local bytes = p:compile([[
            syntax = "proto3";
            import "protoc_test_opt_ext_base.proto";
            extend OptBase { optional string foo = 50000; }
         ]], "protoc_test_opt_ext_main.proto")
         local set = pb.decode(".google.protobuf.FileDescriptorSet", bytes)
         local base
         for _, file in ipairs(set.file) do
            if file.name == "protoc_test_opt_ext_base.proto" then base = file end
         end
         is_nil(base.message_type[1].oneof_decl)
         assert(pb.load(bytes))
         check_msg("OptBase", { id = 1, foo = "bar" })
      end)
   end)
end

-- proto3 `optional` is accepted without any option, matching protoc 3.15+;
-- `p.proto3_optional = false` restores the old opt-in requirement.
function _G.test_protoc_proto3_optional_default_allowed()
   withstate(function()
      local bytes = protoc.new():compile([[
         syntax = "proto3";
         message M { optional int32 x = 1; }
      ]], "opt_default.proto")
      is_true(type(bytes) == "string" and #bytes > 0)
   end)
end

function _G.test_protoc_proto3_optional_disabled()
   withstate(function()
      local p = protoc.new()
      p.proto3_optional = false
      fail("proto3 disallow 'optional' label", function()
         p:parse([[syntax = "proto3"; message M { optional int32 x = 1; }]],
                 "opt_off.proto")
      end)
   end)
end

-- --------------------------------------------------- spec validation gaps

-- BUG: proto3 forbids `required` fields.
function _G.test_protoc_proto3_required_known_issue()
   withstate(function()
      local p = protoc.new()
      error_any(function()
         p:parse([[syntax = "proto3"; message M { required int32 x = 1; }]],
                 "v_required.proto")
      end)
   end)
end

-- BUG: the first enum value must be zero in proto3.
function _G.test_protoc_proto3_enum_zero_known_issue()
   withstate(function()
      local p = protoc.new()
      error_any(function()
         p:parse([[syntax = "proto3"; enum E { A = 5; }]], "v_enum_zero.proto")
      end)
   end)
end

-- BUG: field numbers are only checked for <= 0; the protobuf upper bound
-- (536870911) is not enforced.
function _G.test_protoc_field_number_upper_bound_known_issue()
   withstate(function()
      local p = protoc.new()
      error_any(function()
         p:parse([[syntax = "proto2"; message M { optional int32 x = 536870912; }]],
                 "v_big_num.proto")
      end)
   end)
end

-- BUG: declared reserved numbers are not checked against fields.
function _G.test_protoc_reserved_number_used_known_issue()
   withstate(function()
      local p = protoc.new()
      error_any(function()
         p:parse([[syntax = "proto2"; message M { reserved 5; optional int32 x = 5; }]],
                 "v_reserved_num.proto")
      end)
   end)
end

-- BUG: declared reserved names are not checked against fields.
function _G.test_protoc_reserved_name_used_known_issue()
   withstate(function()
      local p = protoc.new()
      error_any(function()
         p:parse([[syntax = "proto2"; message M { reserved "foo"; optional int32 foo = 1; }]],
                 "v_reserved_name.proto")
      end)
   end)
end

-- BUG: duplicate oneof names are not detected (they are types too).
function _G.test_protoc_duplicate_oneof_name_known_issue()
   withstate(function()
      local p = protoc.new()
      error_any(function()
         p:parse([[
            syntax = "proto3";
            message M {
               oneof a { int32 x = 1; }
               oneof a { int32 y = 2; }
            }
         ]], "v_dup_oneof.proto")
      end)
   end)
end

-- BUG: [packed = true] is only legal on repeated primitive fields.
function _G.test_protoc_packed_non_primitive_known_issue()
   withstate(function()
      local p = protoc.new()
      error_any(function()
         p:parse([[
            syntax = "proto2";
            message M { repeated string s = 1 [packed = true]; }
         ]], "v_packed.proto")
      end)
   end)
end

-- BUG: unknown syntax identifiers are accepted silently; protoc only
-- recognizes "proto2" and "proto3".
function _G.test_protoc_unknown_syntax_known_issue()
   withstate(function()
      local p = protoc.new()
      error_any(function()
         p:parse([[syntax = "proto4"; message M { int32 x = 1; }]],
                 "v_syntax.proto")
      end)
   end)
end

-- BUG: map fields may not be extensions.
function _G.test_protoc_map_as_extension_known_issue()
   withstate(function()
      local p = protoc.new()
      error_any(function()
         p:parse([[
            syntax = "proto2";
            message Base { extensions 100 to max; }
            extend Base { map<string, int32> m = 100; }
         ]], "v_map_ext.proto")
      end)
   end)
end

-- BUG: map fields may not carry a label, but `optional map` is accepted and
-- produces a map field with LABEL_OPTIONAL.
function _G.test_protoc_optional_map_known_issue()
   withstate(function()
      local p = protoc.new()
      error_any(function()
         p:parse([[syntax = "proto2"; message M { optional map<string, int32> m = 1; }]],
                 "v_opt_map.proto")
      end)
   end)
end

-- BUG: proto3 messages may not declare extension ranges.
function _G.test_protoc_proto3_extension_range_known_issue()
   withstate(function()
      local p = protoc.new()
      error_any(function()
         p:parse([[syntax = "proto3"; message M { extensions 100 to 200; }]],
                 "v_ext_range.proto")
      end)
   end)
end

-- ------------------------------------------------------- cache/error state

-- BUG: a failed parse leaves a half-parsed descriptor in `loaded` (and types
-- in `typemap`), so retrying with the same file name silently reuses the
-- broken descriptor instead of re-parsing.
function _G.test_protoc_failed_parse_cache()
   withstate(function()
      local p = protoc.new()
      error_any(function()
         p:parse([[syntax = "proto2"; message M { optional Nope n = 1; }]],
                 "cache_bad.proto")
      end)
      local info = p:parse([[syntax = "proto2"; message M { optional int32 n = 1; }]],
                           "cache_bad.proto")
      eq(info.message_type[1].field[1].type, 5)
   end)
end

function _G.test_protoc_reset()
   withstate(function()
      local p = protoc.new()
      p:parse([[syntax = "proto2"; message R { optional int32 i = 1; }]],
              "reset.proto")
      is_true(p.loaded["reset.proto"] ~= nil)
      p:reset()
      is_nil(p.loaded["reset.proto"])
      local info = p:parse([[syntax = "proto2"; message R { optional int32 i = 1; }]],
                           "reset2.proto")
      eq(info.message_type[1].name, "R")
   end)
end

function _G.test_protoc_parse_errors()
   withstate(function()
      local p = protoc.new()
      fail("unknown keyword 'msg'", function()
         p:parse("msg M {}", "err_keyword.proto")
      end)
      local p2 = protoc.new()
      fail("proto2 disallow missing label", function()
         p2:parse([[syntax = "proto2"; message M { int32 x = 1; }]],
                  "err_label.proto")
      end)
      local p3 = protoc.new()
      fail("invalid key type: float", function()
         p3:parse([[syntax = "proto2"; message M { map<float, int32> m = 1; }]],
                  "err_key.proto")
      end)
      local p4 = protoc.new()
      fail("unknown type 'Nope'", function()
         p4:parse([[syntax = "proto2"; message M { optional Nope n = 1; }]],
                  "err_type.proto")
      end)
      local p5 = protoc.new()
      p5:parse([[syntax = "proto2"; message M { optional int32 x = 1; }]],
               "err_dup_type.proto")
      fail("type .M already defined", function()
         p5:parse([[syntax = "proto2"; message M { optional int32 x = 1; }]],
                  "err_dup_type2.proto")
      end)
      local p6 = protoc.new()
      fail("field name 'x' exists", function()
         p6:parse([[syntax = "proto2"; message M {
            optional int32 x = 1;
            optional int32 x = 2;
         } ]], "err_dup_field.proto")
      end)
      local p7 = protoc.new()
      fail("field number '1' exists", function()
         p7:parse([[syntax = "proto2"; message M {
            optional int32 x = 1;
            optional int32 y = 1;
         } ]], "err_dup_number.proto")
      end)
      local p8 = protoc.new()
      fail("invalid tag number: 0", function()
         p8:parse([[syntax = "proto2"; message M { optional int32 x = 0; }]],
                  "err_zero.proto")
      end)
   end)
end

if _VERSION == "Lua 5.1" and not _G.jit then
   lu.LuaUnit.run()
else
   os.exit(lu.LuaUnit.run(), true)
end
