Google protobuf support for Lua
-------------------------------


This project offers a simple C library for basic protobuf wire format
encode/decode, it splits to three modules:
  - pb.decoder: a wire format decode module.
  - pb.buffer:  a buffer implement that use to encode basic types into
                protobuf's wire format.
  - pb.conv:    a module to convert between integers in protobuf.

Use the C module, one can basicly maintain protobuf data. A test module (WIP)
used to provide high level interface to Lua. now it can produce types.lua, a type
describe module generated to help decode protobuf data to Lua table. Now it
can bootstrap itself (i.e. to decode descriptor.pb, and produce types.lua
itself).

this module is still work-in-progress. Some works to do:
  - Lua modules handle high level interface to decode/encode protobuf data
    from/to Lua table.
  - Lua modules to generate pure C module code to enable the ability that
    write Lua plugins for protoc on Windows.
    protoc needs plugins' stdin/stdout to work on binary mode. but that's
    impossible without modify the Lua code if you use VS compiler and /MT
    flag (link with static standard C library).


