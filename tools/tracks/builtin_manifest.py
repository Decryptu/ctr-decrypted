"""The built-in Cortex Vortex package's manifest and pin (platform/native_custom_builtin.cpp).

The game builds this package from assets/tracks/cortex-vortex/ before a store scan. Its manifest is a
constant in the game, so a changed file in that folder needs a new manifest and pin:

    python tools/tracks/builtin_manifest.py           # print them
    python tools/tracks/builtin_manifest.py --check   # exit 1 if the C++ constants differ

The package UUID never changes, so Time Trial records and the Mods screen's off switch carry over.
"""
import hashlib
import json
import os
import sys
import uuid

sys.path.insert(0, os.path.dirname(__file__))
from package import canonical  # noqa: E402

ROOT = os.path.join(os.path.dirname(__file__), '..', '..')
FOLDER = os.path.join(ROOT, 'assets', 'tracks', 'cortex-vortex')
SOURCE = os.path.join(ROOT, 'platform', 'native_custom_builtin.cpp')
VERSION = '1.0.0'
PRESENTATION = json.dumps({'schema_version': 1, 'provider': 'ctr-decrypted', 'author': 'Lockheart'},
                          separators=(',', ':')).encode()
# role, file in the shipped folder, path inside the package
FILES = (('lev', 'CVortex Arcade All.lev', 'original/lev.lev'),
         ('vrm', 'CVortex Arcade All.vrm', 'original/vrm.vrm'),
         ('sca', 'CVortex Arcade All.sca', 'original/sca.sca'),
         ('picture', 'CVortex Picture.ctp', 'metadata/picture.ctp'))


def manifest():
    files = [{'role': 'presentation', 'path': 'metadata/decrypted-source.json',
              'sha256': hashlib.sha256(PRESENTATION).hexdigest(), 'bytes': len(PRESENTATION)}]
    for role, name, path in FILES:
        data = open(os.path.join(FOLDER, name), 'rb').read()
        files.append({'role': role, 'path': path, 'sha256': hashlib.sha256(data).hexdigest(), 'bytes': len(data)})
    body = canonical({'schema_version': 1,
                      'package_uuid': str(uuid.uuid5(uuid.NAMESPACE_URL, 'ctr-decrypted:builtin:cortex-vortex')),
                      'version': VERSION, 'title': 'Cortex Vortex',
                      'compatibility': {'native_contract': 1, 'apworld_contract': 1}, 'files': files})
    return body.decode(), hashlib.sha256(body).hexdigest()


def main():
    body, pin = manifest()
    if '--check' not in sys.argv:
        print(pin)
        print(body)
        return 0
    source = open(SOURCE, encoding='utf-8').read()
    escape = lambda text: text.replace('"', '\\"')  # the manifest holds no backslash
    ok = f'"{pin}"' in source and f'"{escape(body)}"' in source and f'"{escape(PRESENTATION.decode())}"' in source
    print('built-in manifest matches' if ok else 'built-in manifest differs: run this tool and update ' + SOURCE)
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
