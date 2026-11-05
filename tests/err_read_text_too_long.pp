import fs
import pp

// a string holds at most 2 GB, so read_text cannot read more at once
const input = fs:file(pp:file()).open_read().unwrap()
input.read_text(3000000000)
