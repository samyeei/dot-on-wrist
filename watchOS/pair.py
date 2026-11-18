"""Build a private pairing resource without printing the URL or credential."""
import argparse
import hashlib
import json
import os
import secrets
import ssl
import tempfile
from pathlib import Path
from urllib.parse import urlsplit

ROOT = Path(__file__).resolve().parent


def write_private(path, text):
    with tempfile.NamedTemporaryFile('w', dir=path.parent, delete=False) as file:
        temporary = Path(file.name)
        file.write(text)
    try:
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('private', type=Path, help='Mac gateway private configuration directory')
    parser.add_argument('url', help='HTTPS origin reachable from the watch')
    args = parser.parse_args()
    url = urlsplit(args.url)
    if (url.scheme != 'https' or not url.hostname or url.username or url.password
            or url.path not in ('', '/') or url.query or url.fragment):
        parser.error('Use an HTTPS origin without a path or credentials.')

    token_file = args.private / 'apple-watch-token'
    if not token_file.exists():
        write_private(token_file, secrets.token_urlsafe(32) + '\n')
    token = token_file.read_text().strip()
    if len(token) < 32:
        parser.error('Apple Watch pairing token is too short.')
    cert = ssl.PEM_cert_to_DER_cert((args.private / 'server.pem').read_text())
    fingerprint = hashlib.sha256(cert).hexdigest()
    write_private(ROOT / 'ANNA' / 'Pairing.json',
                  json.dumps({'url': args.url, 'token': token, 'certificateSHA256': fingerprint}))
    print('Apple Watch pairing resource created.')


if __name__ == '__main__':
    main()
