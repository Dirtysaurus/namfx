#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Translation helper.

  tools/i18n.py            regenerate web/lang/en.json (all keys + English text)
                           and report missing / unused keys in the other languages
  tools/i18n.py --check    report only (exit code 1 if a language misses keys)

Adding a language: copy web/lang/en.json to web/lang/<code>.json, set "_language"
to the language's own name and translate the values. Keys that are missing or empty
fall back to English. "fmt.decimal" sets the decimal separator.
"""
import json, re, subprocess, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
LANG = ROOT / 'web' / 'lang'


def unescape(s):
    return s.encode('utf-8').decode('unicode_escape').encode('latin-1').decode('utf-8')


def collect():
    keys = {}
    # C++: T("key", "English")
    for f in (ROOT / 'src').rglob('*.cpp'):
        txt = f.read_text(encoding='utf-8')
        for k, v in re.findall(r'\bT\("([A-Za-z0-9_.]+)",\s*"((?:[^"\\]|\\.)*)"\)', txt):
            keys.setdefault(k, unescape(v))
        # error codes table in engine.cpp: {"err.x", "Text"}
        for k, v in re.findall(r'\{"(err\.[a-z_]+)",\s*"((?:[^"\\]|\\.)*)"\}', txt):
            keys.setdefault(k, v)
        for k, v in re.findall(r'"code":"(err\.[a-z_]+)","error":"([^"]+)"', txt):
            keys.setdefault(k, v)
        # category names in screens.cpp
        if f.name == 'screens.cpp':
            for k, v in re.findall(r'\{"([a-z]+)", "([^"]+)"\}', txt.split('kCats =')[1].split('};')[0]):
                keys.setdefault('cat.' + k, v)
    # JS: t('key', 'English')
    js = (ROOT / 'web' / 'app.js').read_text(encoding='utf-8')
    js = '\n'.join(l for l in js.splitlines() if not l.lstrip().startswith('//'))
    for k, v in re.findall(r"\bt\('([A-Za-z0-9_.]+)',\s*'((?:[^'\\]|\\.)*)'", js):
        keys.setdefault(k, v.replace("\\'", "'"))
    for k, key, en in re.findall(r"\['(\w+)', '(files\.\w+)', '([^']+)'", js):
        keys.setdefault(key, en)
    for k, en in re.findall(r"(\w+): \['#[0-9A-F]{6}', '([^']+)'\]", js):
        keys.setdefault('cat.' + k, en)
    # HTML: data-i18n / data-i18n-attr
    html = (ROOT / 'web' / 'index.html').read_text(encoding='utf-8')
    for key, text in re.findall(r'data-i18n="([^"]+)">([^<]*)<', html):
        keys.setdefault(key, text)
    for m in re.finditer(r'<[^>]*data-i18n-attr="([\w-]+):([^"]+)"[^>]*>', html):
        attr, key = m.group(1), m.group(2)
        a = re.search(attr + r'="([^"]*)"', m.group(0))
        if a: keys.setdefault(key, a.group(1))
    # short legend names, note names
    for k, v in {'dyn': 'Dynamics', 'drive': 'Drive', 'amp': 'Amp', 'cab': 'Cab', 'eq': 'EQ / filter', 'mod': 'Modulation', 'dly': 'Delay', 'rev': 'Reverb'}.items():
        keys.setdefault(f'cat.{k}.short', v)
    for n in ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B']:
        keys.setdefault('note.' + n, n)
    keys.setdefault('fmt.decimal', '.')
    # keys built at runtime (source filter, themes)
    for k, v in {'src.0': 'All', 'src.1': 'Built-in', 'src.2': 'My types', 'src.3': 'LV2',
                 'theme.dark': 'Dark', 'theme.light': 'Light', 'theme.contrast': 'High contrast', 'theme.retro': 'Retro amber'}.items():
        keys.setdefault(k, v)
    keys.setdefault('err.generic', 'Error')
    # block catalog (from the binary)
    exe = ROOT / 'build' / 'namfx'
    if exe.exists():
        cat = json.loads(subprocess.run([str(exe), '--no-lv2', '--fx', str(ROOT / 'fx'), '--data', '/tmp/namfx-i18n-data', '--print-catalog'],
                                        capture_output=True, text=True, check=True, cwd=ROOT).stdout)
        for d in cat:
            if d.get('source') not in ('builtin', 'fx') or d.get('user'):  # LV2 / user types are not translated here
                continue
            t = d['type']
            keys[f'fx.{t}'] = d['name']
            keys[f'fx.{t}.desc'] = d['desc']
            for p in d['params']:
                keys[f'p.{t}.{p["id"]}'] = p['name']
                for i, o in enumerate(p.get('options', [])):
                    keys[f'o.{t}.{p["id"]}.{i}'] = o
    else:
        print('warning: build/namfx not found, catalog keys taken from existing en.json', file=sys.stderr)
        old = json.loads((LANG / 'en.json').read_text(encoding='utf-8'))
        for k, v in old.items():
            if k.split('.')[0] in ('fx', 'p', 'o'): keys[k] = v
    return keys


def main():
    check = '--check' in sys.argv
    keys = collect()
    en = {'_language': 'English', **dict(sorted(keys.items()))}
    if not check:
        LANG.mkdir(parents=True, exist_ok=True)
        (LANG / 'en.json').write_text(json.dumps(en, ensure_ascii=False, indent=1) + '\n', encoding='utf-8')
        print(f'en.json: {len(keys)} keys')
    bad = False
    for f in sorted(LANG.glob('*.json')):
        if f.stem == 'en': continue
        d = json.loads(f.read_text(encoding='utf-8'))
        missing = [k for k in keys if not d.get(k)]
        unused = [k for k in d if k not in keys and not k.startswith('_')]
        print(f'{f.name}: {len(keys) - len(missing)}/{len(keys)} translated' + (f', missing {len(missing)}' if missing else '') + (f', unused {len(unused)}' if unused else ''))
        for k in missing: print('  missing:', k, '=', repr(keys[k]))
        for k in unused: print('  unused: ', k)
        bad |= bool(missing)
    sys.exit(1 if (check and bad) else 0)


if __name__ == '__main__':
    main()
