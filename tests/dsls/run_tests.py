#!/usr/bin/env python3
# Tests for dsls, the DScript language server: talks to bin/dsls over stdio like an editor,
# on the small project in tests/dsls/project. Run from anywhere: tests/dsls/run_tests.py
import json
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
PROJECT = os.path.join(HERE, "project")
MAIN = os.path.join(PROJECT, "main.ds")
SHAPES = os.path.join(PROJECT, "lib", "shapes.ds")


def uri(path):
    return "file://" + path


class Client:
    def __init__(self):
        self.proc = subprocess.Popen([os.path.join(ROOT, "bin", "dsls")], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                     stderr=subprocess.PIPE, cwd=PROJECT)
        self.next_id = 1
        self.diagnostics = {}   # uri -> the last list published

    def send(self, msg):
        body = json.dumps(msg).encode()
        self.proc.stdin.write(b"Content-Length: %d\r\n\r\n" % len(body) + body)
        self.proc.stdin.flush()

    def read(self):
        length = 0
        while True:
            line = self.proc.stdout.readline()
            if not line:
                raise RuntimeError("dsls closed the connection; stderr:\n" + self.proc.stderr.read().decode())
            line = line.strip()
            if not line:
                break
            if line.lower().startswith(b"content-length:"):
                length = int(line.split(b":")[1])
        msg = json.loads(self.proc.stdout.read(length))
        if msg.get("method") == "textDocument/publishDiagnostics":
            self.diagnostics[msg["params"]["uri"]] = msg["params"]["diagnostics"]
        return msg

    def request(self, method, params):
        rid = self.next_id
        self.next_id += 1
        self.send({"jsonrpc": "2.0", "id": rid, "method": method, "params": params})
        while True:
            msg = self.read()
            if msg.get("id") == rid:
                if "error" in msg:
                    raise RuntimeError(method + ": " + json.dumps(msg["error"]))
                return msg["result"]

    def notify(self, method, params):
        self.send({"jsonrpc": "2.0", "method": method, "params": params})

    def sync(self):
        # A request answers after every notification before it was handled (and the analyses
        # they started were published).
        self.request("workspace/symbol", {"query": "\x00"})


passed = 0
failed = 0


def check(name, ok, detail=""):
    global passed, failed
    if ok:
        passed += 1
        print("  ok    " + name)
    else:
        failed += 1
        print("  FAIL  " + name + (": " + detail if detail else ""))


def position(path, text, line, needle, nth=0, offset=0):
    # The LSP position of the nth `needle` on 0-based `line` of `text`.
    l = text.split("\n")[line]
    i = -1
    for _ in range(nth + 1):
        i = l.index(needle, i + 1)
    return {"textDocument": {"uri": uri(path)}, "position": {"line": line, "character": i + offset}}


def definition_is(c, name, path, text, line, needle, target, tline, tneedle, nth=0):
    r = c.request("textDocument/definition", position(path, text, line, needle, nth))
    if r is None:
        check(name, False, "no definition")
        return
    tl = open(target).read().split("\n")[tline]
    want = tl.index(tneedle) if tneedle else 0
    got = (r["uri"], r["range"]["start"]["line"], r["range"]["start"]["character"])
    check(name, got == (uri(target), tline, want), "got %s, expected %s" % (got, (uri(target), tline, want)))


def hover_has(c, name, path, text, line, needle, expected, nth=0):
    r = c.request("textDocument/hover", position(path, text, line, needle, nth))
    value = r["contents"]["value"] if r else ""
    check(name, expected in value, "hover was: " + repr(value))


main_text = open(MAIN).read()
shapes_text = open(SHAPES).read()
c = Client()
c.request("initialize", {"processId": None, "rootUri": uri(PROJECT), "capabilities": {}})
c.notify("initialized", {})
c.notify("textDocument/didOpen", {"textDocument": {"uri": uri(MAIN), "languageId": "dscript", "version": 1, "text": main_text}})
c.sync()

print("diagnostics")
check("no errors in main.ds", c.diagnostics.get(uri(MAIN)) == [], str(c.diagnostics.get(uri(MAIN))))
check("no errors in the included file", c.diagnostics.get(uri(SHAPES)) == [], str(c.diagnostics.get(uri(SHAPES))))

print("definition")
definition_is(c, "struct type", MAIN, main_text, 4, "Point", SHAPES, 2, "Point")
definition_is(c, "field through ->", MAIN, main_text, 8, "y;", SHAPES, 4, "y")
definition_is(c, "field through .", MAIN, main_text, 18, "x", SHAPES, 3, "x")
definition_is(c, "sub", MAIN, main_text, 13, "area", SHAPES, 13, "area")
definition_is(c, "enum", MAIN, main_text, 13, "Color", SHAPES, 8, "Color")
definition_is(c, "enum member", MAIN, main_text, 13, "Blue", SHAPES, 8, "Blue")
definition_is(c, "integer constant", MAIN, main_text, 13, "ORIGIN_X", SHAPES, 10, "ORIGIN_X")
definition_is(c, "string constant", MAIN, main_text, 13, "GREETING", SHAPES, 11, "GREETING")
definition_is(c, "local constant", MAIN, main_text, 13, "units", MAIN, 5, "units")
definition_is(c, "static variable", MAIN, main_text, 7, "calls", MAIN, 6, "calls")
definition_is(c, "local variable", MAIN, main_text, 13, "total", MAIN, 8, "total")
definition_is(c, "parameter", MAIN, main_text, 8, "p", MAIN, 4, "p:")
definition_is(c, "$name in a low block", MAIN, main_text, 10, "total", MAIN, 8, "total")
definition_is(c, "global", MAIN, main_text, 20, "g_count", MAIN, 2, "g_count")
definition_is(c, "#include", MAIN, main_text, 0, "shapes", SHAPES, 0, "")
definition_is(c, "method call", MAIN, main_text, 19, "sum", SHAPES, 5, "sum")
definition_is(c, "this in a method", SHAPES, shapes_text, 5, "this", SHAPES, 5, "sum")
r = c.request("textDocument/definition", position(MAIN, main_text, 13, "ORIGIN_X", 0, 4))
check("position inside a name", r is not None and r["range"]["start"]["line"] == 10, str(r))
r = c.request("textDocument/definition", position(MAIN, main_text, 13, "+"))
check("nothing on an operator", r is None, str(r))

print("hover")
hover_has(c, "sub signature", MAIN, main_text, 13, "area", "sub area(var w: i32, var h: i32 = 2) -> i32")
hover_has(c, "method signature", MAIN, main_text, 19, "sum", "sub Point.sum(var extra: i32) -> i32  // method of Point")
geo_line = shapes_text.split("\n").index("struct Geo {")
definition_is(c, "static sub call", MAIN, main_text, 19, "twice", SHAPES, geo_line + 1, "twice")
definition_is(c, "the struct of a static sub call", MAIN, main_text, 19, "Geo", SHAPES, geo_line, "Geo")
hover_has(c, "static sub signature", MAIN, main_text, 19, "twice", "static sub Geo.twice(var v: i32) -> i32  // static sub of Geo")
hover_has(c, "a struct lists its subs", MAIN, main_text, 4, "Point", "\tsub sum(var extra: i32) -> i32;")
hover_has(c, "priv field", SHAPES, shapes_text, geo_line + 2, "cache", "priv var cache: i32  // field of Geo")
hover_has(c, "priv sub", SHAPES, shapes_text, geo_line + 3, "reset", "priv sub Geo.reset()  // method of Geo")
level_line = shapes_text.split("\n").index("enum Level: u8 { Low = 1, High };")
hover_has(c, "typed enum", SHAPES, shapes_text, level_line, "Level", "enum Level: u8 {\n\tLow = 1,\n\tHigh = 2\n}")
hover_has(c, "struct layout", MAIN, main_text, 4, "Point", "struct Point {  // 8 bytes")
hover_has(c, "field", MAIN, main_text, 8, "y;", "var y: i32  // field of Point at +4")
hover_has(c, "constant value", MAIN, main_text, 13, "ORIGIN_X", "const ORIGIN_X: i32 = 16 (0x10)")
hover_has(c, "string constant", MAIN, main_text, 13, "GREETING", 'const GREETING = "hello"')
hover_has(c, "enum member", MAIN, main_text, 13, "Blue", "Color.Blue = 6")
hover_has(c, "static", MAIN, main_text, 7, "calls", "static var calls: u32  // static, in sub describe")
hover_has(c, "local constant", MAIN, main_text, 13, "units", "const units:")
hover_has(c, "local", MAIN, main_text, 13, "total", "var total: i32  // local")
hover_has(c, "parameter", MAIN, main_text, 8, "p", "var p: Point*  // parameter")
hover_has(c, "builtin type", MAIN, main_text, 2, "u32", "builtin type")
hover_has(c, "where it is declared", MAIN, main_text, 13, "area", "lib/shapes.ds:14")

print("an included file (analyzed through main.ds)")
c.notify("textDocument/didOpen", {"textDocument": {"uri": uri(SHAPES), "languageId": "dscript", "version": 1, "text": shapes_text}})
hover_has(c, "parameter in the included file", SHAPES, shapes_text, 14, "w", "var w: i32  // parameter")
syms = c.request("textDocument/documentSymbol", {"textDocument": {"uri": uri(SHAPES)}})
names = sorted(s["name"] for s in syms)
check("document symbols", names == sorted(["Point", "x", "y", "sum", "Color", "Red", "Green", "Blue", "ORIGIN_X", "GREETING", "area", "Geo", "twice", "cache", "reset", "Level", "Low", "High"]), str(names))
check("a method's container is its struct", any(s["name"] == "sum" and s.get("containerName") == "Point" and s["kind"] == 6 for s in syms), str(syms))
syms = c.request("workspace/symbol", {"query": "are"})
check("workspace symbols", [s["name"] for s in syms] == ["area"], str(syms))

print("editing (unsaved buffers)")
broken = main_text.replace("p->x + p->y", "p->x + p->zz")
c.notify("textDocument/didChange", {"textDocument": {"uri": uri(MAIN), "version": 2}, "contentChanges": [{"text": broken}]})
c.sync()
diags = c.diagnostics.get(uri(MAIN), [])
check("error as you type", len(diags) == 1 and "has no field 'zz'" in diags[0]["message"] and diags[0]["range"]["start"]["line"] == 8, str(diags))
if diags:
    start = diags[0]["range"]["start"]["character"]
    check("error range is the name", broken.split("\n")[8][start:diags[0]["range"]["end"]["character"]] == "zz", str(diags[0]["range"]))
definition_is(c, "the other subs still work", MAIN, broken, 18, "pt", MAIN, 17, "pt")
two_errors = broken.replace("pt.x = 1;", "pt.x = undefined_name;")
c.notify("textDocument/didChange", {"textDocument": {"uri": uri(MAIN), "version": 3}, "contentChanges": [{"text": two_errors}]})
c.sync()
check("an error per sub", len(c.diagnostics.get(uri(MAIN), [])) == 2, str(c.diagnostics.get(uri(MAIN))))
new_shapes = shapes_text.replace("\tvar y: i32 = 7;\n", "\tvar y: i32 = 7;\n\tvar zz: i32;\n")
c.notify("textDocument/didChange", {"textDocument": {"uri": uri(SHAPES), "version": 2}, "contentChanges": [{"text": new_shapes}]})
c.notify("textDocument/didChange", {"textDocument": {"uri": uri(MAIN), "version": 4}, "contentChanges": [{"text": broken}]})
c.sync()
check("an unsaved change in an included file is used", c.diagnostics.get(uri(MAIN)) == [], str(c.diagnostics.get(uri(MAIN))))
c.notify("textDocument/didChange", {"textDocument": {"uri": uri(MAIN), "version": 5}, "contentChanges": [{"text": "sub main() { var x: i32 = ; }"}]})
c.sync()
diags = c.diagnostics.get(uri(MAIN), [])
check("syntax error", len(diags) == 1 and diags[0]["range"]["start"]["line"] == 0, str(diags))

c.request("shutdown", None)
c.notify("exit", None)
code = c.proc.wait(timeout=10)
check("shutdown and exit", code == 0, "exit code %d" % code)
print("%d passed, %d failed" % (passed, failed))
sys.exit(1 if failed else 0)
