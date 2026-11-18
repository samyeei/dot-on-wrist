"""Register the local Chrome bridge without editing Chrome's profile."""
import base64
import hashlib
import json
import os
import shlex
import shutil
import sys
from pathlib import Path

source = Path(__file__).resolve().parent
support = Path.home() / 'Library/Application Support'
control = support / 'ANNA Chrome Extension'
target = support / 'ANNA Chrome Call Helper' / 'target'
registration = support / 'Google/Chrome/NativeMessagingHosts'
name = 'com.samyeei.anna_chrome'

if not target.is_file():
    sys.exit('Missing the existing ANNA target configuration')

control.mkdir(mode=0o700, parents=True, exist_ok=True)
registration.mkdir(mode=0o700, parents=True, exist_ok=True)
manifest = json.loads((source / 'chrome-extension/manifest.json').read_text())
digest = hashlib.sha256(base64.b64decode(manifest['key'])).hexdigest()[:32]
extension_id = digest.translate(str.maketrans('0123456789abcdef', 'abcdefghijklmnop'))

host = control / 'chrome_host.py'
for filename in (host.name, 'inbox.py'):
    shutil.copyfile(source / filename, control / filename)
launcher = control / 'native-host'
temporary = launcher.with_suffix('.tmp')
command = [sys.executable, '-E', '-s', str(host), str(control), str(target)]
temporary.write_text('#!/bin/sh\numask 077\nexec ' + shlex.join(command) + '\n')
os.chmod(temporary, 0o700)
temporary.replace(launcher)
(registration / (name + '.json')).write_text(json.dumps({
    'name': name, 'description': 'ANNA local gateway',
    'path': str(launcher), 'type': 'stdio',
    'allowed_origins': [f'chrome-extension://{extension_id}/'],
}, indent=2) + '\n')
print('Extension folder:', source / 'chrome-extension')
print('Extension ID:', extension_id)
