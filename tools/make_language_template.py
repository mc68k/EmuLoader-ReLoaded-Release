#!/usr/bin/env python3
"""Builds lang/English.ini, the template for EmuLoader ReLoaded translations, and brings existing
language files up to date.

    tools/make_language_template.py                   writes lang/English.ini
    tools/make_language_template.py lang/Deutsch.ini  adds new texts to Deutsch.ini (keeps its translations)

The texts come from Qt's lupdate (the tr() calls in src/ and the windows in forms/ that the
program uses) and from the menus (forms/FormMain.menus.json, section [Menus]).
Each section of the file is a context; each line is "English text=translation", with a comment
above it saying where the text appears.

Texts are listed without the spaces some of the original's labels are padded with; the program
matches them either way and keeps the padding (LanguageFile::translate).
"""
import json, os, re, subprocess, sys, tempfile
import xml.etree.ElementTree as ET

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LUPDATE = os.environ.get("LUPDATE", "lupdate")
for cand in ("/usr/lib/qt6/bin/lupdate", "/usr/lib/x86_64-linux-gnu/qt6/bin/lupdate"):
    if LUPDATE == "lupdate" and os.path.exists(cand):
        LUPDATE = cand

# ------------------------------------------------------------------------ the files' sections

def load_sections():
    """{program context: section} from src/app/LanguageSections.inc (also used by Language.cpp)"""
    inc = open(os.path.join(ROOT, "src", "app", "LanguageSections.inc"), encoding="utf-8").read()
    return dict(re.findall(r'^\{"([^"]+)",\s*"([^"]+)"\},', inc, re.M))


SECTIONS = load_sections()
# the sections first in the file (what translators see most), then the others A to Z
FIRST_SECTIONS = ["Menus", "Main Window", "Tool Bar", "Games List", "Game Info Panel", "Picture Categories",
                  "Picture Layouts", "Video Player", "Startup", "Settings Window", "Find a Setting"]


def section_of(ctx):
    return SECTIONS.get(ctx, ctx)


def contexts_of(section):
    return [c for c, sec in SECTIONS.items() if sec == section] or [section]


def grouped(texts):
    """[(section, [text], {text: [where]})]: a window's contexts (its code, its form) together"""
    groups = {}
    for ctx, lst in texts.order.items():
        sec = section_of(ctx)
        g = groups.setdefault(sec, ([], {}))
        for t in lst:
            if t not in g[0]:
                g[0].append(t)
            w = g[1].setdefault(t, [])
            for x in texts.where.get((ctx, t), []):
                if x not in w:
                    w.append(x)
    first = [s for s in FIRST_SECTIONS if s in groups]
    rest = sorted((s for s in groups if s not in FIRST_SECTIONS), key=str.lower)
    return [(sec, groups[sec][0], groups[sec][1]) for sec in first + rest]

# ---------------------------------------------------------------- file format (= Language.cpp)


def escape(s, key):
    out = []
    for i, c in enumerate(s):
        if c == "\\":
            out.append("\\\\")
        elif c == "\n":
            out.append("\\n")
        elif c == "\t":
            out.append("\\t")
        elif c == "\r":
            continue
        elif c == "=" and key:
            out.append("\\=")
        elif c == " " and (i == 0 or i == len(s) - 1):
            out.append("\\ ")
        elif i == 0 and key and c in ";[":
            out.append("\\" + c)
        else:
            out.append(c)
    return "".join(out)


def unescape(s):
    out, i = [], 0
    while i < len(s):
        c = s[i]
        if c != "\\":
            out.append(c)
            i += 1
            continue
        if i + 1 >= len(s):
            break
        n = s[i + 1]
        out.append({"n": "\n", "t": "\t"}.get(n, n))
        i += 2
    return "".join(out)


def split_line(line):
    i = 0
    while i < len(line):
        if line[i] == "\\":
            i += 2
            continue
        if line[i] == "=":
            return line[:i], line[i + 1:]
        i += 1
    return None


def read_lang(path):
    """{context: {english: translation}} and the [Language] header. A section is given to each of
    its contexts (and kept under its own name); files made before the sections had names use the
    contexts' names."""
    data, header = read_sections(path)
    out = {}
    for sec, d in data.items():
        for ctx in set(contexts_of(sec) + [sec]):
            out.setdefault(ctx, {}).update({k: v for k, v in d.items() if v or k not in out.get(ctx, {})})
    return out, header


def read_sections(path):
    """{section: {english: translation}} as in the file, and the [Language] header."""
    data, header, sec = {}, {}, None
    with open(path, encoding="utf-8-sig") as f:
        for line in f.read().splitlines():
            if not line or line.startswith(";"):
                continue
            if line.startswith("[") and line.rstrip().endswith("]"):
                sec = line.strip()[1:-1]
                continue
            kv = split_line(line)
            if not kv or sec is None:
                continue
            k, v = unescape(kv[0]), unescape(kv[1])
            if sec == "Language":
                header[k] = v
            else:
                data.setdefault(sec, {})[k] = v
    return data, header


def translatable(s):
    """Texts with no word in them (X, ?, 1 2 3 4, 32x32, 127.0.0.1, %g ...) are left out of the
    template: they read the same in every language. A language file can still list them."""
    if re.fullmatch(r"%\d+\s*:", s.strip()):
        return True  # "%1:" - punctuation that differs by language ("%1 :" in French)
    return re.search(r"[A-Za-z]{2,}", s) is not None and not re.fullmatch(r"\d+x\d+", s.strip())


def humanize(name):
    """VideoOutputModeButtonHelp -> Video Output Mode Button Help"""
    name = name.replace("_", " ")
    name = re.sub(r"(?<=[a-z0-9])(?=[A-Z])", " ", name)
    name = re.sub(r"(?<=[A-Z])(?=[A-Z][a-z])", " ", name)
    return re.sub(r"\s+", " ", name).strip()


class Texts:
    """Texts per context, in order, each with the places it appears."""

    def __init__(self):
        self.order = {}   # context -> [text]
        self.where = {}   # (context, text) -> [description]

    def add(self, ctx, text, where=None):
        text = text.strip()
        if not text or not translatable(text):
            return
        lst = self.order.setdefault(ctx, [])
        if text not in lst:
            lst.append(text)
        w = self.where.setdefault((ctx, text), [])
        if where and where not in w:
            w.append(where)

# ------------------------------------------------------------------------------- the windows

UI_PROPS = {"text": "text", "toolTip": "tooltip", "title": "title", "windowTitle": "window title",
            "placeholderText": "hint in the empty box", "statusTip": "status bar hint"}


def compiled_forms():
    cm = open(os.path.join(ROOT, "CMakeLists.txt"), encoding="utf-8").read()
    start = cm.index("add_library(el_app STATIC")
    block = cm[start:cm.index(")", cm.index("forms/", start))]
    return sorted(set(re.findall(r"forms/\w+\.ui", block)))


# Windows whose every text is set by their code (eld::<Dialog> sections): their forms' own
# texts are never shown, so they aren't asked for.
CODE_TEXT_FORMS = {"FormCustomParameters", "FormArcadeRunGameExtraMAME"}


def form_texts(texts):
    for rel in compiled_forms():
        root = ET.parse(os.path.join(ROOT, rel)).getroot()
        form = root.findtext("class")
        if form in CODE_TEXT_FORMS:
            continue
        top = root.find("widget")
        wt = top.find("property[@name='windowTitle']/string") if top is not None else None
        window = wt.text.strip() if wt is not None and wt.text else humanize(form.replace("Form", "", 1))
        if form == "FormMain":
            window = "Main window"

        def walk(w, area):
            name = w.get("name", "")
            title = w.find("property[@name='title']/string")
            if title is not None and title.text and title.text.strip():
                area = title.text.strip()
            for p in w.findall("property"):
                s = p.find("string")
                kind = UI_PROPS.get(p.get("name"))
                if kind and s is not None and s.text and s.get("notr") != "true":
                    where = "%s%s: %s, %s" % (window, " > " + area if area and kind != "title" else "",
                                              humanize(name), kind)
                    texts.add(form, s.text, where)
            for a in w.findall("attribute"):
                s = a.find("string")
                if a.get("name") == "title" and s is not None and s.text:
                    texts.add(form, s.text, "%s: %s, tab" % (window, humanize(name)))
            for item in w.findall("item"):
                s = item.find("property[@name='text']/string")
                if s is not None and s.text and s.get("notr") != "true":
                    texts.add(form, s.text, "%s%s: %s, list entry" % (window, " > " + area if area else "", humanize(name)))
            for child in w.findall("widget"):
                walk(child, area)
            for lay in w.iter("layout"):
                for item in lay.findall("item"):
                    for child in item.findall("widget"):
                        walk(child, area)

        if top is not None:
            walk(top, None)

# ---------------------------------------------------------------------------------- the code

CODE_WINDOWS = {
    "MainWindow": "Main window", "PreferencesDialog": "Preferences", "MameSettingsDialog": "Emulator Settings",
    "MameSettingsExtras": "Emulator Settings", "LayoutSettingsDialog": "Image Layout Settings",
    "GameFontsDialog": "Games List Font Settings", "GameFonts": "Games List Font Settings",
    "MachineFilterDialog": "Machine Type and Systems Filters", "GamesModel": "Games list",
    "ImageFinder": "Image panel", "GameDetailsDialog": "Game Details",
    "LastPlayedDialog": "Last Played Games", "EmulatorsSetupDialog": "Arcade Emulators Setup", "DriverStatusDialog": "Driver Status Filter", "ProgressWindow": "Progress window (creating the games list)", "PlayedGames": "Games list (Playtime column) and Last Played Games", "ImageLayouts": "Image panel", "main": "Start-up", "FirstStart": "Start-up", "Menus": "Menus",
    "FavoritesManagerDialog": "Favorites Manager", "GameFilesDialog": "Delete / Copy / Move Selected Games Files",
    "CustomParametersDialog": "Custom Parameters", "RunGameExtraDialog": "Run Game (Extra)",
    "ImageDeleteRenameDialog": "Rename / Delete Image", "ImagesDeleteClonesDialog": "Delete Images of Clone Games",
    "CategorySettingsDialog": "Image Category Settings", "ImagesManagerDialog": "MAME Images Manager",
    "VideoPreviewSettingsDialog": "Video Preview Settings",
    "ElWidgets": "Color lists (Preferences, Game Fonts, Image Category Settings ...)",
    "MiscFiltersDialog": "Miscellaneous Filters", "GamesFiltersDialog": "Games Filters - MAME and Arcade",
}

CODE_KINDS = [
    (r"drvNames", "Driver Status filter menu"), (r"meaning", "status bar, MAME's exit code"),
    (r"setToolTip", "tooltip"), (r"QMessageBox::question", "question"),
    (r"QMessageBox::(information|warning|critical|about)", "message"), (r"setWindowTitle", "window title"),
    (r"QProgressDialog", "progress window"), (r"getOpenFileName|getSaveFileName|getExistingDirectory", "file dialog"),
    (r"QColorDialog|QFontDialog", "dialog title"), (r"StatusBar", "status bar"), (r"addAction|QAction", "menu item"),
    (r"HeaderLabels|headerData|kColumns|longT|shortT", "column title"), (r"setPlaceholderText", "hint in the empty box"),
    (r"setHintText", "image hint box"), (r"tips\[\]|\{\"\w+\", QT_TR_NOOP", "tooltip"),
    (r"QStandardItem|kRows|kCategories|kSections", "list entry"), (r"QLabel|setText", "text"),
]


def code_where(filename, line, src_lines):
    base = os.path.splitext(os.path.basename(filename))[0]
    window = CODE_WINDOWS.get(base, humanize(base))
    lines = src_lines.get(filename)
    if lines is None:
        try:
            lines = open(filename, encoding="utf-8").read().splitlines()
        except OSError:
            lines = []
        src_lines[filename] = lines
    # the call can start a few lines above the text
    snippet = " ".join(lines[max(0, line - 4):line]) if lines else ""
    kind = None
    for pat, k in CODE_KINDS:
        last = None
        for m in re.finditer(pat, snippet):
            last = m.start()
        if last is not None and (kind is None or last > kind[0]):
            kind = (last, k)
    return "%s: %s" % (window, kind[1]) if kind else window


def code_texts(texts):
    sources = []
    for d in ("src/app", "src/core", "src/ui"):
        for n in sorted(os.listdir(os.path.join(ROOT, d))):
            if n.endswith((".cpp", ".h")):
                sources.append(os.path.join(ROOT, d, n))
    with tempfile.TemporaryDirectory() as tmp:
        ts = os.path.join(tmp, "out.ts")
        lst = os.path.join(tmp, "files.lst")
        open(lst, "w").write("\n".join(sources))
        subprocess.run([LUPDATE, "-silent", "-locations", "absolute", "@" + lst, "-ts", ts], check=True)
        root = ET.parse(ts).getroot()
        src_lines = {}
        found = []
        for ctx in root.iter("context"):
            name = ctx.findtext("name")
            for msg in ctx.iter("message"):
                src = msg.findtext("source")
                if not src:
                    continue
                places = []
                for loc in msg.findall("location"):
                    fn = loc.get("filename")
                    fn = fn if os.path.isabs(fn) else os.path.normpath(os.path.join(tmp, fn))
                    places.append(code_where(fn, int(loc.get("line", "0")), src_lines))
                found.append((name, src, places))
    for name, src, places in sorted(found, key=lambda t: t[0].lower()):
        if not places:
            texts.add(name, src)
        for p in places:
            texts.add(name, src, p)

# ------------------------------------------------------------------------------------- menus

MENU_NAMES = {"MainMenuOptions": "Menu Options", "PopupMenuImages": "Image right-click menu",
              "PopupMenuGamesList": "Games list right-click menu", "PopupGamesColumns": "Column titles right-click menu"}


def menu_texts(texts):
    menus = json.load(open(os.path.join(ROOT, "forms", "FormMain.menus.json"), encoding="utf-8"))

    def walk(items, path):
        for it in items:
            cap = it.get("caption", "")
            if cap == "-":
                continue
            texts.add("Menus", cap, " > ".join(path))
            hint = it.get("hint", "")
            if hint and not hint.lower().startswith("http"):
                texts.add("Menus", hint, " > ".join(path + [cap.replace("&", "")]) + ", tooltip")
            walk(it.get("items") or [], path + [cap.replace("&", "")])

    for name, items in menus.items():
        top = MENU_NAMES.get(name, humanize(re.sub(r"^(Popup|Menu)", "", name)) + " menu")
        walk(items, [top])
    texts.add("Menus", "Not ported to the C++ version yet", "Any menu item that isn't ported yet, tooltip")

# ------------------------------------------------------------------------------------ writing

SECTION_NOTES = {
    "Common": "Texts translated in every section at once",
    "Menus": "Menu Options (the Menu button), the tool bar menus and the right-click menus",
    "Main Window": "The main window: tool bars, search box, status bar, messages and tooltips",
    "Tool Bar": "The captions under the tool bar pictures (the tool bar widens to fit them;\n; a long caption takes two lines)",
}


def write_lang(path, texts, old, header):
    lines = ["; EmuLoader ReLoaded language file (UTF-8).",
             "; Each line is: English text=translation. Leave the right side empty to keep the English text.",
             "; The comment above a line says where the text appears.",
             ";",
             "; To start a translation, copy this file and name the copy after the language code:",
             "; de.ini (German), es.ini (Spanish), pt_BR.ini (Portuguese, Brazil), zh_TW.ini ...",
             "; Under [Language], Name= is what users see in the list (\"Deutsch\"); Locale= may stay",
             "; empty, as the file name gives the language.",
             "; \\n is a new line. Keep %1, %2 ... and HTML such as <b>...</b> as they are.",
             ";",
             "; A [Common] section translates a text in every section at once. A text such as \"Reset\"",
             "; appears in many windows; instead of filling in every line, add a [Common] section near",
             "; the top of the file:",
             ";",
             ";   [Common]",
             ";   Reset=Zurücksetzen",
             ";   Cancel=Abbrechen",
             ";",
             "; Each text is looked up in its window's section first, then in [Common]:",
             ";   - an empty line in a window's section uses the [Common] translation;",
             ";   - a filled-in line wins for that window, so a window can use another wording;",
             ";   - to keep the English text in one window, write it on the right side too:",
             ";       [Favorites Manager]",
             ";       Reset=Reset",
             ";",
             "; LENGTH: the windows have fixed layouts, like the original EmuLoader, so a translation",
             "; that is much longer than the English may not fit. EmuLoader helps where it can:",
             ";   - labels and check boxes widen into empty space beside them, up to the next control;",
             ";   - a text that still doesn't fit is cut with \"…\" and the whole text shows as its tooltip.",
             "; Buttons, list entries and group box titles keep their size, so short words matter most",
             "; there (\"Select\", \"Reset\", \"Default\"). A \\n splits a label or check box over two lines",
             "; where there is room below it.",
             ";",
             "; To check a translation, run langcheck.exe (next to EmuLoader-ReLoaded.exe) on it:",
             ";     langcheck lang\\de.ini",
             "; It lists every text that is too long, with the pixels it needs and has. Add",
             ";     --grab pictures",
             "; to also save a picture of each window with the translation in the folder \"pictures\".",
             ";",
             "; After an update, run tools/make_language_template.py on this file to add the new texts.",
             ";",
             "; FONTS: letters the chosen fonts don't have (Thai, Japanese, Korean, Chinese ...) are shown in",
             "; the system's own font for them, the language's script first. Some scripts read better in",
             "; a font of their own, or a little larger. Under [Language]:",
             ";   Font=      a font for EmuLoader's windows and menus, in place of Segoe UI",
             ";              (for example Leelawadee UI for Thai, Yu Gothic UI for Japanese)",
             ";   FontSize=  points added to all of EmuLoader's text: +1 or +2",
             "; Leave them empty for the usual fonts. The user's own font choices keep their font; the",
             "; extra size is added to them too.",
             "",
             "[Language]",
             "Name=" + header.get("Name", "English"),
             "Locale=" + header.get("Locale", "en"),
             "Author=" + header.get("Author", ""),
             "Font=" + header.get("Font", ""),
             "FontSize=" + header.get("FontSize", ""),
             ""]
    # earlier files listed padded texts; they match their unpadded line now
    old = {c: {k.strip(): v for k, v in d.items()} for c, d in old.items()}
    if old.get("Common"):
        lines += ["; " + SECTION_NOTES["Common"], "[Common]"]
        for k, v in old["Common"].items():
            lines.append(escape(k, True) + "=" + escape(v, False))
        lines.append("")
    groups = grouped(texts)
    present = {(sec, t) for sec, lst, _ in groups for t in lst}

    def translation(sec, src):
        # a window's contexts may have had different translations: the section's own, else the
        # code's (eld::...; a form's was sometimes shortened to fit its fixed size), else the form's
        order = [sec] + sorted(contexts_of(sec), key=lambda c: not c.startswith("eld::"))
        for c in order:
            if old.get(c, {}).get(src):
                return old[c][src]
        return ""

    # translations of texts that moved (another window, a renamed section): carried over by their
    # English text, when it's translated the same wherever it was
    gone, moved = {}, {}
    for c, d in old.items():
        if c == "Common":
            continue
        for k, v in d.items():
            if v and (section_of(c), k) not in present:
                gone[(section_of(c), k)] = v
                moved.setdefault(k, set()).add(v)
    carried = 0
    for sec, lst, where in groups:
        note = SECTION_NOTES.get(sec)
        if note:
            lines.append("; " + note)
        lines.append("[%s]" % sec)
        for src in lst:
            w = where.get(src, [])
            if w:
                more = " (and %d more)" % (len(w) - 3) if len(w) > 3 else ""
                lines.append("; " + " | ".join(w[:3]) + more)
            t = translation(sec, src)
            if not t and len(moved.get(src, ())) == 1:
                t = next(iter(moved[src]))
                carried += 1
                for key in [g for g in gone if g[1] == src]:
                    del gone[key]
            lines.append(escape(src, True) + "=" + escape(t, False))
            lines.append("")  # a blank line after each text, for readability
    if carried:
        print("%s: %d translations carried over to where their texts are now" % (path, carried))
    if gone:
        lines.append("; No longer used:")
        for (c, k), v in gone.items():
            lines.append(";[%s] %s=%s" % (c, escape(k, True), escape(v, False)))
    with open(path, "w", encoding="utf-8", newline="\r\n") as f:
        f.write("\n".join(lines) + "\n")


def main():
    texts = Texts()
    menu_texts(texts)
    code_texts(texts)
    form_texts(texts)
    if len(sys.argv) > 1:
        path = sys.argv[1]
        old, header = read_lang(path) if os.path.exists(path) else ({}, {"Name": os.path.splitext(os.path.basename(path))[0]})
    else:
        path = os.path.join(ROOT, "lang", "English.ini")
        old, header = {}, {"Name": "English", "Locale": "en"}
    os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
    write_lang(path, texts, old, header)
    # Read it back: every text must come back exactly (the escaping is right).
    back, _ = read_lang(path)
    for ctx, lst in texts.order.items():
        missing = [s for s in lst if s not in back.get(ctx, {})]
        if missing:
            sys.exit("%s: [%s] doesn't read back: %r" % (path, ctx, missing[:3]))
    secs = grouped(texts)
    n = sum(len(lst) for _, lst, _ in secs)
    done = sum(1 for sec, lst, _ in secs for s in lst if back.get(contexts_of(sec)[0], {}).get(s))
    print("%s: %d texts in %d sections%s" % (path, n, len(secs), (", %d translated" % done) if old else ""))
    unnamed = sorted(c for c in texts.order if c not in SECTIONS)
    if unnamed:
        print("Not in src/app/LanguageSections.inc (their section is the program's name): " + ", ".join(unnamed))


if __name__ == "__main__":
    main()
