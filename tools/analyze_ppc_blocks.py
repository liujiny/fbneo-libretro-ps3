import re, subprocess, sys

LINE = re.compile(r"^\s+[0-9a-f]+:")
MEM = re.compile(r"(lwz|lbz|lhz|lha|stw|stb|sth)\s+r[0-9]+,\s*(-?[0-9]+)\(([0-9]+)\)")
BR = re.compile(r"\b(b|bc|bne|beq|bge|blt|bgt|ble|blr)\b")

def analyse(path, cmd):
    out = subprocess.run(cmd + ["-D", "-b", "binary", "-m", "powerpc", "-EB", path],
                         capture_output=True, text=True).stdout
    lines = [l for l in out.splitlines() if LINE.match(l)]
    tot = sld = sst = mld = mst = br = 0
    for l in lines:
        tot += 1
        m = MEM.search(l)
        if m:
            op, base = m.group(1), int(m.group(3))
            ld = op.startswith("l")
            if base == 3:
                sld += ld
                sst += (not ld)
            else:
                mld += ld
                mst += (not ld)
        if BR.search(l):
            br += 1
    pct = (100 * (sld + sst)) // max(1, tot)
    print("%-10s total=%4d state_ld=%3d state_st=%3d mem_ld=%3d mem_st=%3d branch=%3d state=%d%%" %
          (path.split("/")[-1], tot, sld, sst, mld, mst, br, pct))

if __name__ == "__main__":
    cmd = sys.argv[1].split()
    for p in sys.argv[2:]:
        analyse(p, cmd)