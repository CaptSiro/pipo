"""Checks (and with --fix, applies) the mechanical rules of md/CODING_STYLE.md.

    python tools/style_check.py          report violations, exit 1 if there are any
    python tools/style_check.py --fix    rewrite the files in place

Rules handled here:
    * comments are // comments (/* */ stays inside a line and in macros)
    * a `return` after other statements has a blank line above it
    * counting loops use ++i / --i
    * three blank lines after the include block, and before a header's #endif
    * a header's #endif repeats the guard name
    * no one-line if / for / while bodies
"""

import glob
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))



def c_files():
    files = glob.glob(os.path.join(ROOT, "src", "*.c")) + glob.glob(os.path.join(ROOT, "src", "*.h"))
    files += glob.glob(os.path.join(ROOT, "src", "modules", "*.c"))

    return sorted(files)


def pp_files():
    files = glob.glob(os.path.join(ROOT, "lib", "*.pp")) + glob.glob(os.path.join(ROOT, "examples", "*.pp"))

    return sorted(files)


# ---- comments

def find_comments(src):
    """Yields (start, end) of every /* */ comment outside string and char literals."""
    i = 0
    n = len(src)
    while i < n:
        c = src[i]
        if c == '"' or c == "'":
            quote = c
            i += 1
            while i < n and src[i] != quote:
                if src[i] == "\\":
                    i += 1
                i += 1
            i += 1
            continue

        if src.startswith("//", i):
            j = src.find("\n", i)
            i = n if j < 0 else j
            continue

        if src.startswith("/*", i):
            j = src.find("*/", i + 2)
            yield i, j + 2
            i = j + 2
            continue

        i += 1


def convert_comments(src):
    out = []
    last = 0
    for start, end in find_comments(src):
        line_start = src.rfind("\n", 0, start) + 1
        line_end = src.find("\n", end)
        if line_end < 0:
            line_end = len(src)

        before = src[line_start:start]
        after = src[end:line_end]
        span = src[line_start:line_end]
        in_macro = any(l.rstrip().endswith("\\") for l in span.split("\n"))
        prev_line_end = src.rfind("\n", 0, line_start - 1) + 1 if line_start > 0 else 0
        prev_line = src[prev_line_end:line_start].rstrip("\n")
        if in_macro or prev_line.rstrip().endswith("\\") or after.strip() != "" or src[start + 2:end - 2].rstrip().endswith("\\"):
            continue

        body = src[start + 2:end - 2]
        lines = body.split("\n")
        cleaned = []
        for k, l in enumerate(lines):
            s = l.strip() if k > 0 else l.strip()
            if k > 0 and s.startswith("*"):
                s = s[1:]
                if s.startswith(" "):
                    s = s[1:]
            elif k > 0:
                # continuation line without a leading *: keep its own indentation relative to the comment
                s = l.strip()
            cleaned.append(s.rstrip())

        while cleaned and cleaned[0] == "":
            cleaned.pop(0)
        while cleaned and cleaned[-1] == "":
            cleaned.pop()

        if before.strip() == "":
            indent = before
        else:
            indent = " " * len(before.expandtabs())
        text_lines = [("// " + c) if c else "//" for c in cleaned] or ["//"]
        replacement = text_lines[0] + "".join("\n" + indent + t for t in text_lines[1:])

        out.append(src[last:start])
        out.append(replacement)
        last = end

    out.append(src[last:])

    return "".join(out)


def comment_violations(src):
    bad = []
    for start, end in find_comments(src):
        line_start = src.rfind("\n", 0, start) + 1
        line_end = src.find("\n", end)
        if line_end < 0:
            line_end = len(src)

        span = src[line_start:line_end]
        prev_line_end = src.rfind("\n", 0, line_start - 1) + 1 if line_start > 0 else 0
        prev_line = src[prev_line_end:line_start]
        in_macro = any(l.rstrip().endswith("\\") for l in span.split("\n")) or prev_line.rstrip().endswith("\\")
        if not in_macro and src[end:line_end].strip() == "":
            bad.append(src.count("\n", 0, start) + 1)

    return bad


# ---- blank line before return

RETURN = re.compile(r"^\s*return\b")


def needs_blank(prev):
    p = prev.strip()
    if p == "":
        return False

    if p.endswith("\\") or p.endswith("{") or p.endswith(":") or p.startswith("#") or p.startswith("case ") or p == "default:":
        return False

    return True


def fix_returns(lines):
    out = list(lines)
    i = 0
    while i < len(out):
        if RETURN.match(out[i]):
            # a comment attached to the return moves with it
            j = i
            while j > 0 and out[j - 1].strip().startswith("//"):
                j -= 1

            if j > 0 and needs_blank(out[j - 1]) and not out[j - 1].rstrip().endswith((",", "(", "&&", "||", "+", "?")):
                out.insert(j, "")
                i += 1
        i += 1

    return out


def return_violations(lines):
    bad = []
    for i, l in enumerate(lines):
        if RETURN.match(l):
            j = i
            while j > 0 and lines[j - 1].strip().startswith("//"):
                j -= 1

            if j > 0 and needs_blank(lines[j - 1]) and not lines[j - 1].rstrip().endswith((",", "(", "&&", "||", "+", "?")):
                bad.append(i + 1)

    return bad


# ---- ++i in for loops

FOR_POSTFIX = re.compile(r"(for \([^;]*;[^;]*; )([A-Za-z_][A-Za-z_0-9]*)(\+\+|--)\)")


def fix_for(src):
    return FOR_POSTFIX.sub(lambda m: m.group(1) + m.group(3) + m.group(2) + ")", src)


# ---- sections

def preamble_end(lines, header):
    """Index of the last line of the include block, or -1."""
    last = -1
    depth = 0
    for i, l in enumerate(lines):
        s = l.strip()
        if s.startswith("#include"):
            last = i
            continue

        if s.startswith("#if"):
            depth += 1
            continue

        if s.startswith("#endif") and depth > 0:
            depth -= 1
            if last >= 0:
                last = i
            continue

        if depth > 0 or s == "" or s.startswith("//") or (header and s.startswith("#define PP_")) or (
                header and s.startswith("#ifndef PP_")):
            continue

        if s.startswith("#define WIN32_LEAN_AND_MEAN") or s.startswith("#else"):
            continue

        break

    return last


def fix_sections(lines, header):
    out = list(lines)
    end = preamble_end(out, header)
    if end >= 0:
        k = end + 1
        while k < len(out) and out[k].strip() == "":
            k += 1
        if k < len(out):
            out[end + 1:k] = ["", "", ""]

    if header:
        k = len(out) - 1
        while k >= 0 and out[k].strip() == "":
            k -= 1
        if k >= 0 and out[k].startswith("#endif"):
            guard = None
            for l in out:
                m = re.match(r"#ifndef (PP_\w+_H)", l)
                if m:
                    guard = m.group(1)
                    break
            if guard:
                out[k] = "#endif // " + guard
            j = k
            while j > 0 and out[j - 1].strip() == "":
                j -= 1
            out[j:k] = ["", "", ""]

    return out


def section_violations(lines, header):
    fixed = fix_sections(lines, header)
    if fixed == lines:
        return []

    for i, (a, b) in enumerate(zip(lines, fixed)):
        if a != b:
            return [i + 1]

    return [len(lines)]


# ---- one-line bodies

ONE_LINE = re.compile(r"^\s*(if|for|while|else if)\s*\(.*\)\s*[^{\s].*;\s*$")


def one_line_violations(lines):
    bad = []
    for i, l in enumerate(lines):
        if ONE_LINE.match(l) and "{" not in l:
            # `while (x);` loops and multi-line conditions are not bodies
            if l.rstrip().endswith(");") and l.count("(") == l.count(")") and re.match(r"^\s*while", l):
                continue
            bad.append(i + 1)

    return bad



def main():
    fix = "--fix" in sys.argv
    problems = 0
    for path in c_files():
        with open(path, encoding="utf-8", newline="") as f:
            src = f.read()
        crlf = "\r\n" in src
        src = src.replace("\r\n", "\n")
        header = path.endswith(".h")
        rel = os.path.relpath(path, ROOT)

        if fix:
            new = convert_comments(src)
            new = fix_for(new)
            lines = new.split("\n")
            lines = fix_returns(lines)
            lines = fix_sections(lines, header)
            new = "\n".join(lines)
            if new != src:
                with open(path, "w", encoding="utf-8", newline="") as f:
                    f.write(new.replace("\n", "\r\n") if crlf else new)
                print("fixed", rel)
            continue

        lines = src.split("\n")
        report = []
        report += [(n, "use a // comment") for n in comment_violations(src)]
        report += [(n, "blank line before return") for n in return_violations(lines)]
        report += [(src.count("\n", 0, m.start()) + 1, "use ++i / --i in a for loop") for m in FOR_POSTFIX.finditer(src)]
        report += [(n, "three blank lines between sections / #endif // GUARD") for n in section_violations(lines, header)]
        report += [(n, "one-line body: use a block") for n in one_line_violations(lines)]
        for n, msg in sorted(report):
            print(f"{rel}:{n}: {msg}")
        problems += len(report)

    for path in pp_files():
        with open(path, encoding="utf-8") as f:
            lines = f.read().split("\n")
        rel = os.path.relpath(path, ROOT)
        if fix:
            new = fix_returns(lines)
            if new != lines:
                with open(path, "w", encoding="utf-8", newline="") as f:
                    f.write("\n".join(new))
                print("fixed", rel)
            continue

        for n in return_violations(lines):
            print(f"{rel}:{n}: blank line before return")
            problems += 1

    if not fix and problems:
        print(f"{problems} problem(s)")
        sys.exit(1)


if __name__ == "__main__":
    main()
