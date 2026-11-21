# Convert Ghidra Resource Hacker dumps under EXE/Disasm/DecompiledResources/<mod>/
# into MSVC rc.exe input. Ignore loose .rc files in the dump root.
# Usage: python import_decompiled_rc.py

from __future__ import print_function

import os
import re
import shutil

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
DUMP = os.path.join(ROOT, "EXE", "Disasm", "DecompiledResources")
SRC = os.path.join(ROOT, "src")

# dump folder (lowercase) -> (dest_dir, rc_filename, compile)
# compile: "dll" ResourceCompile, "lib-norsrc" copy only, "exe" freelancer resources dir
CODE_MAP = {
    "alchemy": ("alchemy", "alchemy.rc", "dll"),
    "dacom": ("dacom", "dacom.rc", "dll"),
    "dalib": ("DALib", "DALib.rc", "dll"),
    "deformable2": ("Deformable2", "Deformable2.rc", "dll"),
    "engbase": ("EngBase", "EngBase.rc", "dll"),
    "flmaterials": ("flmaterials", "flmaterials.rc", "dll"),
    "freelancer": ("freelancer", os.path.join("resources", "freelancer.rc"), "exe"),
    "luaprofile": ("luaprofile", "luaprofile.rc", "dll"),
    "readfile": ("ReadFile", "ReadFile.rc", "dll"),
    "rendcomp": ("RendComp", "RendComp.rc", "dll"),
    "rp8": ("RP8", "RP8.rc", "dll"),
    "shading": ("Shading", "Shading.rc", "dll"),
    "soundmanager": ("SoundManager", "SoundManager.rc", "dll"),
    "soundstreamer": ("SoundStreamer", "SoundStreamer.rc", "dll"),
    "system": ("system", "system.rc", "dll"),
    "thorn": ("Thorn", "Thorn.rc", "dll"),
    "x86math": ("x86math", "x86math.rc", "dll"),
    "ximage": ("ximage", "ximage.rc", "dll"),
    "zlib": ("zlib", "zlib.rc", "dll"),
    "common": ("Common", "Common.rc", "lib"),
}

RESOURCE_ONLY = {
    "resources": ("resources", "resources.rc", "resources"),
    "infocards": ("InfoCards", "InfoCards.rc", "InfoCards"),
    "misctext": ("MiscText", "MiscText.rc", "MiscText"),
    "nameresources": ("NameResources", "NameResources.rc", "NameResources"),
    "equipresources": ("EquipResources", "EquipResources.rc", "EquipResources"),
    "offerbriberesources": ("OfferBribeResources", "OfferBribeResources.rc", "OfferBribeResources"),
    "misctextinfo2": ("MiscTextInfo2", "MiscTextInfo2.rc", "MiscTextInfo2"),
    "serverresources": ("serverresources", "serverresources.rc", "serverresources"),
}

SKIP_DUMP = {
    "debuglib", "dw15", "ebueula", "gundll", "hudshift", "imeui", "jflp",
    "movie", "remoteclient", "remoteserver", "rpclocal", "server", "unicows",
}

def read_text(path):
    data = open(path, "rb").read()
    if data.startswith(b"\xff\xfe"):
        text = data.decode("utf-16-le")
    elif data.startswith(b"\xfe\xff"):
        text = data.decode("utf-16-be")
    elif data.startswith(b"\xef\xbb\xbf"):
        text = data.decode("utf-8-sig")
    else:
        try:
            text = data.decode("utf-8")
        except UnicodeDecodeError:
            text = data.decode("latin-1")
    return text.replace("\ufeff", "")


def matching_brace(s, open_idx):
    depth = 0
    i = open_idx
    n = len(s)
    while i < n:
        ch = s[i]
        if ch == "{":
            depth += 1
        elif ch == "}":
            depth -= 1
            if depth == 0:
                return i
        i += 1
    raise ValueError("unbalanced braces in %s" % path_hint)


path_hint = ""


def braces_to_begin_end(block):
    out = []
    i = 0
    while i < len(block):
        if block[i] == "{":
            out.append("BEGIN")
            i += 1
        elif block[i] == "}":
            out.append("END")
            i += 1
        else:
            out.append(block[i])
            i += 1
    return "".join(out)


def convert_version_block(raw_block, file_type_override=None):
    body = raw_block.strip()
    if body.startswith("{"):
        body = body[1:]
        if body.endswith("}"):
            body = body[:-1]
    converted = braces_to_begin_end(body)
    converted = re.sub(
        r'VALUE\s+"Translation"\s*,\s*(0x[0-9A-Fa-f]+)\s+(0x[0-9A-Fa-f]+)',
        r'VALUE "Translation", \1, \2',
        converted,
    )
    converted = converted.replace('\\"', '""')
    # Leave numeric escapes for convert_rc, which emits their Unicode values in UTF-8.
    header = []
    if "FILEFLAGSMASK" not in converted:
        header.append("FILEFLAGSMASK 0x3fL")
    if "FILEFLAGS" not in converted:
        header.append("FILEFLAGS 0x0L")
    if file_type_override:
        converted = re.sub(r"FILETYPE\s+\S+", "FILETYPE " + file_type_override, converted, count=1)
    prefix = ("\n".join(header) + "\n") if header else ""
    return "VS_VERSION_INFO VERSIONINFO\n" + prefix + converted.strip() + "\n"


def extract_versioninfos(text):
    results = []
    for m in re.finditer(r"^(\d+)\s+VERSIONINFO\b", text, re.MULTILINE):
        header_start = m.start()
        brace = text.find("{", m.end())
        if brace < 0:
            continue
        end = matching_brace(text, brace)
        header = text[m.end():brace]
        block = text[brace:end + 1]
        results.append((header_start, end + 1, m.group(1), header, block))
    return results


def pick_versioninfo(entries, prefer_filename):
    if not entries:
        return None
    if prefer_filename:
        want = prefer_filename.lower()
        for entry in entries:
            orig = re.search(r'VALUE\s+"OriginalFilename"\s*,\s*"([^"]+)"', entry[4], re.I)
            internal = re.search(r'VALUE\s+"InternalName"\s*,\s*"([^"]+)"', entry[4], re.I)
            if orig and orig.group(1).lower() == want:
                return entry
            if internal and (internal.group(1).lower() + ".dll") == want:
                return entry
    # Identical duplicates: first is enough.
    return entries[-1] if len(entries) == 1 else entries[-1]


def hoist_language(text):
    # Only pull LANGUAGE that sits in this resource's header (before its '{').
    pattern = re.compile(
        r"(?P<head>^(?:\d+|[A-Za-z_]\w*)\s+(?:DIALOGEX|DIALOG|MENU)\b[^\n]*\n)"
        r"(?P<mid>(?:(?!LANGUAGE)(?!\{)[^\n]*\n)*?)"
        r"(?P<lang>LANGUAGE[^\n]*\n)",
        re.MULTILINE,
    )
    prev = None
    while prev != text:
        prev = text
        text = pattern.sub(lambda m: m.group("lang") + m.group("head") + m.group("mid"), text)
    return text


def drop_empty_stringtables(text):
    return re.sub(
        r"STRINGTABLE\s+LANGUAGE[^\n]*\s*\{\s*\}\s*",
        "",
        text,
        flags=re.IGNORECASE,
    )


def convert_rc(text, prefer_dll, is_exe=False, strip_version=False):
    global path_hint
    text = text.replace("\r\n", "\n").replace("\ufeff", "")
    entries = extract_versioninfos(text)
    chosen = pick_versioninfo(entries, prefer_dll)
    if entries:
        pieces = []
        last = 0
        for start, end, _id, header, block in entries:
            pieces.append(text[last:start])
            if chosen is not None and start == chosen[0] and not strip_version:
                ft = "VFT_APP" if is_exe else None
                converted = convert_version_block(header + block, ft)
                pieces.append(converted)
            last = end
        pieces.append(text[last:])
        text = "".join(pieces)

    text = hoist_language(text)
    text = drop_empty_stringtables(text)
    text = text.replace('\\"', '""')
    # Resource Hacker's byte escapes denote Unicode code points. rc.exe decodes
    # octal bytes through its code page, so emit non-ASCII characters as UTF-8.
    text = re.sub(r"\\x([0-9A-Fa-f]{2})",
                  lambda m: chr(int(m.group(1), 16)) if int(m.group(1), 16) >= 0x80
                  else "\\%03o" % int(m.group(1), 16), text)
    text = re.sub(r"\bVK_([A-Z])\b", r'"\1"', text)
    # rc.exe treats type 241 as TOOLBAR script, not a filename.
    text = re.sub(
        r'^(\d+)\s+241\s+"([^"]+)"\s*$',
        r'// binary RT_TOOLBAR \1 "\2" skipped (not an rc.exe TOOLBAR script)',
        text,
        flags=re.MULTILINE,
    )
    text = re.sub(
        r'VALUE\s+"Translation"\s*,\s*(0x[0-9A-Fa-f]+)\s+(0x[0-9A-Fa-f]+)',
        r'VALUE "Translation", \1, \2',
        text,
    )

    needs_commctrl = bool(re.search(r"SysTabControl32|SysListView32|\bTCS_|\bLVS_", text))
    headers = ["#include <windows.h>"]
    if needs_commctrl:
        headers.append("#include <commctrl.h>")
    if "LANGUAGE LANG_" in text or text.lstrip().startswith("LANGUAGE"):
        pass
    body = text
    if "#include" in body[:80]:
        pass
    else:
        body = "\n".join(headers) + "\n\n" + body.lstrip()
    if not body.lstrip().startswith("#include"):
        body = "\n".join(headers) + "\n\n" + body
    # Avoid double windows.h if dump already had none (we always prepend).
    if body.count("#include <windows.h>") > 1:
        lines = body.split("\n")
        seen = False
        out_lines = []
        for line in lines:
            if line.strip() == "#include <windows.h>":
                if seen:
                    continue
                seen = True
            out_lines.append(line)
        body = "\n".join(out_lines)
    # The /c65001 of the resource-only DLLs (cmake/Freelancer.cmake) does not reach the code
    # modules' scripts, which this importer writes too.
    return '#pragma code_page(65001)\n' + body.rstrip() + "\n"


def copy_sidecars(dump_dir, dest_dir, rc_name):
    if not os.path.isdir(dest_dir):
        os.makedirs(dest_dir)
    for name in os.listdir(dump_dir):
        src_path = os.path.join(dump_dir, name)
        if not os.path.isfile(src_path):
            continue
        if name.lower().endswith(".rc"):
            continue
        shutil.copy2(src_path, os.path.join(dest_dir, name))


def write_resource_cmakelists(project_dir, target_name, rc_filename):
    """CMakeLists.txt of a resource-only DLL (also add its folder to FL_PROJECTS in the root
    CMakeLists.txt)."""
    os.makedirs(project_dir, exist_ok=True)
    path = os.path.join(project_dir, "CMakeLists.txt")
    text = """# See cmake/Freelancer.cmake. The source list is the link order, and with it the order of
# static initialisation.

# Win32 only.
if(NOT FL_WIN32)
  return()
endif()

fl_add_module(%s RESOURCE_DLL RETAIL
  SOURCES
    %s
)
""" % (target_name, rc_filename)
    open(path, "w", encoding="utf-8", newline="\n").write(text)


def insert_resource_compile(cmakelists_path, rc_rel):
    """Add rc_rel to the SOURCES of the module's fl_add_module() call, at the end."""
    text = open(cmakelists_path, encoding="utf-8").read()
    rc_rel = rc_rel.replace("\\", "/")
    # The call ends at a line holding only ")": comments inside it may contain parentheses.
    m = re.search(r"fl_add_module\(.*?\n\)[ \t]*$", text, re.S | re.M)
    if not m:
        raise RuntimeError("no fl_add_module(... SOURCES ...) in " + cmakelists_path)
    block = m.group(0)
    if re.search(r"^\s+%s\s*$" % re.escape(rc_rel), block, re.M):
        return
    new_block = block[:-1] + "    %s\n)" % rc_rel
    text = text[: m.start()] + new_block + text[m.end():]
    open(cmakelists_path, "w", encoding="utf-8", newline="\n").write(text)


def dump_folder_key(name):
    return name.lower()


def main():
    global path_hint
    if not os.path.isdir(DUMP):
        raise SystemExit("missing dump dir: " + DUMP)

    imported = []
    for folder in sorted(os.listdir(DUMP)):
        dump_dir = os.path.join(DUMP, folder)
        if not os.path.isdir(dump_dir):
            continue
        key = dump_folder_key(folder)
        if key in SKIP_DUMP:
            continue
        rcs = [n for n in os.listdir(dump_dir) if n.lower().endswith(".rc")]
        if not rcs:
            print("skip (no rc):", folder)
            continue
        rc_src = os.path.join(dump_dir, rcs[0])
        path_hint = rc_src
        raw = read_text(rc_src)

        if key in CODE_MAP:
            dest_sub, rc_name, kind = CODE_MAP[key]
            if kind == "exe":
                rc_path = os.path.join(SRC, "freelancer", "resources", "freelancer.rc")
                cml = os.path.join(SRC, "freelancer", "CMakeLists.txt")
                rc_rel = "resources/freelancer.rc"
            else:
                rc_path = os.path.join(SRC, dest_sub, os.path.basename(str(rc_name).replace("\\", "/")))
                cml = os.path.join(SRC, dest_sub, "CMakeLists.txt")
                rc_rel = os.path.basename(rc_path)
            os.makedirs(os.path.dirname(rc_path), exist_ok=True)
            prefer = dest_sub + ".dll"
            converted = convert_rc(
                raw,
                prefer,
                is_exe=(kind == "exe"),
                strip_version=(kind == "lib"),
            )
            open(rc_path, "w", encoding="utf-8", newline="\n").write(converted)
            copy_sidecars(dump_dir, os.path.dirname(rc_path), os.path.basename(rc_path))
            insert_resource_compile(cml, rc_rel)
            imported.append("%s -> %s" % (folder, rc_path))
            continue

        if key in RESOURCE_ONLY:
            dest_name, rc_file, target = RESOURCE_ONLY[key]
            dest_dir = os.path.join(SRC, "Resources", dest_name)
            os.makedirs(dest_dir, exist_ok=True)
            rc_path = os.path.join(dest_dir, rc_file)
            converted = convert_rc(raw, target + ".dll", is_exe=False)
            open(rc_path, "w", encoding="utf-8", newline="\n").write(converted)
            copy_sidecars(dump_dir, dest_dir, rc_file)
            write_resource_cmakelists(dest_dir, target, rc_file)
            imported.append("%s -> %s" % (folder, rc_path))
            continue

        print("unmapped dump folder:", folder)

    print("imported %d" % len(imported))
    for line in imported:
        print(" ", line)


if __name__ == "__main__":
    main()
