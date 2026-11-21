#!/usr/bin/env python3
"""Extract the MAME option <-> control mapping from EmuLoader's uMAMESettings.pas.

ReadMAMEIniFile/WriteMAMEIniFile in the Delphi source are ~3,600 lines of
'if EntryString = '<option> ' then <Control>.<Prop> := ...'. This script turns
the regular cases into a data table (src/forms/MameOptionMap.inc) used by the
generic reader/writer in MameSettingsDialog.cpp. Irregular cases are listed in
the report so they can be ported by hand.

Usage: extract_mame_options.py uMAMESettings.pas -o src/forms/MameOptionMap.inc
"""
import argparse
import os
import re


def strip_comments(src):
    out, i, n = [], 0, len(src)
    while i < n:
        c = src[i]
        if c == "'":
            j = src.index("'", i + 1) if "'" in src[i + 1:] else n
            while j + 1 < n and src[j + 1] == "'":
                j = src.index("'", j + 2)
            out.append(src[i:j + 1]); i = j + 1
        elif src.startswith("//", i):
            j = src.find("\n", i); i = n if j < 0 else j
        elif c == "{":
            j = src.find("}", i); i = n if j < 0 else j + 1
        elif src.startswith("(*", i):
            j = src.find("*)", i); i = n if j < 0 else j + 2
        else:
            out.append(c); i += 1
    return "".join(out)


def statement_at(text, pos):
    """Return the Pascal statement starting at pos: a begin..end block (with
    nested begin/case/try ... end) or a simple statement up to ';' or 'else'."""
    toks = re.finditer(r"'(?:[^']|'')*'|\b(begin|case|try|end|else)\b|;", text[pos:], re.I)
    m0 = re.match(r"\s*begin\b", text[pos:], re.I)
    depth = 0
    for t in toks:
        w = (t.group(1) or "").lower()
        if m0:
            if w in ("begin", "case", "try"):
                depth += 1
            elif w == "end":
                depth -= 1
                if depth == 0:
                    return text[pos:pos + t.end()]
        else:
            if w in ("begin", "case", "try"):
                depth += 1
            elif w == "end":
                depth -= 1
            elif depth == 0 and (t.group(0) == ";" or w == "else"):
                return text[pos:pos + t.start()]
    return text[pos:pos + 400]


def branches(body, var):
    """Find `if <cond on var = 'name '> then <statement>` and return (names, cond, stmt)."""
    pat = re.compile(r"\bif\s+((?:\(\s*)*%s\s*=\s*'[^']*'.*?)\bthen\b" % var, re.S)
    res = []
    for m in pat.finditer(body):
        cond = m.group(1)
        names = re.findall(r"%s\s*=\s*'([a-z0-9_]+) '" % var, cond)
        stmt = statement_at(body, m.end())
        res.append((names, cond, " ".join(stmt.split())))
    return res


READ_RULES = [
    (r"^(\w+)\.Checked\s*:=\s*GetBooleanValue;?$", lambda m: ("bool", m.group(1), "")),
    (r"^(\w+)\.Text\s*:=\s*GetStringValue;?$", lambda m: ("text", m.group(1), "")),
    (r"^(\w+)\.Position\s*:=\s*GetFloatValue\('%\d+\.(\d+)f'\);?$", lambda m: ("float", m.group(1), m.group(2))),
    (r"^(\w+)\.Position\s*:=\s*GetIntegerValue;?$", lambda m: ("int", m.group(1), "")),
    (r"^(\w+)\.ItemIndex\s*:=\s*GetIntegerValue\s*([+-]\s*\d+)?;?$",
     lambda m: ("index", m.group(1), (m.group(2) or "+0").replace(" ", ""))),
    (r"^begin (\w+)\.ItemIndex\s*:=\s*\1\.Items\.IndexOf\(GetStringValue\);\s*SetComboBoxItemZero\(\1\);? end;?$",
     lambda m: ("combo", m.group(1), "")),
    (r"^begin Value\s*:=\s*GetStringValue;\s*Loop2\s*:=\s*(\w+)\.Items\.IndexOf\(Value\);\s*if Loop2 = -1 then Loop2\s*:=\s*0;\s*\1\.ItemIndex\s*:=\s*Loop2;? end;?$",
     lambda m: ("combo", m.group(1), "")),
]


def extra_rules(stmt, names):
    """Second-tier patterns. Returns (kind, control, extra) or None."""
    m = re.match(r"^(\w+)\.Checked\s*:=\s*Boolean\(StrToInt\(ExtractMAMEIniValue\(TextLine\)\)\);?$", stmt)
    if m:
        return ("bool", m.group(1), "")
    m = re.match(r"^begin (\w+)\.Text\s*:=\s*GetStringValue;? end;?$", stmt)
    if m:
        return ("text", m.group(1), "")
    # 'none' (or 'auto') in the ini shows as an empty edit box
    m = re.match(r"^GetGLSLShaderMAME\((\w+)\);?$", stmt)
    if m:
        return ("textnone", m.group(1), "none")
    m = re.match(r"^begin Value\s*:=\s*GetStringValue;\s*(?:if SameText\(Value, 'none'\) then Value\s*:=\s*'';|case SameText\('none', Value\) of True : (\w+)\.Text\s*:=\s*''; False: \1\.Text\s*:=\s*Value; end;)\s*(?:(\w+)\.Text\s*:=\s*Value;)? end;?$", stmt)
    if m:
        return ("textnone", m.group(1) or m.group(2), "none")
    # empty value replaced by a default
    m = re.match(r"^begin Value\s*:=\s*GetStringValue;\s*if Value = '' then Value\s*:=\s*'([^']*)';\s*(\w+)\.Text\s*:=\s*Value;? end;?$", stmt)
    if m:
        return ("text", m.group(2), "default=" + m.group(1))
    m = re.match(r"^GetSDLDeviceMapping\((\w+), (\w+)\);?$", stmt)
    if m:
        return ("devmap", m.group(1), m.group(2))
    # value list: if Value = 'a' then X.ItemIndex:= 0 else if Value = 'b' then X.ItemIndex:= 1 ...
    pairs = re.findall(r"Value = '([^']*)' then (\w+)\.ItemIndex\s*:=\s*(\d+)", stmt)
    ctrls = {c for _, c, _ in pairs}
    if pairs and len(ctrls) == 1 and "IsSDLMAME" not in stmt:
        lower = "LowerCase(GetStringValue)" in stmt
        return ("choice", pairs[0][1], "|".join("%s=%s" % (v, i) for v, _, i in pairs) + (";ci" if lower else ""))
    m = re.match(r"^begin (\w+)\.ItemIndex\s*:=\s*GetIntegerValue;\s*if \1\.ItemIndex = -1 then \1\.ItemIndex\s*:=\s*0;? end;?$", stmt)
    if m:
        return ("index", m.group(1), "+0")
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("pas")
    ap.add_argument("-o", "--out", required=True)
    a = ap.parse_args()
    src = strip_comments(open(a.pas, encoding="cp1252").read())
    r0 = src.index("procedure TFormMAMESettings.ReadMAMEIniFile(const IniFile")
    r1 = src.index("procedure TFormMAMESettings.LoadCustomFilesArray")
    w0 = src.index("procedure TFormMAMESettings.WriteMAMEIniFile(const customIni")
    w1 = src.index("procedure TFormMAMESettings.LoadBiosList")
    read_body, write_body = src[r0:r1], src[w0:w1]

    # write side: float formats and how each option is encoded
    write_info = {}
    for names, cond, stmt in branches(write_body, "tmpEntryStr"):
        fm = re.search(r"Format\('%\d+\.(\d+)f'", stmt)
        for n in names:
            write_info[n] = (stmt, fm.group(1) if fm else None)

    rows, manual = [], []
    for names, cond, stmt in branches(read_body, "EntryString"):
        if not names:
            continue
        global_only = "EmuDefaultSettings" in cond
        for rx, fn in READ_RULES:
            m = re.match(rx, stmt.strip())
            if m:
                kind, ctrl, extra = fn(m)
                if kind == "float" and names[0] in write_info and write_info[names[0]][1]:
                    extra = write_info[names[0]][1]
                rows.append((names, kind, ctrl, extra, global_only))
                break
        else:
            r = extra_rules(stmt.strip(), names)
            if r:
                kind, ctrl, extra = r
                rows.append((names, kind, ctrl, extra, global_only))
            else:
                manual.append((names, stmt[:160]))

    with open(a.out, "w") as f:
        f.write("// Generated by tools/extract_mame_options.py from EmuLoader's uMAMESettings.pas.\n")
        f.write("// { {names...}, kind, control, extra, mame.ini-only }\n")
        f.write("// kinds: bool, text, float (extra = decimals), int, index (extra = offset), combo (match by text)\n")
        for names, kind, ctrl, extra, g in rows:
            f.write('{ {%s}, "%s", "%s", "%s", %s },\n' % (
                ", ".join('"%s"' % n for n in names), kind, ctrl, extra, "true" if g else "false"))
    # EnableSettingsIniMAME: controls that start disabled in the form and are enabled when
    # mame.ini has the option (so the window follows the MAME version in use).
    e0 = src.index("procedure TFormMAMESettings.EnableSettingsIniMAME")
    e1 = src.index("\nprocedure ", e0 + 10)
    enable_rows, enable_manual = [], []
    for names, cond, stmt in branches(src[e0:e1], "EntryString"):
        ctrls = re.findall(r"(\w+)\.Enabled\s*:=\s*True", stmt)
        rest = re.sub(r"(\w+)\.Enabled\s*:=\s*True;?|begin|end;?|//[^\n]*", "", stmt).strip()
        if rest:
            enable_manual.append((names, rest))
        if ctrls:
            enable_rows.append((names, ctrls))
    enable_out = os.path.join(os.path.dirname(a.out), "MameOptionEnable.inc")
    with open(enable_out, "w") as f:
        f.write("// Generated by tools/extract_mame_options.py from TFormMAMESettings.EnableSettingsIniMAME.\n")
        f.write("// { {option names...}, {controls enabled when mame.ini has the option...} }\n")
        for names, ctrls in enable_rows:
            f.write("{ {%s}, {%s} },\n" % (", ".join('"%s"' % n for n in names), ", ".join('"%s"' % c for c in ctrls)))
    print("enable table: %d options" % len(enable_rows))
    for names, rest in enable_manual:
        print("  ENABLE-MANUAL %-30s %s" % ("/".join(names), rest[:120]))
    print("mapped %d option groups, %d need hand porting" % (len(rows), len(manual)))
    for names, stmt in manual:
        print("  MANUAL %-40s %s" % ("/".join(names), stmt))


if __name__ == "__main__":
    main()
