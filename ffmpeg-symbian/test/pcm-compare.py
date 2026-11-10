import array
import sys

ref_path, our_path, channels = sys.argv[1], sys.argv[2], int(sys.argv[3])
max_abs = int(sys.argv[4]) if len(sys.argv) > 4 else 8

def load(path):
    a = array.array('h')
    with open(path, 'rb') as f:
        a.frombytes(f.read())
    if sys.byteorder != 'little':
        a.byteswap()
    return a

ref, ours = load(ref_path), load(our_path)
n_ref, n_our = len(ref) // channels, len(ours) // channels
window = min(n_ref, n_our, 48000)
best_shift, best_err = 0, None
for shift in range(-4096, 4097, 1):
    r0 = max(0, shift)
    o0 = max(0, -shift)
    count = min(window, n_ref - r0, n_our - o0)
    if count < 1024:
        continue
    step = max(1, count // 2048)
    err = 0
    for i in range(0, count, step):
        err += abs(ref[(r0 + i) * channels] - ours[(o0 + i) * channels])
    if best_err is None or err < best_err:
        best_err, best_shift = err, shift
r0, o0 = max(0, best_shift), max(0, -best_shift)
count = min(n_ref - r0, n_our - o0) * channels
worst, sq = 0, 0
for i in range(count):
    d = ref[r0 * channels + i] - ours[o0 * channels + i]
    if d < 0:
        d = -d
    if d > worst:
        worst = d
    sq += d * d
rms = (sq / count) ** 0.5 if count else 0
print(f'samples ref {n_ref} ours {n_our} (ours - ref = {n_our - n_ref}), shift {best_shift}, max abs diff {worst}, rms {rms:.2f}')
sys.exit(0 if worst <= max_abs else 1)
