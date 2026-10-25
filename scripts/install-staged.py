#!/usr/bin/env python3
"""Install a verified stage atomically; build and stage as the regular user first."""
import argparse, datetime, hashlib, json, os, pathlib, shutil, tempfile
p=argparse.ArgumentParser(description=__doc__);p.add_argument('stage',type=pathlib.Path);a=p.parse_args();stage=a.stage.resolve()
if os.geteuid()!=0:raise SystemExit('System files require sudo: sudo python3 '+__file__+' '+str(stage))
os.umask(0o022)
manifest=json.loads((stage/'manifest.json').read_text())
allowed=('usr/local/bin/flterm-xr','usr/local/share/flterm-xr/','usr/local/share/applications/org.domed.FLTerm','usr/local/share/doc/flterm-xr/','opt/fltk-vr/toolkit/')
for item in manifest:
    name=item['path'];parts=pathlib.PurePosixPath(name).parts
    if '..' in parts or name.startswith('/') or not name.startswith(allowed):raise SystemExit('Invalid destination: '+name)
    source=stage/name
    if 'symlink' in item:
        if '/' in item['symlink'] or os.readlink(source)!=item['symlink']:raise SystemExit('Invalid library alias: '+name)
    elif source.is_symlink() or hashlib.sha256(source.read_bytes()).hexdigest()!=item['sha256']:
        raise SystemExit('Stage checksum mismatch: '+name)
backup=pathlib.Path('/var/backups/de-xr')/('flterm-compat-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S'))
# Keep two privileged installs from interleaving their backups/replacements.
import fcntl
lock=open('/run/lock/flterm-compat-install.lock','w');fcntl.flock(lock,fcntl.LOCK_EX)
for item in manifest:
    source=stage/item['path'];dest=pathlib.Path('/')/item['path'];dest.parent.mkdir(parents=True,exist_ok=True);dest.parent.chmod(0o755)
    if dest.exists() or dest.is_symlink():
        saved=backup/item['path'];saved.parent.mkdir(parents=True,exist_ok=True)
        if dest.is_symlink():saved.symlink_to(os.readlink(dest))
        else:shutil.copy2(dest,saved)
    # Rename a fresh inode over each file; already-running applications keep
    # their existing library mapping. Never truncate a mapped shared library.
    fd,temp=tempfile.mkstemp(prefix='.flterm-install-',dir=dest.parent);os.close(fd)
    try:
        if 'symlink' in item:os.unlink(temp);os.symlink(item['symlink'],temp)
        else:shutil.copyfile(source,temp);os.chmod(temp,item['mode'])
        os.replace(temp,dest)
    finally:
        if os.path.lexists(temp):os.unlink(temp)
print('Installed staged release; previous files:',backup)
print('Newly launched FLTK applications use native PRIMARY. No desktop session was restarted.')
