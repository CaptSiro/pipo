import fs
import pp
const s = fs:file(pp:file()).open_read().unwrap()
s.close()
s.read()
