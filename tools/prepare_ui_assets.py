"""Prepare GIF sources and boot MP3 for Retro53; originals stay untouched.
Run with the ESP-IDF Python. Dependencies may be installed in .asset-tools.
"""
from pathlib import Path
import argparse
import json
import struct
import subprocess
import sys
import hashlib

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / '.asset-tools'))
from PIL import Image, ImageChops, ImageOps, ImageSequence
import imageio_ffmpeg


def pack_indices(data):
    """PackBits variant: high bit = repeat, low 7 bits = count minus one."""
    out = bytearray()
    i = 0
    while i < len(data):
        run = 1
        while i + run < len(data) and run < 128 and data[i+run] == data[i]:
            run += 1
        if run >= 3:
            out.extend((0x80 | (run-1), data[i]))
            i += run
        else:
            start = i
            i += run
            while i < len(data) and i-start < 128:
                if i+2 < len(data) and data[i] == data[i+1] == data[i+2]:
                    break
                i += 1
            out.append(i-start-1)
            out.extend(data[start:i])
    return bytes(out)


def prepare_gif(source, output, size):
    with Image.open(source) as gif:
        frames, delays = [], []
        for frame in ImageSequence.Iterator(gif):
            rgb = Image.new('RGB', gif.size, 'black')
            rgba = frame.convert('RGBA')
            rgb.paste(rgba, mask=rgba.getchannel('A'))
            frames.append(ImageOps.contain(rgb, size, Image.Resampling.LANCZOS))
            delays.append(max(20, min(2000, int(frame.info.get('duration', 100)))))
    w, h = frames[0].size
    # One shared palette avoids frame-by-frame palette flicker. No dithering:
    # contiguous index runs and small changed rectangles compress much better.
    atlas = Image.new('RGB', (64, 48*len(frames)))
    for i, frame in enumerate(frames):
        atlas.paste(frame.resize((64, 48)), (0, 48*i))
    palette = atlas.quantize(colors=128)
    pal = palette.getpalette()
    pal += [0] * (768-len(pal))
    output.parent.mkdir(parents=True, exist_ok=True)
    frame_hashes = []
    with output.open('wb') as f:
        f.write(struct.pack('<4sHHHHI', b'R53A', w, h, len(frames), 256, sum(delays)))
        for i in range(256):
            r, g, b = pal[i*3:i*3+3]
            f.write(struct.pack('>H', ((r>>3)<<11) | ((g>>2)<<5) | (b>>3)))
        previous = None
        for frame, delay in zip(frames, delays):
            indexed = frame.quantize(palette=palette, dither=Image.Dither.NONE)
            frame_hashes.append(hashlib.sha256(indexed.tobytes()).hexdigest())
            box = (0, 0, w, h) if previous is None else ImageChops.difference(indexed.convert('RGB'), previous.convert('RGB')).getbbox()
            if box:
                x, y, right, bottom = box
                packed = pack_indices(indexed.crop(box).tobytes())
                f.write(struct.pack('<HHHHHI', delay, x, y, right-x, bottom-y, len(packed)))
                f.write(packed)
            else:
                f.write(struct.pack('<HHHHHI', delay, 0, 0, 0, 0, 0))
            previous = indexed
    preview = ROOT / 'build' / 'ui-preview'
    preview.mkdir(parents=True, exist_ok=True)
    frames[0].quantize(palette=palette, dither=Image.Dither.NONE).convert('RGB').save(preview / (source.parent.name + '-' + source.stem + '.png'))
    return dict(source=str(source), output=str(output), width=w, height=h,
                frames=len(frames), duration_ms=sum(delays), bytes=output.stat().st_size,
                frame_sha256=frame_hashes)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--root', type=Path, default=ROOT / 'filesys')
    args = parser.parse_args()
    base = args.root
    report = []
    for source in sorted(list(base.rglob('*.gif')) + list(base.rglob('cover.png'))):
        size = (320, 144) if source.name == 'inicio.gif' else (112, 84) if source.name == 'cover.png' else (192, 144)
        report.append(prepare_gif(source, source.with_suffix('.r53a'), size))
    mp3 = base / 'inicio.mp3'
    if mp3.exists():
        pcm = mp3.with_suffix('.pcm')
        subprocess.run([imageio_ffmpeg.get_ffmpeg_exe(), '-hide_banner', '-loglevel', 'error',
                        '-y', '-i', str(mp3), '-vn', '-ac', '1', '-ar', '22050',
                        '-af', 'volume=0.5', '-t', '10', '-f', 's16le', str(pcm)], check=True)
        report.append(dict(output=str(pcm), bytes=pcm.stat().st_size,
                           duration_ms=pcm.stat().st_size*1000//44100))
    (base / 'assets-report.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps([{k:v for k,v in item.items() if k != 'frame_sha256'} for item in report], indent=2))


if __name__ == '__main__':
    main()
