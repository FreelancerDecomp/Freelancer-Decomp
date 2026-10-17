# Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
# faithful to the behaviour of the original game.
# This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
# developed by Digital Anvil.
# Author: Giperion.

"""Copy the dev runtime config of bin\\ into a Debug/ASan output dir (bin_debug\\, binasan\\).

*.ini files are copied with absolute bin\\ paths rewritten to the output dir; MPNewCharacter.fl,
NewPlayer.fl and flconfigdatabase.txt (read from beside the exe) are copied verbatim. The output
dirs are siblings of bin\\, so relative paths (..\\DATA, ..\\DATA_EXTRA, ..\\EXE) are kept as is."""

from __future__ import print_function

import os
import shutil
import sys

VERBATIM_FILES = ("mpnewcharacter.fl", "newplayer.fl", "flconfigdatabase.txt")


def main():
    if len(sys.argv) < 2:
        print("usage: copy_dev_ini.py <out_dir>", file=sys.stderr)
        return 1
    out_dir = sys.argv[1].strip().strip('"').rstrip("\\/")
    repo_bin = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "bin"))
    os.makedirs(out_dir, exist_ok=True)
    out_abs = os.path.normpath(os.path.abspath(out_dir)) + os.sep
    bin_abs = os.path.normpath(os.path.abspath(repo_bin)) + os.sep
    copied = 0
    if not os.path.isdir(repo_bin):
        print("copy_dev_ini: missing " + repo_bin, file=sys.stderr)
        return 1
    for name in os.listdir(repo_bin):
        src_path = os.path.join(repo_bin, name)
        if not os.path.isfile(src_path):
            continue
        dest_path = os.path.join(out_dir, name)
        if name.lower().endswith(".ini"):
            with open(src_path, "r", encoding="utf-8", errors="replace") as handle:
                text = handle.read()
            text = text.replace(bin_abs, out_abs)
            text = text.replace(bin_abs.replace("\\", "/"), out_abs.replace("\\", "/"))
            with open(dest_path, "w", encoding="utf-8", newline="\n") as handle:
                handle.write(text)
        elif name.lower() in VERBATIM_FILES:
            shutil.copyfile(src_path, dest_path)
        else:
            continue
        copied += 1
    print("copy_dev_ini: wrote %d files to %s" % (copied, out_abs))
    return 0


if __name__ == "__main__":
    sys.exit(main())
