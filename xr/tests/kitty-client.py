#!/usr/bin/env python3
"""Raw PTY peer used to verify actual frontend Kitty keyboard events."""
import json,os,re,select,sys,termios,time,tty
from pathlib import Path
output=Path(sys.argv[1]);ready=output.with_suffix('.ready')
previous=termios.tcgetattr(0);tty.setraw(0)
data=b'';flags=int(sys.argv[2]) if len(sys.argv)>2 else 31
try:
    os.write(1,('\x1b[>'+str(flags)+'u\x1b[?u').encode())
    end=time.monotonic()+15
    while time.monotonic()<end:
        if not select.select([0],[],[],.1)[0]:continue
        chunk=os.read(0,4096)
        if not chunk:break
        data+=chunk
        output.write_text(json.dumps({'hex':data.hex()}))
        if ('\x1b[?'+str(flags)+'u').encode() in data:ready.write_text('ready')
        if re.search(rb'\x1b\[27;[0-9]+:3u',data):break
finally:
    os.write(1,b'\x1b[<u')
    termios.tcsetattr(0,termios.TCSANOW,previous)
