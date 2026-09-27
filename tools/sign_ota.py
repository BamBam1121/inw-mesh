"""Write ota.json for Wi-Fi updates: version, size, SHA-256 and an Ed25519
signature of that hash.

The signing key comes from the OTA_SIGNING_KEY environment variable (a PEM
private key, kept as a repository secret). Without it no ota.json is written and
pagers simply see no update, so forks and pull requests can't publish one.

usage: python tools/sign_ota.py <firmware.bin> <out ota.json> <version> [notes] [board]

board: for boards after the pager (e.g. t-deck). Adds "sig3", which also names
the board; that firmware requires it, so one board's release can't be taken for
another's. The pager's ota.json is written without it, exactly as before.
"""

import hashlib
import json
import os
import sys

from cryptography.hazmat.primitives import serialization

fw, out, version = sys.argv[1], sys.argv[2], sys.argv[3]
notes = sys.argv[4] if len(sys.argv) > 4 else ""
board = sys.argv[5] if len(sys.argv) > 5 else ""

pem = os.environ.get("OTA_SIGNING_KEY", "").strip()
if not pem:
    print("sign_ota: no OTA_SIGNING_KEY, skipping ota.json")
    sys.exit(0)

data = open(fw, "rb").read()
digest = hashlib.sha256(data).digest()
key = serialization.load_pem_private_key(pem.encode(), password=None)
sig = key.sign(digest)
# "sig" covers only the hash, so an attacker who controls a pager's network could
# relabel an older signed release as a newer version and have it installed. "sig2"
# also binds the version and size; firmware that knows about it requires it, and
# older firmware simply ignores the extra field.
msg2 = b"squatch-ota-v2\n" + digest + version.encode() + b"\n" + str(len(data)).encode()
sig2 = key.sign(msg2)

doc = {
    "version": version,
    "size": len(data),
    "sha256": digest.hex(),
    "sig": sig.hex(),
    "sig2": sig2.hex(),
    "notes": notes[:110],
}
if board:
    msg3 = (b"squatch-ota-v3\n" + board.encode() + b"\n" + digest + version.encode() + b"\n" +
            str(len(data)).encode())
    doc["board"] = board
    doc["sig3"] = key.sign(msg3).hex()
json.dump(doc, open(out, "w"), indent=1)
print("sign_ota: ota.json for %s%s, %d bytes" % (version, " (" + board + ")" if board else "", len(data)))
