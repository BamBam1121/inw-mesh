# PlatformIO pre-script: make MeshCore's contact and channel saves crash-safe.
#
# DataStore::saveContacts/saveChannels truncate /contacts3 and /channels2 and
# rewrite them in place. A freeze or power loss mid-write leaves a shortened file
# and contacts are gone. This compiles a patched copy instead that writes
# "<name>.tmp" and only swaps it in once the whole file was written.
# src/dataio.cpp picks up a leftover .tmp if the swap itself was interrupted.

import os
import re

Import("env")  # noqa: F821

HELPER = r'''
// --- INW: atomic store writes (added by tools/patch_meshcore.py) ---
static void inwCommit(FILESYSTEM* fs, const char* path, bool ok) {
  char tmp[48];
  snprintf(tmp, sizeof(tmp), "%s.tmp", path);
  if (!ok) { fs->remove(tmp); return; }
  fs->remove(path);
  fs->rename(tmp, path);
}
'''


def patch_function(src, name, path):
    start = src.index("void DataStore::%s(" % name)
    end = src.index("\n}\n", start) + 3
    body = src[start:end]
    body = body.replace('openWrite(_getContactsChannelsFS(), "%s")' % path,
                        'openWrite(_getContactsChannelsFS(), "%s.tmp");\n  bool inw_ok = true' % path, 1)
    body = body.replace("if (!success) break; // write failed",
                        "if (!success) { inw_ok = false; break; } // write failed")
    body = re.sub(r"file\.close\(\);\n  \}\n\}\n$",
                  'file.close();\n    inwCommit(_getContactsChannelsFS(), "%s", inw_ok);\n  }\n}\n' % path, body)
    return src[:start] + body + src[end:]


def patched_datastore(env, node):
    src_path = node.srcnode().get_abspath()
    out_dir = os.path.join(env.subst("$BUILD_DIR"), "inw_patched")
    out_path = os.path.join(out_dir, "DataStore.cpp")
    src = open(src_path, encoding="utf-8").read().replace("\r\n", "\n")
    if "inwCommit" not in src:
        # Insert the helper after the file-local openWrite() definition.
        anchor = src.index("static File openWrite(")
        anchor = src.index("\n}\n", anchor) + 3
        src = src[:anchor] + HELPER + src[anchor:]
        src = patch_function(src, "saveContacts", "/contacts3")
        src = patch_function(src, "saveChannels", "/channels2")
        if src.count("inwCommit(") != 3:
            raise SystemExit("patch_meshcore.py: DataStore.cpp changed upstream, patch did not apply")
    os.makedirs(out_dir, exist_ok=True)
    if not os.path.exists(out_path) or open(out_path, encoding="utf-8").read() != src:
        open(out_path, "w", encoding="utf-8").write(src)
    print("patch_meshcore.py: using crash-safe DataStore.cpp")
    return env.File(out_path)


env.AddBuildMiddleware(patched_datastore, "*/companion_radio/DataStore.cpp")  # noqa: F821
