#!/usr/bin/env python3
"""Static checks of build/kernel.elf against SPEC.md section 2 (M0).

Prints one PASS/FAIL line per check and exits 0 only if all pass.
"""
import re
import struct
import subprocess
import sys

MB_MAGIC = 0x1BADB002
MB_SEARCH_LIMIT = 8192
KERNEL_LOAD_MIN = 0x100000
HOST_START_SYMBOLS = {"_init", "_fini", "main", "__libc_start_main",
                      "__libc_csu_init", "_dl_relocate_static_pie"}

failures = []


SHF_ALLOC = 0x2
SHT_NOBITS = 8
PT_LOAD = 1


def parse_elf32(data):
    """Return (sections, loads) parsed straight from ELF32 headers."""
    (e_phoff, e_shoff) = struct.unpack_from("<II", data, 28)
    (e_phentsize, e_phnum, e_shentsize, e_shnum, e_shstrndx) = struct.unpack_from(
        "<HHHHH", data, 42)
    loads = []
    for i in range(e_phnum):
        p_type, p_offset, p_vaddr, p_paddr, p_filesz, p_memsz = struct.unpack_from(
            "<IIIIII", data, e_phoff + i * e_phentsize)
        if p_type == PT_LOAD:
            loads.append(dict(offset=p_offset, vaddr=p_vaddr, paddr=p_paddr,
                              filesz=p_filesz, memsz=p_memsz))
    raw = [struct.unpack_from("<IIIIII", data, e_shoff + i * e_shentsize)
           for i in range(e_shnum)]
    strtab_off = raw[e_shstrndx][4]
    sections = []
    for sh_name, sh_type, sh_flags, sh_addr, sh_offset, sh_size in raw:
        end = data.index(b"\0", strtab_off + sh_name)
        sections.append(dict(name=data[strtab_off + sh_name:end].decode(), type=sh_type,
                             flags=sh_flags, addr=sh_addr, offset=sh_offset, size=sh_size))
    return sections, loads


def uncovered_alloc_sections(sections, loads):
    """Names of non-empty SHF_ALLOC sections not loaded by any PT_LOAD.

    Every such section must lie inside a PT_LOAD memory range. Sections
    with file contents (not SHT_NOBITS) must also lie inside that
    segment's file range at the matching offset. NOBITS (.bss) is judged
    by memory range only.
    """
    bad = []
    for s in sections:
        if not (s["flags"] & SHF_ALLOC) or s["size"] == 0:
            continue
        ok = False
        for l in loads:
            if not (l["vaddr"] <= s["addr"] and s["addr"] + s["size"] <= l["vaddr"] + l["memsz"]):
                continue
            if s["type"] == SHT_NOBITS:
                ok = True
            elif (l["offset"] <= s["offset"]
                  and s["offset"] + s["size"] <= l["offset"] + l["filesz"]
                  and s["addr"] - l["vaddr"] == s["offset"] - l["offset"]):
                ok = True
            if ok:
                break
        if not ok:
            bad.append(s["name"])
    return bad


def run(*cmd):
    return subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                          universal_newlines=True)


def check(name, ok, detail):
    print("%s %s: %s" % ("PASS" if ok else "FAIL", name, detail))
    if not ok:
        failures.append(name)


def header_field(text, field):
    m = re.search(r"^\s*%s:\s*(.+)$" % re.escape(field), text, re.M)
    return m.group(1).strip() if m else ""


def main():
    if len(sys.argv) != 2:
        sys.exit("usage: check_elf.py KERNEL.elf")
    path = sys.argv[1]

    hdr = run("readelf", "-h", "-W", path).stdout
    check("elf-class", header_field(hdr, "Class") == "ELF32", header_field(hdr, "Class"))
    check("elf-data", "little endian" in header_field(hdr, "Data"), header_field(hdr, "Data"))
    check("elf-type", header_field(hdr, "Type").startswith("EXEC"), header_field(hdr, "Type"))
    check("elf-machine", header_field(hdr, "Machine") == "Intel 80386",
          header_field(hdr, "Machine"))

    phdrs = run("readelf", "-l", "-W", path).stdout
    loads = re.findall(r"^\s*LOAD\s+(0x[0-9a-f]+)\s+(0x[0-9a-f]+)\s+(0x[0-9a-f]+)\s+"
                       r"(0x[0-9a-f]+)\s+(0x[0-9a-f]+)\s+(.+?)\s+(0x[0-9a-f]+)$", phdrs, re.M)
    check("no-interp", re.search(r"^\s*INTERP\b", phdrs, re.M) is None,
          "no PT_INTERP program header")
    check("no-dynamic", re.search(r"^\s*DYNAMIC\b", phdrs, re.M) is None,
          "no PT_DYNAMIC program header")
    paddrs = [int(l[2], 16) for l in loads]
    check("load-segments", bool(loads) and min(paddrs) >= KERNEL_LOAD_MIN,
          "%d LOAD, lowest paddr %s" % (len(loads), hex(min(paddrs)) if paddrs else "-"))

    sects = run("readelf", "-S", "-W", path).stdout
    bad_sects = [s for s in (".interp", ".dynamic", ".dynsym", ".got.plt", ".plt",
                             ".rel.dyn", ".init", ".fini")
                 if re.search(r"\s%s\s" % re.escape(s), sects)]
    check("no-dynamic-sections", not bad_sects, "found: %s" % (bad_sects or "none"))
    check("debug-info", re.search(r"\s\.debug_info\s", sects) is not None,
          ".debug_info present")

    undef = run("nm", "-u", path).stdout.split()
    check("no-undefined-symbols", not undef, "nm -u: %s" % (undef or "empty"))

    syms = {}
    for line in run("nm", path).stdout.splitlines():
        parts = line.split()
        if len(parts) == 3:
            syms[parts[2]] = int(parts[0], 16)
    host = sorted(HOST_START_SYMBOLS & set(syms))
    check("no-host-startup-symbols", not host, "found: %s" % (host or "none"))

    entry = int(header_field(hdr, "Entry point address") or "0", 16)
    check("entry-is-_start", "_start" in syms and syms["_start"] == entry,
          "entry %s, _start %s" % (hex(entry), hex(syms["_start"]) if "_start" in syms else "missing"))

    top = syms.get("boot_stack_top")
    check("boot-stack-aligned", top is not None and top % 16 == 0,
          "boot_stack_top %s" % (hex(top) if top is not None else "missing"))
    bs, be = syms.get("__bss_start"), syms.get("__bss_end")
    check("bss-bounds", bs is not None and be is not None and be >= bs and bs % 4 == 0
          and (be - bs) % 4 == 0,
          "__bss_start %s, __bss_end %s" % (hex(bs) if bs is not None else "-",
                                            hex(be) if be is not None else "-"))

    data = open(path, "rb").read()
    mb_off = None
    for off in range(0, min(len(data), MB_SEARCH_LIMIT) - 11, 4):
        magic, flags, checksum = struct.unpack_from("<III", data, off)
        if magic == MB_MAGIC and (magic + flags + checksum) & 0xFFFFFFFF == 0:
            mb_off = off
            break
    in_load = mb_off is not None and any(
        int(l[0], 16) <= mb_off < int(l[0], 16) + int(l[3], 16) for l in loads)
    check("multiboot-header",
          mb_off is not None and in_load,
          "offset %s, flags %s, inside LOAD segment: %s" % (
              hex(mb_off) if mb_off is not None else "not found in first 8 KiB",
              hex(struct.unpack_from("<I", data, mb_off + 4)[0]) if mb_off is not None else "-",
              in_load))

    sections, ph_loads = parse_elf32(data)
    alloc = [x["name"] for x in sections if x["flags"] & SHF_ALLOC and x["size"]]
    bad_alloc = uncovered_alloc_sections(sections, ph_loads)
    check("alloc-sections-loaded", not bad_alloc,
          "%d ALLOC sections %s; not covered by PT_LOAD: %s" % (
              len(alloc), alloc, bad_alloc or "none"))

    gf = run("grub-file", "--is-x86-multiboot", path)
    check("grub-file-multiboot", gf.returncode == 0,
          "grub-file --is-x86-multiboot rc=%d" % gf.returncode)

    print("ELF_CHECK %s (%d failed)" % ("FAIL" if failures else "PASS", len(failures)))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
