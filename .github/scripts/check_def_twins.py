"""Check that every src/**/X_x64.def exports the same thing as its Win32 twin X.def.

The x64 file differs only in how the C++ names are decorated (docs/x64.md: PBD becomes PEBD,
QAE becomes QEAA). Each export is reduced to (undecorated name, internal name, ordinal, flags),
with the undecorated names from MSVC's undname.exe and the platform-only parts (__ptr64 and the
calling convention) removed, and the two files must agree entry for entry, in order.

usage: check_def_twins.py [--undname PATH] [ROOT]   (ROOT defaults to src/)
"""

import argparse
import pathlib
import re
import subprocess
import sys

FLAGS = ("NONAME", "DATA", "PRIVATE", "CONSTANT")
CALLING_CONVENTIONS = re.compile(r"\b__(?:cdecl|thiscall|stdcall|fastcall|vectorcall|ptr64)\b")


class DefError(Exception):
    pass


def parse_def(path):
    """The EXPORTS entries of a .def file: [(line, name, internal, ordinal, flags)]."""
    entries = []
    in_exports = False
    for number, raw in enumerate(path.read_text(encoding="latin-1").splitlines(), 1):
        line = raw.split(";", 1)[0].strip()
        if not line:
            continue
        keyword = line.split()[0].upper()
        if keyword == "EXPORTS":
            in_exports = True
            rest = line[len("EXPORTS"):].strip()
            if not rest:
                continue
            line = rest
        elif keyword in ("LIBRARY", "NAME", "HEAPSIZE", "STACKSIZE", "SECTIONS", "VERSION", "STUB"):
            in_exports = False
            continue
        if not in_exports:
            continue
        match = re.fullmatch(
            r"(?P<name>\S+?)(?:\s*=\s*(?P<internal>\S+))?"
            r"(?:\s+@\s*(?P<ordinal>\d+))?(?P<flags>(?:\s+\w+)*)",
            line,
        )
        if not match:
            raise DefError("%s:%d: cannot parse export line: %s" % (path, number, raw.strip()))
        flags = tuple(sorted(flag.upper() for flag in match.group("flags").split()))
        unknown = [flag for flag in flags if flag not in FLAGS]
        if unknown:
            raise DefError("%s:%d: unknown export flag %s" % (path, number, ", ".join(unknown)))
        entries.append((number, match.group("name"), match.group("internal"), match.group("ordinal"), flags))
    return entries


class Undecorator:
    def __init__(self, undname):
        self.undname = undname
        self.cache = {}

    def __call__(self, name):
        if not name or not name.startswith("?"):
            return name
        if name not in self.cache:
            output = subprocess.run(
                [self.undname, name], capture_output=True, text=True, check=False
            ).stdout
            match = re.search(r'is :- "(.*)"', output)
            if not match or match.group(1) == name:
                raise DefError("undname could not undecorate %s:\n%s" % (name, output))
            text = CALLING_CONVENTIONS.sub(" ", match.group(1))
            text = re.sub(r"\s+", " ", text)
            text = re.sub(r"\s*([(),&*])\s*", r"\1", text).strip()
            self.cache[name] = text
        return self.cache[name]


def compare(win32, x64, undecorate):
    """Mismatch messages between two parsed .def files."""
    problems = []
    if len(win32) != len(x64):
        problems.append("%d exports in the Win32 file, %d in the x64 file" % (len(win32), len(x64)))
    for a, b in zip(win32, x64):
        a_key = (undecorate(a[1]), undecorate(a[2]), a[3], a[4])
        b_key = (undecorate(b[1]), undecorate(b[2]), b[3], b[4])
        if a_key != b_key:
            problems.append(
                "line %d / line %d differ:\n    Win32: %s\n    x64:   %s" % (a[0], b[0], a_key, b_key)
            )
    return problems


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--undname", default="undname", help="path of MSVC's undname.exe")
    parser.add_argument("root", nargs="?", default="src")
    args = parser.parse_args()

    undecorate = Undecorator(args.undname)
    pairs = sorted(pathlib.Path(args.root).rglob("*_x64.def"))
    if not pairs:
        print("check_def_twins: no *_x64.def files under %s" % args.root, file=sys.stderr)
        return 1
    failed = False
    for x64_path in pairs:
        win32_path = x64_path.with_name(x64_path.name[: -len("_x64.def")] + ".def")
        try:
            if not win32_path.exists():
                raise DefError("%s has no Win32 twin %s" % (x64_path, win32_path.name))
            problems = compare(parse_def(win32_path), parse_def(x64_path), undecorate)
        except DefError as error:
            problems = [str(error)]
        if problems:
            failed = True
            print("%s and %s are out of step:" % (win32_path, x64_path.name))
            for problem in problems:
                print("  " + problem)
        else:
            print("%s and %s match (%d exports)" % (win32_path, x64_path.name, len(parse_def(x64_path))))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
