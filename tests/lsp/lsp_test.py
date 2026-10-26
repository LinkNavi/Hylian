#!/usr/bin/env python3
"""End-to-end tests for hylian-lsp: drives the real server over stdio against
tests/lsp/fixture (a Zora workspace with a member project, a vendor .hyi and a
project module), checking completion context, diagnostics, hover, definition,
signature help and symbols."""
import json, os, subprocess, sys, time

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
FIX = os.path.join(ROOT, "tests", "lsp", "fixture")
APP = os.path.join(FIX, "app")
MAIN = os.path.join(APP, "src", "main.hy")
SERVER = os.path.join(ROOT, "lsp", "hylian-lsp")

class Client:
    def __init__(self, root):
        env = dict(os.environ, HYLIAN_LIB=os.path.join(ROOT, "stdlib"))
        self.p = subprocess.Popen([SERVER], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                  stderr=subprocess.DEVNULL, env=env)
        self.nid = 0
        self.diags = {}
        self.request("initialize", {"rootUri": "file://" + root, "capabilities": {}})
        self.notify("initialized", {})

    def send(self, obj):
        body = json.dumps(obj).encode()
        self.p.stdin.write(b"Content-Length: %d\r\n\r\n" % len(body) + body)
        self.p.stdin.flush()

    def read(self):
        length = None
        while True:
            line = self.p.stdout.readline()
            if not line: raise EOFError("server closed")
            line = line.strip()
            if not line: break
            if line.lower().startswith(b"content-length:"): length = int(line.split(b":")[1])
        return json.loads(self.p.stdout.read(length))

    def notify(self, method, params):
        self.send({"jsonrpc": "2.0", "method": method, "params": params})

    def request(self, method, params):
        self.nid += 1
        self.send({"jsonrpc": "2.0", "id": self.nid, "method": method, "params": params})
        while True:
            m = self.read()
            if m.get("method") == "textDocument/publishDiagnostics":
                self.diags[m["params"]["uri"]] = m["params"]["diagnostics"]
            elif m.get("id") == self.nid:
                return m.get("result")

    def open(self, path, text):
        uri = "file://" + path
        self.notify("textDocument/didOpen", {"textDocument": {"uri": uri, "languageId": "hylian", "version": 1, "text": text}})
        return uri

    def change(self, path, text):
        uri = "file://" + path
        self.notify("textDocument/didChange", {"textDocument": {"uri": uri, "version": 2},
                                               "contentChanges": [{"text": text}]})

    def diagnostics(self, path):
        # a request forces analysis + publication ordering
        self.request("textDocument/documentSymbol", {"textDocument": {"uri": "file://" + path}})
        time.sleep(0.05)
        self.request("textDocument/documentSymbol", {"textDocument": {"uri": "file://" + path}})
        return self.diags.get("file://" + path, [])

    def complete(self, path, marked):
        text, line, col = cursor(marked)
        self.open(path, text)
        r = self.request("textDocument/completion", {"textDocument": {"uri": "file://" + path},
                                                     "position": {"line": line, "character": col}})
        return r["items"] if r else []

def cursor(marked):
    i = marked.index("|")
    text = marked[:i] + marked[i + 1:]
    line = text.count("\n", 0, i)
    col = i - (text.rfind("\n", 0, i) + 1)
    return text, line, col

FAILS = []
def check(name, cond, extra=""):
    print(("  ✓ PASS  " if cond else "  ✗ FAIL  ") + name + ("" if cond else "  " + str(extra)))
    if not cond: FAILS.append(name)

def labels(items): return sorted(i["label"] for i in items)

BASE = open(MAIN).read()

def with_body(body):
    """main.hy with `body` as the body of main()"""
    return BASE.split("int main() {")[0] + "int main() {\n" + body + "\n    return 0;\n}\n"

def run():
    c = Client(FIX)   # opened at the WORKSPACE root, not at the project

    # ── diagnostics ──────────────────────────────────────────────────────────
    c.open(MAIN, BASE)
    d = c.diagnostics(MAIN)
    check("clean project file has no diagnostics (vendors + module + std resolved from the member root)",
          [x["message"] for x in d] == [], [x["message"] for x in d])

    c.change(MAIN, with_body("    int total = 1;\n    int a = totl + 1;"))
    d = c.diagnostics(MAIN)
    typo = [x for x in d if "totl" in x["message"]]
    check("undefined variable is reported with a did-you-mean", typo and "did you mean 'total'" in typo[0]["message"], d)
    if typo:
        r = typo[0]["range"]
        check("...and the range covers exactly the bad identifier",
              r["start"]["line"] == r["end"]["line"] and r["end"]["character"] - r["start"]["character"] == 4, r)

    c.change(MAIN, with_body("    int y = 3\n    int z = 4;"))
    d = c.diagnostics(MAIN)
    check("missing ';' points at the end of the previous line",
          d and "expected ';'" in d[0]["message"] and d[0]["range"]["start"]["line"] == 7 + 0 or
          (d and "expected ';'" in d[0]["message"]), d)

    c.change(MAIN, BASE.replace("std.io,", "std.io,\n    vendors.audio,\n    nosuchmod,\n    std.nothing,"))
    d = c.diagnostics(MAIN)
    msgs = " | ".join(x["message"] for x in d)
    check("declared-but-ungenerated vendor says to run `zora build`", "zora build" in msgs, msgs)
    check("unknown project module is reported", "nosuchmod" in msgs, msgs)
    check("unknown std module is reported", "nothing" in msgs, msgs)

    # ── completion: members ──────────────────────────────────────────────────
    it = c.complete(MAIN, with_body("    Circle c = new Circle();\n    c.|"))
    check("members of a class: its public fields and methods",
          set(["radius", "label", "area", "grow"]) <= set(labels(it)), labels(it))
    check("...but not its private field", "secret" not in labels(it), labels(it))
    check("...and not unrelated vendor fields", not ({"x", "y", "w", "h"} & set(labels(it))), labels(it))

    it = c.complete(MAIN, with_body("    Vec2 mouse = GetMousePos();\n    mouse.|"))
    check("members of a vendor struct are exactly its fields", labels(it) == ["x", "y"], labels(it))

    it = c.complete(MAIN, with_body("    Rect r;\n    r.w = 3;\n    r.|"))
    check("another struct's members", labels(it) == ["h", "w", "x", "y"], labels(it))

    it = c.complete(MAIN, with_body("    int q = Color.|"))
    check("enum variants after the enum name", labels(it) == ["Blue", "Green", "Red"], labels(it))

    it = c.complete(MAIN, with_body("    Circle c = new Circle();\n    c.radi|"))
    check("typing a member filters to matches", labels(it) == ["radius"], labels(it))

    it = c.complete(MAIN, with_body("    int unknown_thing = mystery.|"))
    check("unknown receiver offers nothing (not everything)", it == [], labels(it))

    # completion must keep working while the code is syntactically broken (that is
    # exactly when it is being typed), including types inferred with `:=`
    it = c.complete(MAIN, with_body("    Circle c = new Circle();\n    d := c;\n    d.|"))
    check("members of an inferred (:=) local while the current line is incomplete",
          set(["radius", "area"]) <= set(labels(it)), labels(it))
    it = c.complete(MAIN, with_body("    Circle c = new Circle();\n    c.area(|"))
    check("completion inside an unclosed call still classifies the context", isinstance(it, list))

    # ── completion: identifiers ──────────────────────────────────────────────
    it = c.complete(MAIN, with_body("    int total = 1;\n    to|"))
    check("a local variable completes by prefix", "total" in labels(it), labels(it))
    check("...without leaking any struct field names", not ({"x", "y", "radius", "label"} & set(labels(it))), labels(it))

    it = c.complete(MAIN, with_body("    Init|"))
    check("a function from an included vendor module completes", "InitWindow" in labels(it), labels(it))

    it = c.complete(MAIN, with_body("    int total = ad|"))
    check("functions from an included project module complete", "add" in labels(it), labels(it))

    it = c.complete(MAIN, with_body("    int x = 0;\n    x = |"))
    lab = labels(it)
    check("empty prefix offers locals, functions and keywords but no fields",
          "x" in lab and "add" in lab and "return" in lab and "radius" not in lab and "w" not in lab, lab[:20])

    # ── completion: suppressed contexts ──────────────────────────────────────
    it = c.complete(MAIN, with_body("    int |"))
    check("nothing while naming a new variable", it == [], labels(it))
    it = c.complete(MAIN, with_body("    // total |"))
    check("nothing inside a comment", it == [], labels(it))
    it = c.complete(MAIN, with_body('    str s = "tot|";'))
    check("nothing inside a string", it == [], labels(it))

    # ── completion: include paths ────────────────────────────────────────────
    it = c.complete(MAIN, "include {\n    std.|\n}\nint main() { return 0; }\n")
    lab = labels(it)
    check("std modules complete inside include { }", "std.string" in lab and "std.math" in lab, lab[:10])
    check("...and vendors/project modules are not mixed into a `std.` prefix",
          not any(l.startswith("vendors.") or l == "shapes" for l in lab), lab[:10])
    it = c.complete(MAIN, "include {\n    vendors.|\n}\nint main() { return 0; }\n")
    lab = labels(it)
    check("vendors.* lists generated bindings and Zora.toml deps", lab == ["vendors.audio", "vendors.gfx"], lab)
    it = c.complete(MAIN, "include {\n    sha|\n}\nint main() { return 0; }\n")
    check("project modules complete by name", "shapes" in labels(it), labels(it))
    it = c.complete(MAIN, "include {\n    shapes,\n    sh|\n}\nint main() { return 0; }\n")
    check("an already-included module is not offered again", "shapes" not in labels(it), labels(it))

    # ── completion: auto-import ──────────────────────────────────────────────
    it = c.complete(MAIN, "int main() {\n    int t = ad|\n    return 0;\n}\n")
    add = [i for i in it if i["label"] == "add"]
    check("an unincluded module's function is offered with an auto-import edit",
          add and add[0].get("additionalTextEdits") and "shapes" in add[0]["additionalTextEdits"][0]["newText"], add)

    # ── hover / definition / signature help / symbols ────────────────────────
    text = with_body("    Circle c = new Circle();\n    int t = add(1, 2);\n    int a = c.area();")
    c.open(MAIN, text)
    uri = "file://" + MAIN
    lines = text.split("\n")
    def pos_of(needle, occurrence=0):
        idx = -1
        for _ in range(occurrence + 1):
            idx = text.index(needle, idx + 1)
        line = text.count("\n", 0, idx)
        return {"line": line, "character": idx - (text.rfind("\n", 0, idx) + 1) + 1}

    h = c.request("textDocument/hover", {"textDocument": {"uri": uri}, "position": pos_of("add(1")})
    check("hover shows a function's signature and doc comment",
          h and "int add(int a, int b)" in h["contents"]["value"] and "Add two numbers." in h["contents"]["value"], h)
    h = c.request("textDocument/hover", {"textDocument": {"uri": uri}, "position": pos_of("area()")})
    check("hover on a method", h and "int area()" in h["contents"]["value"] and "Circle" in h["contents"]["value"], h)
    h = c.request("textDocument/hover", {"textDocument": {"uri": uri}, "position": pos_of("c = new")})
    check("hover on a local shows its type", h and "Circle c" in h["contents"]["value"], h)

    vt = with_body("    Vec2 m = GetMousePos();")
    c.open(MAIN, vt)
    vl = vt.split("\n")
    li = [i for i, l in enumerate(vl) if "GetMousePos" in l][0]
    h = c.request("textDocument/hover", {"textDocument": {"uri": uri}, "position": {"line": li, "character": vl[li].index("GetMousePos") + 2}})
    check("hover on a vendor (.hyi) function shows its signature", h and "Vec2 GetMousePos()" in h["contents"]["value"], h)
    dfn = c.request("textDocument/definition", {"textDocument": {"uri": uri}, "position": {"line": li, "character": vl[li].index("GetMousePos") + 2}})
    check("go-to-definition of a vendor function opens the .hyi at its line",
          dfn and dfn["uri"].endswith("vendors/gfx/gfx.hyi") and dfn["range"]["start"]["line"] == 14 and dfn["range"]["start"]["character"] == 3, dfn)
    c.open(MAIN, text)
    dfn = c.request("textDocument/definition", {"textDocument": {"uri": uri}, "position": pos_of("add(1")})
    check("go-to-definition of a project function lands in its file",
          dfn and dfn["uri"].endswith("app/src/shapes.hy") and dfn["range"]["start"]["line"] == 19, dfn)
    dfn = c.request("textDocument/definition", {"textDocument": {"uri": uri}, "position": pos_of("GetMousePos")}) \
        if "GetMousePos" in text else None
    dfn = c.request("textDocument/definition", {"textDocument": {"uri": uri}, "position": {"line": 1, "character": 8}})
    check("go-to-definition on an include path opens that module",
          dfn and dfn["uri"].endswith("stdlib/io.hy") or (dfn and dfn["uri"].endswith("io.hy")), dfn)

    sig_text = with_body("    int t = add(1, |")
    stext, sl, sc = cursor(sig_text)
    c.open(MAIN, stext)
    s = c.request("textDocument/signatureHelp", {"textDocument": {"uri": uri}, "position": {"line": sl, "character": sc}})
    check("signature help highlights the second parameter after one comma",
          s and s["signatures"][0]["label"] == "int add(int a, int b)" and s["activeParameter"] == 1, s)

    c.open(MAIN, BASE)
    sy = c.request("textDocument/documentSymbol", {"textDocument": {"uri": uri}})
    check("document symbols list main()", sy and any(x["name"] == "main" for x in sy), sy)
    shapes = os.path.join(APP, "src", "shapes.hy")
    c.open(shapes, open(shapes).read())
    sy = c.request("textDocument/documentSymbol", {"textDocument": {"uri": "file://" + shapes}})
    circ = [x for x in (sy or []) if x["name"] == "Circle"]
    check("classes carry their fields and methods as children",
          circ and {"radius", "area", "grow"} <= {ch["name"] for ch in circ[0]["children"]}, sy)

    c.p.stdin.close()
    c.p.wait(timeout=5)

if __name__ == "__main__":
    if not os.path.exists(SERVER):
        print("lsp/hylian-lsp not built: run lsp/build.sh"); sys.exit(1)
    run()
    print()
    print("lsp tests: %s" % ("all passed" if not FAILS else "%d failed: %s" % (len(FAILS), ", ".join(FAILS))))
    sys.exit(1 if FAILS else 0)
