# 在Lua中操作Google protobuf格式数据

[![Build Status](https://img.shields.io/github/workflow/status/starwing/lua-protobuf/CI)](https://github.com/starwing/lua-protobuf/actions?query=branch%3Amaster)[![Coverage Status](https://img.shields.io/coveralls/github/starwing/lua-protobuf)](https://coveralls.io/github/starwing/lua-protobuf?branch=master)

[English](https://github.com/starwing/lua-protobuf/blob/master/README.md) | 中文

---

Urho3d集成说明：https://note.youdao.com/ynoteshare1/index.html?id=20d06649fab669371140256abd7a362b&type=note

Unreal SLua集成：https://github.com/zengdelang/slua-unreal-pb

Unreal UnLua集成：https://github.com/hxhb/unlua-pb

ToLua集成说明：[链接](http://changxianjie.gitee.io/unitypartner/2019/10/01/tolua中使用protobuf3—集成lua-protobuf/)

xlua集成：[链接](https://github.com/91Act/build_xlua_with_libs)

QQ群：485016061 [![lua-protobuf1交流群](https://pub.idqqimg.com/wpa/images/group.png)](https://shang.qq.com/wpa/qunwpa?idkey=d7e2973604a723c4f77d0a837df39be26e15be2c2ec29d5ebfdb64f94e74e6ae)

本项目提供在 Lua 全版本（5.1+、LuaJIT）下的protobuf 2/3 版本支持。提供了高级的消息编码/解码接口以及底层的protobuf wireformat 二进制数据操作接口。

使用高级接口，你需要通过 `pb.load()` 接口导入 protobuf 的消息描述文件（schema文件，.proto后缀名）的二进制版本（本质上是schema文件通过官方的 `FileDescriptorSet` 编码得到的二进制pb消息），导入的信息被存储在称为“state”的内存数据库中供消息编码/解码使用。你也可以通过`pb`模块提供的一系列接口来读取这个数据库里的内容。

要使用底层接口，你需要使用下面几个库提供的功能：

- `pb.slice`：读取二进制的wireformat信息。
- `pb.buffer`：写入二进制wireformat信息。
- `pb.conv`：在Lua的数字类型和protobuf提供的一众数字数据类型之间转换。
- `pb.io`：用于将`pb`模块用于工具当中使用：通过标准输入输出流读取写入二进制消息。

另外，为了得到schema文件的二进制版本（一般后缀名是.pb的文件），你需要官方protobuf项目提供的schema编译器二进制`protoc.exe`工具，如果在你的平台下获得这个工具太麻烦，或者你希望能有一个小体积的protobuf编译工具，你可以使用项目自带的另一个独立的纯 Lua库：`protoc.lua`文件。该文件提供了纯Lua实现的schema文件编译器，并集成了通过调用`pb.load()`载入编译结果的方便接口。但是要注意因为这个库是纯Lua实现的，它编译的速度会非常慢，如果你的schema文件太大或者编译的时候遇到了性能瓶颈。还是推荐你通过`protoc.exe`或者在开发时利用`pb`库自己写脚本将schema编译成.pb文件供生产环境使用。

## 安装

**注意**：`lua-prootbuf`毕竟是个纯C的Lua库，而Lua库的编译安装是**有门槛**的。如果遇到了问题，建议询问**有Lua的C模块使用经验**的人，或者参阅《Lua程序设计》里的相关内容，预先学习相关知识。

另外，Lua的C模块是通用的，任何使用Lua的环境下都可以使用，然而XLua等C#环境通常有自己的一套规则，需要一些额外的集成操作。请确认**自己对这些环境集成C模块足够了解**，或者**咨询足够了解的人**获得帮助。这里只提供通用C模块的安装方法。

最简单的安装方法是使用Lua生态的包管理器`luarocks`进行安装（注意，这样**依然需要你的电脑上装有C编译器**，如果安装失败，你应该首先检查你的`luarocks`能否正常工作，即，能否正常安装其他Lua C模块）：

```shell
luarocks install lua-protobuf
```

你也可以使用`luarocks`从源码安装（这样装的版本会更新一些）：

```shell
git clone https://github.com/starwing/lua-protobuf
luarocks make rockspecs/lua-protobuf-scm-1.rockspec
```

如果你没有/不会配置`luarocks`，有一个Python写的方便脚本可以在你的电脑上安装`luarocks`——还是需要你有能正常运行的C编译器——当然，也需要你有Python。

```shell
pip install hererocks
git clone https://github.com/starwing/lua-protobuf
hererocks -j 2.0 -rlatest .
bin/luarocks make lua-protobuf/rockspecs/lua-protobuf-scm-1.rockspec CFLAGS="-fPIC -Wall -Wextra" LIBFLAGS="-shared"
cp protoc.lua pb.so ..
```

如果你想尝试Hard模式的话，自己手动编译也是可行的。

这是macOS的编译命令行：

```shell
gcc -O2 -shared -undefined dynamic_lookup pb.c -o pb.so
```

Linux的：

```shell
gcc -O2 -shared -fPIC pb.c -o pb.so
```

Windows的（注意那个Lua_BUILD_AS_DLL！在Windows上必须带这个预处理宏编译）：

```shell
cl /O2 /LD /Fepb.dll /I Lua53\include /DLUA_BUILD_AS_DLL pb.c Lua53\lib\lua53.lib
```

## 样例

```lua
local pb = require "pb"
local protoc = require "protoc"

-- 直接载入schema (这么写只是方便, 生产环境推荐使用 protoc.new() 接口)
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

-- lua 表数据
local data = {
   name = "ilse",
   age  = 18,
   contacts = {
      { name = "alice", phonenumber = 12312341234 },
      { name = "bob",   phonenumber = 45645674567 }
   }
}

-- 将Lua表编码为二进制数据
local bytes = assert(pb.encode("Person", data))
print(pb.tohex(bytes))

-- 再解码回Lua表
local data2 = assert(pb.decode("Person", bytes))
print(require "serpent".block(data2))

```

## 使用案例

[![零境交错](https://img.tapimg.com/market/images/e59627dc9039ff22ba7d000b5c9fe7f6.jpg?imageView2/2/h/560/q/40/format/jpg/interlace/1/ignore-error/1)](http://djwk.qq.com)



## 接口文档

请注意接口文档有些是`.`有些是`:`！`.`代表这是静态函数，直接调用即可，`:`代表这是一个方法，需要在一个对象上调用。

### `protoc` 模块

| 接口            | 返回    | 描述                                          |
| --------------- | ------- | --------------------------------------------- |
| `protoc.new()`      | 编译器对象 | 创建一个新的编译器对象                                |
| `protoc.reload()`   | `true`     | 重新载入谷歌标准的schema信息（编译需要用到）          |
| `p:parse(string[, filename])`   | table      | 将文本schema信息转换成 `DescriptorProto` 消息的Lua表  |
| `p:compile(string[, filename])` | string     | 将文本schema信息转换成二进制.pb文件数据 |
| `p:load(string[, filename])`    | `true`     | 将文本schema信息转换后，调用`pb.load()`载入内存数据库 |
| `p.loaded`          | table      | 一个包含了所有已载入的 `DescriptorProto` 表的缓存表   |
| `p.unknown_import`  | 详情见下   | 处理schema里`import`语句找不到文件的回调              |
| `p.unknown_type`    | 详情见下   | 处理schema里未知类型的回调                            |
| `p.include_imports` | bool       | 编译结果中包含所有`import`的文件    |

要编译一个文本的schema信息，首先，生成一个编译器实例。一个编译器实例会记住你用它编译的每一个scehma文件，从而能够正确处理schema之间的import关系：

```lua
local p = protoc.new()
```

生成了编译器实例之后，可以给编译器实例设置一些选项或者回调：

```lua
-- 设置回调……
p.unknown_import = function(self, module_name) ... end
p.unknown_type   = function(self, type_name) ... end
-- ……和选项
p.include_imports = true
```

`unknown_import`和`unknown_type`可以被设置成多种类型的值：如果被设置成`true`，则所有不存在的模块或者消息类型会自动生成一个默认值（空模块/空消息）而不会报错。`pb.load()`会自动处理空消息的合并，因此这样载入信息也不会出错。如果设置一个字符串值，那么这个字符串是一个Lua的正则表达式，满足正则表达式的模块或者消息类型会被设置成默认值，而不满足的则会报错：

```lua
p.unknown_type = "Foo.*"
```

上面的选项意味着所有`Foo`包里的消息会被当作好像已经载入了，即使没找到也不会报错。

如果这些回调被设置成一个函数，这个函数会在找不到模块/消息的时候被调用。调用的时候会传入找不到的模块/消息的名字，你需要返回一个`DescriptorProto`数据表（对模块而言），或者一个类型的名字和这个类型的实际分类，比如说`message`或者`enum`，如下所示：

```lua
function p:unknown_import(name)
  -- 如果找不到 "foo.proto" 文件而调用了这个函数，那就自己手动载入 "my_foo.proto" 文件并返回信息
  return p:parsefile("my_"..name)
end

function p:unknown_type(name)
  -- 如果找不到 "Type"， 那就把它当 "MyType" 消息编译好了，注意前面那个“.”，那是包名。
  return ".My"..name, "message"
end
```

设置好这些选项或者回调以后，使用`load()`或`compile()`或`parse()`来按照你的需求得到想要的结果。这些函数都需要直接传入scehma的文本内容作为参数，可以可选地多传入当前schema对应的文件名，用于方便schema之间的`import`指令找到对应的文件，但是除非设置了`include_imports`，否则`import`指令即使找到了对应的文件也不会编译/加载对应文件，只是会检查类型引入是否正确。即使没有设置`include_imports`，也可以手动按照拓扑顺序依次load对应文件从而加载所有schema。

### `pb` 模块

`pb`模块提供了编码/解码信息的高级接口、内存schema数据库的载入和读取接口，以及其他的一些方便的工具函数。

下面的表格里给出了`pb`模块里所有的函数，注意表格中的中“返回”一栏中的一些特殊返回值，这些返回值有一些约定俗成的含义：

- `type`：这个返回值代表返回的是一个代表类型的字符串。`".Foo"`代表没有包名的proto文件里的Foo消息，而`"foo.Foo"`代表写了`package foo;`包名的proto文件里的Foo消息。

- `data`：一个字符串，或者`pb.Slice`对象或者`pb.Buffer`对象，反正是个能表示二进制数据的东西

- `iterator`：返回一个能在for in语句里使用的迭代器对象，比如说：

  ```lua
  for name in pb.types() do
    print(name)
  end
  ```

**注意**：只有`pb.load()`通过返回值返回是否出错，你要用`assert(pb.load(...))`去调用这个函数！其他的函数会直接扔一个错误异常，不需要你对返回值调用`assert()`函数。

| 接口            | 返回    | 描述                                          |
| --------------- | ------- | --------------------------------------------- |
| `pb.clear()`                   | None            | 清除所有类型                                            |
| `pb.clear(type)`               | None            | 清除特定类型                                            |
| `pb.load(data)`                | boolean,integer | 将一个二进制schema信息载入内存数据库                    |
| `pb.encode(type, table)`       | string          | 将table按照type消息类型进行编码                         |
| `pb.encode(type, table, b)`    | buffer          | 同上，但是编码进额外提供的buffer对象里并返回            |
| `pb.decode(type, data)`        | table           | 将二进制data按照type消息类型解码为一个表                |
| `pb.decode(type, data, table)` | table           | 同上，但是解码到你提供的表里                            |
| `pb.pack(fmt, ...)`            | string          | 同 `buffer.pack()` ，但直接用字符串返回二进制数据 |
| `pb.unpack(data, fmt, ...)`    | values...       | 同 `slice.unpack()` 但是接受任何二进制类型数据    |
| `pb.types()`                   | iterator        | 遍历内存数据库里所有的消息类型，返回具体信息 |
| `pb.type(type)`                | 详情见下        | 返回内存数据库特定消息类型的具体信息          |
| `pb.fields(type)`              | iterator        | 遍历特定消息里所有的域，返回具体信息 |
| `pb.field(type, string)`       | 详情见下   | 返回特定消息里特定域的具体信息 |
| `pb.field(type, number)` | 详情见下 | 返回特定消息里特定域的具体信息 |
| `pb.typefmt(type)`             | String          | 得到 protobuf 数据类型名对应的 pack/unpack 的格式字符串 |
| `pb.enum(type, string)`        | number          | 提供特定枚举里的名字，返回枚举数字 |
| `pb.enum(type, number)`        | string          | 提供特定枚举里的数字，返回枚举名字 |
| `pb.defaults(type[, boolean])` | table           | 获得或设置特定消息类型的默认表 |
| `pb.hook(type[, function])`    | function        | 获得或设置特定消息类型的解码钩子 |
| `pb.option(string)`            | string          | 设置编码或解码的具体选项 |
| `pb.state()`                   | `pb.State`      | 返回当前的内存数据库 |
| `pb.state(newstate \| nil)`    | `pb.State`      | 设置或删除当前的内存数据库，返回旧的内存数据库 |

#### 内存数据库载入 Schema 信息

`pb.load()` 接受一个二进制的schema数据，并将其载入到内存数据库中。如果载入成功则返回`true`，否则返回`false`，无论成功与否，都会返回读取的二进制数据的字节数。如果载入失败，你可以检查在这个字节位置周围是否有数据错误的情况，比如被`NUL`字符截断等等的问题。

二进制流中是什么样的schema，就会载入什么样的schema。通常只能载入一个文件。如果需要同时载入多个文件（比如包括import后的文件，或者多个不相干文件），可以通过在使用`protoc.exe`或者`protoc.lua`编译二进制schema的时候编译多个文件，或者使用`include_imports`在二进制数据中包含多个文件的内容实现。注意根据protobuf的特性，直接将多个schema二进制数据连接在一起载入也是可行的。


#### 类型映射

| Protobuf 类型                                     | Lua 类型                                                    |
| ------------------------------------------------- | ----------------------------------------------------------- |
| `double`, `float`                                  | `number`                                                   |
| `int32`, `uint32`, `fixed32`, `sfixed32`, `sint32` | `number` 或 `integer` （Lua 5.3+）                         |
| `int64`, `uint64`, `fixed64`, `sfixed64`, `sint64` | `number` 或 `"#"` 打头的 `string` 或 `integer` （Lua 5.3+） |
| `bool`                                             | `boolean`                                                  |
| `string`, `bytes`                                  | `string`                                                   |
| `message`                                          | `table`                                                    |
| `enum`                                             | `string` 或 `number`                                       |

#### 内存数据库信息获取

可以使用`pb.type()`、`pb.types()`、`pb.field()`、`pb.fields()`系列函数获取内存数据库内的消息类型信息。

内存数据库存储了可以编码/解码的所有的消息类型信息，如果内存数据库中无法查询到对应信息，则编码/解码可能失败。使用*限定后的消息类型*名字就可以获取对应的类型信息。比如`foo` 包里的`Foo`消息的*限定消息类型名字*是`".foo.Foo"`，如果没有包名，则直接在消息名前面加`"."`，比如`".Foo"`就是没有包名的`Foo`消息的限定名称。

通过调用`pb.type()`，你可以获得下面的信息：

- name：即限定的消息类型名称（如`".package.TypeName"`）
- basename：即去除了包名的消息类型名称（如`"TypeName"`）
- type：`"map"` | `"enum"` | `"message"`，消息的实际类型——`MapEntry`类型，或者枚举，或者消息。

`pb.types()`返回了一个迭代器，就好像对内存数据库里存储的每个类型调用 `pb.type()`一样：

```lua
-- 打印出 MyType 消息的详细信息
print(pb.type "MyType")

-- 列出内存数据库里存储的所有消息类型的信息
for name, basename, type in pb.types() do
  print(name, basename, type)
end
```

`pb.field()` 返回了一个特定的消息里的一个域的详细信息：

- name: 域名
- number: schema中该域的对应数字（序号）
- type: 域类型
- default value: 域的默认值，没有的话是`nil`
- `"packed"`|`"repeated"`| `"optional"`：域的标签，注意并不支持`required`，会被当作`optional`
- oneof_name：域所属的oneof块的名字，可选
- , oneof_index：域所属的oneof块的索引，可选

 `pb.fields()` 会返回一个好像对消息类型里的每个域调用`pb.field()`一样的迭代器对象:

```lua
-- 打印 MyType 消息类型里 the_first_field 域的详细信息
print(pb.field("MyType", "the_first_field"))

-- 遍历 MyType 消息类型里所有的域，注意你并不需要写全所有的详细信息（后面的信息会被忽略掉）
for name, number, type in pb.fields "MyType" do
  print(name, number, type) -- 只需要打印这三个
end
```

`pb.enum()` 转换枚举的名字和值：

```lua
protoc:load [[
enum Color { Red = 1; Green = 2; Blue = 3 }
]]
print(pb.enum("Color", "Red")) --> 1
print(pb.enum("Color", 2)) --> "Green"
```

其实枚举本身就是一种特殊的“消息类型”，在内存数据库里，枚举和消息其实没什么区别，因此使用`pb.field()`也能做到枚举的名字和值之间的转换，`pb.enum()`这个名字更多的只是语义上的区别，另外因为只返回一个值，可能会相对快一些。

#### 默认值

你可以调用`pb.defaults()`	函数得到对应一个消息类型的一张Lua表，这张表存储了该消息类型所有域的默认值。

`pb.defaults()`函数的第一个参数是指定的消息类型名称，如果可选的第二个参数为true，那么该缓存的默认表会被从内存数据库中清除。

其实通过`pb.decode("Type")`本身就能得到一张填充了默认值的Lua表。这个函数的目的是，它提供的表会被内存数据库记住，如果你设置了`use_default_metatable`这个选项，那么这个默认值表就会成为对应类型被解码时被自动设置的原表——也就是说，可以支持解码一个空表，但是你能通过元表取得所有域的默认值，示例如下：

```lua
   check_load [[
      message TestDefault {
         optional int32 defaulted_int = 10 [ default = 777 ];
         optional bool defaulted_bool = 11 [ default = true ];
         optional string defaulted_str = 12 [ default = "foo" ];
         optional float defaulted_num = 13 [ default = 0.125 ];
      } ]]
   print(require "serpent".block(pb.defaults "TestDefault"))
-- output:
-- {
--   defaulted_bool = true,
--   defaulted_int = 777,
--   defaulted_num = 0.125,
--   defaulted_str = "foo"
-- } --[[table: 0x7f8c1e52b050]]

```

#### 钩子

如果通过`pb.option "enable_hooks"`启用了钩子功能，那么你可以通过`pb.hook()`函数为指定的消息类型设置一个解码钩子。一个钩子是一个会在该消息类型所有的域都被读取完毕之后调用的一个函数。你可以在这个时候对这个已经读取完毕的消息表做任何事。比如设置上一节提到的元表。

`pb.hook()`的第一个参数是指定的消息类型名称，第二个参数就是钩子函数了。如果第二个参数是`nil`，则这个类型的钩子函数会被清除；任何情况下，`pb.hook()`函数都会返回之前设置过的钩子函数（或者`nil`）。

钩子函数只有一个参数，即当前已经处理完的消息表。如果你同时还需要消息类型名称，那么可以使用以下工具函数：

```lua
local function make_hook(name, func)
  return pb.hook(name, function(t)
    return func(name, t)
  end)
end
```

#### 选项

你可以通过调用`pb.option()`函数设置选项来改变编码/解码时的行为。

目前支持的选项如下：

| 选项                  | 描述                                                  |
| --------------------- | ----------------------------------------------------- |
| `enum_as_name`          | 解码枚举的时候，设置值为枚举名 **(默认)** |
| `enum_as_value`         | 解码枚举的时候，设置值为枚举值数字 |
| `int64_as_number`       | 如果值的大小小于uint32允许的最大值，则存储整数，否则存储Lua浮点数类型（$\le$ Lua 5.2，可能会导致不精确）或者64位整数类型（$\ge$ Lua 5.3，这个版本开始才支持64位整数类型） **(默认)** |
| `int64_as_string`       | 同上，但返回一个前缀`"#"`的字符串以避免精度损失 |
| `int64_as_hexstring`    | 同上，但返回一个16进制的字符串 |
| `auto_default_values`   | 对于 proto3，采取 `use_default_values` 的设置；对于其他 protobuf 格式，则采取 `no_default_values` 的设置 **(默认)** |
| `no_default_values`     | 忽略默认值设置 |
| `use_default_values`    | 将默认值表复制到解码目标表中来 |
| `use_default_metatable` | 将默认值表作为解码目标表的元表使用 |
| `enable_hooks`          | `pb.decode` 启用钩子功能      |
| `disable_hooks`         | `pb.decode` 禁用钩子功能 **(默认)**            |
| `encode_default_values` | 默认值也参与编码 |
| `no_encode_default_values` | 默认值不参与编码 **(默认)** |
| `decode_default_array`  | 配合`no_default_values`选项，对于数组，将空值解码为空表 |
| `no_decode_default_array`  | 配合`no_default_values`选项，对于数组，将空值解码为nil **(默认)** |
| `encode_order`          | 保证对相同的schema和data，`pb.encode`编码出的结果一致。注意这个选项会损失效率 |
| `no_encode_order`       | 不保证对相同输入，`pb.encode`编码出的结果一致。**(默认)** |
| `decode_default_message`  | 将空子消息解析成默认值表 |
| `no_decode_default_message`  | 将空子消息解析成 `nil`  **(default)** |


 *注意*： `int64_as_string` 或 `int64_as_hexstring` 返回的字符串会带一个 `'#'` 字符前缀，因为Lua会自动把数字表示的字符串当作数字使用，从而导致精度损失。带一个前缀会让Lua认为这个字符串并不是数字，从而避免了Lua的自动转换。

本模块中所有接受数字参数的函数都支持使用带`'#'`前缀的字符串用于表示数字，无论是否开启了相关的选项都是如此。如果需要表格中提供的数字，也同样支持使用前缀字符串指定。

#### 多内存数据库

`pb` 模块支持同时存在多个内存数据库，但是你每次只能使用其中的一个。内存数据库仅仅存储所有的类型。默认值表、选项等等不受影响。你可以通过`pb.state()`函数来获得/设置内存数据库。

如果要新建一个内存数据库，调用 `pb.state(nil)` 函数清除当前内存数据库（会在返回值中返回），如果在调用`pb.load()`函数载入消息的类型的时候发现当前没有内存数据库，那么载入器会自动创建一个新的内存数据库。从而支持多个内存数据库同时存在。你可以同样使用`pb.state()`	函数来切换这多个内存数据库。

下面的示例能让你在独立的内存数据库中完成某些操作：

```lua
local old = pb.state(nil) -- 清空当前内存数据库
-- 如果要使用 protoc.lua, 这里还需要调用 protoc.reload() 函数
assert(pb.load(...)) -- 载入新的消息类型信息
-- 开始编码/解码 ...
pb.state(old) -- 恢复旧的内存数据库（并丢弃刚才新建的那个）
```

需要注意的是 `protoc.Lua` 模块会注册一些Google标准消息类型到内存数据库中，因此一定要记得在创建新的内存数据库之后，调用 `proto.reload()` 函数恢复这些信息。

### `pb.io` 模块

`pb.io` 模块从文件或者 `stdin`/`stdout`中读取或者写入二进制数据。提供这个模块的目的是在Windows下，Lua没有二进制读写标准输入输出的能力。然而要实现一个官方的`protoc`插件则必须能够读写二进制的标准输入输出流。因为官方的`protoc`找到插件以后会用插件启动新进程，然后把读取编译好的proto文件的内容用二进制的`FileDescriptorSet`消息的格式发给新进程的`stdin`。所以提供了这个插件，才可以用纯Lua写官方的插件。

 `pb.io.read(filename)` 负责从提供的文件名指定的文件里读取二进制数据，如果不提供文件名，那么就直接从 `stdin` 读取。

`pb.io.write()` 和 `pb.io.dump()` 和Lua标准库里的 `io.write()` 是一样的，只是会写二进制数据。前者写`stdout`，而后者写到第一个参数提供的文件名所指定的文件中。

这些函数执行成功的时候都会返回`true`，执行失败的时候会返回 `nil, errmsg`，所以调用的时候记得用`assert()`包住以捕获错误。

| 接口            | 返回    | 描述                                          |
| --------------- | ------- | --------------------------------------------- |
| `io.read()`            | string  | 从 `stdin`读取所有二进制数据 |
| `io.read(string)`      | string  | 从文件中读取所有二进制数据     |
| `io.write(...)`        | true    | 将二进制数据写入 `stdout` |
| `io.dump(string, ...)` | string  | write binary data to file name      |

### `pb.conv` 模块

`pb.conv` 能够在Lua和protobuf提供的各种数字类型之间进行转换。如果你要使用底层接口，那么这个模块就会很有用。

| Encode Function        | Decode Function        |
| ---------------------- | ---------------------- |
| `conv.encode_int32()`  | `conv.decode_int32()`  |
| `conv.encode_uint32()` | `conv.decode_uint32()` |
| `conv.encode_sint32()` | `conv.decode_sint32()` |
| `conv.encode_sint64()` | `conv.decode_sint64()` |
| `conv.encode_float()`  | `conv.decode_float()`  |
| `conv.encode_double()` | `conv.decode_double()` |

### `pb.slice` 模块

“Slice”是一种类似于“视图”的对象，它代表某个二进制数据的一部分。使用`slice.new()`可以创建一个slice视图，它会自动关联你传给new函数的那个对象，并且在它之上获取一个指针用于读取二进制的底层wireformat信息。

slice对象最重要的方法是`slice:unpack()`，它的第一个参数是一个格式字符串，每个格式字符代表需要解码的一个类型。具体的格式字符下面会用表格的形式给出，这些格式字符也可以使用`pb.typefmt()`函数从protobuf的基础类型的名字转换而来。请注意，`pb.buffer`模块的重要方法`buffer:pack()`使用的是同一套格式字符：

| 格式字符 | 描述                                                                   |
| ------   | ------------------------------------------------------------           |
| v        | 基础类型，变长的整数类型，1到10个字节（`varint`）                      |
| d        | 基础类型，4 字节数字类型                                               |
| q        | 基础类型，8 字节数字类型                                               |
| s        | 基础类型，带长度数据通常是 `string`, `bytes` 或者 `message` 类型的数据 |
| c        | fmt之后额外接受一个数字参数 `count`，直接读取接下来的 `count` 个字节   |
| b        | 布尔类型：`bool`                                                       |
| f        | 4 字节浮点数类型：`float`                                              |
| F        | 8 字节浮点数类型：`double`                                             |
| i        | `varint`表示的32位有符号整数：`int32`                                    |
| j        | `varint`表示的zig-zad 编码的有符号32位整数：`sint32`                     |
| u        | `varint`表示的32位无符号整数：`uint32`                                   |
| x        | 4 字节的无符号32位整数：`fixed32`                                      |
| y        | 4 字节的有符号32位整数：`sfixed32`                                     |
| I        | `varint`表示的64位有符号整数：`int64`                                    |
| J        | `varint`表示的zig-zad 编码的有符号64位整数：`sint64`                     |
| U        | `varint`表示的64位无符号整数：`uint64`                                   |
| X        | 4 字节的无符号32位整数：`fixed32`                                      |
| Y        | 4 字节的有符号32位整数：`sfixed32`                                     |

slice对象内部会维护“当前读到哪儿”的位置信息。每当使用unpack读取的时候，会自动指向下一个待读取的偏移，可以使用`#slice` 方法得知“还剩下多少字节的数据没有读”。下面的表给出了操控“当前位置”的“格式字符”——注意，这些格式字符只能用在`unpack`函数里，`pack`是不给用的：

| 格式字符 | 描述                                                         |
| ------   | ------------------------------------------------------------ |
| @        | 返回1开始的当前读取位置偏移，以当前视图开始为1               |
| *        | fmt参数后提供一个额外参数，直接设置偏移为这个参数的值        |
| +        | fmt参数后提供一个额外参数，设置偏移为加上这个参数以后的值    |

下面是一个“将一个 `varint` 类型的值读取两次”的例子：
```lua
local v1, v2 = s:unpack("v*v", 1)
-- v: 读取一个 varint
-- *: 接受下一个参数（这里是1），将其设置为当前位置：现在读取位置回到一开始了
-- v: 现在，把上一个 varint 再读一遍
```

除了读取位置以外，slice还支持“进入”和“退出”视图，也就是视图栈的功能。比如说，你的协议里是一个消息A，套一个消息B，再套一个消息C，你用`slice:new()`可以得到整个消息A的视图，那么在发现消息B出现的时候，你可以通过`s:enter()`先读取一个带长度数据（这个数据读取后被跳过），然后将视图缩小到这个带长度数据内部：也就是消息B的内容。同理可以处理消息C。当处理完之后，调用`s:leave()`可以回到原视图了，注意在读取带长度数据的时候这个数据就被跳过了，这时回到原视图读取位置正好是带长度数据之后，就可以继续处理后续的消息了。

`s:enter()`还支持接受两个参数i和j直接给出偏移量位置。注意这种情况下，`s:leave()`就不会修改读取位置了——因为并没有读取操作。

下面是一个使用底层接口读取一个消息的示例：

```lua
local s = slice.new("<data here>")
local tag = s:unpack "v"
if tag%8 == 2 then -- tag 是 string/bytes 类型？这可能就是个子消息
  s:enter() -- 读取这个带长度数据，进入数据本身
  -- 现在可以对这个带长度数据做任何事儿了：比如说，读取一堆的fixed32数据
  local t = {}
  while #s > 0 do
    t[#t+1] = s:unpack "d"
  end
  s:leave() -- 搞定了？回到上级视图继续读取接下来的数据
end
```

以下是`pb.slice`模块里的所有接口：

| 接口            | 返回    | 描述                                          |
| --------------- | ------- | --------------------------------------------- |
| `slice.new(data[,i[,j]])` | Slice object | 创建一个新的 slice 对象 |
| `s:delete()`              | none         | 和 `s:reset()`相同，重置并释放slice对象引用的内存 |
| `tostring(s)`             | string       | 返回slice的字符串表示信息 |
| `#s`                      | number       | 得到当前视图还未读取的字节数 |
| `s:result([i[, j]])` | String | 得到当前视图的二进制数据 |
| `s:reset([data[,i[,j]]])` | self         | 将slice对象重置绑定另一个数据源 |
| `s:level()`               | number       | 返回当前视图栈的深度 |
| `s:level(number)`         | p, i, j      | 返回第n层视图栈的信息（读取位置、视图偏移） |
| `s:enter()`               | self         | 读取一个带长度数据，并将其视图推入视图栈 |
| `s:enter(i[, j])`         | self         | 将[i,j]字节范围的数据推入视图栈 |
| `s:leave([number])`       | self, n      | 离开n层的视图栈（默认离开一层），返回当前视图栈深度 |
| `s:unpack(fmt, ...)`      | values...    | 利用fmt和额外参数，读取当前视图内的信息 |

### `pb.buffer` 模块

Buffer模块本质上其实就是一个内存缓存，类似Java的“StringBuilder”的东西。他是一个构建wireformat数据的底层接口。使用`buffer:pack()`可以向缓存里新增数据，使用`buffer:result()`可以得到编码后的二进制数据结构。或者使用`buffer:tohex()`获取人类可读的16进制编码字符串。

 `buffer.pack()` 使用和 `slice.unpack()`相同的格式字符，请参见`pb.slice`的文档获取详细的格式字符的表格。除此以外，pack还支持 `'()'`格式字符，用于支持编码嵌套的带长度数据——也就是嵌套消息的结构。括号格式字符是可以嵌套的。下面是一个例子：

```lua
b:pack("(vvv)", 1, 2, 3) -- 获得一个编码了3个varint的带长度数据
```

`buffer.pack()` 也支持 '#' 格式字符：该格式字符会额外读取一个长度参数`oldlen`，并将`buffer`从`oldlen`开始到结束的所有字节数据重新编码为一个带长度数据：

```lua
b:pack("#", 5) -- 将b里从第五个字节开始的所有数据编码为一个带长度数据类型数据
```

这个功能可以用来更方便地编码长度不清楚的嵌套子消息：先读取一个当前长度，然后直接将子消息编码进buffer，一旦编码结束，就编码一个`"#"`格式，这样之前编码的所有信息就自动成为了一个带长度数据——即合法的子消息类型数据。

下面是 `pb.buffer` 模块里所有的接口：

| 接口            | 返回    | 描述                                          |
| --------------- | ------- | --------------------------------------------- |
| `buffer.new([...])` | Buffer object | 创建一个新的buffer对象，额外参数会传递给`b:reset(...)`函数  |
| `b:delete()`        | none          | 即`b:reset()`，释放buffer使用的内存 |
| `tostring(b)`       | string        | 返回buffer的字符串表示信息 |
| `#b`                | number        | 返回buffer中已经完成编码的字节数                 |
| `b:reset()`         | self          | 清空buffer中的所有数据                                     |
| `b:reset([...])`    | self          | 清空buffer，并将其数据设置为所有的参数，如同`io.write()`一样处理其参数 |
| `b:tohex([i[, j]])` | string        | 返回可选范围（默认是全部）的数据的16进制表示 |
| `b:result([i[,j]])` | string        | 返回编码后二进制数据。允许只返回一部分。默认返回全部 |
| `b:pack(fmt, ...)`  | self          | 利用fmt和额外参数，将参数里提供的数据编码到buffer中 |

---

