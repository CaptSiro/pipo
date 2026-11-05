import io
import types

// ---- construction: a fixed length of zeros, or a copy of an array / typed array
const bytes = types:int8array(4)
io:print_nl(bytes, #bytes, type_of(bytes), types:Int8Array)
io:print_nl(types:int16array([1, -2, 300]), types:int32array(0), types:int64array([9_000_000_000]))
io:print_nl(types:float32array([1, 2.5, 0.1]), types:float64array([0.1, 3]))
io:print_nl(types:float64array(types:int8array([1, 2])), types:int16array(types:int8array([-5])))

// ---- indexing: ints for Int*Array, floats for Float*Array; negative counts from the end
bytes[0] = 127
bytes[-1] = -128
bytes[1] += 5
io:print_nl(bytes, bytes[0], bytes[-1], type_of(bytes[0]))
const floats = types:float32array(2)
floats[0] = 3          // ints become floats
floats[1] = 0.1        // stored as a 32-bit float
io:print_nl(floats, floats[0], floats[1] == 0.1, floats[1] > 0.1)

// ---- for, ==, copy
for i, b in bytes { io:print_nl(i, b) }
let total = 0
for b in types:int64array([1, 2, 3]) { total += b }
io:print_nl(total)
const other = copy(bytes)
io:print_nl(other == bytes, other == types:int16array([127, 5, 0, -128]))
other[2] = 1
io:print_nl(other == bytes, bytes[2], types:float64array([1.5]) == types:float64array([1.5]))

// ---- functions
const nums = types:int32array(5)
nums.fill(7)
nums.fill(1, 1, 3)
io:print_nl(nums, nums.slice(1), nums.slice(1, -1), nums.to_array(), nums.element_size())
const fs = types:float64array(3)
fs.fill(2)
io:print_nl(fs, fs.to_array(), fs.element_size(), type_of(fs.slice(0, 1)))
types:copy_into(nums, 0, nums, 1, 4)    // ranges may overlap
io:print_nl(nums)

// ---- strings and bytes (UTF-8)
const encoded = types:bytes("héllo")
io:print_nl(encoded, #encoded, encoded.text(), encoded.text(1, 3), types:bytes(""))

// ---- the union types
function sum(types:IntArray a) -> int {
    let t = 0
    for x in a { t += x }
    return t
}
function kind(types:TypedArray a) -> string { return type_of(a) }
function kind(any a) -> string { return "other" }
io:print_nl(sum(bytes), sum(types:int16array([1, 2])), kind(floats), kind([1, 2]))
const types:FloatArray fa = floats
io:print_nl(fa)
