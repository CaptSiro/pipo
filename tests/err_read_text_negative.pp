import fs
import pp

// a negative length is a mistake in the program, not an end of the stream
const input = fs:file(pp:file()).open_read().unwrap()
input.read_text(-1)
