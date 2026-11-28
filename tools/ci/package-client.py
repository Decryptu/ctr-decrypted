#!/usr/bin/env python3
"""Verify native artifacts without executing them; package exact build bytes."""
import hashlib
import json
import os
from pathlib import Path
import plistlib
import re
import shutil
import struct
import subprocess
import sys
import tarfile
import tempfile
import zipfile
import zlib


AUTHORING_NOTICE = (
    'BOX AUTHORING BUILD. For placing extra item boxes only.\n'
    'It does not connect to Archipelago. Do not play seeds with it.\n'
    'Unzip it into its own folder, never into your normal CTR Decrypted folder.\n'
    'Read HELP-PLACE-BOXES.md for the steps.\n')

AUTHORING_CLIENT_NOTICE = (
    'AUTHORING CLIENT. Custom tracks, Box Author Mode and the AI lap recorder.\n'
    'It does not connect to Archipelago. Do not play seeds with it.\n'
    'Unzip it into its own folder. Read CUSTOM-TRACKS.md for how it works.\n')


def output(*args):
    return subprocess.check_output(args, text=True, stderr=subprocess.STDOUT)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def require_version_resource(pe):
    """Require a nonempty RT_VERSION (16) in a PE32 executable's resource tree."""
    try:
        pe_header = struct.unpack_from('<I', pe, 0x3c)[0]
        sections = struct.unpack_from('<H', pe, pe_header + 6)[0]
        optional_size = struct.unpack_from('<H', pe, pe_header + 20)[0]
        resource_rva = struct.unpack_from('<I', pe, pe_header + 24 + 96 + 16)[0]
        if not resource_rva:
            raise ValueError('Windows client has no VERSIONINFO resource')

        def file_offset(rva):
            for i in range(sections):
                section = pe_header + 24 + optional_size + 40 * i
                virtual_size, virtual_address, raw_size, raw_offset = struct.unpack_from('<IIII', pe, section + 8)
                if virtual_address <= rva < virtual_address + max(virtual_size, raw_size):
                    offset = raw_offset + rva - virtual_address
                    if offset >= len(pe):
                        break
                    return offset
            raise ValueError('Windows resource RVA is outside the executable')

        base = file_offset(resource_rva)

        def entries(relative):
            directory = base + relative
            named, numbered = struct.unpack_from('<HH', pe, directory + 12)
            if named + numbered > 1024:
                raise ValueError('Windows resource directory is malformed')
            return [struct.unpack_from('<II', pe, directory + 16 + 8 * i)
                    for i in range(named + numbered)]

        for resource_type, child in entries(0):
            if resource_type != 16 or not child & 0x80000000:
                continue
            for _, language_dir in entries(child & 0x7fffffff):
                if not language_dir & 0x80000000:
                    continue
                for _, data_entry in entries(language_dir & 0x7fffffff):
                    if data_entry & 0x80000000:
                        continue
                    data_rva, size = struct.unpack_from('<II', pe, base + data_entry)
                    if size and file_offset(data_rva) + size <= len(pe):
                        return
    except (struct.error, IndexError) as error:
        raise ValueError('Windows resource directory is malformed') from error
    raise ValueError('Windows client has no VERSIONINFO resource')


INTERNAL_TITLE_MARKER = b' | Internal'


def require_release_title(binary):
    """A CI release build must not carry the dev-only window title suffix."""
    if INTERNAL_TITLE_MARKER in binary:
        raise ValueError('Release build window title still contains the Internal dev marker')


# Build targets (tools/release/release_policy.py TARGETS) and the ELF machine
# of each Linux one.
TARGETS = ('windows-x86', 'linux-x86_64', 'linux-arm64', 'macos-arm64')
ELF_MACHINE = {'linux-x86_64': (62, 'x86-64'), 'linux-arm64': (183, 'AArch64')}
# The minimum macOS (CMAKE_OSX_DEPLOYMENT_TARGET in CMakeLists.txt).
MACOS_MINIMUM = (11, 0)
# The app bundle (docs/MACOS.md): a launcher, and the release folder as
# Contents/Resources/game/.
# Each variant is its own app with its own game folder in Application Support
# (CMakeLists.txt, CTR_MACOS_APP_NAME, gives the launcher the same name).
MACOS_APP_NAME = {'ap': 'CTR Decrypted', 'vanilla': 'CTR Decrypted Vanilla',
                  'authoring': 'CTR Decrypted Box Authoring', 'authoring-client': 'CTR Decrypted Authoring Client'}
MACOS_BUNDLE_ID = {'ap': 'io.github.decryptu.ctr-decrypted', 'vanilla': 'io.github.decryptu.ctr-decrypted.vanilla',
                   'authoring': 'io.github.decryptu.ctr-decrypted.box-authoring',
                   'authoring-client': 'io.github.decryptu.ctr-decrypted.authoring-client'}


def macos_game_dir(variant):
    """Where the release folder sits inside the variant's app bundle."""
    return f'{MACOS_APP_NAME[variant]}.app/Contents/Resources/game'


def target_platform(target):
    """The operating system of a build target: windows, linux or macos."""
    return target.split('-', 1)[0]


def debug_sidecar(exe, target):
    """The debug file a build leaves beside its executable: a .debug ELF or
    PE file, or on macOS the dSYM bundle zipped into one file."""
    return Path(str(exe) + ('.dSYM.zip' if target_platform(target) == 'macos' else '.debug'))


def macho(data, name):
    """The parts of a 64-bit little-endian Mach-O file the release gate checks.

    Read here so the assembler checks a macOS build on Linux."""
    if data[:4] != b'\xcf\xfa\xed\xfe':
        raise ValueError(f'Not a 64-bit Mach-O file: {name}')
    cputype, _subtype, filetype, ncmds, _sizeofcmds = struct.unpack_from('<iiIII', data, 4)
    info = {'cputype': cputype, 'filetype': filetype, 'dylibs': [], 'rpaths': 0, 'uuid': None,
            'minos': None, 'segments': set(), 'sections': set(), 'stabs': 0, 'signed': False}
    offset = 32
    for _ in range(ncmds):
        cmd, size = struct.unpack_from('<II', data, offset)
        if cmd in (0xc, 0x80000018, 0x8000001f):  # LC_LOAD_DYLIB, LC_LOAD_WEAK_DYLIB, LC_REEXPORT_DYLIB
            at = offset + struct.unpack_from('<I', data, offset + 8)[0]
            info['dylibs'].append(data[at:data.index(b'\0', at)].decode())
        elif cmd == 0x8000001c:  # LC_RPATH
            info['rpaths'] += 1
        elif cmd == 0x1b:  # LC_UUID
            info['uuid'] = data[offset + 8:offset + 24]
        elif cmd == 0x32:  # LC_BUILD_VERSION
            platform_id, minos = struct.unpack_from('<II', data, offset + 8)
            if platform_id == 1:  # macOS
                info['minos'] = (minos >> 16, (minos >> 8) & 0xff)
        elif cmd == 0x19:  # LC_SEGMENT_64
            segment = data[offset + 8:offset + 24].rstrip(b'\0').decode()
            info['segments'].add(segment)
            nsects, = struct.unpack_from('<I', data, offset + 64)
            for i in range(nsects):
                at = offset + 72 + i * 80
                info['sections'].add((segment, data[at:at + 16].rstrip(b'\0').decode()))
        elif cmd == 0x2:  # LC_SYMTAB
            symoff, nsyms = struct.unpack_from('<II', data, offset + 8)
            # Debugging (stab) entries carry the debug map; dsymutil reads it
            # and strip -S removes it.
            info['stabs'] = sum(1 for i in range(nsyms) if data[symoff + i * 16 + 4] & 0xe0)
        elif cmd == 0x1d:  # LC_CODE_SIGNATURE
            info['signed'] = True
        offset += size
    return info


def verify_macho_program(data, name):
    """An arm64 macOS executable that runs on every supported Mac as it is."""
    info = macho(data, name)
    if info['cputype'] != 0x0100000c or info['filetype'] != 2:
        raise ValueError(f'Expected an arm64 macOS executable: {name}')
    if info['minos'] != MACOS_MINIMUM:
        raise ValueError(f'{name} needs macOS {info["minos"]}, expected {MACOS_MINIMUM}')
    # Only the system's own libraries: nothing a player would have to install.
    foreign = [lib for lib in info['dylibs'] if not lib.startswith(('/usr/lib/', '/System/Library/'))]
    if foreign or info['rpaths']:
        raise ValueError(f'{name} links outside the system: {foreign}')
    if not info['signed']:
        raise ValueError(f'{name} has no code signature (arm64 macOS refuses to run it)')
    return info


def elf_sections(path):
    """Section name -> contents of a little-endian ELF64 file.

    Read here rather than with objdump/objcopy, which only understand the
    host's own architecture: the release check validates the arm64 client on
    an x86_64 runner."""
    data = path.read_bytes()
    if data[:6] != b'\x7fELF\x02\x01':
        raise ValueError(f'Not a little-endian ELF64 file: {path.name}')
    shoff, = struct.unpack_from('<Q', data, 0x28)
    shentsize, shnum, shstrndx = struct.unpack_from('<HHH', data, 0x3a)
    headers = [struct.unpack_from('<IIQQQQ', data, shoff + i * shentsize) for i in range(shnum)]
    names = headers[shstrndx][4]
    sections = {}
    for name, kind, _flags, _addr, offset, size in headers:
        label = data[names + name:data.index(b'\0', names + name)].decode()
        # SHT_NOBITS (8) occupies no file bytes.
        sections[label] = b'' if kind == 8 else data[offset:offset + size]
    return sections


def verify_macos(exe, debug):
    info = verify_macho_program(exe.read_bytes(), exe.name)
    if '__DWARF' in info['segments'] or info['stabs']:
        raise ValueError('Shipped executable still contains debug information')
    member = f'{exe.name}.dSYM/Contents/Resources/DWARF/{exe.name}'
    with zipfile.ZipFile(debug) as bundle:
        if member not in bundle.namelist():
            raise ValueError(f'dSYM sidecar has no {member}')
        dwarf = macho(bundle.read(member), member)
    if ('__DWARF', '__debug_info') not in dwarf['sections']:
        raise ValueError('Sidecar has no DWARF debug information')
    # The UUID ties a dSYM to the exact executable, as the debuglink CRC does
    # on Linux and Windows.
    if info['uuid'] is None or dwarf['uuid'] != info['uuid']:
        raise ValueError('dSYM UUID does not match the executable')
    require_release_title(exe.read_bytes())
    return {'minimum_macos': '.'.join(map(str, MACOS_MINIMUM)), 'linked_libraries': info['dylibs']}


def verify(target, exe, variant):
    if target not in TARGETS:
        raise ValueError(f'Unknown build target: {target}')
    platform = target_platform(target)
    debug = debug_sidecar(exe, target)
    if not exe.is_file() or not debug.is_file():
        raise ValueError('Missing executable or exact debug sidecar')
    if platform == 'macos':
        return verify_macos(exe, debug)
    if platform == 'windows':
        if re.search(r'\s\.debug_\S+', output('objdump', '-h', str(exe))):
            raise ValueError('Shipped executable still contains debug sections')
        if '.debug_info' not in output('objdump', '-h', str(debug)):
            raise ValueError('Sidecar has no DWARF debug information')
        with tempfile.TemporaryDirectory() as tmp:
            section = Path(tmp) / 'debuglink'
            # A scratch output prevents objcopy from modifying the gated binary.
            subprocess.run(['objcopy', '--dump-section', f'.gnu_debuglink={section}',
                            str(exe), str(Path(tmp) / 'copy')], check=True)
            data = section.read_bytes()
    else:
        sections = elf_sections(exe)
        if any(name.startswith('.debug_') for name in sections):
            raise ValueError('Shipped executable still contains debug sections')
        if '.debug_info' not in elf_sections(debug):
            raise ValueError('Sidecar has no DWARF debug information')
        if '.gnu_debuglink' not in sections:
            raise ValueError('Shipped executable has no .gnu_debuglink')
        data = sections['.gnu_debuglink']
    end = data.index(b'\0')
    crc_at = (end + 1 + 3) & ~3
    if data[:end].decode() != debug.name or len(data) != crc_at + 4:
        raise ValueError('Debuglink name/size mismatch')
    if struct.unpack_from('<I', data, crc_at)[0] != zlib.crc32(debug.read_bytes()):
        raise ValueError('Debuglink CRC does not match the exact sidecar')
    require_release_title(exe.read_bytes())
    result = {}
    if platform == 'windows':
        pe = exe.read_bytes()
        if pe[:2] != b'MZ':
            raise ValueError('Not a Windows executable')
        offset = struct.unpack_from('<I', pe, 0x3c)[0]
        if pe[offset:offset + 4] != b'PE\0\0' or struct.unpack_from('<H', pe, offset + 4)[0] != 0x14c:
            raise ValueError('Expected PE32 i386 client')
        if struct.unpack_from('<H', pe, offset + 24)[0] != 0x10b:
            raise ValueError('Expected PE32 optional header')
        flags = struct.unpack_from('<H', pe, offset + 24 + 70)[0]
        # NX is required. ASLR (0x40) is not: the game is linked at a fixed
        # base because savestates and replays hold raw pointers (CMakeLists.txt).
        if flags & 0x100 != 0x100:
            raise ValueError('Windows NX flag missing')
        if variant == 'ap':
            require_version_resource(pe)
        imports = re.findall(r'DLL Name:\s*(\S+)', output('objdump', '-p', str(exe)))
        system = {'advapi32.dll', 'bcrypt.dll', 'cfgmgr32.dll', 'crypt32.dll',
                  'dinput8.dll', 'dwmapi.dll', 'gdi32.dll', 'hid.dll', 'imm32.dll',
                  'kernel32.dll', 'msvcrt.dll', 'ole32.dll', 'oleaut32.dll',
                  'setupapi.dll', 'shell32.dll', 'shlwapi.dll', 'user32.dll',
                  'uuid.dll', 'version.dll', 'winhttp.dll', 'winmm.dll', 'ws2_32.dll'}
        if not imports or any(name.lower() not in system for name in imports):
            raise ValueError(f'Unexpected runtime DLL dependency: {imports}')
        result['imported_dlls'] = imports
        result['aslr_nx'] = True
    else:
        elf = exe.read_bytes()[:20]
        machine, label = ELF_MACHINE[target]
        if elf[:6] != b'\x7fELF\x02\x01' or struct.unpack_from('<H', elf, 18)[0] != machine:
            raise ValueError(f'Expected little-endian ELF64 {label} client')
        versions = re.findall(r'\bGLIBC_(\d+(?:\.\d+)+)', output('readelf', '-V', str(exe)))
        highest = max((tuple(map(int, v.split('.'))) for v in versions), default=(0,))
        if highest > (2, 38):
            raise ValueError(f'glibc requirement exceeds 2.38: {highest}')
        dynamic = output('readelf', '-d', str(exe))
        if '(RPATH)' in dynamic or '(RUNPATH)' in dynamic:
            raise ValueError('Unexpected embedded runtime search path')
        stack = next((s for s in output('readelf', '-W', '-l', str(exe)).splitlines() if 'GNU_STACK' in s), '')
        if not stack or re.search(r'\bRWE\b', stack):
            raise ValueError('Missing or executable GNU_STACK')
        result['maximum_glibc'] = '.'.join(map(str, highest))
        result['needed_libraries'] = re.findall(r'\(NEEDED\).*\[(.*?)\]', dynamic)
    return result


def zip_dsym(exe):
    """The build's <exe>.dSYM bundle as one file, <exe>.dSYM.zip, beside it.
    Fixed timestamps and order, so equal symbols give equal bytes."""
    source = Path(str(exe) + '.dSYM')
    if not source.is_dir():
        raise ValueError(f'Missing {source.name} (dsymutil, CMakeLists.txt)')
    with zipfile.ZipFile(debug_sidecar(exe, 'macos-arm64'), 'w', zipfile.ZIP_DEFLATED) as bundle:
        for file in sorted(path for path in source.rglob('*') if path.is_file()):
            info = zipfile.ZipInfo(f'{source.name}/{file.relative_to(source).as_posix()}', (1980, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o644 << 16
            bundle.writestr(info, file.read_bytes())


def make_app(game, app, variant, launcher):
    """Wrap the release folder `game` into the app bundle `app` (docs/MACOS.md)
    and seal it with an ad-hoc signature. The game executable inside is not
    re-signed: the signature covers it as a resource."""
    name = MACOS_APP_NAME[variant]
    verify_macho_program(launcher.read_bytes(), launcher.name)
    if name.encode() not in launcher.read_bytes():
        raise ValueError(f'The launcher does not install into "{name}" (CMakeLists.txt, CTR_MACOS_APP_NAME)')
    contents = app / 'Contents'
    (contents / 'MacOS').mkdir(parents=True)
    (contents / 'Resources').mkdir()
    shutil.move(str(game), contents / 'Resources' / 'game')
    shutil.copy2(launcher, contents / 'MacOS' / name)
    version = re.search(r'#define CTR_AP_VERSION "v?([^"]+)"', Path('ap/ap_version.h').read_text()).group(1)
    with open(contents / 'Info.plist', 'wb') as plist:
        plistlib.dump({
            'CFBundleDevelopmentRegion': 'en',
            'CFBundleDisplayName': name,
            'CFBundleExecutable': name,
            'CFBundleIconFile': 'AppIcon',
            'CFBundleIdentifier': MACOS_BUNDLE_ID[variant],
            'CFBundleInfoDictionaryVersion': '6.0',
            'CFBundleName': name,
            'CFBundlePackageType': 'APPL',
            'CFBundleShortVersionString': version,
            'CFBundleVersion': version,
            'LSApplicationCategoryType': 'public.app-category.racing-games',
            'LSMinimumSystemVersion': '.'.join(map(str, MACOS_MINIMUM)),
            'NSHighResolutionCapable': True,
        }, plist)
    with tempfile.TemporaryDirectory() as temporary:
        iconset = Path(temporary) / 'AppIcon.iconset'
        iconset.mkdir()
        for size, file in ((16, 'icon_16x16'), (32, 'icon_16x16@2x'), (32, 'icon_32x32'), (64, 'icon_32x32@2x'),
                           (128, 'icon_128x128'), (256, 'icon_128x128@2x'), (256, 'icon_256x256')):
            subprocess.run(['sips', '-z', str(size), str(size), 'platform/icon/icon-256.png',
                            '--out', str(iconset / f'{file}.png')], check=True, stdout=subprocess.DEVNULL)
        subprocess.run(['iconutil', '-c', 'icns', str(iconset), '-o', str(contents / 'Resources' / 'AppIcon.icns')],
                       check=True)
    seal_app(app)


def seal_app(app):
    """Ad-hoc sign the bundle: a quarantined app that is not notarized must be
    signed and sealed for macOS to offer Open Anyway rather than call it
    damaged. Run again after anything inside the bundle changes."""
    subprocess.run(['codesign', '--force', '--sign', '-', '--timestamp=none', str(app)], check=True)
    subprocess.run(['codesign', '--verify', '--strict', '--verbose=2', str(app)], check=True)


def main():
    target, variant, executable, destination = sys.argv[1:]
    if target not in TARGETS or variant not in ('ap', 'vanilla', 'authoring', 'authoring-client'):
        raise ValueError('Invalid target/configuration')
    platform = target_platform(target)
    exe, out = Path(executable), Path(destination)
    if platform == 'macos':
        zip_dsym(exe)
    checks = verify(target, exe, variant)
    commit = output('git', 'rev-parse', 'HEAD').strip()
    if output('git', 'diff', '--name-only', 'HEAD').strip():
        raise ValueError('Tracked source differs from the recorded commit')
    # ctr-decrypted (the player client), ctr-decrypted-box-authoring,
    # ctr-decrypted-vanilla, ctr-decrypted-authoring-client.
    label = {'ap': '', 'authoring': '-box-authoring'}.get(variant, '-' + variant)
    name = f'ctr-decrypted{label}-{target}-{commit[:12]}'
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary) / name
        root.mkdir()
        shutil.copy2(exe, root / exe.name)
        # The box authoring download never connects to a room, so it carries the
        # placement guide instead of the connection example.
        # default_controls.ini is the commented controls.ini template for the
        # Options -> Controls page, which every variant has.
        companions = ('LICENSE', 'THIRD_PARTY_NOTICES.md', 'SETUP.md', 'default_controls.ini')
        companions += ('ap-config.example.txt',) if variant in ('ap', 'vanilla') else ()
        for file in companions:
            shutil.copy2(file, root / file)
        if variant in ('ap', 'authoring', 'authoring-client'):
            # The package library (player client and Track Manager) vendors
            # xdelta3 (Apache 2.0), utf8proc (MIT, Unicode data license) and
            # stb_image (public domain or MIT).
            shutil.copy2('vendor/xdelta3/LICENSE', root / 'LICENSE-xdelta3.txt')
            shutil.copy2('vendor/utf8proc/LICENSE.md', root / 'LICENSE-utf8proc.md')
            shutil.copy2('vendor/stb/LICENSE', root / 'LICENSE-stb.txt')
        if variant == 'authoring':
            shutil.copy2('docs/HELP_PLACE_BOXES.md', root / 'HELP-PLACE-BOXES.md')
        if variant == 'authoring-client':
            shutil.copy2('docs/CUSTOM_TRACKS.md', root / 'CUSTOM-TRACKS.md')
        shutil.copy2('tools/extract-assets/extract_assets.py', root / 'extract_assets.py')
        if variant in ('ap', 'authoring'):
            cortex = root / 'assets' / 'tracks' / 'cortex-vortex'
            cortex.mkdir(parents=True)
            for file in ('CVortex Arcade All.lev', 'CVortex Arcade All.vrm', 'CVortex Arcade All.sca',
                         'CVortex Picture.ctp'):
                source = Path('assets/tracks/cortex-vortex') / file
                shutil.copy2(source, cortex / file)
            if digest(cortex / 'CVortex Arcade All.lev') != '4e3a2daf56c67be3ac645d3bb5375e516c828a0bca24c35ac69b3366c466fe13':
                raise ValueError('Bundled Cortex Vortex LEV hash mismatch')
            if digest(cortex / 'CVortex Arcade All.vrm') != '4131444b9d1d53971befcfd11349efceaf887c20b795c8890fdcb2c36bdff07d':
                raise ValueError('Bundled Cortex Vortex VRM hash mismatch')
            if digest(cortex / 'CVortex Arcade All.sca') != 'ddde81cf1be8042400c717edeeded0d8727e3bb8ba24f0100201708372ef8b1c':
                raise ValueError('Bundled Cortex Vortex music hash mismatch')
        for file in (('support-bundle.bat', 'support-bundle.ps1') if platform == 'windows' else ('support-bundle.sh',)):
            shutil.copy2(file, root / file)
        (root / 'versions.txt').write_text(output('bash', 'tools/release-versions.sh'))
        evidence = {'source_commit': commit, 'platform': target, 'variant': variant,
                    'custom_tracks': variant in ('ap', 'authoring', 'authoring-client'),
                    'authoring': variant == 'authoring',
                    'authoring_client': variant == 'authoring-client',
                    'executable_sha256': digest(exe), 'debug_sha256': digest(debug_sidecar(exe, target)),
                    'vendor_lock_sha256': digest(Path('ap/vendor/versions.lock')),
                    'checks': checks, 'compiler': output('cc' if platform == 'macos' else 'gcc', '--version').splitlines()[0],
                    'cmake': output('cmake', '--version').splitlines()[0],
                    'run_id': os.environ.get('GITHUB_RUN_ID'),
                    'run_attempt': os.environ.get('GITHUB_RUN_ATTEMPT'),
                    'workflow_sha': os.environ.get('GITHUB_WORKFLOW_SHA')}
        evidence['installed_packages'] = {'windows': lambda: output('pacman', '-Q'),
                                          'linux': lambda: output('dpkg-query', '-W'),
                                          'macos': lambda: output('sw_vers') + output('brew', 'list', '--versions')}[platform]()
        (root / 'BUILD.json').write_text(json.dumps(evidence, indent=2) + '\n')
        (root / 'BUILD-NOTICE.txt').write_text(
            'CI build artifact, not a complete tested release. The AP variant includes only the credited Cortex Vortex track pair.\n'
            'No extracted retail game assets are included.\n'
            'Use the matching ctr.apworld from the reviewed release pair.\n'
            'No gameplay acceptance or antivirus clearance is implied.\n')
        if variant == 'authoring':
            (root / 'AUTHORING-BUILD.txt').write_text(AUTHORING_NOTICE)
        if variant == 'authoring-client':
            (root / 'AUTHORING-CLIENT.txt').write_text(AUTHORING_CLIENT_NOTICE)
        archive = out / (name + ('.zip' if platform == 'windows' else '.tar.gz'))
        if platform == 'windows':
            with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as bundle:
                for file in sorted(path for path in root.rglob('*') if path.is_file()):
                    bundle.write(file, f'{name}/{file.relative_to(root).as_posix()}')
        elif platform == 'macos':
            # The archive holds the app; the release folder is inside it.
            top = Path(temporary) / 'archive' / name
            top.mkdir(parents=True)
            make_app(root, top / f'{MACOS_APP_NAME[variant]}.app', variant, exe.parent / 'ctr_macos_launcher')
            with tarfile.open(archive, 'w:gz') as bundle:
                bundle.add(top, arcname=name)
        else:
            with tarfile.open(archive, 'w:gz') as bundle:
                bundle.add(root, arcname=name)
    debug = debug_sidecar(exe, target)
    sidecar = out / debug.name
    shutil.copy2(debug, sidecar)
    for file in (archive, sidecar):
        Path(str(file) + '.sha256').write_text(f'{digest(file)}  {file.name}\n')
    print(json.dumps({key: value for key, value in evidence.items()
                      if key != 'installed_packages'}, indent=2))


if __name__ == '__main__':
    main()
