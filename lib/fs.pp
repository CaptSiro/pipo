// ---- fs: files and directories, modelled on Java's java.io.File and its streams (C half: src/modules/fs.c)
//
//     import fs
//     const f = fs:file("data", "order.txt")        // only a path: nothing is opened or created
//     f.write("hello")                              // success(5)
//     f.read_text()                                 // success("hello")
//     f.read()                                      // success(Int8Array[104, 101, 108, 108, 111])
//
//     const in = f.open_read().unwrap()             // a stream, read in chunks
//     const buf = types:int8array(4096)
//     let n = in.read(buf)                          // bytes read, -1 at the end
//     in.close()
//
// File and Directory hold a path and look at the file system each time a function runs.
// Contents are bytes in a types:Int8Array. What can fail for reasons outside the program
// (opening, reading or writing a whole file, listing) returns an Error with a message;
// simple queries answer like Java does (exists() is false, size() is 0, delete() is false).
import types

export native type File
export native type Directory
export native type InputStream
export native type OutputStream

// ---- constants, like Java's File.separator and File.pathSeparator. A mixed module's constants
// live in this file; when a value depends on the platform, a private native function computes
// it once, while the module loads.
native "name_separator" function name_separator() -> string
native "list_separator" function list_separator() -> string
// between the parts of a path: "\" on Windows, "/" elsewhere
export const separator = name_separator()
// between the paths of a list such as PATH: ";" on Windows, ":" elsewhere
export const path_separator = list_separator()

// ---- plain paths
export native function exists(string path) -> bool
export native function is_dir(string path) -> bool
export native function is_file(string path) -> bool
// the working directory
export native function cwd() -> Directory

// ---- constructors (the types are not callable): a path as written, or parent + name, where
// parent is a string or a Directory. d.file(name) is file(d, name), d.dir(name) is directory(d, name).
export native function file(string path) -> File
export native function file(string parent, string name) -> File
export native function file(Directory parent, string name) -> File
export native function directory(string path) -> Directory
export native function directory(string parent, string name) -> Directory
export native function directory(Directory parent, string name) -> Directory
export native "directory" function dir(Directory parent, string name) -> Directory

// ---- File and Directory
export native "p_path" function path(File f) -> string
export native "p_path" function path(Directory d) -> string
// the last part of the path: "order.txt"
export native "p_name" function name(File f) -> string
export native "p_name" function name(Directory d) -> string
// the path without its last part; none() for a path with one part, like "order.txt"
export native "p_parent" function parent(File f) -> Directory?
export native "p_parent" function parent(Directory d) -> Directory?
export native "p_absolute" function absolute(File f) -> File
export native "p_absolute" function absolute(Directory d) -> Directory
export native "p_exists" function exists(File f) -> bool
export native "p_exists" function exists(Directory d) -> bool
// milliseconds since 1970, 0 when missing
export native "p_modified" function modified(File f) -> int
export native "p_modified" function modified(Directory d) -> int
// moves / renames; false when that failed
export native "p_rename" function rename(File f, File to) -> bool
export native "p_rename" function rename(File f, string to) -> bool
export native "p_rename" function rename(Directory d, Directory to) -> bool
export native "p_rename" function rename(Directory d, string to) -> bool

// ---- File
export native "f_is_file" function is_file(File f) -> bool
// "txt" for order.txt, "" when there is none
export native function extension(File f) -> string
// bytes, 0 when missing
export native function size(File f) -> int

// the whole contents, as bytes or as UTF-8 text
export native "f_read" function read(File f) -> Error<types:Int8Array>
export native function read_text(File f) -> Error<string>
// replace the contents / add to the end (creating the file); success(bytes written)
export native "f_write" function write(File f, types:Int8Array data) -> Error<int>
export native "f_write" function write(File f, string text) -> Error<int>
export native function append(File f, types:Int8Array data) -> Error<int>
export native function append(File f, string text) -> Error<int>
// an empty file; success(false) when it already existed
export native "f_create" function create(File f) -> Error<bool>
// false when it was not removed (missing, a directory, ...)
export native "f_delete" function delete(File f) -> bool
// copies the contents, replacing the target; success(bytes copied)
export native function copy_to(File f, File to) -> Error<int>
export native function copy_to(File f, string to) -> Error<int>

// streams: read or write a file a piece at a time; close them when done
export native function open_read(File f) -> Error<InputStream>
export native function open_write(File f) -> Error<OutputStream>
export native function open_append(File f) -> Error<OutputStream>

// ---- Directory
export native "d_is_dir" function is_dir(Directory d) -> bool
// makes the directory, false when that failed (also when it existed); create_all makes missing parents too
export native "d_create" function create(Directory d) -> bool
export native function create_all(Directory d) -> bool
// removes an empty directory; false when that failed
export native "d_delete" function delete(Directory d) -> bool
// what the directory contains, sorted by name: every name / the files / the directories
export native function list(Directory d) -> Error<string[]>
export native function files(Directory d) -> Error<File[]>
export native function dirs(Directory d) -> Error<Directory[]>

// ---- InputStream (from File.open_read)
// the next byte as 0...=255, -1 at the end
export native "in_read_byte" function read(InputStream stream) -> int
// fills buffer (or length bytes of it, from offset): the number of bytes read, -1 at the end
export native "in_read" function read(InputStream stream, types:Int8Array buffer) -> int
export native "in_read" function read(InputStream stream, types:Int8Array buffer, int offset, int length) -> int
// skips up to n bytes, the number skipped
export native function skip(InputStream stream, int n) -> int

// ---- OutputStream (from File.open_write / File.open_append)
// one byte, -128...=255
export native "out_write_byte" function write(OutputStream stream, int byte) -> void
export native "out_write" function write(OutputStream stream, types:Int8Array data) -> void
export native "out_write" function write(OutputStream stream, types:Int8Array data, int offset, int length) -> void
export native "out_write" function write(OutputStream stream, string text) -> void
export native function flush(OutputStream stream) -> void

// ---- both streams; using a closed stream is an error, closing twice is not
export native "s_close" function close(InputStream stream) -> void
export native "s_close" function close(OutputStream stream) -> void
export native "s_is_open" function is_open(InputStream stream) -> bool
export native "s_is_open" function is_open(OutputStream stream) -> bool
export native "s_path" function path(InputStream stream) -> string
export native "s_path" function path(OutputStream stream) -> string
