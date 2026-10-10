"""How many people use Squatch Mesh, from counts.jsonl (helpdesk.py /api/help/count).

    python stats.py            everything so far
    python stats.py 7          the last 7 days

Installs: finished web installs, by board and version. A person who tries twice
counts twice; "people" is distinct visitors (a scrambled address) that got one done.
Devices: distinct devices that checked in, and how many were seen in the last day
and week (a device checks in once a day while it has Wi-Fi).
"""
import collections
import datetime
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
OK = ("ok", "written")


def main():
    days = int(sys.argv[1]) if len(sys.argv) > 1 else None
    since = (datetime.datetime.now() - datetime.timedelta(days=days)) if days else None
    rows = []
    try:
        with open(os.path.join(HERE, "counts.jsonl"), encoding="utf-8") as f:
            for line in f:
                try:
                    r = json.loads(line)
                    r["_at"] = datetime.datetime.strptime(r["at"], "%Y-%m-%d %H:%M:%S")
                except Exception:
                    continue
                if not since or r["_at"] >= since:
                    rows.append(r)
    except OSError:
        pass
    print("Squatch Mesh counts%s" % (" (last %d days)" % days if days else ""))
    for board in ("t-lora-pager", "t-deck"):
        ins = [r for r in rows if r["event"] == "install" and r["board"] == board]
        good = [r for r in ins if r.get("result") in OK]
        # Stopped before anything was written (the page's own results since 2026-10-09):
        # the other device's page, a port that would not open, a device that never answered.
        early = [r for r in ins if r.get("result") in ("wrong-hardware", "port-not-open", "never-reached")]
        by_ver = collections.Counter(r["version"] for r in good)
        print("\n%s" % board)
        print("  web installs done: %d by %d people, %d failed" %
              (len(good), len({r["who"] for r in good}), len(ins) - len(good) - len(early)))
        for v, n in sorted(by_ver.items()):
            print("    %-16s %d" % (v, n))
        if early:
            print("  stopped before writing: " + ", ".join("%d %s" % (n, k) for k, n in
                                                         sorted(collections.Counter(r["result"] for r in early).items())))
        chk = [r for r in rows if r["event"] == "checkin" and r["board"] == board]
        if chk:
            now = datetime.datetime.now()
            last = {}
            ver = {}
            for r in chk:
                if r["device"] not in last or r["_at"] > last[r["device"]]:
                    last[r["device"]] = r["_at"]
                    ver[r["device"]] = r["version"]
            day = sum(1 for t in last.values() if now - t < datetime.timedelta(days=1))
            week = sum(1 for t in last.values() if now - t < datetime.timedelta(days=7))
            print("  devices checking in: %d ever, %d in the last day, %d in the last week" % (len(last), day, week))
            for v, n in sorted(collections.Counter(ver.values()).items()):
                print("    now on %-12s %d" % (v, n))


if __name__ == "__main__":
    main()
