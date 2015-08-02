local pb = require "pb"

-- to get .pb file, first copy test/descriptor.proto to
-- test/google/protobuf/descriptor.proto, then run:
-- protoc -o test/descriptor.pb -Itest test/descriptor.proto
-- protoc -o test/plugin.pb -Itest test/plugin.proto
pb.loadfile "test/descriptor.pb"
pb.loadfile "test/plugin.pb"
pb.dump "pb_typeinfo.lua"
