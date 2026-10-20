#!/usr/bin/env python3
"""Copy every animation on the sign to this PC, or upload such a copy to a sign
(another one, or this one after a reflash).

    python.exe tools/anim_backup.py                     # sign on a USB cable -> tools/gfx/backup/<date-time>/
    python.exe tools/anim_backup.py COM19 --to D:\\anims
    python.exe tools/anim_backup.py http://192.168.4.1  # over the sign's WiFi
    python.exe tools/anim_backup.py --restore tools/gfx/backup/<date-time>             # to the sign on a USB cable
    python.exe tools/anim_backup.py --restore <folder> http://192.168.4.1 --replace

A backup folder holds one <name>.anm per animation (the same files the sign's
page and the studio upload) and index.json (what the sign listed, plus a CRC32
per file). A restore uploads every .anm in the folder under its file name and
skips names the sign already has unless --replace is given. Masked clips need
the sign's firmware from 2026-10-20 or later.
"""
import argparse, datetime, json, os, re, sys, urllib.request, zlib

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import anim_convert as ac


def resolve(target):
    """'usb' / 'auto' -> the first sign's COM port; COM ports and URLs as given."""
    if target in ("", "usb", "auto"):
        ports = ac.sign_ports()
        if not ports:
            sys.exit("no sign found on a USB port")
        return ports[0][0]
    return target


def backup(target, out):
    lst = ac.sign_list(target)
    if "error" in lst:
        sys.exit(lst["error"])
    anims = lst["anims"]
    if not anims:
        sys.exit("no animations on the sign")
    os.makedirs(out, exist_ok=True)
    print(f"{len(anims)} animation(s), {sum(a['bytes'] for a in anims) // 1024} KB, from {target} to {out}")
    s = None
    if target.startswith("http"):
        get = lambda a: urllib.request.urlopen(f"{target.rstrip('/')}/api/anims/{a['id']}.anm", timeout=60).read()
    else:
        s = ac.serial_link(target)
        get = lambda a: ac.fetch_serial(s, a["id"], a["bytes"], lambda d, t: ac.print_progress("read", d, t, "bytes"))
    index = []
    try:
        for a in anims:
            data = get(a)
            if len(data) != a["bytes"] or data[:3] != b"SAN":
                sys.exit(f"{a['id']}: got {len(data)} of {a['bytes']} bytes, stopping")
            with open(os.path.join(out, a["id"] + ".anm"), "wb") as f:
                f.write(data)
            index.append(dict(a, crc32=f"{zlib.crc32(data) & 0xFFFFFFFF:08x}"))
            print(f"  {a['id']}: {len(data) // 1024} KB")
    finally:
        if s:
            s.close()
    with open(os.path.join(out, "index.json"), "w") as f:
        json.dump({"from": target, "at": datetime.datetime.now().isoformat(timespec="seconds"), "anims": index}, f, indent=1)
    print(f"saved {len(index)} animation(s) in {out}")


def restore(folder, target, replace):
    files = sorted(f for f in os.listdir(folder) if f.lower().endswith(".anm"))
    if not files:
        sys.exit(f"no .anm files in {folder}")
    crcs = {}
    try:
        with open(os.path.join(folder, "index.json")) as f:
            crcs = {a["id"]: a["crc32"] for a in json.load(f)["anims"]}
    except (OSError, ValueError, KeyError):
        pass
    lst = ac.sign_list(target)
    if "error" in lst:
        sys.exit(lst["error"])
    if target.startswith("http"):   # animTotal and #ANIM SPACE came with masked clips
        new_fw = "animTotal" in json.loads(urllib.request.urlopen(target.rstrip("/") + "/api/state", timeout=10).read())
    else:
        new_fw = "total" in lst
    if not new_fw:
        sys.exit("the sign's firmware is older than 2026-10-20 and cannot play masked clips: flash the current firmware first")
    have = {a["id"]: a["bytes"] for a in lst["anims"]}
    plan = []
    for fn in files:
        name = re.sub(r"[^a-z0-9_-]+", "-", os.path.splitext(fn)[0].lower()).strip("-")[:24] or "anim"
        with open(os.path.join(folder, fn), "rb") as f:
            data = f.read()
        if name in crcs and f"{zlib.crc32(data) & 0xFFFFFFFF:08x}" != crcs[name]:
            print(f"  {name}: does not match the checksum in index.json, skipped")
        elif name in have and not replace:
            print(f"  {name}: the sign already has one, skipped (--replace overwrites it)")
        else:
            plan.append((name, data))
    if not plan:
        sys.exit("nothing to upload")
    need = sum(len(d) + 8192 for _, d in plan) - sum(have.get(n, 0) for n, _ in plan)
    print(f"{len(plan)} to upload, {sum(len(d) for _, d in plan) // 1024} KB; {lst['free'] // 1024} KB free on the sign")
    if need > lst["free"]:
        print(f"note: about {(need - lst['free']) // 1024} KB short, the last ones will not fit")
    failed = []
    for name, data in plan:
        res = ac.upload_to(target, name, data, ac.print_progress)
        if "error" in res:
            print(f"  {name}: upload failed ({res['error']})")
            failed.append(name)
        else:
            print(f"  {name}: {len(data) // 1024} KB uploaded, {res.get('free', 0) // 1024} KB free")
    if failed:
        sys.exit(f"{len(failed)} not uploaded: {', '.join(failed)}")
    print(f"uploaded {len(plan)} animation(s)")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("sign", nargs="?", default="usb", help="usb (first sign on a cable, the default), a COM port, or http://192.168.4.1 (WiFi)")
    ap.add_argument("--to", help="backup folder (default tools/gfx/backup/<date-time>)")
    ap.add_argument("--restore", metavar="FOLDER", help="upload the .anm files in FOLDER to the sign instead")
    ap.add_argument("--replace", action="store_true", help="restore: overwrite animations the sign already has under the same name")
    a = ap.parse_args()
    target = resolve(a.sign)
    if a.restore:
        restore(a.restore, target, a.replace)
    else:
        backup(target, a.to or os.path.join(HERE, "gfx", "backup", datetime.datetime.now().strftime("%Y%m%d-%H%M%S")))


if __name__ == "__main__":
    main()
