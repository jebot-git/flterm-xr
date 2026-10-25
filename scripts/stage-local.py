#!/usr/bin/env python3
"""Stage an optimized FLTerm and the shared FLTK PRIMARY fix, without root."""
import argparse, hashlib, json, os, pathlib, shutil, subprocess
root=pathlib.Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('stage',type=pathlib.Path)
p.add_argument('--toolkit',type=pathlib.Path,default=root.parent/'fltk-vr/_toolkit')
a=p.parse_args();stage=a.stage.resolve();stage.mkdir(parents=True,exist_ok=True)
cache=(root/'build/CMakeCache.txt').read_text()
if 'CMAKE_BUILD_TYPE:STRING=Release' not in cache:raise SystemExit('Build Release before staging')
if 'FLTERM_INSTALL_RPATH:STRING=/opt/fltk-vr/toolkit/lib' not in cache:raise SystemExit('Configure -DFLTERM_INSTALL_RPATH=/opt/fltk-vr/toolkit/lib before staging')
subprocess.run(['cmake','--install',str(root/'build')],env={**os.environ,'DESTDIR':str(stage)},check=True)
for directory in ('lib','lib64'):
    dest=stage/'opt/fltk-vr/toolkit'/directory;dest.mkdir(parents=True,exist_ok=True)
    for name in ('fltk','fltk_gl','fltk_images','fltk_cairo'):
        library=f'lib{name}.so.1.4.5'
        source=a.toolkit/'lib'/library
        shutil.copy2(source,dest/library)
        for alias,target in ((f'lib{name}.so.1.4',library),(f'lib{name}.so',f'lib{name}.so.1.4')):
            path=dest/alias;path.unlink(missing_ok=True);path.symlink_to(target)
notices=stage/'opt/fltk-vr/toolkit/share/fltk-bdf-source';notices.mkdir(parents=True,exist_ok=True)
for name in ('COPYING','fltk-1.4.5-primary-selection.patch'):
    source=a.toolkit/'share/fltk-bdf-source'/name
    if name.endswith('.patch'):source=root.parent/'DomeD/native/toolkit-hooks'/name
    shutil.copy2(source,notices/name)
manifest=[]
for base in ('usr','opt'):
    for path in sorted((stage/base).rglob('*')):
        if path.is_symlink():manifest.append({'path':str(path.relative_to(stage)),'symlink':os.readlink(path)})
        elif path.is_file():manifest.append({'path':str(path.relative_to(stage)),'sha256':hashlib.sha256(path.read_bytes()).hexdigest(),'mode':path.stat().st_mode&0o777})
(stage/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
print('Verified manifest:',stage/'manifest.json')
