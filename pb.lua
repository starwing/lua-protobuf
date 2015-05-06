local pb = {}
local decoder = require "pb.decoder"
local buffer = require "pb.buffer"
local conv = require "pb.conv"
local pbio = require "pb.io"
local ipairs = ipairs
local pairs = pairs
local type = type


------------------------------------------------------------ 

local loaded_files = {}

function pb.clearfiles()
   loaded_files = {}
end

function pb.file(filename)
   if filename then
      return loaded_files[filename]
   else
      return loaded_files
   end
end

------------------------------------------------------------ 

local typeinfo = require "pb_typeinfo"

local function qualitied_type(qname)
   local realtype = typeinfo
   local package = ""
   for comp in qname:gmatch "[^.]+" do
      realtype = realtype[comp]
      if not realtype then
         error(("no such type '%s' in package '%s'")
            :format(comp, package))
      end
      package = package .. "." .. comp
   end
   return realtype
end

local function field_type(field)
   local realtype = typeinfo
   for k,v in ipairs(field.type_name) do
      realtype = realtype[v]
      if not realtype then return end
   end
   return realtype
end

local function subtable(t, k, type)
   local subt = t[k]
   if not subt then
      subt = {}
      t[k] = subt
   end
   subt.type = type or subt.type
   return subt
end

local function make_package(package)
   local cur = typeinfo
   for pkg in package:gmatch "[^.]+" do
      cur = subtable(cur, pkg, "package")
   end
   return cur
end

function pb.cleartypes()
   package.loaded.pb_typeinfo = nil
   typeinfo = require "pb_typeinfo"
end

function pb.type(qname)
   if qname then
      return qualitied_type(qname)
   else
      return typeinfo
   end
end

local buffer_pool = {}
local buffer_used = setmetatable({}, { __mode="k" })

--[[
function buffer.new()
   local t = {}
   function t:add(tag, type, value)
      t[#t+1] = ("[%s %d %s]\n"):format(type, tag, tostring(value))
   end
   function t:tag(tag, wiretype)
      t[#t+1] = ("[%s %d "):format(wiretype, tag)
   end
   function t:varint(n)
      t[#t+1] = ("%d]\n"):format(n)
   end
   function t:bytes(s)
      if type(s) == "table" then
         s = table.concat(s)
         t[#t+1] = ("\n"..s.."]"):gsub("\n", "\n  "):gsub("  ]%s*$", "]\n")
      else
         t[#t+1] = ("'%s'\n]\n"):format(s)
      end
   end
   function t:clear(len, result)
      if result then
         result = table.concat(t)
      end
      for k, v in ipairs(t) do
         t[k] = nil
      end
      return result
   end
   return t
end
--]]

local function get_buffer()
   local buff = next(buffer_pool)
   if buff then
      buffer_pool[buff] = nil
   else
      buff = buffer.new()
   end
   buffer_used[buff] = true
   return buff
end

local function put_buffer(buff)
   buffer_used[buff] = nil
   buffer_pool[buff] = true
end

function pb.clearbuffers()
   for k, v in pairs(buffer_pool) do
      k:reset()
      buffer_pool[k] = nil
   end
end

local decode, encode

local function decode_unknown_field(t, dec, wiretype, tag)
   do return end -- XXX ignore unknown fields
   local value = dec:fetch(wiretype)
   local uf = t.unknown_fields
   if not uf then
      uf = {}
      t.unknown_fields = uf
   end
   local uft = uf[tag]
   if not uft then
      uf[tag] = value
   elseif type(uft) ~= 'table' then
      uf[tag] = { uf[tag], value }
   else
      uft[#uft+1] = value
   end
end

local repeated_fixed32 = { fixed32 = true, sfixed32 = true, float = true }
local repeated_fixed64 = { fixed64 = true, sfixed64 = true, double = true }
local function decode_packed_repeated(t, dec, field)
   local len = assert(dec:varint())
   local old = dec:len(dec:pos() + len - 1)
   local wt = "varint"
   if repeated_fixed32[field.type_name] then
      wt = "fixed32"
   elseif repeated_fixed64[field.type_name] then
      wt = "fixed64"
   end
   repeat
      local value = dec:fetch(wt, field.type_name)
      t[#t+1] = value
   until not value
   dec:len(old)
end

local function decode_field(t, dec, wiretype, tag, field)
   local value
   if not field.scalar then
      local ftype = field_type(field)
      if not ftype then
         --return decode_unknown_field(t, dec, wiretype, tag)
         return -- XXX ignore type-unknown fields
      elseif ftype.type == "enum" then
         value = dec:fetch(wiretype)
         value = ftype[value] or value
      else
         local len = assert(dec:varint())
         local old = dec:len(dec:pos() + len - 1)
         value = decode(dec, ftype)
         dec:len(old)
      end
   elseif wiretype == 2 and field.packed then
      local t = subtable(t, field.name)
      return decode_packed_repeated(t, dec, field)
   else
      value = dec:fetch(wiretype, field.type_name)
   end

   if not field.repeated then
      t[field.name] = value
   else
      local vs = subtable(t, field.name)
      vs[#vs+1] = value
   end
end

function decode(dec, ptype)
   local t = {}
   while not dec:finished() do
      local tag, wiretype = dec:tag()
      local field = ptype[tag]
      if field then -- known fields
         decode_field(t, dec, wiretype, tag, field)
      else -- unknown fields
         decode_unknown_field(t, dec, wiretype, tag)
      end
   end
   if ptype.defaults then
      for k,v in pairs(ptype.defaults) do
         if t[k] == nil then t[k] = v end
      end
   end
   return t
end

local function encode_message(buff, tag, msg, ftype)
   local inner = get_buffer()
   inner:clear()
   encode(inner, msg, ftype)
   buff:tag(tag, "bytes")
   buff:bytes(inner)
   inner:clear()
   put_buffer(inner)
end

local function encode_enum(buff, tag, enum, ftype)
   local value = assert(ftype.map[enum])
   buff:tag(tag, "varint")
   buff:varint(value)
end

local function encode_scalar(buff, tag, v, field)
   if not field.repeated then
      if v == field.default_value then return end
      return buff:add(tag, field.type_name, v)
   end

   if field.packed and field.type_name ~= "string" then 
      local inner = get_buffer()
      inner:clear()
      for k,v in ipairs(v) do
         inner:add(nil, field.type_name, v)
      end
      buff:tag(tag, "bytes")
      buff:bytes(inner)
      inner:clear()
      put_buffer(inner)
      return
   end

   for k,v in ipairs(v) do
      buff:add(tag, field.type_name, v)
   end
end

local function encode_field(buff, tag, v, ptype)
   --print(("encode_field(%d, %s)"):format(tag,
   --require"serpent".block(v)))
   local field = ptype[tag]
   if not field then return end

   if field.scalar then
      return encode_scalar(buff, tag, v, field)
   end

   local ftype = field_type(field)
   if ftype.type == "message" then
      if not field.repeated then
         encode_message(buff, tag, v, ftype)
      else
         for _, v in ipairs(v) do
            encode_message(buff, tag, v, ftype)
         end
      end
      return
   end

   if ftype.type == "enum" then
      if not field.repeated then
         if v ~= field.default_value then
            encode_enum(buff, tag, v, ftype)
         end
      else
         for _, v in ipairs(v) do
            encode_enum(buff, tag, v, ftype)
         end
      end
      return
   end

   error("unknown type: "..ftype.type)
end

function encode(buff, t, ptype)
   for k,v in pairs(t) do
      local tag = ptype.map[k]
      if tag then
         encode_field(buff, tag, v, ptype)
      end
   end
end

function pb.decode(s, ptype, dec)
   if type(ptype) ~= "table" then
      ptype = qualitied_type(ptype)
   end
   if dec then
      dec:source(s)
   else
      dec = decoder.new(s)
   end
   local res = decode(dec, ptype)
   dec:reset()
   return res
end

function pb.encode(t, ptype)
   if type(ptype) ~= "table" then
      ptype = qualitied_type(ptype)
   end
   local buff = get_buffer()
   buff:clear()
   encode(buff, t, ptype)
   local res = buff:clear(nil, true)
   put_buffer(buff)
   return res
end

------------------------------------------------------------

local scalar_typemap = {
   TYPE_BOOL     = "bool";
   TYPE_BYTES    = "bytes";
   TYPE_DOUBLE   = "double";
   TYPE_ENUM     = "enum";
   TYPE_FIXED32  = "fixed32";
   TYPE_FIXED64  = "fixed64";
   TYPE_FLOAT    = "float";
   TYPE_GROUP    = "group";
   TYPE_INT32    = "int32";
   TYPE_INT64    = "int64";
   TYPE_MESSAGE  = "message";
   TYPE_SFIXED32 = "sfixed32";
   TYPE_SFIXED64 = "sfixed64";
   TYPE_SINT32   = "sint32";
   TYPE_SINT64   = "sint64";
   TYPE_STRING   = "string";
   TYPE_UINT32   = "uint32";
   TYPE_UINT64   = "uint64";
}

local function split_qname(name)
   local t = {}
   for comp in name:gmatch "[^.]+" do
      t[#t+1] = comp
   end
   return t
end

local function load_field(msg, field)
   if field.extendee then
      error(("message '%s': field '%s' should not have extendee!")
         :format(msg.name, field.name))
   end
   local namemap = subtable(msg, 'map')
   namemap[field.name] = field.number

   local t = subtable(msg, field.number, "field")
   t.type = "field"
   t.name = field.name
   t.repeated = field.label == 'LABEL_REPEATED'
   if field.type_name then
      t.type_name = split_qname(field.type_name)
   else
      t.type_name = scalar_typemap[field.type]
      t.scalar = true
   end
   if field.options then
      t.packed = field.options.packed
      t.lazy = field.options.lazy
      t.deprecated = field.options.deprecated
   end
   t.default_value = field.default_value
   return t
end

local function load_extension(field)
   if not field.extendee then
      error("'extendee' required in extension '"..field.name.."'")
   end
   local msg = make_package(field.extendee)
   msg.type = "message"
   field.extendee = nil
   load_field(msg, field)
end

local function load_enum(pkg, enum)
   local t = subtable(pkg, enum.name, "enum")
   local namemap = subtable(t, 'map')
   local deprecated_names
   if enum.value then
      for i, v in ipairs(enum.value) do
         t[v.number] = v.name
         namemap[v.name] = v.number
         if v.options and v.options.deprecated then
            if not deprecated_names then
               deprecated_names = subtable(t, 'deprecated_names')
            end
            deprecated_names[k] = true
         end
      end
   end
   if enum.options and enum.options.deprecated then
      t.deprecated = true
   end
   return t
end

local function load_message(pkg, msg)
   local t = subtable(pkg, msg.name, "message")
   if msg.name then
      pkg.name = msg.name
   end
   local defaults
   if msg.field then
      for i, v in ipairs(msg.field) do
         load_field(t, v)
         if v.default_value ~= nil then
            defaults = subtable(t, 'defaults')
            defaults[v.name] = v.default_value
         end
      end
   end
   if msg.nested_type then
      for i, v in ipairs(msg.nested_type) do
         load_message(t, v)
      end
   end
   if msg.enum_type then
      for i, v in ipairs(msg.enum_type) do
         load_enum(t, v)
      end
   end
   if msg.extension then
      for i, v in ipairs(msg.extension) do
         load_extension(v)
      end
   end
   if msg.options and msg.options.deprecated then
      t.deprecated = true
   end
   return t
end

local function load_file(file)
   local pkg = make_package(file.package)
   if file.message_type then
      for i, v in ipairs(file.message_type) do
         load_message(pkg, v)
      end
   end
   if file.enum_type then
      for i, v in ipairs(file.enum_type) do
         load_enum(pkg, v)
      end
   end
   if file.extension then
      for i, v in ipairs(file.extension) do
         load_extension(v)
      end
   end
end

local function load_fileset(pb)
   for i,v in ipairs(pb.file) do
      if not loaded_files[v.name] then
         loaded_files[v.name] = v
         load_file(v)
      end
   end
end

function pb.load(data)
   local proto = pb.decode(data,
      "google.protobuf.FileDescriptorSet")
   load_fileset(proto)
   return proto
end

function pb.loadfile(filename)
   return pb.load(assert(pbio.read(filename)))
end

function pb.loadproto(proto)
   return load_file(proto)
end

------------------------------------------------------------

local function merge_table(dst, t, key)
   local st = t[key]
   if st then
      local dt = dst[k]
      if not dt then
         dst[k] = st
      else
         for k,v in pairs(st) do
            dt[k] = v
         end
      end
   end
end

local function merge_enum(pkg, enum)
   local namemap = pkg.map
   if not namemap then
      namemap = {}; pkg.map = namemap
   end
   for k, v in pairs(enum) do
      if type(k) == "number" then
         pkg[k] = v
         namemap[v] = k
      end
   end
   if enum.deprecated ~= nil then
      pkg.deprecated = enum.deprecated
   end
   merge_table(pkg, enum, "deprecated_names")
end

local function merge_message(pkg, msg)
   local namemap = pkg.map
   if not namemap then
      namemap = {}; pkg.map = namemap
   end
   for k, v in pairs(msg) do
      if type(k) == "number" then
         pkg[k] = v
         namemap[v] = k
      elseif v.type == "enum" then
         merge_enum(subtable(pkg, k, "enum"), v)
      elseif v.type == "message" then
         merge_message(subtable(pkg, k, "message"), v)
      end
   end
   if msg.deprecated ~= nil then
      pkg.deprecated = msg.deprecated
   end
   merge_table(pkg, msg, "defaults")
end

local function merge_package(pkg, other)
   for k,v in pairs(other) do
      if v.type == "enum" then
         merge_enum(subtable(pkg, k, "enum"), v)
      elseif v.type == "message" then
         merge_message(subtable(pkg, k, "message"), v)
      elseif v.type == "package" then
         merge_package(subtable(pkg, "package"), v)
      end
   end
end

function pb.merge(package)
   merge_package(typeinfo, package)
end

------------------------------------------------------------

local function G(...) io.write(...) return G end

local keywords = {
     ["and"]   = true;  ["break"]  = true;  ["do"]       = true;
     ["else"]  = true;  ["elseif"] = true;  ["end"]      = true;
     ["false"] = true;  ["for"]    = true;  ["function"] = true;
     ["goto"]  = true;  ["if"]     = true;  ["in"]       = true;
     ["local"] = true;  ["nil"]    = true;  ["not"]      = true;
     ["or"]    = true;  ["repeat"] = true;  ["return"]   = true;
     ["then"]  = true;  ["true"]   = true;  ["until"]    = true;
     ["while"] = true;
}

local function key(k)
   if type(k) == "string" then
      if not keywords[k] and k:match "[%w_][%w%d_]*" then
         return k
      end
      return ("[%q]"):format(k)
   end
   return ("[%s]"):format(k)
end

local function value(v)
   local n = tonumber(v)
   if n then return v end
   if type(v) ~= 'string' then
      return tostring(v)
   end
   return ("%q"):format(v)
end

local function sorted_pairs(t)
   local keys = {}
   for k, v in pairs(t) do
      if type(k) == 'string' then
         keys[#keys+1] = k
      end
   end
   table.sort(keys)
   local i = 1
   return function()
      local k = keys[i]
      if k then
         i = i + 1
         return k, t[k]
      end
   end
end

local function sorted_ipairs(t)
   local keys = {}
   for k, v in pairs(t) do
      if type(k) == 'number' then
         keys[#keys+1] = k
      end
   end
   table.sort(keys)
   local i = 1
   return function()
      local k = keys[i]
      if k then
         i = i + 1
         return k, t[k]
      end
   end
end

local function dump_table(t, k, lvls)
   if t[k] then
      G'  '(lvls, key(k))' = {\n'
      for k,v in sorted_pairs(t[k]) do
         G'    '(lvls, key(k))' = '(value(v))';\n'
      end
      G'  '(lvls)'};\n'
   end
end

local function dump_enum(name, enum, lvl)
   local lvls = ('  '):rep(lvl)
   G(lvls, name)' = { type = "enum";\n'
   for k,v in sorted_ipairs(enum) do
      G'  '(lvls)(key(k))' = "'(v)'";\n'
   end
   dump_table(enum, 'map', lvls)
   dump_table(enum, 'deprecated_names', lvls)
   G(lvls)'};\n'
end

local function dump_message(name, msg, lvl)
   local lvls = ('  '):rep(lvl)
   G(lvls, name)' = { type = "message";\n'
   local nested = {}
   for k,v in sorted_ipairs(msg) do
      if v.type == "field" then
         G'  '(lvls)(key(k))' = { type = "field";'
         if v.scalar then G' scalar = true;' end
         if v.repeated then G' repeated = true;' end
         G'\n'
         G'    '(lvls)'name = "'(v.name)'";\n'
         if v.scalar then
            G'    '(lvls)'type_name = "'(v.type_name)'";\n'
         else
            G'    '(lvls)'type_name = { "'
               (table.concat(v.type_name, '","'))'" };\n'
         end
         if v.default_value then
            G'    '(lvls)'default_value = '(
               v.type_name == "bool" and v.default_value
                  or value(v.default_value))';\n'
         end
         if v.deprecated then
            G'    '(lvls)'deprecated = '(value(v.deprecated))';\n'
         end
         if v.lazy then
            G'    '(lvls)'lazy = '(value(v.lazy))';\n'
         end
         if v.packed then
            G'    '(lvls)'packed = '(value(v.packed))';\n'
         end
         G'  '(lvls)'};\n'
      end
   end
   dump_table(msg, 'map', lvls)
   if msg.defaults then
      G'  '(lvls)'defaults = {\n'
      for k,v in sorted_pairs(msg.defaults) do
         if msg[msg.map[k]].type_name == "bool" then
            G'    '(lvls, key(k))' = '(v)';\n'
         else
            G'    '(lvls, key(k))' = '(value(v))';\n'
         end
      end
      G'  '(lvls)'};\n'
   end
   for k,v in sorted_pairs(msg) do
      if v.type == "enum" then
         dump_enum(k, v, lvl+1)
      elseif v.type == "message" then
         dump_message(k, v, lvl+1)
      end
   end
   G(lvls)'};\n'
end

local function dump_info(info)
   local packages = {}
   local function _dfs(t, lvl)
      local lvls = ('  '):rep(lvl)
      for k, v in sorted_pairs(t) do
         if v.type == "package" then
            G(lvls)(key(k))' = { type = "package";\n'
            _dfs(v, lvl+1)
            G(lvls)'};\n'
         elseif v.type == "enum" then
            dump_enum(k, v, lvl)
         elseif v.type == "message" then
            dump_message(k, v, lvl)
         end
      end
   end
   G "-- auto-generated file, DO NOT MODIFY\n"
     "return {\n"
   _dfs(info, 1)
   G "}\n\n"
end

function pb.dump(filename, ptype)
   local realtype = typeinfo
   if ptype then
      realtype = type(ptype) == "table" and ptype
         or qualitied_type(ptype)
   end
   io.output(filename)
   dump_info(realtype)
   io.output():close()
   io.output(io.stdout)
end

------------------------------------------------------------

return pb
