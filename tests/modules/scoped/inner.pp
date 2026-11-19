export scope _tagged(Block<string> code, string tag) -> string {
    return code("<" + tag + ">")
}
