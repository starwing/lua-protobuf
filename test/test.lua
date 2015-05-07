package.path = "../?.lua;"..package.path
package.cpath = "../?.dll;../?.so;"..package.cpath
local decoder = require "pb.decoder"
local serpent = require "serpent"

local types = require "pb_typeinfo"

local function decode(dec, ptype)
   local t = {}
   repeat
      local ftype, repeated, value
      local tag, wiretype = dec:tag()
      local field = ptype[tag]
      if not field then
         value = dec:fetch(wiretype)
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
         goto next
      end

      if field.scalar then
         value = dec:fetch(wiretype)
      else
         ftype = types
         for k,v in ipairs(field.type_name) do
            ftype = ftype[v]
            if not ftype then break end
         end
         if ftype.type == "enum" then
            value = dec:fetch(wiretype)
            value = ftype[value] or value
         else
            local len = dec:fetch "varint"
            local old = dec:len(dec:pos() + len - 1)
            value = decode(dec, ftype)
            dec:len(old)
         end
      end

      if not field.repeated then
         t[field.name] = value
      elseif not t[field.name] then
         t[field.name] = { value }
      else
         local ft = t[field.name]
         ft[#ft+1] = value
      end

      ::next::
   until dec:finished()
   return t
end

--------------------------------------------------

local files = {}
local info = {}

local scalar_typemap = {
   TYPE_BOOL = "bool";
   TYPE_BYTES = "bytes";
   TYPE_DOUBLE = "double";
   TYPE_ENUM = "enum";
   TYPE_FIXED32 = "fixed32";
   TYPE_FIXED64 = "fixed64";
   TYPE_FLOAT = "float";
   TYPE_GROUP = "group";
   TYPE_INT32 = "int32";
   TYPE_INT64 = "int64";
   TYPE_MESSAGE = "message";
   TYPE_SFIXED32 = "sfixed32";
   TYPE_SFIXED64 = "sfixed64";
   TYPE_SINT32 = "sint32";
   TYPE_SINT64 = "sint64";
   TYPE_STRING = "string";
   TYPE_UINT32 = "uint32";
   TYPE_UINT64 = "uint64";
}

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
   local cur = info
   for pkg in package:gmatch "[^.]+" do
      cur = subtable(cur, pkg, "package")
   end
   return cur
end

local function split_qname(name)
   local t = {}
   for comp in name:gmatch "[^.]+" do
      t[#t+1] = comp
   end
   return t
end

local function load_field(msg, field)
   local t = msg[field.number]
   if t then
      error(("message '%s': field '%s' tag(%d) conflict with field '%s'")
         :format(msg.name, field.name, field.number, t.name))
   end
   if field.extendee then
      error(("message '%s': field '%s' should not have extendee!")
         :format(msg.name, field.name))
   end
   t = {}
   msg[field.number] = t
   t.type = "field"
   t.name = field.name
   t.repeated = field.label == 'LABEL_REPEATED'
   if field.type_name then
      t.type_name = split_qname(field.type_name)
   else
      t.type_name = scalar_typemap[field.type]
      t.scalar = true
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
   if enum.value then
      for i, v in ipairs(enum.value) do
         t[v.number] = v.name
      end
   end
   return t
end

local function load_message(pkg, msg)
   local t = subtable(pkg, msg.name, "message")
   if msg.field then
      for i, v in ipairs(msg.field) do
         load_field(t, v)
      end
   end
   if msg.extension then
      for i, v in ipairs(msg.extension) do
         load_extension(v)
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
   for k,v in pairs(t) do
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
      if not files[v.name] then
         files[v.name] = true
         load_file(v)
      end
   end
end

local function make_namemap(info)
   for k,v in pairs(info) do
      if v.type == "package" then
         make_namemap(v)
      elseif v.type == "enum" then
         local map = {}
         for k,v in pairs(v) do
            if type(k) == "number" then
               map[v] = k
            end
         end
         v.map = map
      elseif v.type == "message" then
         local map = {}
         for k,v in pairs(v) do
            if v.type == "field" then
               map[v.name] = k
            end
         end
         v.map = map
         make_namemap(v)
      end
   end
end

--------------------------------------------------

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

local function dump_enum(name, enum, lvl)
   local lvls = ('  '):rep(lvl)
   G(lvls, name)' = { type = "enum";\n'
   for k,v in sorted_ipairs(enum) do
      if k ~= 'type' then
         G'  '(lvls)(key(k))' = "'(v)'";\n'
      end
   end
   if enum.map then
      G'  '(lvls)'map = {\n'
      for k,v in sorted_pairs(enum.map) do
         G'    '(lvls, key(k))' = '(v)';\n'
      end
      G'  '(lvls)'};\n'
   end
   G(lvls)'};\n'
end

local function dump_message(name, msg, lvl)
   local lvls = ('  '):rep(lvl)
   G(lvls, name)' = { type = "message";\n'
   local nested = {}
   for k,v in sorted_ipairs(msg) do
      if v.type == "enum" then
         dump_enum(k, v, lvl+1)
      elseif v.type == "message" then
         dump_message(k, v, lvl+1)
      elseif v.type == "field" then
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
            G'    '(lvls)'default_value = "'(v.default_value)'";\n'
         end
         G'  '(lvls)'};\n'
      end
   end
   if msg.map then
      G'  '(lvls)'map = {\n'
      for k,v in sorted_pairs(msg.map) do
         G'    '(lvls, key(k))' = '(v)';\n'
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
            G(lvls)'}\n'
         elseif v.type == "enum" then
            dump_enum(k, v, lvl)
         elseif v.type == "message" then
            dump_message(k, v, lvl)
         end
      end
   end
   G [[
-- auto-generated file, DO NOT MODIFY
return {
]]
   _dfs(info, 1)
   G [[
}
]]

end

--------------------------------------------------

local content = require "pb.io".read "descriptor.pb"
local t = decode(decoder.new(content), types.google.protobuf.FileDescriptorSet)
load_fileset(t)
io.output "types.lua"
make_namemap(info)
dump_info(info)
print(serpent.block(t))

