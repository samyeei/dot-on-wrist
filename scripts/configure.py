#!/usr/bin/env python3
"""Generate local TLS, device pairing, and Chrome bridge configuration."""
import argparse
import getpass
import ipaddress
import json
import os
from pathlib import Path
import re
import secrets
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def write(path, text):
    path.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
    with tempfile.NamedTemporaryFile('w', dir=path.parent, delete=False) as file:
        temporary = Path(file.name)
        file.write(text)
    temporary.replace(path)


def openssl(*arguments):
    subprocess.run(['openssl', *map(str, arguments)], check=True, stdout=subprocess.DEVNULL,
                   stderr=subprocess.DEVNULL)


def gateway_authorization_header(token):
    """HTTP header line for firmware setExtraHeaders. The client appends the CRLF."""
    return 'Authorization: Bearer ' + token.strip()


def gateway_authorization_declaration(token):
    """C declaration written to firmware/src/local_pairing.h."""
    return 'constexpr char gatewayAuthorization[] = ' + json.dumps(gateway_authorization_header(token)) + ';'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', required=True, help='Mac LAN IPv4 address or DNS name')
    parser.add_argument('--private', type=Path, default=ROOT / 'private')
    args = parser.parse_args()
    try:
        san_type = 'IP' if ipaddress.IPv4Address(args.host) else 'DNS'
    except ipaddress.AddressValueError:
        san_type = 'DNS'
        if not re.fullmatch(r'[A-Za-z0-9](?:[A-Za-z0-9.-]{0,251}[A-Za-z0-9])?', args.host):
            parser.error('Provide a LAN IPv4 address or DNS hostname.')
    target = getpass.getpass('Paste your own ChatGPT Dot conversation URL from Chrome (hidden): ').strip()
    if not re.fullmatch(r'https://chatgpt\.com/dots/[a-zA-Z0-9~-]+', target):
        parser.error('Use the exact conversation URL from Chrome.')

    os.umask(0o077)
    directory = args.private.resolve()
    directory.mkdir(parents=True, exist_ok=True, mode=0o700)
    ca, ca_key = directory / 'ca.pem', directory / 'ca.key'
    leaf, leaf_key = directory / 'server.pem', directory / 'server.key'
    if ca.exists() != ca_key.exists():
        raise SystemExit('The local CA is incomplete. Recover it or choose a new private directory.')
    if not ca.exists():
        openssl('req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-sha256', '-days', '3650',
                '-subj', '/CN=Dot on Wrist local CA', '-keyout', ca_key, '-out', ca)
    if not leaf.exists() or not leaf_key.exists() or not (directory / 'host').is_file() or (directory / 'host').read_text().strip() != args.host:
        with tempfile.TemporaryDirectory(dir=directory) as staging:
            path = Path(staging)
            ext = path / 'server.ext'
            ext.write_text(f'basicConstraints=CA:FALSE\nkeyUsage=digitalSignature,keyEncipherment\nextendedKeyUsage=serverAuth\nsubjectAltName={san_type}:{args.host}\n')
            openssl('req', '-newkey', 'rsa:2048', '-nodes', '-subj', '/CN=' + args.host,
                    '-keyout', path/'server.key', '-out', path/'server.csr')
            openssl('x509', '-req', '-in', path/'server.csr', '-CA', ca, '-CAkey', ca_key,
                    '-CAcreateserial', '-out', path/'server.pem', '-days', '365', '-sha256', '-extfile', ext)
            (path/'server.key').replace(leaf_key)
            (path/'server.pem').replace(leaf)
            write(directory/'host', args.host + '\n')
    for name, length in (('token', 32), ('setup-password', 12)):
        if not (directory/name).exists():
            write(directory/name, secrets.token_urlsafe(length) + '\n')

    password = (directory/'setup-password').read_text().strip()
    pairing = ['#pragma once',
               f'constexpr char gatewayHost[] = {json.dumps(args.host)};',
               gateway_authorization_declaration((directory/'token').read_text()),
               f'constexpr char setupPassword[] = {json.dumps(password)};',
               'constexpr char remoteGatewayHost[] = "";',
               'constexpr uint16_t remoteGatewayPort = 8765;',
               'constexpr char gatewayCa[] = R"ANNA_CA(' + ca.read_text().strip() + '\n)ANNA_CA";']
    write(ROOT/'firmware/src/local_pairing.h', '\n'.join(pairing) + '\n')
    write(directory/'native.json', json.dumps({'audio_tools': str(ROOT/'mac/coreaudio'),
                                               'transcriber': sys.executable,
                                               'chrome_calls': True}, indent=2))
    write(Path.home()/'Library/Application Support/ANNA Chrome Call Helper/target', target + '\n')
    subprocess.run([sys.executable, str(ROOT/'mac/install_chrome_host.py')], check=True)
    subprocess.run([sys.executable, str(ROOT/'watchOS/pair.py'), str(directory),
                    f'https://{args.host}:8767'], check=True)
    print('Local pairing is ready. Keep private/ and generated pairing files on this Mac.')


if __name__ == '__main__':
    main()
