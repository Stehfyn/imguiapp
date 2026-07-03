#!/usr/bin/env python
"""Column-align struct/class member declarations via libclang (NOT regex).

For each contiguous run of simple field declarations in a record, align:
  - ONE type column  (max type width in the run; wide types are NOT split off)
  - the member-name column
  - the `=` of default member initializers
  - trailing `//` comments (same-line + pure-comment continuation lines)

so every identifier in the run lines up. Fields with a complex declarator
(function pointer, bitfield, brace-init, multi-line) make the whole run be
skipped, left byte-for-byte untouched.

Field identity/name/location come from the AST. Only the trivial split of the
one-line declarator at " = " is lexical (default member inits in these structs
are always spelled `Type Name[opt] = value;` on a single line). This avoids
libclang's incomplete-parse quirk of dropping the initializer from a field's
token extent.

Usage:
  python align_records.py <file...> [--dry] -- <compile flags>
"""
import sys, argparse

def load_cindex():
    import clang.cindex as ci
    try: ci.Index.create()
    except Exception:
        ci.Config.set_library_file(
            r"C:\Users\stehf\AppData\Local\Microsoft\WinGet\Packages\LLVM\bin\libclang.dll")
    return ci

def split_args(argv):
    if "--" in argv:
        i = argv.index("--"); return argv[:i], argv[i+1:]
    return argv, []

def field_facts(f, lines):
    """(line, indent, type, name_field, init, comment) or None to skip its run.

    Uses only f.location.line (reliable) + source text — NOT f.extent.start.column,
    which libclang mislocates for enum/typedef-initialized fields under incomplete parse.
    """
    line = f.location.line
    text = lines[line-1]
    semi = text.find(";")
    if semi < 0:                          # multi-line declarator: skip
        return None
    indent = text[:len(text)-len(text.lstrip())]
    seg = text[len(indent):semi]          # whole declarator [+ = init], no ';'
    name = f.spelling
    idx = seg.find(" = ")
    if idx >= 0:
        left, init = seg[:idx].rstrip(), seg[idx+3:].strip()
    else:
        left, init = seg.rstrip(), None
    npos = left.rfind(name)
    if npos < 0:
        return None
    if "(" in left[:npos] or ":" in left[npos+len(name):]:   # func ptr / bitfield
        return None
    name_field = left[npos:].strip()
    if init is None and ("{" in name_field or "(" in name_field):   # brace/paren init
        return None
    tail = text[semi+1:]
    c = tail.find("//")
    comment = tail[c:].rstrip() if c >= 0 else None
    return dict(line=line, endline=line, indent=indent,
                type=left[:npos].strip(), name=name_field, init=init,
                comment=comment, cont=[])

def build_runs(ci, rec, lines):
    runs, cur, seen = [], [], set()
    def flush():
        nonlocal cur
        if len(cur) >= 2: runs.append(cur)
        cur = []
    for f in rec.get_children():
        if f.kind != ci.CursorKind.FIELD_DECL or not f.spelling:
            continue
        ff = field_facts(f, lines)
        if ff is None or ff["line"] in seen:   # skip multi-field-per-line
            flush(); continue
        seen.add(ff["line"])
        if cur:
            prev = cur[-1]
            gap = list(range(prev["endline"]+1, ff["line"]))
            # absorb wrapped continuation ONLY when prev has its own trailing comment;
            # otherwise a pure-comment line is a LEADING comment for the next field -> break.
            if gap and prev["comment"] and all(lines[g-1].strip().startswith("//") for g in gap):
                prev["cont"] += [lines[g-1].strip() for g in gap]
                prev["endline"] = ff["line"]-1
            elif gap:
                flush()
        cur.append(ff)
    flush()
    return runs

def fmt_run(run):
    indent = run[0]["indent"]
    typew = max(len(f["type"]) for f in run) + 1
    inits = [f for f in run if f["init"] is not None]
    namew = (max(len(f["name"]) for f in inits) + 1) if inits else 0
    rows = []
    for f in run:
        if f["init"] is None:
            rows.append((f, f"{indent}{f['type'].ljust(typew)}{f['name']};"))
        else:
            rows.append((f, f"{indent}{f['type'].ljust(typew)}{f['name'].ljust(namew)}= {f['init']};"))
    cw = max((len(l) for f, l in rows if f["comment"] or f["cont"]), default=0) + 1
    out = []
    for f, left in rows:
        out.append(left if not f["comment"] else f"{left.ljust(cw)}{f['comment']}")
        out += [" "*cw + cc for cc in f["cont"]]
    return out

def parse_asg(line):
    """Lexical: a single `LHS = RHS;` statement -> (indent, lhs, rhs), else None."""
    s = line.rstrip()
    if not s.endswith(";") or " = " not in s:
        return None
    indent = line[:len(line)-len(line.lstrip())]
    body = s[len(indent):-1]                        # drop indent + trailing ';'
    lhs, rhs = body.split(" = ", 1)
    if "=" in lhs or "=" in rhs or ";" in rhs:      # chained / compound / multi-stmt
        return None
    if any(ch in lhs for ch in "(){}, "):           # not a simple lvalue
        return None
    return indent, lhs, rhs

def ctor_runs(ci, tu, lines, base):
    """Edits (a,b,newlines) that align `=` in contiguous ctor-body assignment runs."""
    edits = []
    for c in tu.cursor.walk_preorder():
        if c.kind != ci.CursorKind.CONSTRUCTOR or not c.is_definition():
            continue
        if not c.location.file or not c.location.file.name.replace("\\","/").endswith(base):
            continue
        a, b = c.extent.start.line, c.extent.end.line
        i = a
        while i <= b:
            p = parse_asg(lines[i-1])
            if not p:
                i += 1; continue
            indent = p[0]; run = []
            while i <= b:
                q = parse_asg(lines[i-1])
                if not q or q[0] != indent:
                    break
                run.append((i, q[1], q[2])); i += 1
            if len(run) >= 2:
                w = max(len(l) for _, l, _ in run)
                new = [f"{indent}{l.ljust(w)} = {r};" for _, l, r in run]
                if new != [lines[k-1] for k, _, _ in run]:
                    edits.append((run[0][0], run[-1][0], new))
    return edits

def process(ci, path, flags, dry):
    idx = ci.Index.create()
    tu = idx.parse(path, args=flags,
                   options=ci.TranslationUnit.PARSE_INCOMPLETE
                           | ci.TranslationUnit.PARSE_DETAILED_PROCESSING_RECORD)
    lines = open(path, encoding="utf-8", errors="replace").read().split("\n")
    RECORD = {ci.CursorKind.STRUCT_DECL, ci.CursorKind.CLASS_DECL}
    base = path.replace("\\","/").split("/")[-1]
    edits = []
    for c in tu.cursor.walk_preorder():
        if c.kind in RECORD and c.is_definition() and c.spelling and c.location.file \
           and c.location.file.name.replace("\\","/").endswith(base):
            for run in build_runs(ci, c, lines):
                edits.append((run[0]["line"], run[-1]["endline"], fmt_run(run)))
    edits += ctor_runs(ci, tu, lines, base)
    edits.sort(key=lambda e: e[0], reverse=True)      # bottom-up: keep line #s valid
    changed = 0
    for a, b, new in edits:
        if new != lines[a-1:b]:
            lines[a-1:b] = new
            changed += 1
    if changed and not dry:
        open(path, "w", encoding="utf-8", newline="\n").write("\n".join(lines))
    print(f"{path}: {changed} runs {'(dry)' if dry else 'changed'}")
    return changed

def main():
    files, flags = split_args(sys.argv[1:])
    ap = argparse.ArgumentParser()
    ap.add_argument("files", nargs="+")
    ap.add_argument("--dry", action="store_true")
    known = ap.parse_args(files)
    ci = load_cindex()
    for f in known.files:
        process(ci, f, flags, known.dry)

if __name__ == "__main__":
    main()
