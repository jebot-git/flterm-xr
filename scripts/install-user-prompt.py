#!/usr/bin/env python3
"""Enable the de-xr bitmap prompt for this user's future interactive shells."""
import datetime, os, pathlib, shutil
root=pathlib.Path(__file__).resolve().parents[1]
home=pathlib.Path.home()
installed=pathlib.Path('/usr/local/share/flterm-xr/shell/prompt.bash')
script=installed if installed.is_file() else root/'xr/shell/prompt.bash'
def quote(path):return "'"+str(path).replace("'", "'\\''")+"'"
line=("if [[ -r "+quote(installed)+" ]]; then\n    source "+quote(installed)+
      "\nelse\n    source "+quote(root/'xr/shell/prompt.bash')+"\nfi\n")
rc=home/'.bashrc'
text=rc.read_text() if rc.exists() else ''
if '.bashrc.d/' in text or '~/.bashrc.d' in text:
    dest=home/'.bashrc.d/70-domed-prompt.bash';dest.parent.mkdir(exist_ok=True)
    if dest.exists() and dest.read_text()!=line:
        backups=pathlib.Path(os.environ.get('XDG_STATE_HOME',str(home/'.local/state')))/'de-xr/prompt-backups'
        backups.mkdir(parents=True,exist_ok=True)
        shutil.copy2(dest,backups/(dest.name+'-'+datetime.datetime.now().strftime('%Y%m%d%H%M%S')))
    dest.write_text(line)
else:
    marker='# de-xr bitmap prompt\n'
    if marker not in text:
        if rc.exists():shutil.copy2(rc,str(rc)+'.backup-'+datetime.datetime.now().strftime('%Y%m%d%H%M%S'))
        rc.write_text(text+'\n'+marker+line)
    dest=rc
print('Enabled de-xr prompt:',dest)
