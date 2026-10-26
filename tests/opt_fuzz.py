#!/usr/bin/env python3
"""Differential fuzzer for the optimizer and the x64 backend.

Generates random, terminating Hylian programs (integer and float arithmetic,
control flow incl. break/continue, globals, fixed and dynamic arrays, calls with
many arguments, recursion), computes the expected output with a small Python
interpreter of its own (64-bit wrapping ints, C-style division, IEEE doubles),
and checks that the program compiled with the optimizer on AND off prints
exactly that.

    python3 tests/opt_fuzz.py [count] [first_seed]

Needs ./hylian; the stdlib runtime objects are built into a temp dir.
"""
import math, os, random, subprocess, sys, tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HYLIAN = os.path.join(ROOT, "hylian")
STD = os.path.join(ROOT, "stdlib")

M = (1 << 64) - 1
def wrap(x):
    x &= M
    return x - (1 << 64) if x >> 63 else x

def cdiv(a, b):
    q = abs(a) // abs(b)
    return wrap(-q if (a < 0) != (b < 0) else q)

def cmod(a, b):
    return wrap(a - cdiv(a, b) * b)

def ftoi(x):
    if x != x or x >= 9.2233720368547758e18 or x < -9.2233720368547758e18:
        return -(1 << 63)
    return int(x)

FCLAMP = 1.0e6
def fclamp(x):
    if x > FCLAMP: return FCLAMP
    if x < -FCLAMP: return -FCLAMP
    return x

class Env(dict):
    """locals in the dict itself; names starting with 'g' are shared globals"""
    def __init__(self, G):
        super().__init__(); self.G = G
    def __getitem__(self, k):
        return self.G[k] if k[0] == "g" else dict.__getitem__(self, k)
    def __setitem__(self, k, v):
        if k[0] == "g": self.G[k] = v
        else: dict.__setitem__(self, k, v)

# ---------------------------------------------------------------- expressions

class Ctx:
    def __init__(self, rnd, vars_, fvars, arrays, darrays, funcs, budget, structs=(), sfuncs=(), rfuncs=()):
        self.r = rnd; self.vars = vars_; self.fvars = fvars; self.arrays = arrays
        self.darrays = darrays; self.funcs = funcs; self.budget = budget
        self.structs = list(structs); self.sfuncs = list(sfuncs); self.rfuncs = list(rfuncs)

    def sub(self):
        return Ctx(self.r, self.vars, self.fvars, self.arrays, self.darrays, self.funcs, self.budget,
                   self.structs, self.sfuncs, self.rfuncs)

DIVISORS = [2, 3, 4, 5, 7, 8, 10, 16, 100, 1000, 1000003, 1 << 20, 641, -2, -3, -8, -10, 6, 9, 12, 25, 60, 255, 256, 65537]

def gen_const(r):
    k = r.random()
    if k < 0.5: return r.randint(-20, 20)
    if k < 0.75: return r.randint(-1000, 1000)
    if k < 0.9: return r.choice([255, 256, 65535, 65536, 1 << 31, (1 << 31) - 1, -(1 << 31), 1 << 40, 0x7fffffff])
    return r.randint(-(1 << 62), 1 << 62)

def lit(v):
    return str(v) if v >= 0 else "(0 - %d)" % (-v)

def flit(x):
    s = repr(float(x))
    if "e" in s or "inf" in s or "nan" in s: s = "%.6f" % x
    return s if x >= 0 else "(0.0 - %s)" % s[1:]

def gen_expr(c, d=0):
    r = c.r
    if d >= 4 or c.budget <= 0 or r.random() < 0.18 + 0.1 * d:
        if c.vars and r.random() < 0.65:
            v = r.choice(c.vars)
            return (lambda env, v=v: env[v], v)
        v = gen_const(r)
        return (lambda env, v=v: v, lit(v))
    c.budget -= 1
    k = r.random()
    if k < 0.40:
        op = r.choice(["+", "-", "*", "&", "|", "^"])
        a = gen_expr(c, d + 1); b = gen_expr(c, d + 1)
        f = {"+": lambda x, y: wrap(x + y), "-": lambda x, y: wrap(x - y), "*": lambda x, y: wrap(x * y),
             "&": lambda x, y: x & y, "|": lambda x, y: x | y, "^": lambda x, y: x ^ y}[op]
        return (lambda env, a=a, b=b, f=f: f(a[0](env), b[0](env)), "(%s %s %s)" % (a[1], op, b[1]))
    if k < 0.50:
        a = gen_expr(c, d + 1); dv = r.choice(DIVISORS)
        if r.random() < 0.5:
            return (lambda env, a=a, dv=dv: cdiv(a[0](env), dv), "(%s / %s)" % (a[1], lit(dv)))
        return (lambda env, a=a, dv=dv: cmod(a[0](env), dv), "(%s %% %s)" % (a[1], lit(dv)))
    if k < 0.57:
        a = gen_expr(c, d + 1); sh = r.randint(0, 40)
        if r.random() < 0.5:
            return (lambda env, a=a, sh=sh: wrap(a[0](env) << sh), "(%s << %d)" % (a[1], sh))
        return (lambda env, a=a, sh=sh: a[0](env) >> sh, "(%s >> %d)" % (a[1], sh))
    if k < 0.62:
        a = gen_expr(c, d + 1)
        if r.random() < 0.5:
            return (lambda env, a=a: wrap(-a[0](env)), "(0 - %s)" % a[1])
        return (lambda env, a=a: wrap(~a[0](env)), "(~%s)" % a[1])
    if k < 0.72:
        cnd = gen_cond(c, d + 1); a = gen_expr(c, d + 1); b = gen_expr(c, d + 1)
        return (lambda env, cnd=cnd, a=a, b=b: a[0](env) if cnd[0](env) else b[0](env),
                "(%s ? %s : %s)" % (cnd[1], a[1], b[1]))
    if k < 0.78 and c.arrays:
        name, n = r.choice(c.arrays)
        idx = gen_expr(c, d + 1)
        if r.random() < 0.7:
            return (lambda env, name=name, n=n, idx=idx: env[name][idx[0](env) & (n - 1)],
                    "%s[(%s) & %d]" % (name, idx[1], n - 1))
        def ev(env, name=name, n=n, idx=idx):
            i = idx[0](env)
            return env[name][i] if 0 <= i < n else 0
        return (ev, "%s[%s]" % (name, idx[1]))
    if k < 0.84 and c.darrays:
        name = r.choice(c.darrays)
        if r.random() < 0.3:
            return (lambda env, name=name: len(env[name]), "%s.len" % name)
        idx = gen_expr(c, d + 1)
        def ev(env, name=name, idx=idx):
            i = idx[0](env); a = env[name]
            return a[i] if 0 <= i < len(a) else 0
        return (ev, "%s[%s]" % (name, idx[1]))
    if k < 0.80 and c.structs:
        sname = r.choice(c.structs); fld = r.choice(["a", "b"])
        key = sname + "." + fld
        return (lambda env, key=key: env[key], key)
    if k < 0.84 and c.structs and c.sfuncs:
        fname, fpy = r.choice(c.sfuncs); sname = r.choice(c.structs)
        return (lambda env, fpy=fpy, sname=sname: fpy(env.G, env[sname + ".a"], env[sname + ".b"]),
                "%s(%s)" % (fname, sname))
    if k < 0.87:
        return gen_sized_expr(c, d)
    if k < 0.90 and c.fvars:
        f = gen_fexpr(c, d + 1)
        return (lambda env, f=f: ftoi(fclamp(f[0](env))), "cast<int>(fclamp(%s))" % f[1])
    if c.funcs:
        fname, nargs, fpy = r.choice(c.funcs)
        args = [gen_expr(c, d + 1) for _ in range(nargs)]
        return (lambda env, args=args, fpy=fpy: fpy(env.G, *[a[0](env) for a in args]),
                "%s(%s)" % (fname, ", ".join(a[1] for a in args)))
    v = gen_const(r)
    return (lambda env, v=v: v, lit(v))

U64 = (1 << 64) - 1

def gen_sized_expr(c, d):
    """unsigned 64-bit and 32/8-bit wrapping integer arithmetic, brought back to int"""
    r = c.r
    kind = r.choice(["u64", "u32", "u8"])
    a = gen_expr(c, d + 1)
    if kind == "u64":
        ct = "usize"
        ua = lambda env, a=a: a[0](env) & U64
        op = r.choice(["/", "%", ">>", "<<", "+", "*", "cmp"])
        if op in ("/", "%"):
            dv = r.choice([3, 5, 7, 10, 100, 1000003, 641, 65537, 1 << 20, 6, 9, 12, 25, 60, 255, 1 << 63, (1 << 63) + 12345])
            f = (lambda env, ua=ua, dv=dv: wrap(ua(env) // dv)) if op == "/" else (lambda env, ua=ua, dv=dv: wrap(ua(env) % dv))
            if dv >= (1 << 63):           # written as an expression: the lexer can't hold a literal this big
                dsrc = "(cast<usize>(1) << 63)" if dv == (1 << 63) else "((cast<usize>(1) << 63) + cast<usize>(%d))" % (dv - (1 << 63))
            else:
                dsrc = "cast<usize>(%d)" % dv
            return (f, "cast<int>(cast<usize>(%s) %s %s)" % (a[1], op, dsrc))
        if op in (">>", "<<"):
            sh = r.randint(0, 40)
            if op == ">>": return (lambda env, ua=ua, sh=sh: wrap(ua(env) >> sh), "cast<int>(cast<usize>(%s) >> %d)" % (a[1], sh))
            return (lambda env, ua=ua, sh=sh: wrap((ua(env) << sh) & U64), "cast<int>(cast<usize>(%s) << %d)" % (a[1], sh))
        b = gen_expr(c, d + 1)
        ub = lambda env, b=b: b[0](env) & U64
        if op == "+": return (lambda env, ua=ua, ub=ub: wrap((ua(env) + ub(env)) & U64), "cast<int>(cast<usize>(%s) + cast<usize>(%s))" % (a[1], b[1]))
        if op == "*": return (lambda env, ua=ua, ub=ub: wrap((ua(env) * ub(env)) & U64), "cast<int>(cast<usize>(%s) * cast<usize>(%s))" % (a[1], b[1]))
        cmpop = r.choice(["<", "<=", ">", ">=", "==", "!="])
        f = CMPS[cmpop]
        return (lambda env, ua=ua, ub=ub, f=f: 1 if f(ua(env), ub(env)) else 0,
                "((cast<usize>(%s) %s cast<usize>(%s)) ? 1 : 0)" % (a[1], cmpop, b[1]))
    bits = 32 if kind == "u32" else 8
    mask = (1 << bits) - 1
    ct = "uint32" if kind == "u32" else "uint8"
    ua = lambda env, a=a, mask=mask: a[0](env) & mask
    op = r.choice(["+", "-", "*", "^", "&", "|", ">>", "<<", "/", "%"])
    if op in (">>", "<<"):
        sh = r.randint(0, bits - 1)
        if op == ">>": return (lambda env, ua=ua, sh=sh: ua(env) >> sh, "cast<int>(cast<%s>(%s) >> %d)" % (ct, a[1], sh))
        return (lambda env, ua=ua, sh=sh, mask=mask: (ua(env) << sh) & mask, "cast<int>(cast<%s>(%s) << %d)" % (ct, a[1], sh))
    if op in ("/", "%"):
        dv = r.choice([3, 5, 7, 10, 100, 13, 255, 6])
        f = (lambda env, ua=ua, dv=dv: ua(env) // dv) if op == "/" else (lambda env, ua=ua, dv=dv: ua(env) % dv)
        return (f, "cast<int>(cast<%s>(%s) %s %d)" % (ct, a[1], op, dv))
    b = gen_expr(c, d + 1)
    ub = lambda env, b=b, mask=mask: b[0](env) & mask
    f = {"+": lambda x, y: x + y, "-": lambda x, y: x - y, "*": lambda x, y: x * y,
         "^": lambda x, y: x ^ y, "&": lambda x, y: x & y, "|": lambda x, y: x | y}[op]
    return (lambda env, ua=ua, ub=ub, f=f, mask=mask: f(ua(env), ub(env)) & mask,
            "cast<int>(cast<%s>(%s) %s cast<%s>(%s))" % (ct, a[1], op, ct, b[1]))

CMPS = {"<": lambda a, b: a < b, "<=": lambda a, b: a <= b, ">": lambda a, b: a > b,
        ">=": lambda a, b: a >= b, "==": lambda a, b: a == b, "!=": lambda a, b: a != b}

def gen_cond(c, d=0):
    r = c.r
    k = r.random()
    if d < 3 and k < 0.2:
        a = gen_cond(c, d + 1); b = gen_cond(c, d + 1)
        if r.random() < 0.5:
            return (lambda env, a=a, b=b: a[0](env) and b[0](env), "(%s && %s)" % (a[1], b[1]))
        return (lambda env, a=a, b=b: a[0](env) or b[0](env), "(%s || %s)" % (a[1], b[1]))
    if d < 3 and k < 0.27:
        a = gen_cond(c, d + 1)
        return (lambda env, a=a: not a[0](env), "(!%s)" % a[1])
    if c.fvars and k < 0.40:
        op = r.choice(list(CMPS))
        a = gen_fexpr(c, d + 1); b = gen_fexpr(c, d + 1)
        f = CMPS[op]
        return (lambda env, a=a, b=b, f=f: f(a[0](env), b[0](env)), "(%s %s %s)" % (a[1], op, b[1]))
    op = r.choice(list(CMPS))
    a = gen_expr(c, d + 1); b = gen_expr(c, d + 1)
    f = CMPS[op]
    return (lambda env, a=a, b=b, f=f: f(a[0](env), b[0](env)), "(%s %s %s)" % (a[1], op, b[1]))

def gen_fexpr(c, d=0):
    """float expression; results are kept small by fclamp at every assignment"""
    r = c.r
    if d >= 3 or c.budget <= 0 or r.random() < 0.25 + 0.1 * d:
        if c.fvars and r.random() < 0.7:
            v = r.choice(c.fvars)
            return (lambda env, v=v: env[v], v)
        x = r.choice([0.0, 1.0, 0.5, 2.0, 3.25, 10.0, 100.5, 0.001, 7.0, 1000.0, 12345.678])
        if r.random() < 0.3: x = -x
        return (lambda env, x=x: x, flit(x))
    c.budget -= 1
    k = r.random()
    if k < 0.55:
        op = r.choice(["+", "-", "*"])
        a = gen_fexpr(c, d + 1); b = gen_fexpr(c, d + 1)
        f = {"+": lambda x, y: x + y, "-": lambda x, y: x - y, "*": lambda x, y: x * y}[op]
        return (lambda env, a=a, b=b, f=f: f(a[0](env), b[0](env)), "(%s %s %s)" % (a[1], op, b[1]))
    if k < 0.68:
        a = gen_fexpr(c, d + 1); b = gen_fexpr(c, d + 1)
        return (lambda env, a=a, b=b: a[0](env) / (b[0](env) * b[0](env) + 1.0),
                "(%s / (%s * %s + 1.0))" % (a[1], b[1], b[1]))
    if k < 0.76:
        a = gen_fexpr(c, d + 1)
        return (lambda env, a=a: -a[0](env), "(0.0 - %s)" % a[1])
    if k < 0.88:
        e = gen_expr(c, d + 1)
        return (lambda env, e=e: float(cmod(e[0](env), 100000)), "cast<float>(%s %% 100000)" % e[1])
    cnd = gen_cond(c, d + 1); a = gen_fexpr(c, d + 1); b = gen_fexpr(c, d + 1)
    return (lambda env, cnd=cnd, a=a, b=b: a[0](env) if cnd[0](env) else b[0](env),
            "(%s ? %s : %s)" % (cnd[1], a[1], b[1]))

# ---------------------------------------------------------------- statements

class Fresh:
    def __init__(self): self.n = 0
    def name(self, p):
        self.n += 1
        return "%s%d" % (p, self.n)

BREAK, CONT = "break", "continue"

def gen_block(c, fresh, depth, count, indent, in_loop=False):
    """returns (exec_fn(env) -> None|'break'|'continue', source lines)"""
    r = c.r
    parts = []
    for _ in range(count):
        k = r.random()
        pad = "    " * indent
        if k < 0.30 or depth >= 3:
            v = r.choice(c.vars)
            e = gen_expr(c)
            if r.random() < 0.3:
                op = r.choice(["+=", "-=", "*=", "^=", "|=", "&="])
                f = {"+=": lambda x, y: wrap(x + y), "-=": lambda x, y: wrap(x - y), "*=": lambda x, y: wrap(x * y),
                     "^=": lambda x, y: x ^ y, "|=": lambda x, y: x | y, "&=": lambda x, y: x & y}[op]
                parts.append((lambda env, v=v, e=e, f=f: env.__setitem__(v, f(env[v], e[0](env))),
                              ["%s%s %s %s;" % (pad, v, op, e[1])]))
            else:
                parts.append((lambda env, v=v, e=e: env.__setitem__(v, e[0](env)),
                              ["%s%s = %s;" % (pad, v, e[1])]))
        elif k < 0.34 and c.structs:
            sname = r.choice(c.structs); fld = r.choice(["a", "b"]); e = gen_expr(c)
            key = sname + "." + fld
            parts.append((lambda env, key=key, e=e: env.__setitem__(key, e[0](env)),
                          ["%s%s.%s = %s;" % (pad, sname, fld, e[1])]))
        elif k < 0.36 and c.structs and len(c.structs) >= 1 and r.random() < 0.6:
            dst = r.choice(c.structs); src = r.choice(c.structs)
            kk = r.random()
            if kk < 0.4 or not c.rfuncs:
                def ex(env, dst=dst, src=src):
                    env[dst + ".a"] = env[src + ".a"]; env[dst + ".b"] = env[src + ".b"]
                parts.append((ex, ["%s%s = %s;" % (pad, dst, src)]))
            else:
                fname, fpy = r.choice(c.rfuncs)
                def ex(env, dst=dst, src=src, fpy=fpy):
                    a, b = fpy(env.G, env[src + ".a"], env[src + ".b"])
                    env[dst + ".a"] = a; env[dst + ".b"] = b
                parts.append((ex, ["%s%s = %s(%s);" % (pad, dst, fname, src)]))
        elif k < 0.38 and c.fvars:
            v = r.choice(c.fvars); e = gen_fexpr(c)
            parts.append((lambda env, v=v, e=e: env.__setitem__(v, fclamp(e[0](env))),
                          ["%s%s = fclamp(%s);" % (pad, v, e[1])]))
        elif k < 0.46 and c.arrays:
            name, n = r.choice(c.arrays)
            idx = gen_expr(c); e = gen_expr(c)
            def ex(env, name=name, n=n, idx=idx, e=e):
                i = idx[0](env); val = e[0](env)      # both are evaluated, index first
                if 0 <= i < n: env[name][i] = val
            parts.append((ex, ["%s%s[%s] = %s;" % (pad, name, idx[1], e[1])]))
        elif k < 0.52 and c.darrays:
            name = r.choice(c.darrays)
            kk = r.random()
            if kk < 0.6:
                e = gen_expr(c)
                parts.append((lambda env, name=name, e=e: env[name].append(e[0](env)) if len(env[name]) < 64 else None,
                              ["%sif (%s.len < 64) { %s.push(%s); }" % (pad, name, name, e[1])]))
            elif kk < 0.8:
                v = r.choice(c.vars)
                def ex(env, name=name, v=v):
                    a = env[name]
                    env[v] = a.pop() if a else 0
                parts.append((ex, ["%s%s = %s.pop();" % (pad, v, name)]))
            else:
                idx = gen_expr(c); e = gen_expr(c)
                def ex(env, name=name, idx=idx, e=e):
                    i = idx[0](env); val = e[0](env); a = env[name]
                    if 0 <= i < len(a): a[i] = val
                parts.append((ex, ["%s%s[%s] = %s;" % (pad, name, idx[1], e[1])]))
        elif k < 0.70:
            cnd = gen_cond(c)
            t = gen_block(c, fresh, depth + 1, r.randint(1, 3), indent + 1, in_loop)
            has_else = r.random() < 0.6
            e2 = gen_block(c, fresh, depth + 1, r.randint(1, 3), indent + 1, in_loop) if has_else else None
            def ex(env, cnd=cnd, t=t, e2=e2):
                if cnd[0](env): return t[0](env)
                elif e2: return e2[0](env)
            lines = ["%sif (%s) {" % (pad, cnd[1])] + t[1]
            if has_else: lines += ["%s} else {" % pad] + e2[1]
            lines += ["%s}" % pad]
            parts.append((ex, lines))
        elif in_loop and k < 0.76:
            cnd = gen_cond(c)
            kw = r.choice([BREAK, CONT])
            parts.append((lambda env, cnd=cnd, kw=kw: kw if cnd[0](env) else None,
                          ["%sif (%s) { %s; }" % (pad, cnd[1], kw)]))
        elif depth < 2:
            it = fresh.name("i"); n = r.randint(1, 9)
            body = gen_block(c.sub(), fresh, depth + 1, r.randint(1, 3), indent + 1, True)
            def loop(env, n=n, body=body):
                for _ in range(n):
                    s = body[0](env)
                    if s == BREAK: break
            if r.random() < 0.5:
                lines = ["%sfor (int %s = 0; %s < %d; %s++) {" % (pad, it, it, n, it)] + body[1] + ["%s}" % pad]
            else:
                lines = ["%sint %s = 0;" % (pad, it), "%swhile (%s < %d) {" % (pad, it, n),
                         "%s    %s += 1;" % (pad, it)] + body[1] + ["%s}" % pad]
            parts.append((loop, lines))
        else:
            v = r.choice(c.vars); e = gen_expr(c)
            parts.append((lambda env, v=v, e=e: env.__setitem__(v, e[0](env)), ["%s%s = %s;" % (pad, v, e[1])]))
    def run(env):
        for f, _ in parts:
            s = f(env)
            if s: return s
        return None
    lines = []
    for _, l in parts: lines += l
    run.parts = parts
    return (run, lines)

def rec_py(G, n, a):
    if n <= 0: return a
    return wrap(rec_py(G, n - 1, wrap(a * 3 + n)) ^ n)

def gen_program(seed, limit=None):
    r = random.Random(seed)
    fresh = Fresh()
    funcs = []
    G = {}
    src = ["include { std.io, }", ""]
    # globals
    gvars = ["g%d" % k for k in range(r.randint(0, 3))]
    for g in gvars:
        G[g] = gen_const(r); src.append("int %s = %d;" % (g, G[g]))    # static initialisers must be plain literals
    gfl = ["gw%d" % k for k in range(r.randint(0, 2))]
    for g in gfl:
        G[g] = r.choice([0.0, 1.5, -2.25, 100.0]); src.append("float %s = %r;" % (g, G[g]))
    src.append("")
    src.append("float fclamp(float x) { if (x > 1000000.0) { return 1000000.0; } if (x < 0.0 - 1000000.0) { return 0.0 - 1000000.0; } return x; }")
    src.append("int rec(int n, int a) { if (n <= 0) { return a; } return rec(n - 1, a * 3 + n) ^ n; }")
    src.append("")
    funcs.append(("rec", 2, lambda G, n, a: rec_py(G, min(max(n % 12, 0), 12) if False else n, a)))
    # rec with unbounded n could be deep: only call it with a masked n
    funcs.pop()
    rec_call = True

    for fi in range(r.randint(0, 4)):
        nargs = r.choice([1, 2, 3, 4, 6, 8, 9]) if r.random() < 0.9 else 1
        params = ["p%d" % k for k in range(nargs)]
        fvars_l = ["q0"] if r.random() < 0.5 else []
        c = Ctx(r, params + ["t"] + gvars, fvars_l + gfl, [], [], list(funcs), 14)
        body = gen_block(c, fresh, 1, r.randint(1, 3), 1)
        ret = gen_expr(c)
        name = "f%d" % fi
        def fpy(G, *args, params=params, body=body, ret=ret, fvars_l=fvars_l):
            env = Env(G)
            for p, a in zip(params, args): dict.__setitem__(env, p, a)
            dict.__setitem__(env, "t", 0)
            for fv in fvars_l: dict.__setitem__(env, fv, 0.0)
            body[0](env)
            return ret[0](env)
        src.append("int %s(%s) {" % (name, ", ".join("int " + p for p in params)))
        src.append("    int t = 0;")
        for fv in fvars_l: src.append("    float %s = 0.0;" % fv)
        src += body[1]
        src.append("    return %s;" % ret[1])
        src.append("}")
        src.append("")
        funcs.append((name, nargs, fpy))

    # a struct type, and functions taking it by value
    src.insert(2, "struct S { int a; int b; }")
    sfuncs = []
    for si in range(r.randint(0, 2)):
        c2 = Ctx(r, ["p.a", "p.b"], [], [], [], [], 8)
        e = gen_expr(c2)
        name = "sf%d" % si
        src.append("int %s(S p) { return %s; }" % (name, e[1]))
        def spy(G, a, b, e=e):
            env = Env(G); dict.__setitem__(env, "p.a", a); dict.__setitem__(env, "p.b", b)
            return e[0](env)
        sfuncs.append((name, spy))
    rfuncs = []
    for si in range(r.randint(0, 2)):
        c2 = Ctx(r, ["p.a", "p.b"], [], [], [], [], 8)
        e1 = gen_expr(c2); e2 = gen_expr(c2)
        name = "sr%d" % si
        src.append("S %s(S p) { S r; r.a = %s; r.b = %s; return r; }" % (name, e1[1], e2[1]))
        def rpy(G, a, b, e1=e1, e2=e2):
            env = Env(G); dict.__setitem__(env, "p.a", a); dict.__setitem__(env, "p.b", b)
            return e1[0](env), e2[0](env)
        rfuncs.append((name, rpy))
    varnames = ["v%d" % k for k in range(r.randint(2, 6))]
    fvarnames = ["w%d" % k for k in range(r.randint(0, 3))]
    arrays = [("ar%d" % k, 8) for k in range(r.randint(0, 2))]
    darrays = ["da%d" % k for k in range(r.randint(0, 2))]
    structs = ["st%d" % k for k in range(r.randint(0, 2))]
    c = Ctx(r, varnames + gvars, fvarnames + gfl, arrays, darrays, funcs, 40, structs, sfuncs, rfuncs)
    src.append("Error? main() {")
    env = Env(G)
    for v in varnames:
        k = gen_const(r); dict.__setitem__(env, v, k)
        src.append("    int %s = %s;" % (v, lit(k)))
    for w in fvarnames:
        x = r.choice([0.0, 1.0, -3.5, 42.0]); dict.__setitem__(env, w, x)
        src.append("    float %s = %s;" % (w, flit(x)))
    for sname in structs:
        a0, b0 = gen_const(r), gen_const(r)
        src.append("    S %s; %s.a = %s; %s.b = %s;" % (sname, sname, lit(a0), sname, lit(b0)))
        dict.__setitem__(env, sname + ".a", a0); dict.__setitem__(env, sname + ".b", b0)
    for name, n in arrays:
        src.append("    int %s[%d];" % (name, n))
        src.append("    for (int q = 0; q < %d; q++) { %s[q] = q * 3 + 1; }" % (n, name))
        dict.__setitem__(env, name, [q * 3 + 1 for q in range(n)])
    for name in darrays:
        src.append("    array<int> %s = [];" % name)
        dict.__setitem__(env, name, [])
    main_body = gen_block(c, fresh, 0, r.randint(4, 10), 1)
    if limit is not None:              # debugging aid: keep only the first `limit` statements of main
        parts = main_body[0].parts[:limit]
        src += [l for _, ls in parts for l in ls]
        for f, _ in parts: f(env)
    else:
        src += main_body[1]
        main_body[0](env)
    out = []
    for v in varnames + gvars:
        src.append("    println(%s);" % v); out.append(str(env[v]))
    for w in fvarnames + gfl:
        src.append("    println(cast<int>(%s));" % w); out.append(str(ftoi(env[w])))
    for sname in structs:
        src.append("    println(%s.a);" % sname); out.append(str(env[sname + ".a"]))
        src.append("    println(%s.b);" % sname); out.append(str(env[sname + ".b"]))
    for name, n in arrays:
        src.append("    int s_%s = 0;" % name)
        src.append("    for (int q = 0; q < %d; q++) { s_%s = s_%s * 31 + %s[q]; }" % (n, name, name, name))
        src.append("    println(s_%s);" % name)
        acc = 0
        for x in env[name]: acc = wrap(acc * 31 + x)
        out.append(str(acc))
    for name in darrays:
        src.append("    int s_%s = %s.len;" % (name, name))
        src.append("    for (int q = 0; q < %s.len; q++) { s_%s = s_%s * 31 + %s[q]; }" % (name, name, name, name))
        src.append("    println(s_%s);" % name)
        acc = len(env[name])
        for x in env[name]: acc = wrap(acc * 31 + x)
        out.append(str(acc))
    # a bounded recursive call as a fixed tail check
    src.append("    println(rec(9, %s));" % (varnames[0]))
    out.append(str(rec_py(G, 9, env[varnames[0]])))
    src.append("    return nil;")
    src.append("}")
    return "\n".join(src) + "\n", "\n".join(out) + "\n"

# ---------------------------------------------------------------- driver

def sh(cmd, env=None, timeout=60):
    e = dict(os.environ)
    if env: e.update(env)
    return subprocess.run(cmd, shell=True, capture_output=True, text=True, env=e, timeout=timeout)

def build_runtime(tmp):
    objs = []
    for m in ("mem", "runtime", "array"):
        o = os.path.join(tmp, m + ".o")
        r = sh("%s %s/%s.hy --src-dir %s -o %s" % (HYLIAN, STD, m, STD, o))
        if r.returncode != 0: sys.exit("runtime build failed: " + r.stderr)
        objs.append(o)
    return " ".join(objs)

def run_one(seed, tmp, rt):
    src, expect = gen_program(seed)
    hy = os.path.join(tmp, "f%d.hy" % seed)
    open(hy, "w").write(src)
    bad = []
    for label, env in (("opt", {}), ("noopt", {"HYLIAN_OPT": "0"})):
        o = os.path.join(tmp, "f%d_%s.o" % (seed, label)); exe = o[:-2]
        r = sh("%s %s --src-dir %s -o %s" % (HYLIAN, hy, STD, o), env)
        if r.returncode != 0:
            bad.append((label, "compile failed: " + (r.stderr or r.stdout)[-300:])); continue
        r = sh("gcc %s %s -o %s -no-pie" % (o, rt, exe))
        if r.returncode != 0:
            bad.append((label, "link failed: " + r.stderr[-300:])); continue
        try:
            r = sh(exe, timeout=20)
        except subprocess.TimeoutExpired:
            bad.append((label, "timeout")); continue
        if r.returncode != 0:
            bad.append((label, "exit code %d" % r.returncode)); continue
        if r.stdout != expect:
            got = r.stdout.split("\n"); exp = expect.split("\n")
            diff = [(i, g, e) for i, (g, e) in enumerate(zip(got, exp)) if g != e][:3]
            bad.append((label, "output differs at %s (line, got, expected)" % diff))
    return bad, hy

def main():
    count = int(sys.argv[1]) if len(sys.argv) > 1 else 200
    first = int(sys.argv[2]) if len(sys.argv) > 2 else 1
    tmp = tempfile.mkdtemp(prefix="hyfuzz_")
    rt = build_runtime(tmp)
    fails = 0
    for seed in range(first, first + count):
        bad, hy = run_one(seed, tmp, rt)
        if bad:
            fails += 1
            print("seed %d FAILED (%s)" % (seed, hy))
            for label, msg in bad: print("  [%s] %s" % (label, msg))
            if fails >= 5: break
    print("%d programs, %d failed  (tmp: %s)" % (count, fails, tmp))
    sys.exit(1 if fails else 0)

if __name__ == "__main__":
    main()
