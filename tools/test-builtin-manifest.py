"""The built-in Cortex Vortex package (platform/native_custom_builtin.cpp) matches the shipped files.

The game builds the package from assets/tracks/cortex-vortex/ with a manifest it holds as a constant. A
changed file there with an old constant would leave the track off the Arcade and Time Trial pages, so the
constant, the pin and the release packaging must name the same files.
"""
import os
import subprocess
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
sys.path.insert(0, os.path.join(ROOT, 'tools', 'tracks'))
import builtin_manifest  # noqa: E402

failures = []
if subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'tracks', 'builtin_manifest.py'), '--check']).returncode:
    failures.append('platform/native_custom_builtin.cpp: manifest or pin differs from the shipped files')
packaging = open(os.path.join(ROOT, 'tools', 'ci', 'package-client.py'), encoding='utf-8').read()
for _, name, _ in builtin_manifest.FILES:
    if f"'{name}'" not in packaging:
        failures.append(f'tools/ci/package-client.py does not ship {name}')
ignore = open(os.path.join(ROOT, '.gitignore'), encoding='utf-8').read()
for _, name, _ in builtin_manifest.FILES:
    if f'!/assets/tracks/cortex-vortex/{name}' not in ignore:
        failures.append(f'.gitignore does not keep {name}')
for failure in failures:
    print('FAIL', failure)
print('builtin manifest: %s' % ('FAIL' if failures else 'OK'))
sys.exit(1 if failures else 0)
