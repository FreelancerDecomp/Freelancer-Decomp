# Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
# faithful to the behaviour of the original game.
# This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
# developed by Digital Anvil.
# Author: Giperion.

import struct, re, collections

path = r"D:\Freelancer\EXE\Common.dll"
data = open(path, "rb").read()

e_lfanew = struct.unpack_from("<I", data, 0x3C)[0]
coff_off = e_lfanew + 4
machine, nsec, _ts, _sym, _nsym, opthdr_size, _char = struct.unpack_from("<HHIIIHH", data, coff_off)
opt_off = coff_off + 20
exp_rva, exp_size = struct.unpack_from("<II", data, opt_off + 96)

sec_off = opt_off + opthdr_size
sections = []
for i in range(nsec):
    off = sec_off + i * 40
    name, vsize, vaddr, rawsize, rawptr = struct.unpack_from("<8sIIII", data, off)
    sections.append((name.rstrip(b"\x00").decode(), vaddr, vsize, rawptr, rawsize))
print("SECTIONS", sections)

def rva_to_off(rva):
    for _n, va, vs, rp, rs in sections:
        if va <= rva < va + max(vs, rs):
            return rp + (rva - va)
    raise ValueError("bad RVA " + hex(rva))

e = rva_to_off(exp_rva)
(_ch, _ts, _maj, _min, _dllname_rva, base, nf, nn,
 a_funcs, a_names, a_ords) = struct.unpack_from("<IIHHIIIIIII", data, e)

exports = []
for i in range(nn):
    name_rva = struct.unpack_from("<I", data, rva_to_off(a_names) + i * 4)[0]
    noff = rva_to_off(name_rva)
    name = data[noff:data.index(b"\x00", noff)].decode("ascii", "replace")
    ord_idx = struct.unpack_from("<H", data, rva_to_off(a_ords) + i * 2)[0]
    func_rva = struct.unpack_from("<I", data, rva_to_off(a_funcs) + ord_idx * 4)[0]
    exports.append((base + ord_idx, func_rva, name))

# --- parse Common.map publics ---
map_by_rva = {}
seg_re = re.compile(r"^\s+([0-9A-F]{4}):([0-9A-F]{8})\s{2,}(\S.*)$")
sect_va = {}  # map segment number -> guessed VA, resolved empirically below
for line in open(r"D:\Freelancer\EXE\Common.map", encoding="utf-8", errors="replace"):
    m = seg_re.match(line)
    if not m:
        continue
    seg, off_s, sym = m.groups()
    sym = sym.strip()
    if sym.startswith("?"):  # SEH/mangled duplicates
        continue
    seg = int(seg, 16)
    off = int(off_s, 16)
    map_by_rva.setdefault((seg, off), sym)

# empirical: which segment holds code? try 1 and 2 against .text VA
text = next(s for s in sections if s[0] == ".text")
text_va = text[1]
rdata = next((s for s in sections if s[0] == ".rdata"), None)
data_s = next((s for s in sections if s[0] == ".data"), None)

def seg_va(seg):
    if seg in (1, 2):
        return text_va
    if seg == 3:
        return rdata[1] if rdata else None
    if seg == 4:
        return data_s[1] if data_s else None
    return None

rows = []
matched = 0
for ordinal, rva, mangled in exports:
    dem = None
    tries = ((1, text_va),)
    if rdata:
        tries += ((2, rdata[1]),)
    if data_s:
        tries += ((3, data_s[1]),)
    for seg, va in tries:
        dem = map_by_rva.get((seg, rva - va))
        if dem:
            break
    if dem:
        matched += 1
    rows.append((ordinal, rva, mangled, dem or "(no-map-entry)"))

print("EXPORTS", len(exports), "MAP_MATCHED", matched)

def classify(dem, mangled):
    # PRIMARY: classify from the mangled name (authoritative per-export).
    # The map reverse-lookup is unreliable for ICF-folded exports that share
    # one RVA. Templates are grouped by known container substrings.
    member = None
    key = None
    for pat, tkey in (("?$vector@", "std::vector"), ("?$list@", "std::list"),
                      ("?$map@", "std::map"), ("?$MetaList@", "MetaList"),
                      ("?$basic_string@", "std::basic_string"),
                      ("?$weighted_vector@", "weighted_vector"),
                      ("?$OwnerList@", "OwnerList")):
        if pat in mangled:
            key = tkey
            break
    if key is None and "?$" in mangled:
        key = "(template)"
    rest = mangled
    if rest.startswith("?"):
        rest = rest[1:]
        if rest.startswith("?"):
            kind = rest[1]
            if kind == "0":
                member = "ctor"
            elif kind == "1":
                member = "dtor"
            elif kind == "_":
                member = "vtable_or_meta"
            else:
                member = "operator_" + kind
            rest = rest[2:]
    if "@@" in rest:
        qual_part = rest.split("@@", 1)[0]
    else:
        qual_part = rest
    toks = [x for x in qual_part.split("@") if x != ""]
    if member is None:
        if toks:
            member = toks[0]
            quals = toks[1:]
        else:
            member = mangled
            quals = []
    else:
        quals = toks
    if key is None:
        if not quals:
            key = "(global)"
        elif quals[-1] == "pub" and len(quals) >= 2:
            key = "pub::" + quals[-2]
        else:
            key = quals[-1]
    # demangled display: map name only when the RVA is unique among exports
    dem_display = dem if dem and not dem.startswith("(") else mangled
    return (key, member, dem_display)

rva_counts = collections.Counter(rva for _o, rva, _m in exports)

by_class = collections.defaultdict(list)
for ordinal, rva, mangled, dem in rows:
    key, member, full = classify(dem, mangled)
    if rva_counts[rva] > 1:
        full = "[ICF-folded] " + (mangled if full == mangled else full)
    by_class[key].append((ordinal, rva, member, full, mangled))

top = sorted(by_class.items(), key=lambda kv: -len(kv[1]))
print("UNIQUE_CLASS_KEYS", len(by_class))
for key, syms in top[:50]:
    print("%5d  %s" % (len(syms), key))
hist = collections.Counter()
for _k, syms in by_class.items():
    n = len(syms)
    if n == 1: hist["1"] += 1
    elif n <= 3: hist["2-3"] += 1
    elif n <= 5: hist["4-5"] += 1
    elif n <= 10: hist["6-10"] += 1
    elif n <= 20: hist["11-20"] += 1
    elif n <= 50: hist["21-50"] += 1
    else: hist["50+"] += 1
print("HISTOGRAM", dict(hist))

out = open(r"D:\Temp\common_exports.txt", "w", encoding="utf-8")
out.write("# Common.dll PE export table: %d exports, base ordinal %d\n" % (len(exports), base))
out.write("# matched to demangled names from EXE/Common.map: %d\n" % matched)
out.write("# format: ordinal\tclass_key\tmember\tdemangled\tmangled\tfunc_rva\n")
for key, syms in top:
    for ordinal, rva, member, full, mangled in syms:
        out.write("%d\t%s\t%s\t%s\t%s\t%08X\n" % (ordinal, key, member, full, mangled, rva))
out.write("\n# per-class summary\n")
for key, syms in top:
    out.write("# CLASS %-24s %d\n" % (key, len(syms)))
out.close()
print("WROTE D:/Temp/common_exports.txt")
