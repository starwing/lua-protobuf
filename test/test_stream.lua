package.path = "../?.lua;"..package.path
package.cpath = "../?.dll;../?.so;"..package.cpath
local decoder = require "pb.decoder"
local buffer = require "pb.buffer"

local buf = buffer.new()
local dec = decoder.new(buf)

print(dec:pos(), #dec, #buf) -- 1, 0, 0
assert(dec:varint() == nil)

-- add a varint
buf:varint(150)
print(dec:pos(), #dec, #buf) -- 1, 0, 2

-- update decoder
dec:update()
print(dec:pos(), #dec, #buf) -- 1, 2, 2
assert(dec:varint() == 150)

-- clear readed buffer
dec:update()
print(dec:pos(), #dec, #buf) -- 1, 0, 0

-- add imcomplete bytes message
buf:varint(10)
buf:concat("abcde")
print(dec:pos(), #dec, #buf) -- 1, 0, 6

-- update decoder
dec:update()
print(dec:pos(), #dec, #buf) -- 1, 6, 6

-- can not read anything now (bytes imcomplete)
assert(dec:bytes() == nil)
dec:update() -- does nothing
print(dec:pos(), #dec, #buf) -- 1, 6, 6

-- now complete bytes
buf:concat("fghij")
print(dec:pos(), #dec, #buf) -- 1, 6, 11

-- update decoder
dec:update()
print(dec:pos(), #dec, #buf) -- 1, 11, 11


-- now we read whole message
assert(dec:bytes() == "abcdefghij")

print "ok"
