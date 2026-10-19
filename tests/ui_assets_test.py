"""Independently decode all prepared deltas and compare every source-frame hash."""
from pathlib import Path
import hashlib
import json
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
base = Path(sys.argv[1]) if len(sys.argv)>1 else ROOT / 'filesys'
report = json.loads((base/'assets-report.json').read_text(encoding='utf-8'))
total_frames = 0
for item in report:
    path = Path(item['output'])
    if path.suffix != '.r53a':
        assert path.stat().st_size % 2 == 0
        continue
    data = path.read_bytes()
    magic, w, h, count, colors, duration = struct.unpack_from('<4sHHHHI', data)
    assert magic == b'R53A' and 0 < w <= 320 and 0 < h <= 160 and colors == 256
    assert count == item['frames'] and len(item['frame_sha256']) == count
    canvas = bytearray(w*h)
    offset, elapsed = 528, 0
    for frame in range(count):
        delay, x, y, fw, fh, length = struct.unpack_from('<HHHHHI', data, offset)
        offset += 14
        assert 20 <= delay <= 2000 and x+fw <= w and y+fh <= h
        assert offset+length <= len(data)
        if frame == 0:
            assert (x,y,fw,fh) == (0,0,w,h)
        compressed = data[offset:offset+length]
        offset += length
        pixels = bytearray()
        i = 0
        while i < len(compressed):
            ctrl = compressed[i]
            i += 1
            n = (ctrl & 127)+1
            if ctrl & 128:
                assert i < len(compressed)
                pixels.extend(bytes([compressed[i]])*n)
                i += 1
            else:
                assert i+n <= len(compressed)
                pixels.extend(compressed[i:i+n])
                i += n
            assert len(pixels) <= fw*fh
        assert len(pixels) == fw*fh
        for row in range(fh):
            canvas[(y+row)*w+x:(y+row)*w+x+fw] = pixels[row*fw:(row+1)*fw]
        assert hashlib.sha256(canvas).hexdigest() == item['frame_sha256'][frame], (path, frame)
        elapsed += delay
    assert offset == len(data) and elapsed == duration == item['duration_ms']
    total_frames += count
    print(f'PASS {path.parent.name}/{path.name}: {count} frames, {elapsed} ms, {len(data)} bytes')
assert total_frames > 0
print(f'PASS: all {total_frames} decoded frames exactly match prepared source indices; PCM is sample-aligned')
