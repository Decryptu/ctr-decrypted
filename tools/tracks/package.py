"""Write a custom track package (docs/CUSTOM_TRACKS.md section 3) from a LEV and VRM."""
import hashlib, json, os, shutil, sys, uuid


def canonical(manifest):
    m = dict(manifest)
    m['files'] = sorted(m['files'], key=lambda f: (f['role'], f['path']))
    return json.dumps(m, sort_keys=True, separators=(',', ':'), ensure_ascii=False).encode('utf-8')


def build(lev, vrm, title, key, version, store, author='Decrypt', picture=None):
    """Install into <store>/<pin>/ and return the pin. picture: the Mods screen's, as bytes (track_picture)."""
    files = []
    blobs = {}
    source = json.dumps({'schema_version': 1, 'provider': 'ctr-decrypted', 'author': author},
                        separators=(',', ':'), ensure_ascii=False).encode('utf-8')
    blobs['metadata/decrypted-source.json'] = source
    files.append({'role': 'presentation', 'path': 'metadata/decrypted-source.json',
                  'sha256': hashlib.sha256(source).hexdigest(), 'bytes': len(source)})
    if picture:
        blobs['metadata/picture.ctp'] = picture
        files.append({'role': 'picture', 'path': 'metadata/picture.ctp',
                      'sha256': hashlib.sha256(picture).hexdigest(), 'bytes': len(picture)})
    for role, src in (('lev', lev), ('vrm', vrm)):
        data = open(src, 'rb').read()
        path = f'original/{role}.{role}'
        blobs[path] = data
        files.append({'role': role, 'path': path, 'sha256': hashlib.sha256(data).hexdigest(), 'bytes': len(data)})
    manifest = {
        'schema_version': 1,
        'package_uuid': str(uuid.uuid5(uuid.NAMESPACE_URL, 'ctr-decrypted:converted:' + key)),
        'version': version,
        'title': title,
        'compatibility': {'native_contract': 1, 'apworld_contract': 1},
        'files': files,
    }
    body = canonical(manifest)
    pin = hashlib.sha256(body).hexdigest()
    out = os.path.join(store, pin)
    if os.path.isdir(out):
        shutil.rmtree(out)
    for path, data in blobs.items():
        os.makedirs(os.path.dirname(os.path.join(out, path)), exist_ok=True)
        open(os.path.join(out, path), 'wb').write(data)
    open(os.path.join(out, 'manifest.json'), 'wb').write(body)
    return pin


if __name__ == '__main__':
    lev, vrm, title, key, store = sys.argv[1:6]
    print(build(lev, vrm, title, key, '1.0.0', store))
