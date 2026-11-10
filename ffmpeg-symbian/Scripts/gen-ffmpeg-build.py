import os
import re
import subprocess
import sys

src, config, group = (os.path.abspath(a) for a in sys.argv[1:4])
libs = ['libavutil', 'libavcodec', 'libavformat']
out = os.path.join(config, 'n8-src')
os.makedirs(out, exist_ok=True)

float_files = {'amrnbdec.c', 'acelp_filters.c', 'acelp_pitch_delay.c', 'acelp_vectors.c', 'celp_filters.c',
               'celp_math.c', 'lsp.c'}

listing = subprocess.run(['gmake', '-n', 'V=1'] + [f'{lib}/{lib}.a' for lib in libs], cwd=config,
                         capture_output=True, text=True, check=True).stdout
sources = sorted(set(re.findall(r'(src/lib(?:avutil|avcodec|avformat)/[\w/]+\.[cS])$', listing, re.M)))
if not sources:
    sys.exit('gen-ffmpeg-build: make -n listed no sources')

cflags = subprocess.run(['gmake', '-s', '-f', '-', 'print'], cwd=config, capture_output=True, text=True, check=True,
                        input='include ffbuild/config.mak\nprint:\n\t@echo $(CC) $(CPPFLAGS) $(ASFLAGS)\n').stdout.split()

def asm_wrapper(path):
    name = os.path.basename(path)[:-2] + '_s'
    cmd = cflags + ['-I.', '-Isrc', '-x', 'assembler-with-cpp', '-E', '-P', os.path.join(config, path)]
    text = subprocess.run(cmd, cwd=config, capture_output=True, text=True, check=True).stdout
    lines = [l for l in text.splitlines() if l.strip()]
    body = '\n'.join('    "%s\\n"' % l.replace('\\', '\\\\').replace('"', '\\"') for l in lines)
    with open(os.path.join(out, name + '.c'), 'w') as f:
        f.write('__asm__(\n%s\n);\n' % body)
    return name + '.c'

plain, fp, seen_float = {}, {}, set()
for s in sources:
    base = os.path.basename(s)
    if s.endswith('.S'):
        plain.setdefault(out, []).append(asm_wrapper(s))
        continue
    name = s[7:].replace('/', '_')
    with open(os.path.join(out, name), 'w') as f:
        f.write(f'#include "../../../{s[4:]}"\n')
    (fp if base in float_files else plain).setdefault(out, []).append(name)
    if base in float_files:
        seen_float.add(base)

missing = float_files - seen_float
if missing:
    sys.exit(f'gen-ffmpeg-build: float files not in the build: {sorted(missing)}')

def write_mmph(path, table):
    with open(path, 'w') as f:
        for d in sorted(table):
            f.write(f'\nSOURCEPATH      {os.path.relpath(d, group)}\n')
            files = sorted(table[d])
            line = 'SOURCE         '
            for name in files:
                if len(line) + 1 + len(name) > 118:
                    f.write(line + '\n')
                    line = 'SOURCE         '
                line += ' ' + name
            f.write(line + '\n')

write_mmph(os.path.join(group, 'ffmpeg_sources.mmph'), plain)
write_mmph(os.path.join(group, 'ffmpeg_sources_fp.mmph'), fp)
root = os.path.relpath(src, group)
with open(os.path.join(group, 'ffmpeg_paths.mmph'), 'w') as f:
    f.write(f'USERINCLUDE     {os.path.relpath(config, group)}\n')
    f.write(f'USERINCLUDE     {root}\n')
    f.write('USERINCLUDE     /epoc32/include/stdapis\n')
    for d in ('libavutil', 'libavcodec', 'libavformat', 'libavutil/arm', 'libavcodec/arm'):
        f.write(f'USERINCLUDE     {root}/{d}\n')
    f.write(f'SYSTEMINCLUDE   {root}/compat/atomics/dummy\n')
n_plain = sum(len(v) for v in plain.values())
n_fp = sum(len(v) for v in fp.values())
print(f'gen-ffmpeg-build: {n_plain} sources in n8media.lib, {n_fp} in n8mediafp.lib')
