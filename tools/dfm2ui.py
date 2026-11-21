#!/usr/bin/env python3
"""dfm2ui - convert Delphi text .dfm forms into Qt Designer .ui files.

Each VCL control becomes a Qt widget at the same position and size. VCL-only
behavior that Qt Designer can't express (Align, Anchors, colors, pixel font
sizes, visibility, gradient styles, event handler names) is stored as dynamic
properties named "vcl*". The runtime helper eld::vcl::apply() reads those after
setupUi() and reproduces the Delphi behavior (see src/ui/VclRuntime.cpp).

Menus (TMainMenu / TPopupMenu / TBcBarPopupMenu and their TMenuItems) are
written to <Form>.menus.json so the C++ code can build the same QMenus.

Usage:  dfm2ui.py <input.dfm> [more.dfm ...] -o <output folder>
"""
import argparse
import json
import os
import re
import sys
import xml.sax.saxutils as sx

# ------------------------------------------------------------------ parsing

class Obj:
    def __init__(self, kind, name, cls):
        self.kind, self.name, self.cls = kind, name, cls
        self.props = {}
        self.children = []

    def get(self, key, default=None):
        return self.props.get(key, default)


class Parser:
    def __init__(self, text):
        self.lines = text.splitlines()
        self.i = 0

    def peek(self):
        return self.lines[self.i] if self.i < len(self.lines) else None

    def parse(self):
        while self.i < len(self.lines) and not self.lines[self.i].strip():
            self.i += 1
        return self.parse_object()

    def parse_object(self):
        line = self.lines[self.i].strip()
        self.i += 1
        m = re.match(r'(object|inherited|inline)\s+(?:(\w+)\s*:\s*)?(\w+)(?:\s*\[\d+\])?', line)
        if not m:
            raise ValueError("expected object at line %d: %r" % (self.i, line))
        obj = Obj(m.group(1), m.group(2) or "", m.group(3))
        while self.i < len(self.lines):
            s = self.lines[self.i].strip()
            if not s:
                self.i += 1
                continue
            if s == "end":
                self.i += 1
                return obj
            if re.match(r'(object|inherited|inline)\s', s):
                obj.children.append(self.parse_object())
                continue
            m = re.match(r'([\w.]+)\s*=\s*(.*)$', s)
            if not m:
                self.i += 1
                continue
            self.i += 1
            obj.props[m.group(1)] = self.parse_value(m.group(2))
        return obj

    # value forms: number, 'string'#13'more' + (continued), ident, [set], (list), <collection>, {binary}
    def parse_value(self, rest):
        rest = rest.strip()
        if rest == "" :           # value continues on next lines (multi-line string)
            return self.parse_string_lines()
        c = rest[0]
        if c in "'#":
            text, cont = parse_string(rest)
            while cont:
                text2, cont = parse_string(self.lines[self.i].strip())
                self.i += 1
                text += text2
            return text
        if c == "[":
            while not rest.endswith("]"):
                rest += " " + self.lines[self.i].strip()
                self.i += 1
            return [x.strip() for x in rest[1:-1].split(",") if x.strip()]
        if c == "(":
            # Collect lines until the closing ')' that is outside any quoted string.
            lines = [rest[1:]]
            def closed(t):
                t = t.rstrip()
                return t.endswith(")") and t.count("'") % 2 == 0
            while not closed(lines[-1]):
                lines.append(self.lines[self.i].strip())
                self.i += 1
            lines[-1] = lines[-1].rstrip()[:-1]
            items, pending = [], False
            for ln in lines:
                ln = ln.strip()
                if not ln:
                    continue
                if ln[0] in "'#":
                    t, cont = parse_string(ln)
                    if pending:
                        items[-1] += t
                    else:
                        items.append(t)
                    pending = cont
                else:
                    items.extend(ln.split())
                    pending = False
            return items
        if c == "<":
            items = []
            if rest.endswith(">"):
                return items
            cur = None
            while True:
                s = self.lines[self.i].strip()
                self.i += 1
                if s.startswith("item"):
                    cur = {}
                    continue
                if s.startswith("end"):
                    items.append(cur)
                    if s.endswith(">"):
                        return items
                    continue
                if s == ">":
                    return items
                m = re.match(r'([\w.]+)\s*=\s*(.*)$', s)
                if m and cur is not None:
                    cur[m.group(1)] = self.parse_value(m.group(2))
        if c == "{":
            data = rest[1:]
            while not data.rstrip().endswith("}"):
                data += self.lines[self.i].strip()
                self.i += 1
            return {"binary": re.sub(r'[^0-9A-Fa-f]', "", data)}
        return rest

    def parse_string_lines(self):
        text = ""
        cont = True
        while cont:
            t, cont = parse_string(self.lines[self.i].strip())
            self.i += 1
            text += t
        return text


def parse_string(s):
    """Parse 'abc'#13#10'def' [+]. Returns (text, continues_on_next_line)."""
    out = []
    i = 0
    n = len(s)
    while i < n:
        c = s[i]
        if c == "'":
            i += 1
            while i < n:
                if s[i] == "'":
                    if i + 1 < n and s[i + 1] == "'":
                        out.append("'")
                        i += 2
                        continue
                    i += 1
                    break
                out.append(s[i])
                i += 1
        elif c == "#":
            m = re.match(r'#(\$[0-9A-Fa-f]+|\d+)', s[i:])
            v = m.group(1)
            out.append(chr(int(v[1:], 16) if v.startswith("$") else int(v)))
            i += len(m.group(0))
        elif c == "+":
            return "".join(out), True
        elif c.isspace():
            i += 1
        else:
            break
    return "".join(out), False

# ----------------------------------------------------------------- mapping

# VCL class -> (Qt class, extends, is_container)
CLASSMAP = {
    "TPanel": ("ElPanel", "QFrame", True), "TPanelEx": ("ElPanel", "QFrame", True),
    "TPanelLabelEx": ("ElPanel", "QFrame", True),
    "TGroupBox": ("ElGroupBox", "QGroupBox", True), "TAdvGroupBoxEx": ("ElGroupBox", "QGroupBox", True),
    "TScrollBox": ("ElPanel", "QFrame", True), "TToolBar": ("ElToolBar", "QFrame", True),
    "TTabSheet": ("QWidget", None, True), "TPage": ("QWidget", None, True),
    "TPageControl": ("QTabWidget", None, True), "TNotebook": ("QStackedWidget", None, True),
    "TShadowLabel": ("ElLabel", "QLabel", False), "TLabel": ("ElLabel", "QLabel", False),
    "TGlowLabel": ("ElLabel", "QLabel", False), "TEKLabel": ("ElLabel", "QLabel", False),
    "TStaticText": ("ElLabel", "QLabel", False),
    "TBitBtnEx": ("ElButton", "QPushButton", False), "TBitBtn": ("ElButton", "QPushButton", False),
    "TButton": ("ElButton", "QPushButton", False), "TButtonsEx": ("ElButton", "QPushButton", False),
    "TSpeedButtonEx": ("ElButton", "QPushButton", False), "TSpeedButton": ("ElButton", "QPushButton", False),
    "TToolButton": ("ElButton", "QPushButton", False),
    "TAdvOfficeCheckBoxEx": ("QCheckBox", None, False), "TCheckBox": ("QCheckBox", None, False),
    "TAdvOfficeRadioButtonEx": ("QRadioButton", None, False), "TRadioButton": ("QRadioButton", None, False),
    "TEditEx": ("QLineEdit", None, False), "TTntEditEx": ("QLineEdit", None, False), "TEdit": ("QLineEdit", None, False),
    "TComboBox2Ex": ("QComboBox", None, False), "TComboBox": ("QComboBox", None, False),
    "TColorBoxEx": ("ElColorBox", "QComboBox", False),
    "TMemo": ("QPlainTextEdit", None, False), "TTntRichEdit": ("QTextBrowser", None, False),
    "TRichEditURL": ("QTextBrowser", None, False), "TRichEdit": ("QTextBrowser", None, False),
    "TListBox": ("QListWidget", None, False), "TCheckListBox": ("QListWidget", None, False),
    "TEasyListview": ("QTreeView", None, False), "TListView": ("QTreeView", None, False),
    "TTreeView": ("QTreeWidget", None, False), "TShellTreeView": ("QTreeView", None, False),
    "TGaugeBar": ("QSlider", None, False), "TGaugeBar2": ("QSlider", None, False),
    "TXiTrackBar": ("QSlider", None, False), "TTrackBar": ("QSlider", None, False),
    "TXiProgressBar": ("QProgressBar", None, False), "TProgressBar": ("QProgressBar", None, False),
    "TImage": ("ElImage", "QLabel", False), "TImage32": ("ElImage", "QLabel", False),
    "TShape": ("ElShape", "QFrame", False), "TBevel": ("ElShape", "QFrame", False),
    "TBevelEx": ("ElShape", "QFrame", False),
    "TSplitterEx": ("ElSplitter", "QFrame", False), "TSplitter": ("ElSplitter", "QFrame", False),
    "TSpinEdit": ("QSpinBox", None, False), "TUpDown": ("QSpinBox", None, False),
    "TWebBrowser": ("QTextBrowser", None, False),
}
MENU_CLASSES = {"TPopupMenu", "TMainMenu", "TBcBarPopupMenu"}
NONVISUAL = {"TImageList", "TTimer", "TFontDialog", "TZipForge", "TBcCustomDrawModule", "TOpenDialog",
             "TSaveDialog", "TColorDialog", "TActionList", "TApplicationEvents", "TMenuItem"}

# Standard VCL color names -> RGB hex. System colors use Windows defaults.
COLORS = {
    "clBlack": "#000000", "clMaroon": "#800000", "clGreen": "#008000", "clOlive": "#808000",
    "clNavy": "#000080", "clPurple": "#800080", "clTeal": "#008080", "clGray": "#808080",
    "clSilver": "#c0c0c0", "clRed": "#ff0000", "clLime": "#00ff00", "clYellow": "#ffff00",
    "clBlue": "#0000ff", "clFuchsia": "#ff00ff", "clAqua": "#00ffff", "clWhite": "#ffffff",
    "clMoneyGreen": "#c0dcc0", "clSkyBlue": "#a6caf0", "clCream": "#fffbf0", "clMedGray": "#a0a0a4",
    "clLtGray": "#c0c0c0", "clDkGray": "#808080",
    "clBtnFace": "#f0f0f0", "clWindow": "#ffffff", "clWindowText": "#000000", "clBtnText": "#000000",
    "clBtnShadow": "#a0a0a0", "clBtnHighlight": "#ffffff", "clHighlight": "#0078d7",
    "clHighlightText": "#ffffff", "clGrayText": "#6d6d6d", "cl3DLight": "#e3e3e3",
    "cl3DDkShadow": "#696969", "clInfoBk": "#ffffe1", "clInfoText": "#000000", "clMenu": "#f0f0f0",
    "clMenuText": "#000000", "clActiveCaption": "#99b4d1", "clInactiveCaption": "#bfcddb",
    "clWindowFrame": "#646464", "clHotLight": "#0066cc", "clMenuHighlight": "#3399ff",
    "clMenuBar": "#f0f0f0", "clScrollBar": "#c8c8c8", "clGradientActiveCaption": "#b9d1ea",
}


def color(v):
    if v is None or v in ("clNone", "clDefault"):
        return None
    if isinstance(v, str) and v in COLORS:
        return COLORS[v]
    try:
        n = int(v[1:], 16) if isinstance(v, str) and v.startswith("$") else int(v)
    except (TypeError, ValueError):
        return None
    if n < 0 or n > 0xFFFFFF:
        return None
    r, g, b = n & 0xFF, (n >> 8) & 0xFF, (n >> 16) & 0xFF
    return "#%02x%02x%02x" % (r, g, b)


def strip_accel(s):
    return re.sub(r'&(.)', lambda m: m.group(1), s)


def esc(s):
    return sx.escape(str(s))

# ------------------------------------------------------------------ output

class Writer:
    def __init__(self):
        self.out = []
        self.custom = {}
        self.menus = {}
        self.unknown = {}
        self.names = set()
        # Combo items that are data, not text (MAME option values written to mame.ini):
        # not translated (notr), so a language file can't change them.
        self.notr_items = False

    def w(self, depth, s):
        self.out.append("  " * depth + s)

    def prop(self, depth, name, typ, value, stdset=True):
        attr = '' if stdset else ' stdset="0"'
        # Dynamic (vcl*) properties are data for the VCL runtime, never translated.
        notr = ' notr="true"' if (not stdset and typ == "string") else ''
        self.w(depth, '<property name="%s"%s><%s%s>%s</%s></property>' % (name, attr, typ, notr, esc(value), typ))

    def dyn(self, depth, name, value):
        typ = "bool" if isinstance(value, bool) else ("number" if isinstance(value, int) else "string")
        if isinstance(value, bool):
            value = "true" if value else "false"
        self.prop(depth, name, typ, value, stdset=False)

    def uniq(self, name, cls):
        base = name or cls[1:].lower()
        n, k = base, 1
        while n in self.names:
            k += 1
            n = "%s_%d" % (base, k)
        self.names.add(n)
        return n

    # ---- menus
    def menu_items(self, obj):
        items = []
        for c in obj.children:
            if c.cls != "TMenuItem":
                continue
            items.append({
                "name": c.name, "caption": c.get("Caption", ""), "hint": c.get("Hint", ""),
                "checked": c.get("Checked") == "True", "radio": c.get("RadioItem") == "True",
                "group": int(c.get("GroupIndex", "0")), "visible": c.get("Visible") != "False",
                "enabled": c.get("Enabled") != "False", "tag": int(c.get("Tag", "0")),
                "autoCheck": c.get("AutoCheck") == "True", "imageIndex": int(c.get("ImageIndex", "-1")),
                "shortcut": c.get("ShortCut", ""), "onClick": c.get("OnClick", ""),
                "items": self.menu_items(c),
            })
        return items

    # ---- widgets
    def widget(self, depth, obj, parent_cls, root=False):
        if obj.cls in MENU_CLASSES:
            self.menus[obj.name] = self.menu_items(obj)
            return
        if obj.cls in NONVISUAL:
            return
        if root:
            qcls, ext, container = "QWidget", None, True
        elif obj.cls in CLASSMAP:
            qcls, ext, container = CLASSMAP[obj.cls]
        else:
            # Unknown class: keep it visible as a panel if it has children, else a frame placeholder.
            self.unknown[obj.cls] = self.unknown.get(obj.cls, 0) + 1
            if "Left" not in obj.props and "Width" not in obj.props:
                return
            qcls, ext, container = ("ElPanel", "QFrame", True)
        if ext:
            self.custom[qcls] = (ext, container)

        name = self.uniq(obj.name, obj.cls)
        p = obj.props
        if parent_cls == "QTabWidget":
            pass
        self.w(depth, '<widget class="%s" name="%s">' % (qcls, name))
        d = depth + 1

        # geometry
        if root:
            w = int(p.get("ClientWidth", p.get("Width", 640)))
            h = int(p.get("ClientHeight", p.get("Height", 480)))
            self.w(d, '<property name="geometry"><rect><x>0</x><y>0</y><width>%d</width><height>%d</height></rect></property>' % (w, h))
            if "Caption" in p:
                self.prop(d, "windowTitle", "string", p["Caption"])
        elif parent_cls not in ("QTabWidget", "QStackedWidget"):
            x, y = int(p.get("Left", 0)), int(p.get("Top", 0))
            w, h = int(p.get("Width", 0)), int(p.get("Height", 0))
            if obj.cls == "TToolButton":
                w = w or 55
                h = h or 55
            if w == 0 and h == 0:
                w, h = 50, 20
            self.w(d, '<property name="geometry"><rect><x>%d</x><y>%d</y><width>%d</width><height>%d</height></rect></property>' % (x, y, w, h))

        # text
        cap = p.get("Caption")
        if cap is not None and not root:
            if qcls in ("ElLabel", "ElButton", "QCheckBox", "QRadioButton"):
                if qcls == "ElLabel":
                    # TShadowLabel defaults ShowAccelChar to False, TLabel to True.
                    accel = p.get("ShowAccelChar", "False" if obj.cls == "TShadowLabel" else "True") == "True"
                    text = strip_accel(cap) if accel else cap
                else:
                    text = cap
                self.prop(d, "text", "string", text)
            elif qcls == "ElGroupBox":
                self.prop(d, "title", "string", strip_accel(cap))
            elif qcls == "ElPanel" and cap:
                self.dyn(d, "vclCaption", cap)
        if p.get("Hint"):
            self.prop(d, "toolTip", "string", p["Hint"])
        if qcls == "QLineEdit" and p.get("Text"):
            self.prop(d, "text", "string", p["Text"])
        if qcls == "QLineEdit" and p.get("ReadOnly") == "True":
            self.prop(d, "readOnly", "bool", "true")
        if qcls == "QLineEdit" and p.get("PasswordChar", "#0") not in ("#0", "", "\x00"):
            self.prop(d, "echoMode", "enum", "QLineEdit::Password")
        if qcls == "QPlainTextEdit" and isinstance(p.get("Lines.Strings"), list):
            self.prop(d, "plainText", "string", "\n".join(p["Lines.Strings"]))
        if qcls in ("QCheckBox", "QRadioButton"):
            checked = p.get("Checked") == "True" or p.get("State") == "cbChecked"
            if checked:
                self.prop(d, "checked", "bool", "true")
            if p.get("AllowGrayed") == "True":
                self.prop(d, "tristate", "bool", "true")
            if p.get("Alignment") == "taLeftJustify" and obj.cls in ("TCheckBox", "TRadioButton"):
                self.dyn(d, "vclRightToLeft", True)
        if qcls == "QSlider":
            self.w(d, '<property name="orientation"><enum>%s</enum></property>' %
                   ("Qt::Vertical" if p.get("Orientation", p.get("Kind", "")) in ("trVertical", "sbVertical") else "Qt::Horizontal"))
            if obj.cls == "TGaugeBar2":
                # float slider: the runtime maps it onto an integer QSlider (x1000)
                self.dyn(d, "vclMinF", p.get("Min", "0"))
                self.dyn(d, "vclMaxF", p.get("Max", "100"))
                self.dyn(d, "vclPosF", p.get("Position", "0"))
            else:
                for key, qn in (("Min", "minimum"), ("Max", "maximum"), ("Position", "value"),
                                ("LargeChange", "pageStep"), ("SmallChange", "singleStep")):
                    if key in p and re.match(r'^-?\d+$', str(p[key])):
                        self.prop(d, qn, "number", p[key])
                if "Max" not in p:
                    self.prop(d, "maximum", "number", 100)
        if qcls == "QProgressBar":
            self.prop(d, "maximum", "number", p.get("Max", 100))
            self.prop(d, "value", "number", p.get("Position", 0))
        if qcls == "QSpinBox":
            for key, qn in (("MinValue", "minimum"), ("MaxValue", "maximum"), ("Value", "value")):
                if key in p:
                    self.prop(d, qn, "number", p[key])
        if obj.cls in ("TSpeedButtonEx", "TSpeedButton") and int(p.get("GroupIndex", "0")) > 0:
            self.prop(d, "checkable", "bool", "true")
            self.dyn(d, "vclGroupIndex", int(p["GroupIndex"]))
            if p.get("Down") == "True":
                self.prop(d, "checked", "bool", "true")
            if p.get("AllowAllUp") == "True":
                self.dyn(d, "vclAllowAllUp", True)
        if obj.cls == "TToolButton":
            if p.get("Style") == "tbsSeparator":
                self.dyn(d, "vclSeparator", True)
            if "ImageIndex" in p:
                self.dyn(d, "vclImageIndex", int(p["ImageIndex"]))
            if p.get("DropdownMenu"):
                self.dyn(d, "vclDropdownMenu", p["DropdownMenu"])
            if p.get("Grouped") == "True" or p.get("Style") == "tbsCheck":
                self.prop(d, "checkable", "bool", "true")
        if obj.cls in ("TBitBtnEx", "TBitBtn", "TButton") and p.get("ModalResult"):
            self.dyn(d, "vclModalResult", p["ModalResult"])
        if p.get("Default") == "True" and qcls == "ElButton":
            self.prop(d, "default", "bool", "true")

        # combo / list items
        items = p.get("Items.Strings")
        if isinstance(items, list) and qcls in ("QComboBox", "QListWidget", "ElColorBox"):
            notr = ' notr="true"' if self.notr_items and qcls == "QComboBox" else ''
            for it in items:
                self.w(d, '<item><property name="text"><string%s>%s</string></property></item>' % (notr, esc(it)))
            if "ItemIndex" in p:
                self.prop(d, "currentIndex" if qcls != "QListWidget" else "currentRow", "number", p["ItemIndex"])
        if qcls == "QComboBox" and p.get("Style") in (None, "csDropDown", "csSimple"):
            self.prop(d, "editable", "bool", "true")
            if p.get("Text"):
                self.prop(d, "currentText", "string", p["Text"])

        # VCL behavior carried as dynamic properties
        if not root:
            if p.get("Align") and p["Align"] != "alNone":
                self.dyn(d, "vclAlign", p["Align"])
            if isinstance(p.get("Anchors"), list):
                self.dyn(d, "vclAnchors", ",".join(p["Anchors"]))
            if p.get("Visible") == "False":
                self.dyn(d, "vclVisible", False)
            if p.get("Enabled") == "False":
                self.prop(d, "enabled", "bool", "false")
        if "Tag" in p:
            self.dyn(d, "vclTag", int(p["Tag"]))
        self.dyn(d, "vclClass", obj.cls)
        col = color(p.get("Color"))
        if col and (p.get("ParentColor") != "True"):
            self.dyn(d, "vclColor", col)
        if p.get("Transparent") == "True":
            self.dyn(d, "vclTransparent", True)
        if root or p.get("ParentFont") == "False" or "Font.Name" in p:
            if "Font.Name" in p:
                self.dyn(d, "vclFontName", p["Font.Name"])
            if "Font.Height" in p:
                self.dyn(d, "vclFontPx", abs(int(p["Font.Height"])))
            fc = color(p.get("Font.Color"))
            if fc:
                self.dyn(d, "vclFontColor", fc)
            st = p.get("Font.Style")
            if isinstance(st, list) and st:
                self.dyn(d, "vclFontStyle", ",".join(st))
        if p.get("Alignment") in ("taCenter", "taRightJustify") and qcls in ("ElLabel", "QLineEdit"):
            self.dyn(d, "vclAlignment", p["Alignment"])
        if p.get("Layout") in ("tlCenter", "tlBottom") and qcls == "ElLabel":
            self.dyn(d, "vclLayout", p["Layout"])
        if p.get("WordWrap") == "True" and qcls == "ElLabel":
            self.prop(d, "wordWrap", "bool", "true")
        # TLabel / TShadowLabel are AutoSize unless the form says otherwise (VCL default True):
        # the label grows with its text, so longer translations aren't cut off.
        autosize = p.get("AutoSize") == "True" or (obj.cls in ("TLabel", "TShadowLabel") and p.get("AutoSize") != "False")
        if autosize and qcls == "ElLabel" and p.get("WordWrap") != "True":
            self.dyn(d, "vclAutoSize", True)
        # custom-control looks
        for key in ("Color1", "Color2", "Color3", "Color4", "ColorFrame", "ColorInnerFrame",
                    "ShadowColor", "GradientColorTop", "GradientColorBottom", "FrameColor", "Brush.Color", "Pen.Color",
                    "DefaultColorColor", "Selected"):
            c = color(p.get(key))
            if c:
                self.dyn(d, "vcl" + key.replace(".", ""), c)
        for key in ("Style", "Shape", "BevelOuter", "BevelInner", "BorderStyle", "Steps", "Kind",
                    "ShadowEnabled", "EllipsType", "Frames", "Flat", "Margin", "Spacing"):
            v = p.get(key)
            if v is not None and not isinstance(v, (dict,)):
                self.dyn(d, "vcl" + key, ",".join(v) if isinstance(v, list) else str(v))
        for ev in ("OnClick", "OnChange", "OnSelect", "OnDblClick", "OnKeyPress", "OnExit"):
            if p.get(ev):
                self.dyn(d, "vcl" + ev, p[ev])

        # children
        if container:
            for c in obj.children:
                if qcls == "QTabWidget" and c.cls == "TTabSheet":
                    self.w(d, '<widget class="QWidget" name="%s">' % self.uniq(c.name, c.cls))
                    self.w(d + 1, '<attribute name="title"><string>%s</string></attribute>' % esc(strip_accel(c.get("Caption", ""))))
                    for cc in c.children:
                        self.widget(d + 1, cc, "QWidget")
                    self.w(d, '</widget>')
                elif qcls == "QStackedWidget" and c.cls == "TPage":
                    self.w(d, '<widget class="QWidget" name="%s">' % self.uniq(c.name or "page", c.cls))
                    for cc in c.children:
                        self.widget(d + 1, cc, "QWidget")
                    self.w(d, '</widget>')
                else:
                    self.widget(d, c, qcls)
        self.w(depth, '</widget>')


def convert(path, outdir):
    raw = open(path, "rb").read()
    text = raw.decode("utf-8") if raw.startswith(b"\xef\xbb\xbf") or _is_utf8(raw) else raw.decode("cp1252")
    text = text.lstrip("﻿")
    root = Parser(text).parse()
    wr = Writer()
    form_name = root.cls[1:] if root.cls.startswith("T") else root.cls
    wr.notr_items = form_name.startswith("FormMAMESettings")
    wr.w(0, '<?xml version="1.0" encoding="UTF-8"?>')
    wr.w(0, '<ui version="4.0">')
    wr.w(1, '<class>%s</class>' % form_name)
    root.name = form_name
    wr.widget(1, root, None, root=True)
    if wr.custom:
        wr.w(1, '<customwidgets>')
        for cls, (ext, cont) in sorted(wr.custom.items()):
            wr.w(2, '<customwidget><class>%s</class><extends>%s</extends><header>ElWidgets.h</header>%s</customwidget>'
                 % (cls, ext, '<container>1</container>' if cont else ''))
        wr.w(1, '</customwidgets>')
    wr.w(1, '<resources/><connections/>')
    wr.w(0, '</ui>')
    base = os.path.join(outdir, form_name)
    with open(base + ".ui", "w", encoding="utf-8") as f:
        f.write("\n".join(wr.out) + "\n")
    if wr.menus:
        with open(base + ".menus.json", "w", encoding="utf-8") as f:
            json.dump(wr.menus, f, indent=1, ensure_ascii=False)
    return form_name, wr.unknown


def _is_utf8(b):
    try:
        b.decode("utf-8")
        return True
    except UnicodeDecodeError:
        return False


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dfm", nargs="+")
    ap.add_argument("-o", "--out", required=True)
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    unknown = {}
    ok = 0
    for path in a.dfm:
        try:
            name, unk = convert(path, a.out)
            ok += 1
            for k, v in unk.items():
                unknown[k] = unknown.get(k, 0) + v
        except Exception as e:  # keep going; report at the end
            print("FAILED %s: %s" % (path, e), file=sys.stderr)
    print("converted %d form(s)" % ok)
    if unknown:
        print("unmapped classes (shown as plain panels):", ", ".join("%s x%d" % kv for kv in sorted(unknown.items())))


if __name__ == "__main__":
    main()
