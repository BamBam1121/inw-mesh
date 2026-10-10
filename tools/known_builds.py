"""Lists the ELF hashes of our own firmware builds, for the help desk's builds.txt.

A crash report carries the first 16 hex digits of the ELF hash of the build that wrote
the core dump. The help desk mails a crash only when that hash is one of ours
(helpdesk.py report_noise): other firmware on a T-Deck leaves its dumps in the same
partition, and they are not our crashes.

    python tools/known_builds.py scan DIR [DIR...]     every .bin and .elf under the folders
    python tools/known_builds.py github OWNER/REPO     every release's firmware.bin (needs gh and curl)

Prints "hash  where it came from", one a line. After a release: run both, add the new
lines to builds.txt on the help desk's machine (C:\\squatchmesh\\helpdesk\\builds.txt).
No restart is needed; the file is read for each report.

Where the hash is: an app image holds its description 0x20 in (magic ABCD5432), and the
ELF's SHA-256 144 bytes into that, so at 0xB0. A full flash image has the app at 0x10000.
A .elf's hash is the SHA-256 of the file itself.
"""
import hashlib, json, os, subprocess, sys

MAGIC = bytes.fromhex("3254cdab")          # 0xABCD5432, little-endian


def from_image(head, base=0):
    """The hash in an app image whose first bytes are head, or None if it isn't one."""
    if len(head) < base + 0xB0 + 32 or head[base] != 0xE9 or head[base + 0x20:base + 0x24] != MAGIC:
        return None
    return head[base + 0xB0:base + 0xB0 + 8].hex()


def scan(folder):
    for root, _, names in os.walk(folder):
        for n in sorted(names):
            p = os.path.join(root, n)
            low = n.lower()
            if low.endswith(".elf"):
                h = hashlib.sha256()
                with open(p, "rb") as f:
                    for block in iter(lambda: f.read(1 << 20), b""):
                        h.update(block)
                yield h.hexdigest()[:16], p
            elif low.endswith(".bin"):
                with open(p, "rb") as f:
                    head = f.read(0x100)
                    sha = from_image(head)
                    if sha is None:                      # a full image: the app starts at 0x10000
                        f.seek(0x10000)
                        sha = from_image(f.read(0x100))
                if sha:
                    yield sha, p


def github(repo):
    rels = json.loads(subprocess.run(["gh", "release", "list", "--repo", repo, "--limit", "200", "--json", "tagName"],
                                     capture_output=True, text=True, check=True).stdout)
    for r in rels:
        tag = r["tagName"]
        assets = json.loads(subprocess.run(["gh", "release", "view", tag, "--repo", repo, "--json", "assets"],
                                           capture_output=True, text=True, check=True).stdout)["assets"]
        for a in assets:
            if not a["name"].lower().endswith(".bin"):
                continue
            for base in (0, 0x10000):                    # an app image, or a full flash image
                got = subprocess.run(["curl", "-sL", "-r", "%d-%d" % (base, base + 0xFF), a["url"]], capture_output=True).stdout
                sha = from_image(got[:0x100])
                if sha:
                    yield sha, "%s %s %s" % (repo, tag, a["name"])
                    break


if __name__ == "__main__":
    if len(sys.argv) < 3 or sys.argv[1] not in ("scan", "github"):
        sys.exit(__doc__)
    seen = set()
    for arg in sys.argv[2:]:
        for sha, where in (scan(arg) if sys.argv[1] == "scan" else github(arg)):
            if (sha, os.path.basename(where)) not in seen:
                seen.add((sha, os.path.basename(where)))
                print("%s  %s" % (sha, where))
