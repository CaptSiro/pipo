# PiPo – Toy Programming Language

Uses the .pp file extension

## Basic Idea

I would like to create a new programming language that serves similar purpose to Javascript, Python, or Lua. 
It is a toy language that does not need to be bleeding edge performant but it would be nice for it to be decently fast. 

## Syntax

The syntax uses a minimal set of keywords for implementation with one special symbol. Example:

```
// line comment
/* block comment */

// imports
import file
import file -> renamed
import file -> { destrucured }

// custom type definitions
// semi colons can be omitted if the code line ends, semi colons do basically nothing
type number = float || int

// function name can be basically anything as long as it is not restricted keyword, uses type overloading for resolution
<T>function +(T[] a, T[] b) -> T[] {
    return a.concat(b) 
}

// calling functions all produces empty array
// classic
+([], [])

// dot-syntax, basically elevates the caller as a first argument for the function it is identical to the classical calling
[].+([])

// unary-syntax, the function must take exactly two arguments
-[]
// the parenthesis are omitted, success will be discused later on
success var

// binary-syntax, similar to the dot-syntax but the function must take exactly two arguments. 
[] + []
// Supports setting precedence for functions at runtime for newly created functions
<T>function *(T[] array, int count) -> T[] {
    return array.multiply(count)
}

precedence * over +
['b'] + ['a'] * 5
// ['b', 'a', 'a', 'a', 'a', 'a']

// variables, const is identical to JavaScript const
const [type, can be omitted] name = value
let [type, can be omitted] name = value
let type name

// valid, works as #DEFINE MACRO_NAME in C or C++
const name

if condition {
    // true branch
} else { 
    // false branch
}

// also valid:
if condition {
    // true branch
}

// also valid (can be chained infinitely):
if condition {
    // true condition 1 branch
} else if condition {
    // true condition 2 branch
} else {
    // false branch
}

// python style for cycle
for i in ...10 {
    // i is from 0 to 9
}

// by default i is const, can be set to let
for let i in ...10 {
}

// ... is a range function from python
for let i in 10...=1 {
    // i is from 10 to 1 (inclusive, descending)
}

for let i in ...#array {
    // # is function for getting the number of elements in a collection (similar to len in python)
}

for x in array {
    // iterates over all items in array
}

while condition {
    if condition2 {
        break
    }
    
    if condition3 {
        continue
    }
}

// works like rust match
const x = match var {
    10 -> var * 5
    5 -> {
        // multiline
    }
    _ -> {
        // default branch
    }
}

// the programming language includes null as a type system optional from rust. 
// in the type system expressed as ? after the type. Example: number?
some(value) // return boxed value that is something
none() // return "null"

// the programming language includes Error<type_if_success, type_if_failure> by default type_if_failure is set to string but can be overwritten
// creates general function to create the typed enum
enum Error<S, F = string> {
    success(S),
    fail(F)
}

enum Properties {
    NAME,
    VALUE,
    DATASET,
}

enum string Messages {
    NOTICE = "Notice",
    WARNING = "Warning",
    ERROR = "Error"
}

success(var) // return successfull value
fail var // return error type_if_failure, unary syntax

if some(x) = might_null_function() {
    // x is const by default, can be typed as let
}

match might_fail_function() {
    success(x) -> {
        // ok!
    }
    fail(y) -> {
        // reason
    }
}

// JSON syntax for objects and arrays
type Person = {
    string name,
    number age,
}

const Person john = {
    name: "John Pork",
    age: 22
}

const Person[] people = [john]
for p in people {
    print(p) // pretty JSON print
}

```

This might be it. Lets start as a general language like python, but I would like to extend it to be compilable for web