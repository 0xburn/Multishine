#!/usr/bin/env python3
# Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later
"""Credential/PII scan of the working tree and all git history (stdlib only).

Never prints a matched value: only location (commit, file, line) and pattern type.
Usage: tools/scan_secrets.py [--tree-only] [--history-only] [--pii]
Config: tools/scan_secrets.py reads owner identifiers from the environment variable
SCAN_OWNER_TERMS (comma separated, e.g. a personal name or email) so they are not stored in the repo;
the repo the maintainer's git user.name/user.email are also added automatically.
Exit code 1 if anything is found. Ignores.example and the submodule dirs.
"""
import os, re, subprocess, sys

IGNORE_PATHS = ('docs/research/user.json.example', 'third_party/', 'vendor/', 'tools/scan_secrets.py')
GENERIC_EMAIL_OK = re.compile(r'(@example\.(com|org)|noreply@|@users\.noreply\.github\.com|@localhost)', re.I)
UID = r'[A-Za-z0-9]{28}'
PATTERNS = [
    ('playKey-value', re.compile(r'''["']?playKey["']?\s*[:=]\s*["'][A-Za-z0-9_\-]{8,}["']''')),
    ('uid-28char', re.compile(r'''["']?\buid["']?\s*[:=]\s*["']''' + UID + r'''["']''')),
    ('uid-near-key', re.compile(r'''(?i)(playkey|connectCode)\b.{0,60}\b''' + UID + r'''\b''')),
    ('google-api-key', re.compile(r'AIza[0-9A-Za-z_\-]{35}')),
    ('private-key-block', re.compile(r'-----BEGIN (?:[A-Z]+ )?PRIVATE KEY-----')),
    ('email', re.compile(r'[A-Za-z0-9._%+\-]+@[A-Za-z0-9.\-]+\.[A-Za-z]{2,}')),
    ('refresh/idToken', re.compile(r'''(?i)["']?(refreshToken|idToken|accessToken)["']?\s*[:=]\s*["'][A-Za-z0-9._\-]{20,}["']''')),
    # added by the security review: key material and service secrets of this project
    ('hex-secret-assign', re.compile(r'''(?i)\b\w*(secret|seed|pepper|private|signing_?key|notify_?key)\w*["']?\s*[:=]\s*(bytes\.fromhex\()?["']?[0-9a-f]{32,128}\b''')),
    ('discord-secret', re.compile(r'''(?i)(client_secret|MS_DISCORD_SECRET|discord[_.]?secret)["']?\s*[:=]\s*["']?[A-Za-z0-9_\-]{20,}''')),
    ('discord-bot-token', re.compile(r'\b[MNO][A-Za-z\d_\-]{23,25}\.[\w\-]{6}\.[\w\-]{27,}')),
]
# kinds whose match is never excused by a placeholder word on the line (a real seed next to the word "example" is still a seed)
NO_PLACEHOLDER = {'hex-secret-assign', 'private-key-block', 'discord-bot-token'}
# published test vectors and synthetic fixtures that look like secrets on purpose (value prefix, file): RFC 8032 test 1 (the public DEV key of mscf_sign.py)
KNOWN_PUBLIC = ('9d61b19deffd5a60', 'd75a980182b10ab7')
# tracked FILE NAMES that must never be in git (key files, server state, databases, identities); *.example and test fixtures are exempt
SECRET_NAME = re.compile(r'(^|/)([^/]*\.(key|pem|p12|pfx|seed|sqlite3?|db|db-wal|db-shm)|notify\.key|server\.(keys|pepper|env)|discord\.secret|\.env|MultishineIdentity[^/]*|user\.json)$')
SECRET_NAME_OK = re.compile(r'(\.example$|(^|/)tests?/|(^|/)fx/|^docs/data/melee_records\.sqlite$)')   # melee_records.sqlite = public character/stage record data, no account data
# --pii: also list LAN addresses and MAC addresses (informational, not counted in the exit code: docs name lab addresses on purpose)
PII = [('lan-ip', re.compile(r'\b(?:192\.168|10\.\d{1,3}|172\.(?:1[6-9]|2\d|3[01]))\.\d{1,3}\.\d{1,3}\b')),
       ('mac-address', re.compile(r'\b(?:[0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}\b'))]
# Documented exceptions (file, kind):  quotes the public legal contact address that Slippi LLC publishes in its own
# terms of service (a company role address, not a person's private data); nothing else in that file is exempt from other kinds.
ALLOW = {('docs/research/SLIPPI_TERMS.md', 'email'),
         ('ops/03_SERVER.md', 'email'),                       # the the server project's published apt-key user id ("the server Web Server <...>"), not a person of ours
         ('offline/multishine/menus/on/tests/test_onfile.py', 'hex-secret-assign')}     # a synthetic device secret (0x50 0x51 ...) of the record tests
#: the public copyright line and the public repository URL are meant to be published; they are not findings.
# Stripped from a line before our-term check only (real secret patterns are not affected).
PUBLIC_OK = re.compile(r'(Nigzel\s*/\s*Bjerreman|github\.com/bjerreman/Multishine)', re.I)
PLACEHOLDER = re.compile(r'(?i)(xxx|placeholder|example|your[_-]|<[^>]+>|\.\.\.|0{6,}|fake|dummy|REDACTED)')

def git(*a):
    return subprocess.run(('git',) + a, capture_output=True, text=True, errors='replace').stdout

def owner_terms():
    t = [x.strip() for x in os.environ.get('SCAN_OWNER_TERMS', '').split(',') if x.strip()]
    for k in ('user.name', 'user.email'):
        v = git('config', k).strip()
        if len(v) > 3:
            t.append(v)
    return [(re.compile(re.escape(x), re.I)) for x in set(t)]

def check_line(line, terms, path=None):
    kinds = []
    for name, rx in PATTERNS:
        for m in rx.finditer(line):
            s = m.group(0)
            if name not in NO_PLACEHOLDER and PLACEHOLDER.search(s):
                continue
            if name == 'hex-secret-assign' and any(k in s.lower() for k in KNOWN_PUBLIC):
                continue
            if name == 'email' and GENERIC_EMAIL_OK.search(s):
                continue
            kinds.append(name)
            break
    pub_line = PUBLIC_OK.sub('', line)
    for rx in terms:
        if rx.search(pub_line):
            kinds.append('owner-identifier')
            break
    return [k for k in kinds if (path, k) not in ALLOW]

def scan_names(found):
    for f in git('ls-files', '-z').split('\0'):
        if f and not f.startswith(IGNORE_PATHS) and SECRET_NAME.search(f) and not SECRET_NAME_OK.search(f):
            found.append(('tree', f, 0, 'secret-file-name'))

def scan_pii():
    for f in git('ls-files', '-z').split('\0'):
        if not f or f.startswith(IGNORE_PATHS):
            continue
        try:
            data = open(f, 'rb').read()
        except OSError:
            continue
        if b'\0' in data:
            continue
        for n, line in enumerate(data.decode('utf-8', 'replace').splitlines(), 1):
            for k, rx in PII:
                if rx.search(line):
                    print('%-18s %s:%d  %s' % ('pii (info)', f, n, k))

def scan_tree(terms, found):
    scan_names(found)
    for f in git('ls-files', '-z').split('\0'):
        if not f or f.startswith(IGNORE_PATHS):
            continue
        try:
            data = open(f, 'rb').read()
        except OSError:
            continue
        if b'\0' in data:
            continue
        for n, line in enumerate(data.decode('utf-8', 'replace').splitlines(), 1):
            for k in check_line(line, terms, f):
                found.append(('tree', f, n, k))

def scan_history(terms, found):
    p = subprocess.Popen(['git', 'log', '--all', '-p', '--no-color', '-U0', '--format=@@C %h'],
                         stdout=subprocess.PIPE, text=True, errors='replace')
    commit = file = None
    ln = 0
    seen = set()
    for line in p.stdout:
        if line.startswith('@@C '):
            commit = line.split()[1]
        elif line.startswith('+++ '):
            file = line[6:].strip() if line.startswith('+++ b/') else None
        elif line.startswith('@@ '):
            m = re.search(r'\+(\d+)', line)
            ln = int(m.group(1)) - 1 if m else 0
        elif line.startswith('+') and file and not file.startswith(IGNORE_PATHS):
            ln += 1
            for k in check_line(line[1:], terms, file):
                key = (file, ln, k, commit)
                if key not in seen:
                    seen.add(key)
                    found.append(('history ' + (commit or '?'), file, ln, k))
    p.wait()

def main():
    os.chdir(subprocess.check_output(['git', 'rev-parse', '--show-toplevel'], text=True).strip())
    terms = owner_terms()
    found = []
    if '--history-only' not in sys.argv:
        scan_tree(terms, found)
    if '--tree-only' not in sys.argv:
        scan_history(terms, found)
    if '--pii' in sys.argv:
        scan_pii()
    for where, f, n, k in found:
        print('%-18s %s:%d  %s' % (where, f, n, k))
    print('findings: %d' % len(found))
    return 1 if found else 0

if __name__ == '__main__':
    sys.exit(main())
