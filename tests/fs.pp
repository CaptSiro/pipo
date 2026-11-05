import io
import fs
import pp
import types

// everything happens in a scratch directory next to this file, removed at the end
const root = fs:directory(pp:dir(), "fs_test_tmp")
io:print_nl(root.name(), root.dir("a").dir("b").create_all(), root.is_dir(), fs:is_dir(root.path()))

// ---- File: a path; nothing exists until it is written
const f = root.file("notes.txt")
io:print_nl(f.name(), f.extension(), f.exists(), f.size(), f.modified(), f.parent().unwrap() == root, type_of(f))
io:print_nl(f.read_text().is_fail(), f.write("hello"), f.exists(), f.is_file(), f.size(), f.modified() > 0)
io:print_nl(f.read_text(), f.read(), f.append(types:bytes(" world")), f.read_text())
io:print_nl(f.write(types:int8array([104, 105])), f.read_text(), f.create(), root.file("empty").create())
io:print_nl(f == root.file("notes.txt"), f == fs:file("notes.txt"), fs:file("x.txt").parent(), fs:file("a", "b.txt").name())

// ---- streams: a fixed buffer, filled again and again
const out = f.open_write().unwrap()
out.write("abc")
out.write(10)
out.write(types:int8array([65, 66, 67, 68]), 1, 2)
io:print_nl(out.is_open())
out.close()
out.close()                                    // closing twice does nothing
io:print_nl(out.is_open(), f.read())

const input = f.open_read().unwrap()
const buf = types:int8array(4)
let n = input.read(buf)
while n != -1 {
    io:print_nl(n, buf.text(0, n).replace("\n", "|"))
    n = input.read(buf)
}
input.close()
const again = f.open_read().unwrap()
io:print_nl(again.read(), again.skip(2), again.read(), again.read(buf, 1, 2), buf, again.read())
again.close()
const log = f.open_append().unwrap()
log.write("!")
log.close()
io:print_nl(f.read_text().unwrap().replace("\n", "|"))

// ---- read_text on a stream: up to length bytes, the rest without one, "" at the end
const text = root.file("text.txt")
text.write("hello world")
const words = text.open_read().unwrap()
io:print_nl(words.read_text(5), words.read_text(0) == "", words.read_text(100), words.read_text() == "")
words.close()
const whole = text.open_read().unwrap()
io:print_nl(whole.read_text(), whole.read_text(1) == "", whole.read(), whole.is_open())
whole.close()
io:print_nl(whole.is_open(), text.delete())

// ---- Directory
io:print_nl(f.copy_to(root.file("copy.txt")), root.file("copy.txt").read_text() == f.read_text())
io:print_nl(root.list())
io:print_nl(root.files().unwrap().map(x -> x.name()), root.dirs().unwrap().map(x -> x.name()))
io:print_nl(root.file("copy.txt").rename(root.file("moved.txt")), root.list())
io:print_nl(root.dir("a").delete(), root.dir("a").dir("b").delete(), root.dir("a").delete())
io:print_nl(fs:directory(root.path(), "missing").list().is_fail(), root.file("missing").delete())

// ---- clean up
for file in root.files().unwrap() { io:print_nl(file.name(), file.delete()) }
io:print_nl(root.delete(), root.exists(), fs:exists(root.path()))
io:print_nl(fs:exists("definitely/missing"), fs:cwd().exists(), fs:file(".gitignore").extension() == "", fs:file("a.tar.gz").extension())

// ---- constants
io:print_nl(["/", "\\"].contains(fs:separator), [":", ";"].contains(fs:path_separator), fs:file("a", "b").path() == "a" + fs:separator + "b")
