"""Compare the MSBuild and the CMake builds of Freelancer Decomp.

    build_parity.py commands --repo . --cmake-build build/release [--config Release]
                             [--platform Win32|x64]
        Every cl / rc / link / lib command line MSBuild ran (its .tlog files under src/obj)
        against the ones CMake generated (compile_commands.json and build.ninja): the same
        sources and modules, with the same options, defines, include order, objects in the
        same order, libraries and library directories in the same order.

    build_parity.py binaries <msbuild-bin> <cmake-bin>
        Every .exe / .dll of the two output directories, section by section (needs pefile).
        The debug directory (PDB GUID, timestamps) and the header timestamp/checksum are the
        only bytes allowed to differ.

Exit status 1 when something that can change the output differs. Differences in options that
only affect diagnostics (warning levels, /nologo, /FC-style report options, file names of
intermediate files) are listed but do not fail. With GITHUB_STEP_SUMMARY set, the report is
appended there as well.
"""

import argparse
import glob
import json
import os
import re
import sys

# ---------------------------------------------------------------------------------------------
# Command lines
# ---------------------------------------------------------------------------------------------


def split_command_line(line):
    """Split like CommandLineToArgvW (quotes removed, 2n backslashes before a quote -> n)."""
    args, cur, in_quotes, have_arg, i = [], [], False, False, 0
    while i < len(line):
        c = line[i]
        if c == "\\":
            n = 0
            while i < len(line) and line[i] == "\\":
                n += 1
                i += 1
            if i < len(line) and line[i] == '"':
                cur.append("\\" * (n // 2))
                if n % 2:
                    cur.append('"')
                    i += 1
                have_arg = True
            else:
                cur.append("\\" * n)
                have_arg = True
            continue
        if c == '"':
            in_quotes = not in_quotes
            have_arg = True
        elif c in " \t" and not in_quotes:
            if have_arg:
                args.append("".join(cur))
                cur, have_arg = [], False
        else:
            cur.append(c)
            have_arg = True
        i += 1
    if have_arg:
        args.append("".join(cur))
    return args


def norm_path(path, base=None):
    path = path.strip().strip('"')
    if base and not os.path.isabs(path) and not re.match(r"^[A-Za-z]:", path):
        path = os.path.join(base, path)
    path = os.path.normpath(path).replace("\\", "/").rstrip("/").lower()
    # vcpkg installs into src/vcpkg_installed/<triplet>/<triplet>/ for MSBuild and into
    # <build>/vcpkg_installed/<triplet>/ for CMake: the same package either way.
    m = re.search(r"/vcpkg_installed/(?:[^/]+/)*?([^/]+-windows)(/.*)?$", path)
    if m:
        rest = m.group(2) or ""
        if rest.startswith("/" + m.group(1)):
            rest = rest[len(m.group(1)) + 1:]
        path = "<vcpkg>/" + m.group(1) + rest
    return path


def stem(path):
    """common_ini.cpp.obj (CMake) and common_ini.obj (MSBuild) -> common_ini."""
    name = os.path.basename(path.replace("\\", "/")).lower()
    for ext in (".obj", ".res", ".lib"):
        if name.endswith(ext):
            name = name[: -len(ext)]
    for ext in (".cpp", ".c", ".rc"):
        if name.endswith(ext):
            name = name[: -len(ext)]
    return name


# cl options that do not change the object file (diagnostics, file names, scheduling).
CL_NO_OUTPUT = re.compile(
    r"^/(nologo|c|showIncludes|FS|TP|TC|MP\d*|Gm-|analyze-|WX-?|W[0-4]|Wall|wd\d+|we\d+|w[1-4]\d+|"
    r"diagnostics:.*|errorReport:.*|external:W\d|external:env:.*|sourceDependencies.*)$",
    re.I,
)
# Output file names (case matters: /Fp<file> is not /fp:precise).
CL_FILE_NAMES = re.compile(r"^/(Fo|Fd|Fa|Fp|Fe|Fi|Fm|FR|Fr|fo)")
# Families of cl options where the last one on the command line wins.
CL_FAMILIES = [
    ("optimization", r"^/O[12dsx]$"),
    ("intrinsics", r"^/Oi-?$"),
    ("frame-pointers", r"^/Oy-?$"),
    ("arch", r"^/arch:"),
    ("fp", r"^/fp:"),
    ("wpo", r"^/GL-?$"),
    ("sdl", r"^/sdl-?$"),
    ("debug-info", r"^/Z[iI7]$"),
    ("runtime", r"^/M[DT]d?$"),
    ("rtc", r"^/RTC"),
    ("jmc", r"^/JMC-?$"),
    ("std", r"^/std:"),
    ("conformance", r"^/permissive-?$"),
    ("gs", r"^/GS-?$"),
    ("eh", r"^/EH"),
    ("callconv", r"^/G[drzv]$"),
    ("warning-level", r"^/(W[0-4]|Wall)$"),
]


def parse_cl(args, base):
    """-> dict(defines, includes, forced, options, diagnostics) from cl arguments."""
    defines, includes, forced, opts, diag = {}, [], [], {}, set()
    i = 0
    while i < len(args):
        a = args[i]
        if a[:1] == "-":
            a = "/" + a[1:]
        if a in ("/D", "/I", "/FI", "/external:I") and i + 1 < len(args):
            a, i = a + args[i + 1], i + 1
        if a.startswith("/external:I"):
            # A package's headers (vcpkg): CMake passes an imported target's include directories
            # as external ones, MSBuild's vcpkg integration as ordinary ones, after the rest.
            p = norm_path(a[len("/external:I"):], base)
            if p not in includes:
                includes.append(p)
        elif a.startswith("/FI"):
            # Forced includes in order (the precompiled header goes in front of fl_layout.h).
            forced.append(os.path.basename(a[3:].strip('"')).lower())
        elif a.startswith(("/Yu", "/Yc")):
            # The precompiled header, by name: MSBuild and CMake spell its path differently.
            opts[a[:3]] = a[:3] + os.path.basename(a[3:].strip('"')).lower()
        elif a.startswith("/D"):
            name, _, value = a[2:].partition("=")
            defines[name] = value
        elif a.startswith("/I"):
            p = norm_path(a[2:], base)
            if p not in includes:
                includes.append(p)
        elif a.startswith("/") and not os.path.isfile(a):
            family = next((f for f, r in CL_FAMILIES if re.match(r, a, re.I)), None)
            if CL_NO_OUTPUT.match(a) or CL_FILE_NAMES.match(a):
                diag.add(a)
            else:
                opts[family or a] = a
        i += 1
    return {"defines": defines, "includes": includes, "forced": forced, "options": set(opts.values()),
            "diagnostics": diag}


def read_tlog(path):
    raw = open(path, "rb").read()
    for enc in ("utf-16", "utf-8-sig", "mbcs" if os.name == "nt" else "latin-1"):
        try:
            return raw.decode(enc)
        except (UnicodeDecodeError, LookupError):
            continue
    return raw.decode("latin-1")


def tlog_entries(path):
    """(inputs, command line) pairs of a *.command.*.tlog."""
    inputs, out = None, []
    for line in read_tlog(path).splitlines():
        if line.startswith("^"):
            inputs = [p for p in line[1:].split("|") if p]
        elif inputs is not None and line.strip():
            out.append((inputs, line))
            inputs = None
    return out


def msbuild_commands(repo, config, platform):
    """Commands MSBuild ran, from the tlogs of src/obj/<project>/<config>[_x64]."""
    projects = {}
    for vcx in glob.glob(os.path.join(repo, "src", "**", "*.vcxproj"), recursive=True):
        projects[os.path.splitext(os.path.basename(vcx))[0].lower()] = os.path.abspath(os.path.dirname(vcx))
    cl, rc, link, lib = {}, {}, {}, {}
    intdir = config + ("_x64" if platform == "x64" else "")
    for tlog in glob.glob(os.path.join(repo, "src", "obj", "*", intdir, "*.tlog", "*.command.*.tlog")):
        project = os.path.basename(os.path.dirname(os.path.dirname(os.path.dirname(tlog)))).lower()
        base = projects.get(project)
        kind = os.path.basename(tlog).split(".")[0].lower()
        for inputs, line in tlog_entries(tlog):
            args = split_command_line(line)
            if kind == "cl":
                parsed = parse_cl(args, base)
                for src in inputs:
                    cl[(project, norm_path(src))] = dict(parsed, project=project)
            elif kind == "rc":
                for src in inputs:
                    rc[(project, norm_path(src))] = dict(parse_cl(args, base), project=project,
                                                         base=norm_path(base) if base else None)
            elif kind == "link" or kind.startswith("lib"):
                parsed = parse_link(args, base)
                # The command line leaves out what MSBuild passes through its input list: the
                # objects and the import libraries of project references.
                objs = [stem(p) for p in inputs if p.lower().endswith(".obj")]
                parsed["objects"] = sorted(set(parsed["objects"]) | set(objs))
                parsed["resources"] = sorted(set(parsed["resources"]) | {stem(p) for p in inputs if p.lower().endswith(".res")})
                parsed["input_libs"] = {os.path.basename(p.replace("\\", "/")).lower()
                                        for p in inputs if p.lower().endswith(".lib")}
                table = link if kind == "link" else lib
                prev = table.get(parsed["output"])
                if prev:
                    # Several tlog entries for one output (lib.exe batches, relinks): merge.
                    parsed["objects"] = sorted(set(prev["objects"]) | set(parsed["objects"]))
                    parsed["options"] |= prev["options"]
                    parsed["input_libs"] |= prev["input_libs"]
                table[parsed["output"]] = dict(parsed, project=project)
    return cl, rc, link, lib


# /MANIFESTINPUT: CMake hands the same manifest to mt.exe as a source instead; the embedded
# manifest is compared in the binaries.
LINK_NO_OUTPUT = re.compile(
    r"^/(NOLOGO|ERRORREPORT:.*|OUT:.*|PDB:.*|IMPLIB:.*|ManifestFile:.*|LTCGOUT:.*|PGD:.*|ILK:.*|"
    r"VERSION:0\.0|MANIFEST:EMBED.*|MANIFESTINPUT:.*)$",
    re.I,
)


def parse_link(args, base):
    output, objects, resources, libs, opts, diag = None, [], [], [], set(), set()
    libpath = []
    for a in args:
        if a[:1] == "-":
            a = "/" + a[1:]
        low = a.lower()
        if low.startswith("/out:"):
            output = os.path.basename(a[5:].strip('"').replace("\\", "/")).lower()
            diag.add("/OUT")
            continue
        if re.match(r"^/[A-Za-z]+(:|$)", a) and not os.path.isfile(a):
            if low.startswith("/libpath:"):
                d = norm_path(a[9:], base)
                if d not in libpath and not d.startswith("<vcpkg>"):
                    libpath.append(d)
            elif low.startswith("/def:"):
                opts.add("/DEF:" + os.path.basename(a[5:].replace("\\", "/")).lower())
            elif LINK_NO_OUTPUT.match(a):
                diag.add(a.split(":")[0].upper())
            else:
                head, sep, tail = a.partition(":")
                opts.add(head.upper() + sep + tail)
        elif low.endswith(".obj"):
            objects.append(stem(a))
        elif low.endswith(".res"):
            resources.append(stem(a))
        elif low.endswith(".lib"):
            name = os.path.basename(a.replace("\\", "/")).lower()
            # vcpkg's packages: MSBuild links <installed>\lib\*.lib, CMake each library by path.
            if name == "*.lib" or "vcpkg_installed" in low:
                name = "<vcpkg libraries>"
            if name not in libs:
                libs.append(name)
    return {"output": output, "objects": objects, "resources": resources, "libs": libs,
            "libpath": libpath, "options": opts, "diagnostics": diag}


# --- CMake side ------------------------------------------------------------------------------


def ninja_unescape(s):
    return re.sub(r"\$(.)", lambda m: m.group(1) if m.group(1) in " :$" else "$" + m.group(1), s)


def ninja_split(s):
    return [ninja_unescape(p) for p in re.split(r"(?<!\$) +", s.strip()) if p]


def ninja_builds(path):
    """[(rule, outputs, explicit inputs, {var: value})] of a build.ninja (no includes needed)."""
    builds, cur = [], None
    for line in open(path, encoding="utf-8"):
        line = line.rstrip("\n")
        if line.startswith("build "):
            head = line[6:]
            m = re.match(r"(.*?)(?<!\$):\s*(\S+)\s*(.*)$", head)
            outputs = ninja_split(m.group(1).split(" | ")[0])
            rest = re.split(r"(?<!\$) \|", m.group(3))[0]
            cur = (m.group(2), outputs, ninja_split(rest), {})
            builds.append(cur)
        elif line.startswith("  ") and cur is not None and " = " in line:
            k, _, v = line.strip().partition(" = ")
            cur[3][k] = v
        elif not line.startswith("  "):
            cur = None
    return builds


def target_of(output):
    """The CMake target of an object file: .../CMakeFiles/<target>.dir/... -> <target>."""
    m = re.search(r"CMakeFiles[\\/]([^\\/]+)\.dir[\\/]", output)
    return m.group(1).lower() if m else None


def cmake_commands(build_dir):
    cl, rc, link, lib = {}, {}, {}, {}
    for e in json.load(open(os.path.join(build_dir, "compile_commands.json"), encoding="utf-8")):
        if not e["file"].lower().endswith((".c", ".cpp")):
            continue  # rc commands are read from build.ninja below
        args = e.get("arguments") or split_command_line(e["command"])
        cl[(target_of(e.get("output", "")), norm_path(e["file"]))] = dict(
            parse_cl(args[1:], e["directory"]), project=None)
    for rule, outputs, inputs, var in ninja_builds(os.path.join(build_dir, "build.ninja")):
        if rule.startswith("RC_COMPILER__"):
            args = split_command_line(" ".join([var.get("DEFINES", ""), var.get("INCLUDES", ""),
                                                var.get("FLAGS", "")]))
            for src in inputs:
                rc[(target_of(outputs[0]), norm_path(src))] = dict(parse_cl(args, build_dir), project=None)
        elif re.match(r"C(XX)?_(SHARED_LIBRARY|EXECUTABLE|MODULE)_LINKER__", rule):
            flags = split_command_line(var.get("LINK_FLAGS", "") + " " + var.get("LINK_PATH", ""))
            if "SHARED_LIBRARY" in rule:
                flags.append("/DLL")
            libs = split_command_line(var.get("LINK_LIBRARIES", ""))
            parsed = parse_link(flags + inputs + libs + ["/OUT:" + outputs[0]], build_dir)
            link[parsed["output"]] = dict(parsed, project=None)
        elif re.match(r"C(XX)?_STATIC_LIBRARY_LINKER__", rule):
            flags = split_command_line(var.get("LINK_FLAGS", ""))
            parsed = parse_link(flags + inputs + ["/OUT:" + outputs[0]], build_dir)
            lib[parsed["output"]] = dict(parsed, project=None)
    return cl, rc, link, lib


def compare_compiles(kind, a, b, report):
    failed = False
    for key in sorted(set(a) | set(b)):
        src = "%s` (%s)" % (key[1], key[0])
        if key not in b:
            report.append("- %s `%s: built by MSBuild only" % (kind, src))
            failed = True
            continue
        if key not in a:
            report.append("- %s `%s: built by CMake only" % (kind, src))
            failed = True
            continue
        x, y, msgs = a[key], b[key], []
        if key[1].endswith(".c"):
            # A C++ standard switch does nothing for a C file.
            for z in (x, y):
                z["options"] = {o for o in z["options"] if not o.lower().startswith("/std:c++")}
        if x["defines"] != y["defines"]:
            only_a = {k: v for k, v in x["defines"].items() if y["defines"].get(k) != v}
            only_b = {k: v for k, v in y["defines"].items() if x["defines"].get(k) != v}
            msgs.append("defines MSBuild-only %s, CMake-only %s" % (only_a, only_b))
        if kind == "rc":
            # rc tlogs are upper-cased. MSBuild runs rc in the project directory; CMake passes
            # that directory as /I instead (relative ICON / #include names resolve the same).
            here = x.get("base")
            for z in (x, y):
                z["options"] = {o.lower() for o in z["options"]}
                z["includes"] = [i for i in z["includes"] if i != here]
        # The INCLUDE environment (Windows SDK, VC, DX8 appended) is not on either command line;
        # MSBuild passes the project's own dirs, CMake the same ones.
        if x["includes"] != y["includes"]:
            msgs.append("include order\n    MSBuild %s\n    CMake   %s" % (x["includes"], y["includes"]))
        if x.get("forced", []) != y.get("forced", []):
            msgs.append("forced includes MSBuild %s, CMake %s" % (x.get("forced"), y.get("forced")))
        if x["options"] != y["options"]:
            msgs.append("options MSBuild-only %s, CMake-only %s"
                        % (sorted(x["options"] - y["options"]), sorted(y["options"] - x["options"])))
        if msgs:
            failed = True
            report.append("- %s `%s: %s" % (kind, src, "; ".join(msgs)))
        elif kind == "cl":
            # Warning switches (a project's DisableSpecificWarnings replacing the shared list)
            # and the other diagnostics are listed, not compared.
            dd = {d for d in x["diagnostics"] ^ y["diagnostics"]
                  if not re.match(r"^/(c|TP|TC|FS|showIncludes|wd\d+|Fo.*|Fd.*|Fa.*|Fp.*)$", d, re.I)}
            if dd:
                report.append("- (diagnostics only) cl `%s: %s" % (src, sorted(dd)))
    return failed


def compare_links(kind, a, b, report):
    failed = False
    for out in sorted(set(a) | set(b)):
        if out not in a or out not in b:
            report.append("- %s `%s`: built by %s only" % (kind, out, "MSBuild" if out in a else "CMake"))
            failed = True
            continue
        x, y, msgs = a[out], b[out], []
        # The .tlog lists the objects sorted, not in link order: compare them as a set. CMake
        # links them in the .vcxproj's item order, as MSBuild does.
        if sorted(x["objects"]) != sorted(y["objects"]):
            msgs.append("objects MSBuild-only %s, CMake-only %s"
                        % (sorted(set(x["objects"]) - set(y["objects"])),
                           sorted(set(y["objects"]) - set(x["objects"]))))
        if sorted(x["resources"]) != sorted(y["resources"]):
            msgs.append("resources MSBuild %s, CMake %s" % (x["resources"], y["resources"]))
        # MSBuild's command line names AdditionalDependencies (+ the defaults) in order; project
        # reference libraries only show up among its inputs. CMake must name the former in the
        # same order and may add only the latter.
        named = [l for l in y["libs"] if l in x["libs"]]
        extra = [l for l in y["libs"] if l not in x["libs"] and l not in x.get("input_libs", set())]
        if named != x["libs"] or extra:
            msgs.append("libraries differ\n    MSBuild %s (+ inputs %s)\n    CMake   %s"
                        % (x["libs"], sorted(x.get("input_libs", ())), y["libs"]))
        if x["libpath"] != y["libpath"]:
            msgs.append("library directories\n    MSBuild %s\n    CMake   %s" % (x["libpath"], y["libpath"]))
        ox = {o.upper() for o in x["options"]}
        oy = {o.upper() for o in y["options"]}
        if ox != oy:
            msgs.append("options MSBuild-only %s, CMake-only %s" % (sorted(ox - oy), sorted(oy - ox)))
        if msgs:
            failed = True
            report.append("- %s `%s`: %s" % (kind, out, "; ".join(msgs)))
    return failed


def cmd_commands(ns):
    a = msbuild_commands(ns.repo, ns.config, ns.platform)
    b = cmake_commands(ns.cmake_build)
    report = ["## Command lines: MSBuild vs CMake (%s|%s)" % (ns.config, ns.platform), ""]
    report.append("MSBuild: %d cl, %d rc, %d link, %d lib; CMake: %d cl, %d rc, %d link, %d lib"
                  % tuple(len(x) for x in a + b))
    report.append("")
    failed = False
    failed |= compare_compiles("cl", a[0], b[0], report)
    failed |= compare_compiles("rc", a[1], b[1], report)
    failed |= compare_links("link", a[2], b[2], report)
    failed |= compare_links("lib", a[3], b[3], report)
    if not a[0]:
        report.append("- no MSBuild cl tlogs found under src/obj/*/%s" % ns.config)
        failed = True
    report.append("")
    report.append("**Result: %s**" % ("DIFFERENT" if failed else "identical"))
    return failed, report


# ---------------------------------------------------------------------------------------------
# Binaries
# ---------------------------------------------------------------------------------------------

HEADER_FIELDS = [
    ("FILE_HEADER", ["Machine", "NumberOfSections", "Characteristics", "SizeOfOptionalHeader"]),
    ("OPTIONAL_HEADER", ["Magic", "MajorLinkerVersion", "MinorLinkerVersion", "SizeOfCode",
                         "SizeOfInitializedData", "SizeOfUninitializedData", "AddressOfEntryPoint",
                         "BaseOfCode", "ImageBase", "SectionAlignment", "FileAlignment",
                         "MajorOperatingSystemVersion", "MinorOperatingSystemVersion",
                         "MajorImageVersion", "MinorImageVersion", "MajorSubsystemVersion",
                         "MinorSubsystemVersion", "SizeOfImage", "SizeOfHeaders", "Subsystem",
                         "DllCharacteristics", "SizeOfStackReserve", "SizeOfStackCommit",
                         "SizeOfHeapReserve", "SizeOfHeapCommit"]),
]


def build_identity_ranges(pe):
    """File offsets of bytes that record when/where the module was built, not what it is."""
    import pefile

    ranges = []
    if hasattr(pe, "DIRECTORY_ENTRY_DEBUG"):
        for d in pe.DIRECTORY_ENTRY_DEBUG:
            s = d.struct
            ranges.append((s.get_file_offset(), s.get_file_offset() + s.sizeof()))
            if s.PointerToRawData and s.SizeOfData:
                ranges.append((s.PointerToRawData, s.PointerToRawData + s.SizeOfData))
    if hasattr(pe, "DIRECTORY_ENTRY_EXPORT"):
        s = pe.DIRECTORY_ENTRY_EXPORT.struct
        off = s.get_file_offset() + 4  # TimeDateStamp
        ranges.append((off, off + 4))
    if hasattr(pe, "DIRECTORY_ENTRY_RESOURCE"):
        def walk(entry):
            s = entry.struct
            off = s.get_file_offset() + 4
            ranges.append((off, off + 4))
            for e in getattr(entry, "entries", []):
                if hasattr(e, "directory"):
                    walk(e.directory)
        walk(pe.DIRECTORY_ENTRY_RESOURCE)
    if hasattr(pe, "DIRECTORY_ENTRY_LOAD_CONFIG"):
        s = pe.DIRECTORY_ENTRY_LOAD_CONFIG.struct
        off = s.get_file_offset() + 4
        ranges.append((off, off + 4))
    del pefile
    return ranges


def masked(data, start, ranges):
    data = bytearray(data)
    end = start + len(data)
    for lo, hi in ranges:
        lo, hi = max(lo, start), min(hi, end)
        if lo < hi:
            data[lo - start:hi - start] = b"\0" * (hi - lo)
    return bytes(data)


def printable(b):
    return "".join(chr(c) if 32 <= c < 127 else "." for c in b)


def pe_summary(path):
    import pefile

    pe = pefile.PE(path)
    ranges = build_identity_ranges(pe)
    header = {}
    for part, fields in HEADER_FIELDS:
        obj = getattr(pe, part)
        for f in fields:
            header["%s.%s" % (part, f)] = getattr(obj, f)
    sections = []
    for s in pe.sections:
        raw = s.get_data()
        sections.append((s.Name.rstrip(b"\0").decode("latin-1"), s.Misc_VirtualSize,
                         s.Characteristics, masked(raw, s.PointerToRawData, ranges), s.VirtualAddress))
    imports = []
    for entry in getattr(pe, "DIRECTORY_ENTRY_IMPORT", []):
        for imp in entry.imports:
            imports.append((entry.dll.decode().lower(),
                            imp.name.decode() if imp.name else "#%d" % imp.ordinal))
    exports = []
    if hasattr(pe, "DIRECTORY_ENTRY_EXPORT"):
        for e in pe.DIRECTORY_ENTRY_EXPORT.symbols:
            exports.append((e.ordinal, e.name.decode() if e.name else None,
                            e.forwarder.decode() if e.forwarder else None, e.address))
    pe.close()
    return header, sections, imports, exports


def cmd_binaries(ns):
    report = ["## Binaries: %s vs %s" % (ns.a, ns.b), ""]
    failed = False
    ignored = {n.lower() for n in ns.ignore}
    names_a = {os.path.basename(p).lower(): p for p in glob.glob(os.path.join(ns.a, "*")) if p.lower().endswith((".exe", ".dll"))}
    names_b = {os.path.basename(p).lower(): p for p in glob.glob(os.path.join(ns.b, "*")) if p.lower().endswith((".exe", ".dll"))}
    same = []
    for name in sorted(set(names_a) | set(names_b)):
        if name not in names_a or name not in names_b:
            report.append("- `%s`: only in %s" % (name, ns.a if name in names_a else ns.b))
            failed = True
            continue
        if name in ignored:
            report.append("- `%s`: not compared (--ignore)" % name)
            continue
        ha, sa, ia, ea = pe_summary(names_a[name])
        hb, sb, ib, eb = pe_summary(names_b[name])
        msgs = []
        for k in ha:
            if ha[k] != hb[k]:
                msgs.append("%s %s != %s" % (k, ha[k], hb[k]))
        if [s[:3] for s in sa] != [s[:3] for s in sb]:
            msgs.append("section table %s != %s" % ([s[:3] for s in sa], [s[:3] for s in sb]))
        else:
            for x, y in zip(sa, sb):
                if x[3] != y[3]:
                    diff = [i for i in range(min(len(x[3]), len(y[3]))) if x[3][i] != y[3][i]]
                    msgs.append("section %s: %d bytes differ (first at +0x%x)" % (x[0], len(diff), diff[0] if diff else 0))
                    shown = []
                    for i in diff:
                        if shown and i - shown[-1] < 32:
                            continue
                        shown.append(i)
                        if len(shown) > 4:
                            break
                        lo = max(0, i - 16)
                        msgs.append("\n    rva 0x%x\n      MSBuild %s |%s|\n      CMake   %s |%s|"
                                    % (x[4] + i, x[3][lo:lo + 48].hex(" "), printable(x[3][lo:lo + 48]),
                                       y[3][lo:lo + 48].hex(" "), printable(y[3][lo:lo + 48])))
        if ia != ib:
            msgs.append("imports differ: MSBuild-only %s, CMake-only %s"
                        % (sorted(set(ia) - set(ib))[:10], sorted(set(ib) - set(ia))[:10]))
        if ea != eb:
            msgs.append("exports differ: MSBuild-only %s, CMake-only %s"
                        % (sorted(set(ea) - set(eb), key=str)[:10], sorted(set(eb) - set(ea), key=str)[:10]))
        if msgs:
            failed = True
            report.append("- `%s`: %s" % (name, "; ".join(msgs)))
        else:
            same.append(name)
    report.append("")
    report.append("Identical (apart from the debug directory and timestamps): %d of %d: %s"
                  % (len(same), len(set(names_a) | set(names_b)), ", ".join(same)))
    report.append("")
    report.append("**Result: %s**" % ("DIFFERENT" if failed else "identical"))
    return failed, report


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    c = sub.add_parser("commands")
    c.add_argument("--repo", default=".")
    c.add_argument("--cmake-build", required=True)
    c.add_argument("--config", default="Release")
    c.add_argument("--platform", default="Win32", choices=("Win32", "x64"))
    b = sub.add_parser("binaries")
    b.add_argument("a", help="MSBuild output directory")
    b.add_argument("b", help="CMake output directory")
    b.add_argument("--ignore", action="append", default=[],
                   help="a DLL not to compare (one that neither build compiles, such as a vcpkg package)")
    ns = ap.parse_args()
    failed, report = cmd_commands(ns) if ns.cmd == "commands" else cmd_binaries(ns)
    text = "\n".join(report) + "\n"
    sys.stdout.write(text)
    summary = os.environ.get("GITHUB_STEP_SUMMARY")
    if summary:
        with open(summary, "a", encoding="utf-8") as f:
            f.write(text)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
