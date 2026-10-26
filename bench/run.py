import subprocess, time, statistics, os, sys
B = ["fib","loop","sieve","mandel","collatz","matmul"]
RT = "mem_rt.o runtime_rt.o array_rt.o"
os.makedirs("bin", exist_ok=True)
# Prefer the repo's own compiler and stdlib over an installed (possibly stale) copy
_repo = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HYLIAN = os.path.join(_repo, "hylian") if os.path.exists(os.path.join(_repo, "hylian")) else "hylian"
STD = next((d for d in [os.path.join(_repo, "stdlib"), "/usr/local/lib/hylian/std", "../stdlib", "../Hylian/stdlib"] if os.path.exists(d + "/mem.hy")), None)
for s_ in ("mem", "runtime", "array"):
    if not os.path.exists(f"{s_}_rt.o"):
        r = subprocess.run(f"{HYLIAN} {STD}/{s_}.hy --src-dir {STD} -o {s_}_rt.o", shell=True, capture_output=True, text=True)
        if r.returncode != 0: sys.exit("failed to build " + s_ + "_rt.o: " + r.stderr)
def sh(c):
    return subprocess.run(c, shell=True, capture_output=True, text=True)
build = {
 "hylian": lambda b: f"{HYLIAN} src/{b}.hy --src-dir {STD} -o bin/{b}_hy.o && gcc bin/{b}_hy.o {RT} -o bin/{b}_hy -no-pie",
 "c":      lambda b: f"gcc -O2 src/{b}.c -o bin/{b}_c",
 "c++":    lambda b: f"g++ -O2 src/{b}.cpp -o bin/{b}_cpp",
 "rust":   lambda b: f"rustc -O src/{b}.rs -o bin/{b}_rs 2>/dev/null",
 "go":     lambda b: f"go build -o bin/{b}_go src/go/{b}.go",
}
runcmd = {
 "hylian": lambda b: f"bin/{b}_hy", "c": lambda b: f"bin/{b}_c", "c++": lambda b: f"bin/{b}_cpp",
 "rust": lambda b: f"bin/{b}_rs", "go": lambda b: f"bin/{b}_go", "python": lambda b: f"python3 src/{b}.py",
}
langs = ["hylian","c","c++","rust","go","python"]
RUNS = 5
res = {}; outs = {}
for b in B:
    for l in langs:
        if l in build:
            r = sh(build[l](b))
            if r.returncode != 0:
                res[(b,l)] = None; outs[(b,l)] = "BUILD FAIL: " + (r.stderr or r.stdout)[:150]; continue
        ts=[]; o=None
        for _ in range(RUNS):
            t=time.perf_counter(); r=sh(runcmd[l](b)); ts.append(time.perf_counter()-t)
            if r.returncode != 0: o=f"RUN FAIL rc={r.returncode}"; break
            o=r.stdout.strip()
            if ts[-1] > 3: break
        res[(b,l)] = None if (o or "").startswith("RUN FAIL") else statistics.median(ts)
        outs[(b,l)] = o
    print(b, "done", file=sys.stderr)
print("\nMedian wall time (s), %d runs\n" % RUNS)
print("%-9s" % "bench" + "".join("%10s" % l for l in langs))
for b in B:
    print("%-9s" % b + "".join(("%10.3f" % res[(b,l)]) if res[(b,l)] is not None else "%10s" % "FAIL" for l in langs))
print("\nSlowdown vs C (x)\n")
print("%-9s" % "bench" + "".join("%10s" % l for l in langs))
for b in B:
    c = res[(b,"c")]
    print("%-9s" % b + "".join(("%10.1f" % (res[(b,l)]/c)) if res[(b,l)] is not None and c else "%10s" % "-" for l in langs))
print("\nOutput check (checksum per language)\n")
for b in B:
    vals = {l: outs[(b,l)] for l in langs}
    ok = len(set(vals.values())) == 1
    print(b, "OK" if ok else "MISMATCH", vals["c"] if ok else vals)
