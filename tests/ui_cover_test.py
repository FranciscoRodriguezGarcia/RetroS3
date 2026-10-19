"""Exercise the PNG cover path without adding test images to filesys."""
from pathlib import Path
import subprocess
import sys
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'.asset-tools'))
sys.path.insert(0,str(ROOT/'tools'))
from PIL import Image
import prepare_ui_assets

base=ROOT/'build'/'ui-cover-test'
folder=base/'GameBoy'/'Test'
folder.mkdir(parents=True,exist_ok=True)
image=Image.new('RGB',(240,320),'navy')
image.paste('gold',(40,30,200,290))
image.save(folder/'cover.png')
subprocess.run([sys.executable,str(ROOT/'tools/prepare_ui_assets.py'),'--root',str(base)],check=True)
subprocess.run([sys.executable,str(ROOT/'tests/ui_assets_test.py'),str(base)],check=True)
