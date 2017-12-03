
local function meta(name, t)
   t = t or {}
   t.__name  = name
   t.__index = t
   return t
end

local function default(t, k, def)
   local v = t[k]
   if not v then
      v = def or {}
      t[k] = v
   end
   return v
end

local Lexer = meta "Lexer" do

local escape = {
   a = "\a", b = "\b", f = "\f", n = "\n",
   r = "\r", t = "\t", v = "\v"
}

local function tohex(x) return string.byte(tonumber(x, 16)) end
local function todec(x) return string.byte(tonumber(x, 10)) end
local function toesc(x) return escape[x] or x end

function Lexer.new(name, src)
   local self = {
      name = name,
      src = src,
      pos = 1
   }
   return setmetatable(self, Lexer)
end

function Lexer:__call(patt, pos)
   return self.src:match(patt, pos or self.pos)
end

function Lexer:test(patt)
   self:whitespace()
   local pos = self('^'..patt..'%s*()')
   if not pos then return false end
   self.pos = pos
   return true
end

function Lexer:expected(patt, name)
   if not self:test(patt) then
      return self:error((name or "'"..patt.."'").." expected")
   end
   return self
end

function Lexer:pos2loc(pos)
   local linenr = 1
   pos = pos or self.pos
   for start, stop in self.src:gmatch "()[^\n]*()\n?" do
      if start <= pos and pos <= stop then
         return linenr, pos - start + 1
      end
      linenr = linenr + 1
   end
end

function Lexer:error(fmt, ...)
   local ln, co = self:pos2loc()
   return error(("%s:%d:%d: "..fmt):format(self.name, ln, co, ...))
end

function Lexer:opterror(opt, msg)
   if not opt then return self:error(msg) end
   return nil
end

function Lexer:whitespace()
   local pos, c = self "^%s*()(%/?)"
   self.pos = pos
   if c == '' then return self end
   return self:comment()
end

function Lexer:comment()
   local pos = self "^%/%/[^\n]*\n?()"
   if pos then
      if self "^%/%*" then
         pos = self "^%/%*.-%*%/()"
         if not pos then
            self:error "unfinished comment"
         end
      end
   end
   if not pos then return self end
   self.pos = pos
   return self:whitespace()
end

function Lexer:line_end(opt)
   self:whitespace()
   local pos = self '^[%s;]*%s*()'
   if not pos then
      return self:opterror(opt, "';' expected")
   end
   self.pos = pos
   return pos
end

function Lexer:eof()
   self:whitespace()
   return self.pos > #self.src
end

function Lexer:keyword(kw, opt)
   self:whitespace()
   local ident, pos = self "^([%a_][%w_]*)%s*()"
   if not ident or ident ~= kw then
      return self:opterror(opt, "''"..kw..'" expected')
   end
   self.pos = pos
   return kw
end

function Lexer:ident(name, opt)
   self:whitespace()
   local b, ident, pos = self "^()([%a_][%w_]*)%s*()"
   if not ident then
      return self:opterror(opt, (name or 'name')..' expected')
   end
   self.pos = pos
   return ident, b
end

function Lexer:full_ident(name, opt)
   self:whitespace()
   local b, ident, pos = self "^()([%a_][%w_.]*)%s*()"
   if not ident or ident:match "%.%.+" then
      return self:opterror(opt, (name or 'name')..' expected')
   end
   self.pos = pos
   return ident, b
end

function Lexer:integer(opt)
   self:whitespace()
   local ns, oct, hex, s, pos =
      self "^([+-]?)(0?)([xX]?)([0-9a-fA-F]+)%s*()"
   local n
   if oct == '0' and hex == '' then
      n = tonumber(s, 8)
   elseif oct == '' and hex == '' then
      n = tonumber(s, 10)
   elseif oct == '0' and hex ~= '' then
      n = tonumber(s, 16)
   end
   if not n then
      return self:opterror(opt, 'integer expected')
   end
   self.pos = pos
   return ns == '-' and -n or n
end

function Lexer:number(opt)
   self:whitespace()
   if self:test "nan%f[%A]" then
      return 0.0/0.0
   elseif self:test "inf%f[%A]" then
      return 1.0/0.0
   end
   local ns, d1, s, d2, pos = self "^([+-]?)(%.?)([0-9]+)(%.?)()"
   if not ns then
      return self:opterror(opt, 'floating-point number expected')
   end
   local es, pos2 = self("([eE][+-]?[0-9]+)%s*()", pos)
   if (d1 == "" and d2 == "" and not es) or (d1 == "." and d2 == ".") then
      return self:error "malformed floating-point number"
   end
   self.pos = pos2 or pos
   local n = tonumber(d1..s..d2..es)
   return ns == '-' and -n or n
end

function Lexer:quote(opt)
   self:whitespace()
   local q, start = self '^(["\'])()'
   if not start then
      return self:opterror(opt, 'string expected')
   end
   self.pos = start
   local patt = '()(\\?'..q..')%s*()'
   while true do
      local stop, s, pos = self(patt)
      if not stop then
         self.pos = start-1
         return self:error "unfinished string"
      end
      self.pos = pos
      if s == q then
         return self.src:sub(start, stop-1)
                        :gsub("\\x(%x+)", tohex)
                        :gsub("\\(%d+)", todec)
                        :gsub("\\(.)", toesc)
      end
   end
end

function Lexer:constant(opt)
   local c = self:full_ident('constant', 'opt') or
             self:integer('opt') or
             self:number('opt') or
             self:quote('opt')
   if not c and not opt then
      return self:error "constant expected"
   end
   return c
end

function Lexer:option_name()
   local ident
   if self:test "%(" then
      ident = self:full_ident "option name"
      self:expected "%)"
   else
      ident = self:ident "option name"
   end
   while self:test "%." do
      ident = ident .. "." .. self:ident()
   end
   return ident
end

function Lexer:type_name()
   if self:test "%." then
      local id, pos = self:full_ident "type name"
      return "."..id, pos
   else
      return self:full_ident "type name"
   end
end

end

local Parser = meta "Parser" do

function Parser.new()
   local self = {}
   self.typemap = {}
   self.loaded  = {}
   self.paths   = { "." }
   return setmetatable(self, Parser)
end

function Parser:error(msg)
   return self.lex:error(msg)
end

function Parser:addpath(path)
   self.paths[#self.paths+1] = path
end

function Parser:loadfile(name)
   for _, path in ipairs(self.paths) do
      local fh = io.open(path.."/"..name)
      if fh then
         local content = fh:read "*a"
         local info = self:parse(name, content)
         fh:close()
         return info
      end
   end
   error("can not find file: "..name)
end

-- parser

local labels = { optional = 1; required = 2; repeated = 3 }

local key_types = {
   int32    = 5;  int64    = 3;  uint32   = 13;
   uint64   = 4;  sint32   = 17; sint64   = 18;
   fixed32  = 7;  fixed64  = 6;  sfixed32 = 15;
   sfixed64 = 16; bool     = 8;  string   = 9;
}

local com_types = {
   group    = 10; message  = 11; enum     = 14;
}

local types = {
   double   = 1;  float    = 2;  int32    = 5;
   int64    = 3;  uint32   = 13; uint64   = 4;
   sint32   = 17; sint64   = 18; fixed32  = 7;
   fixed64  = 6;  sfixed32 = 15; sfixed64 = 16;
   bool     = 8;  string   = 9;  bytes    = 12;
   group    = 10; message  = 11; enum     = 14;
}

local function register_type(self, lex, tname, type)
   if not tname:match "%."then
      tname = self.prefix..tname
   end
   if self.typemap[tname] then
      return lex:error("type %s already defined", tname)
   end
   self.typemap[tname] = type
end

local function type_info(lex, tname)
   local tenum = types[tname]
   if com_types[tname] then
      return lex:error("invalid type name: "..tname)
   elseif tenum then
      tname = nil
   end
   return tenum, tname
end

local function map_info(lex)
   local keyt = lex:ident "key type"
   if not key_types[keyt] then
      return lex:error("invalid key type: "..keyt)
   end
   local valt = lex:expected "," :type_name()
   local name = lex:expected ">" :ident()
   local ident = name:gsub("^%a", string.upper)
                     :gsub("_(%a)", string.upper).."Entry"
   local kt, ktn = type_info(lex, keyt)
   local vt, vtn = type_info(lex, valt)
   return name, types.message, ident, {
      name = ident,
      field = {
         {
            name      = "key",
            number    = 1;
            label     = labels.optional,
            type      = kt,
            type_name = ktn
         },
         {
            name      = "value",
            number    = 2;
            label     = labels.optional,
            type      = vt,
            type_name = vtn
         },
      },
      options = { map_entry = true }
   }
end

local function inline_option(lex, info)
   if lex:test "%[" then
      info = info or {}
      while true do
         local name  = lex:option_name()
         local value = lex:expected '=' :constant()
         info[name] = value
         if lex:test "%]" then
            return info
         end
         lex:expected ','
      end
   end
end

local function field(lex, ident)
   local name, type, type_name, map_entry
   if ident == "map" and lex:test "%<" then
      name, type, type_name, map_entry = map_info(lex)
   else
      type, type_name = type_info(lex, ident)
      name = lex:ident()
   end
   local info = {
      name      = name;
      number    = lex:expected "=":integer();
      label     = labels.optional;
      type      = type;
      type_name = type_name;
   }
   local options = inline_option(lex)
   if options then
      info.default_value, options.default = options.default, nil
      info.json_name, options.json_name = options.json_name, nil
   end
   info.options = options
   if info.number <= 0 then
      lex:error("invalid tag number: "..info.number)
   end
   return info, map_entry
end

local function label_field(self, lex, ident)
   local label = labels[ident]
   local info, map_entry
   if not label then
      if self.syntax == "proto2" then
         return lex:error("proto2 disallow missing label")
      end
      return field(lex, ident)
   end
   if label == labels.optional and self.syntax == "proto3" then
      return lex:error("proto3 disallow 'optional' label")
   end
   info, map_entry = field(lex, lex:type_name())
   info.label = label
   return info, map_entry
end

local toplevel = {} do

function toplevel:package(lex, info)
   local package = lex:full_ident 'package name'
   lex:line_end()
   info.package = package
   self.prefix = "."..package.."."
   return self
end

function toplevel:import(lex, info)
   local mode = lex:ident('"weak" or "public"', 'opt') or "public"
   if mode ~= 'weak' and mode ~= 'public' then
      return lex:error '"weak or "public" expected'
   end
   local name = lex:quote()
   lex:line_end()
   self:loadfile(name)
   local dep = default(info, 'dependency')
   local index = #dep
   dep[index+1] = name
   if mode == "public" then
      local it = default(info, 'public_dependency')
      it[#it+1] = index
   else
      local it = default(info, 'weak_dependency')
      it[#it+1] = index
   end
end

local msg_body = {} do

function msg_body:message(lex, info)
   local nested_type = default(info, 'nested_type')
   nested_type[#nested_type+1] = toplevel.message(self, lex)
   return self
end

function msg_body:enum(lex, info)
   local nested_type = default(info, 'enum_type')
   nested_type[#nested_type+1] = toplevel.enum(self, lex)
   return self
end

function msg_body:extend(lex, info)
   local extension = default(info, 'extension')
   local nested_type = default(info, 'nested_type')
   toplevel.extend(self, lex, extension, nested_type)
   return self
end

function msg_body:extensions(lex, info)
   local rt = default(info, 'extension_range')
   repeat
      local start = lex:integer "field number range"
      local stop = math.floor(2^29)
      lex:keyword 'to'
      if not lex:keyword('max', 'opt') then
         stop = lex:integer "field number range end or 'max'"
      end
      rt[#rt+1] = { start = start, ['end'] = stop }
   until not lex:test ','
   lex:line_end()
   return self
end

function msg_body:reserved(lex, info)
   if lex:test '%a' then
      local rt = default(info, 'reserved_name')
      repeat
         rt[#rt+1] = lex:ident 'field name'
      until not lex:test ','
   else
      local rt = default(info, 'reserved_range')
      local first = true
      repeat
         local start = lex:integer(first and 'field name or number range'
                                    or 'field number range')
         if lex:keyword('to', 'opt') then
            local stop = lex:integer 'field number range end'
            rt[#rt+1] = { start = start, ['end'] = stop }
         else
            rt[#rt+1] = { start = start, ['end'] = start }
         end
         first = false
      until not lex:test ','
   end
   lex:line_end()
   return self
end

function msg_body:oneof(lex, info)
   local fs = default(info, "field")
   local ts = default(info, "nested_type")
   local ot = default(info, "oneof_decl")
   local index = #ot + 1
   local oneof = { name = lex:ident() }
   lex:expected "{"
   while not lex:test "}" do
      local ident = lex:type_name()
      if ident == "option" then
         toplevel.option(self, lex, oneof)
      else
         local f, t = field(lex, ident, "no_label")
         if t then ts[#ts+1] = t end
         f.oneof_index = index
         fs[#fs+1] = f
      end
      lex:line_end 'opt'
   end
   ot[index] = oneof
end

end

function toplevel:message(lex, info)
   local name = lex:ident 'message name'
   local type = { name = name }
   register_type(self, lex, name, types.message)
   local prefix = self.prefix
   self.prefix = prefix..name.."."
   lex:expected "{"
   while not lex:test "}" do
      local ident, pos = lex:type_name()
      local body_parser = msg_body[ident]
      if body_parser then
         body_parser(self, lex, type)
      else
         local fs = default(type, 'field')
         local f, t = label_field(self, lex, ident)
         self.locmap[f] = pos
         fs[#fs+1] = f
         if t then
            local ts = default(type, 'nested_type')
            ts[#ts+1] = t
         end
      end
      lex:line_end 'opt'
   end
   lex:line_end 'opt'
   if info then
      info = default(info, 'message_type')
      info[#info+1] = type
   end
   self.prefix = prefix
   return type
end

function toplevel:enum(lex, info)
   local name = lex:ident 'enum name'
   local enum = { name = name }
   register_type(self, lex, name, types.enum)
   lex:expected "{"
   while not lex:test "}" do
      local ident = lex:ident 'enum constant name'
      if ident == 'option' then
         toplevel.option(self, lex, default(enum, 'options'))
      else
         local values  = default(enum, 'values')
         local number  = lex:expected '=' :integer()
         lex:line_end()
         values[#values+1] = {
            name    = ident,
            number  = number,
            options = inline_option(lex)
         }
      end
      lex:line_end 'opt'
   end
   lex:line_end 'opt'
   if info then
      info = default(info, 'enum_type')
      info[#info+1] = enum
   end
   return enum
end

function toplevel:option(lex, info)
   local ident = lex:option_name()
   lex:expected "="
   local value = lex:constant()
   lex:line_end()
   local options = info and default(info, 'options') or {}
   options[ident] = value
   return options, self
end

function toplevel:extend(lex, info)
   local name = lex:type_name()
   local ft = info and default(info, 'extension') or {}
   local mt = info and default(info, 'message_type') or {}
   lex:expected "{"
   while not lex:test "}" do
      local ident, pos = lex:type_name()
      local f, t = label_field(self, lex, ident)
      self.locmap[f] = pos
      f.extendee = name
      ft[#ft+1] = f
      mt[#mt+1] = t
      lex:line_end 'opt'
   end
   return ft, mt
end

local svr_body = {} do

function svr_body:rpc(lex, info)
   local name, pos = lex:ident "rpc name"
   local rpc = { name = name }
   self.locmap[rpc] = pos
   local _, tn
   lex:expected "%("
   rpc.client_stream = lex:keyword("stream", "opt")
   _, tn = type_info(lex, lex:type_name())
   if not tn then return lex:error "rpc input type must by message" end
   rpc.input_type = tn
   lex:expected "%)" :expected "returns" :expected "%("
   rpc.server_stream = lex:keyword("stream", "opt")
   _, tn = type_info(lex, lex:type_name())
   if not tn then return lex:error "rpc output type must by message" end
   rpc.output_type = tn
   lex:expected "%)"
   if lex:test "{" then
      while not lex:test "}" do
         lex:line_end "opt"
         lex:keyword "option"
         toplevel.option(self, lex, default(rpc, 'options'))
      end
   end
   lex:line_end "opt"
   local t = default(info, "method")
   t[#t+1] = rpc
end

function svr_body.stream(_, lex)
   lex:error "stream not implement yet"
end

end

function toplevel:service(lex, info)
   local name = lex:ident 'service name'
   local svr = { name = name }
   lex:expected "{"
   while not lex:test "}" do
      local ident = lex:type_name()
      local body_parser = svr_body[ident]
      if body_parser then
         body_parser(self, lex, type)
      else
         return lex:error "expected 'rpc' or 'option' in service body"
      end
      lex:line_end 'opt'
   end
   lex:line_end 'opt'
   if info then
      info = default(info, 'enum_type')
      info[#info+1] = svr
   end
   return svr
end

end

function Parser:parse(name, src)
   local loaded = self.loaded[name]
   if loaded then
      if loaded == true then
         error("loop loaded: "..name)
      end
      return loaded
   end

   local lex = Lexer.new(name or "<input>", src)
   local info = { name = lex.name }
   if name then self.loaded[name] = true end
   local ctx = setmetatable({
      syntax  = "proto2";
      locmap  = {};
      prefix  = ".";
      lex     = lex;
      loaded  = self.loaded;
      typemap = self.typemap;
      paths   = self.paths;
   }, Parser)

   local syntax = lex:keyword('syntax', 'opt')
   if syntax then
      info.syntax = lex:expected '=' :quote()
      ctx.syntax  = info.syntax
      lex:line_end()
   end

   while not lex:eof() do
      local ident = lex:ident()
      local top_parser = toplevel[ident]
      if top_parser then
         top_parser(ctx, lex, info)
      else
         lex:error("unknown keyword '"..ident.."'")
      end
      lex:line_end "opt"
   end
   if name then self.loaded[name] = info end
   return ctx:resolve(lex, info)
end

-- resolver

local function empty() end

local function iter(t, k)
   local v = t[k]
   if v then return ipairs(v) end
   return empty
end

local function check_type(self, lex, tname)
   if tname:match "^%." then
      local t = self.typemap[tname]
      if not t then
         return lex:error("unknown type '%s'", tname)
      end
      return t, tname
   end
   local prefix = self.prefix
   for i = #prefix+1, 1, -1 do
      local op = prefix[i]
      prefix[i] = tname
      local tn = table.concat(prefix, ".", 1, i)
      prefix[i] = op
      local t = self.typemap[tn]
      if t then return t, tn end
   end
   return lex:error("unknown type '%s'", tname)
end

local function check_field(self, lex, info)
   if info.extendee then
      lex.pos = self.locmap[info]
      local t = check_type(self, lex, info.extendee)
      if t ~= types.message then
         lex:error("message type expected in extension")
      end
   end
   if info.type_name then
      local t, tn = check_type(self, lex, info.type_name)
      info.type      = t
      info.type_name = tn
   end
end

local function check_message(self, lex, info)
   self.prefix[#self.prefix+1] = info.name
   for _, v in iter(info, 'field') do
      lex.pos = self.locmap[v]
      check_field(self, lex, v)
   end
   for _, v in iter(info, 'extension') do
      lex.pos = self.locmap[v]
      check_field(self, lex, v)
   end
   self.prefix[#self.prefix] = nil
end

local function check_service(self, lex, info)
   for _, v in iter(info, 'method') do
      lex.pos = self.locmap[v]
      local t, tn = check_type(self, lex, v.input_type)
      v.input_type = tn
      if not t ~= types.message then
         lex:error "message type expected in parameter"
      end
      t, tn = check_type(self, lex, v.output_type)
      v.output_type = tn
      if not t ~= types.message then
         lex:error "message type expected in return"
      end
   end
end

function Parser:resolve(lex, info)
   self.prefix = { "", info.package }
   for _, v in iter(info, 'message_type') do
      check_message(self, lex, v)
   end
   for _, v in iter(info, 'service') do
      check_service(self, lex, v)
   end
   for _, v in iter(info, 'extension') do
      lex.pos = self.locmap[v]
      check_field(self, lex, v)
   end
   self.prefix = nil
   return info
end

end

local p = Parser.new()
local S = require "serpent"
print(S.block(p:loadfile "descriptor.proto"))

return Parser

