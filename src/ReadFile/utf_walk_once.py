# Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
# faithful to the behaviour of the original game.
# This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
# developed by Digital Anvil.
# Author: Giperion.

# One-off Cycle 3 verifier: mmap retail UTF the same way as src/ReadFile.
# Not part of the build. Run: python utf_walk_once.py

import mmap
import os
import struct
import sys

UTF_MAGIC = 0x20465455
UTF_VERSION = 0x101


class Node:
    def __init__(self, name, is_dir, data_off, data_size):
        self.name = name
        self.is_dir = is_dir
        self.data_off = data_off
        self.data_size = data_size
        self.parent = None
        self.children = []


def name_ok(strings, off):
    if off < 0 or off >= len(strings):
        return False
    return strings.find(b"\x00", off) != -1


def cstr(strings, off):
    if not name_ok(strings, off):
        return ""
    end = strings.find(b"\x00", off)
    return strings[off:end].decode("latin-1", errors="replace")


def add_child(parent, node):
    node.parent = parent
    parent.children.append(node)
    return node


def build_12(parent, tree, strings, first, count, depth):
    if parent is None or depth > 32:
        return
    count = min(count, 4096)
    for i in range(count):
        off = first + i * 12
        if off + 12 > len(tree):
            break
        name_and_dir, data_off, data_size = struct.unpack_from("<III", tree, off)
        is_dir = (name_and_dir >> 31) != 0
        name = cstr(strings, (name_and_dir & 0x7FFFFFFF) + 4)
        node = add_child(parent, Node(name, is_dir, data_off, data_size))
        if is_dir:
            build_12(node, tree, strings, data_off, data_size, depth + 1)


def build_2c(parent, tree, strings, first, depth):
    if parent is None or depth > 32:
        return
    off = first
    guard = 0
    while off != 0 and off + 0x2C <= len(tree) and guard < 4096:
        sibling, name_off, flags = struct.unpack_from("<III", tree, off)
        child_or_data = struct.unpack_from("<I", tree, off + 0x10)[0]
        data_size = struct.unpack_from("<I", tree, off + 0x1C)[0]
        is_dir = (flags & 0x10) != 0
        name = cstr(strings, name_off)
        node = add_child(parent, Node(name, is_dir, child_or_data, data_size))
        if is_dir and child_or_data != 0:
            build_2c(node, tree, strings, child_or_data, depth + 1)
        if sibling == off:
            break
        off = sibling
        guard += 1


def find_child(parent, name):
    for child in parent.children:
        if child.name.lower() == name.lower():
            return child
    return None


def walk(root, path):
    node = root
    cursor = path.lstrip("\\/")
    while cursor:
        slash = cursor.find("\\")
        slash2 = cursor.find("/")
        if slash < 0:
            slash = slash2
        elif slash2 >= 0:
            slash = min(slash, slash2)
        if slash < 0:
            comp, cursor = cursor, ""
        else:
            comp, cursor = cursor[:slash], cursor[slash + 1 :].lstrip("\\/")
        if not comp:
            continue
        if comp in (".", ".."):
            if node.parent is not None:
                node = node.parent
            continue
        found = find_child(node, comp)
        if found is None:
            return None
        node = found
    return node


def parse_utf(path):
    with open(path, "rb") as fh:
        size = os.path.getsize(path)
        if size < 0x38:
            return None, "too small"
        mm = mmap.mmap(fh.fileno(), 0, access=mmap.ACCESS_READ)
        magic, version, tree_off, _, _, node_size, str_off, _, _, data_off = struct.unpack_from(
            "<IIIIIIIIII", mm, 0
        )
        if magic != UTF_MAGIC or version != UTF_VERSION:
            mm.close()
            return None, "not UTF 0x101"
        if node_size not in (0x0C, 0x2C):
            mm.close()
            return None, "bad node size"
        if tree_off >= size or str_off >= size or data_off >= size:
            mm.close()
            return None, "bad offsets"
        tree = mm[tree_off:]
        strings = mm[str_off:]
        root = Node("", True, 0, 0)
        if node_size == 0x0C:
            if len(tree) < 12:
                mm.close()
                return None, "short tree"
            name_and_dir, d_off, d_size = struct.unpack_from("<III", tree, 0)
            root.is_dir = (name_and_dir >> 31) != 0
            root.name = cstr(strings, (name_and_dir & 0x7FFFFFFF) + 4)
            root.data_off = d_off
            root.data_size = d_size
            if root.is_dir:
                build_12(root, tree, strings, d_off, d_size, 0)
        else:
            if len(tree) < 0x2C:
                mm.close()
                return None, "short tree"
            name_off = struct.unpack_from("<I", tree, 4)[0]
            flags = struct.unpack_from("<I", tree, 8)[0]
            child = struct.unpack_from("<I", tree, 0x10)[0]
            d_size = struct.unpack_from("<I", tree, 0x1C)[0]
            root.name = cstr(strings, name_off)
            root.is_dir = (flags & 0x10) != 0
            root.data_off = child
            root.data_size = d_size
            if root.is_dir and child != 0:
                build_2c(root, tree, strings, child, 0)
        info = {
            "node_size": node_size,
            "tree_off": tree_off,
            "str_off": str_off,
            "data_off": data_off,
            "root_name": root.name,
            "kids": [c.name for c in root.children],
            "mm": mm,
        }
        return root, info


def dump_tree(node, prefix, limit, lines):
    if limit <= 0:
        return
    for child in node.children:
        lines.append("%s%s%s" % (prefix, child.name, "/" if child.is_dir else ""))
        dump_tree(child, prefix + "  ", limit - 1, lines)


def find_named(node, want, acc):
    if node.name.lower() == want.lower():
        acc.append(node)
    for child in node.children:
        find_named(child, want, acc)


def path_of(node):
    parts = []
    cursor = node
    while cursor is not None and cursor.parent is not None:
        parts.append(cursor.name)
        cursor = cursor.parent
    parts.reverse()
    return "\\" + "\\".join(parts)


def check(path):
    root, info = parse_utf(path)
    print("FILE", path)
    if root is None:
        print("  FAIL", info)
        return
    print(
        "  header nodeSize=0x%X tree=0x%X str=0x%X data=0x%X root='%s'"
        % (
            info["node_size"],
            info["tree_off"],
            info["str_off"],
            info["data_off"],
            info["root_name"],
        )
    )
    lines = []
    dump_tree(root, "    ", 3, lines)
    for line in lines[:40]:
        print(line)
    if len(lines) > 40:
        print("    ... %d more" % (len(lines) - 40))

    for want in ("Geometry", "VMeshPart", "Sphere"):
        hits = []
        find_named(root, want, hits)
        print("  find '%s': %d" % (want, len(hits)))
        for hit in hits[:4]:
            dest = path_of(hit)
            found = walk(root, dest)
            parent = hit.parent
            leave = walk(root, dest + "\\.")
            print(
                "    walk '%s' -> %s; '.' -> %s"
                % (
                    dest,
                    "ok dir=%s size=%s" % (found.is_dir, found.data_size) if found else "MISS",
                    path_of(leave) if leave else "MISS",
                )
            )
            if parent is not None:
                rel = walk(parent, "\\" + want)
                print(
                    "    relative OpenNode('%s') from '%s' -> %s"
                    % (
                        want,
                        path_of(parent),
                        "ok" if rel is not None else "MISS",
                    )
                )
    info["mm"].close()


def main():
    files = sys.argv[1:]
    if not files:
        files = [
            r"D:\Freelancer\DATA\SHIPS\LIBERTY\LI_ELITE\li_elite.cmp",
            r"D:\Freelancer\DATA\SHIPS\LIBERTY\LI_ELITE\li_elite_shield.3db",
            r"D:\Freelancer\DATA\SHIPS\LIBERTY\LI_FIGHTER\li_fighter.cmp",
        ]
    for path in files:
        check(path)
        print()


if __name__ == "__main__":
    main()
