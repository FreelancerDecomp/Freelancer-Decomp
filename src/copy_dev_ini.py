# Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
# faithful to the behaviour of the original game.
# This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
# developed by Digital Anvil.
# Author: Giperion.

"""Copy the dev runtime config of bin\\ into another output dir (bin_debug\\, binasan\\, bin_x64\\,
bin_x64_debug\\).

*.ini files are copied with absolute bin\\ paths rewritten to the output dir (matched without regard
to case, with either slash); MPNewCharacter.fl, NewPlayer.fl and flconfigdatabase.txt (read from
beside the exe) are copied verbatim. Everything is handled as bytes: the files are in the ANSI code
page (freelancer.ini holds cp1252 bytes such as 0x92) and keep their line endings. The output dirs
are siblings of bin\\, so relative paths (..\\DATA, ..\\DATA_EXTRA, ..\\EXE) are kept as is."""

from __future__ import print_function

import os
import re
import shutil
import sys

VERBATIM_FILES = ("mpnewcharacter.fl", "newplayer.fl", "flconfigdatabase.txt")

# The game reads the ini files in the ANSI code page, so paths are matched and written in it.
PATH_ENCODING = "mbcs" if os.name == "nt" else "utf-8"


def rewrite_paths(data, bin_abs, out_abs):
    """Replace every bin_abs prefix in data (bytes) with out_abs, ignoring case, for \\ and /.

    A bin_abs the ANSI code page cannot spell cannot occur in the file, so data is returned
    unchanged. An out_abs it cannot spell cannot be written either: those paths keep pointing
    at bin\\, with a warning."""
    for sep in (os.sep, "/"):
        try:
            old = bin_abs.replace(os.sep, sep).encode(PATH_ENCODING)
        except UnicodeEncodeError:
            return data
        if not re.search(re.escape(old), data, flags=re.IGNORECASE):
            continue
        try:
            new = out_abs.replace(os.sep, sep).encode(PATH_ENCODING)
        except UnicodeEncodeError:
            print("copy_dev_ini: %s has no %s spelling; its paths are left pointing at bin"
                  % (out_abs, PATH_ENCODING), file=sys.stderr)
            return data
        data = re.sub(re.escape(old), lambda _match: new, data, flags=re.IGNORECASE)
    return data


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
            with open(src_path, "rb") as handle:
                data = rewrite_paths(handle.read(), bin_abs, out_abs)
            # Rewritten before the copy is opened, so a failure cannot leave it truncated.
            with open(dest_path, "wb") as handle:
                handle.write(data)
        elif name.lower() in VERBATIM_FILES:
            shutil.copyfile(src_path, dest_path)
        else:
            continue
        copied += 1
    print("copy_dev_ini: wrote %d files to %s" % (copied, out_abs))
    return 0


if __name__ == "__main__":
    sys.exit(main())
