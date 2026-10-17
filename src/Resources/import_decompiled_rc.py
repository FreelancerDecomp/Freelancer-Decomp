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

RESOURCE_GUIDS = {
    "resources": "{E1A10001-5E50-4A01-9C01-000000000007}",
    "InfoCards": "{E1A10001-5E50-4A01-9C01-000000000001}",
    "MiscText": "{E1A10001-5E50-4A01-9C01-000000000002}",
    "NameResources": "{E1A10001-5E50-4A01-9C01-000000000003}",
    "EquipResources": "{E1A10001-5E50-4A01-9C01-000000000004}",
    "OfferBribeResources": "{E1A10001-5E50-4A01-9C01-000000000005}",
    "MiscTextInfo2": "{E1A10001-5E50-4A01-9C01-000000000006}",
    "serverresources": "{E1A10001-5E50-4A01-9C01-000000000008}",
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
    # Code-bearing projects also consume this importer and do not import ResourceDll.props.
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


def write_resource_vcxproj(project_dir, target_name, rc_filename, guid):
    os.makedirs(project_dir, exist_ok=True)
    path = os.path.join(project_dir, target_name + ".vcxproj")
    xml = """<?xml version="1.0" encoding="utf-8"?>
<Project DefaultTargets="Build" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <ItemGroup Label="ProjectConfigurations">
    <ProjectConfiguration Include="Debug|Win32">
      <Configuration>Debug</Configuration>
      <Platform>Win32</Platform>
    </ProjectConfiguration>
    <ProjectConfiguration Include="Release|Win32">
      <Configuration>Release</Configuration>
      <Platform>Win32</Platform>
    </ProjectConfiguration>
  </ItemGroup>
  <PropertyGroup Label="Globals">
    <VCProjectVersion>17.0</VCProjectVersion>
    <ProjectGuid>%s</ProjectGuid>
    <Keyword>Win32Proj</Keyword>
    <RootNamespace>%s</RootNamespace>
    <WindowsTargetPlatformVersion>10.0</WindowsTargetPlatformVersion>
  </PropertyGroup>
  <Import Project="$(VCTargetsPath)\\Microsoft.Cpp.Default.props" />
  <PropertyGroup Condition="'$(Configuration)|$(Platform)'=='Debug|Win32'" Label="Configuration">
    <ConfigurationType>DynamicLibrary</ConfigurationType>
    <UseDebugLibraries>true</UseDebugLibraries>
  </PropertyGroup>
  <PropertyGroup Condition="'$(Configuration)|$(Platform)'=='Release|Win32'" Label="Configuration">
    <ConfigurationType>DynamicLibrary</ConfigurationType>
    <UseDebugLibraries>false</UseDebugLibraries>
  </PropertyGroup>
  <Import Project="$(VCTargetsPath)\\Microsoft.Cpp.props" />
  <Import Project="$(ProjectDir)..\\ResourceDll.props" />
  <PropertyGroup>
    <TargetName>%s</TargetName>
  </PropertyGroup>
  <ItemGroup>
    <ResourceCompile Include="%s" />
  </ItemGroup>
  <Import Project="$(VCTargetsPath)\\Microsoft.Cpp.targets" />
</Project>
""" % (guid, target_name, target_name, rc_filename)
    open(path, "w", encoding="utf-8", newline="\r\n").write(xml)


def insert_resource_compile(vcxproj_path, rc_rel, as_none=False):
    text = open(vcxproj_path, encoding="utf-8").read()
    tag = "None" if as_none else "ResourceCompile"
    needle = '<%s Include="%s"' % (tag, rc_rel.replace("/", "\\"))
    alt = rc_rel.replace("\\", "/")
    if needle in text or ('Include="%s"' % alt) in text and tag in text:
        return
    if not as_none:
        text = re.sub(
            r"\s*<ItemGroup>\s*<ResourceCompile Include=\"[^\"]+\"\s*/>\s*</ItemGroup>",
            "",
            text,
            count=1,
        )
    snippet = '  <ItemGroup>\n    <%s Include="%s" />\n  </ItemGroup>\n' % (
        tag,
        rc_rel.replace("/", "\\"),
    )
    token = '  <Import Project="$(VCTargetsPath)\\Microsoft.Cpp.targets" />'
    if token not in text:
        raise RuntimeError("no Cpp.targets in " + vcxproj_path)
    if snippet.strip() not in text:
        text = text.replace(token, snippet + token)
    open(vcxproj_path, "w", encoding="utf-8", newline="\r\n").write(text)


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
                vcx = os.path.join(SRC, "freelancer", "freelancer.vcxproj")
                rc_rel = "resources\\freelancer.rc"
            else:
                rc_path = os.path.join(SRC, dest_sub, os.path.basename(str(rc_name).replace("\\", "/")))
                vcx = os.path.join(SRC, dest_sub, dest_sub + ".vcxproj")
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
            insert_resource_compile(vcx, rc_rel)
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
            guid = RESOURCE_GUIDS[target]
            write_resource_vcxproj(dest_dir, target, rc_file, guid)
            imported.append("%s -> %s" % (folder, rc_path))
            continue

        print("unmapped dump folder:", folder)

    print("imported %d" % len(imported))
    for line in imported:
        print(" ", line)


if __name__ == "__main__":
    main()
