import os
import re
import sys

source = open(sys.argv[1]).read()
out = sys.argv[2]
precision = re.search(r'#define FS_PRECISION \\\n((?:\s*".*"\s*\\?\n)+)', source).group(1)

def literal(text):
    return ''.join(bytes(m, 'utf-8').decode('unicode_escape') for m in re.findall(r'"((?:[^"\\]|\\.)*)"', text))

prefix = literal(precision)
for name, kind, body in re.findall(r'const char (K\w+?(Vs|Fs))\[\] =(.*?);\n', source, re.S):
    text = literal(body)
    if 'FS_PRECISION' in body.split('"')[0]:
        text = prefix + text
    with open(os.path.join(out, name + ('.vert' if kind == 'Vs' else '.frag')), 'w') as f:
        f.write(text)
    print(name)
