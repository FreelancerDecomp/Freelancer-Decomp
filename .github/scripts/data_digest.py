"""Print the file count and one SHA-256 digest of a game DATA folder.

The digest covers every file's relative path (forward slashes, case as stored) and its
contents, in a fixed order, so the same extraction gives the same digest on any OS.
Usage: python data_digest.py <DATA dir>
"""
import hashlib
import os
import sys


def main() -> int:
    root = sys.argv[1]
    entries = []
    for dirpath, _, filenames in os.walk(root):
        for name in filenames:
            full = os.path.join(dirpath, name)
            entries.append((os.path.relpath(full, root).replace(os.sep, "/"), full))
    entries.sort()

    total = hashlib.sha256()
    for rel, full in entries:
        with open(full, "rb") as f:
            content = hashlib.sha256(f.read()).hexdigest()
        total.update(f"{rel}\0{content}\n".encode("utf-8"))

    print(f"{len(entries)} {total.hexdigest()}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
