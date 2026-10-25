#!/usr/bin/env python3
"""Kitty graphics negotiation over a real terminal PTY, with pixel size reports."""
import fcntl,json,os,re,select,struct,sys,termios,time,tty
saved=termios.tcgetattr(0);received=b''
try:
    tty.setraw(0)
    os.write(1,b'\x1b_Ga=q,i=31,s=1,v=1,f=24;AAAA\x1b\\'
               b'\x1b_Ga=T,i=32,s=1,v=1,c=4,r=2,C=1;/wAA/w==\x1b\\'
               b'\x1b[16t')
    deadline=time.monotonic()+3
    while time.monotonic()<deadline:
        if select.select([0],[],[],.05)[0]:received+=os.read(0,65536)
        if b'\x1b_Gi=31;OK\x1b\\' in received and b'\x1b_Gi=32;OK\x1b\\' in received and re.search(rb'\x1b\[6;\d+;\d+t',received):break
    assert b'\x1b_Gi=31;OK\x1b\\' in received,repr(received)
    assert b'\x1b_Gi=32;OK\x1b\\' in received,repr(received)
    match=re.search(rb'\x1b\[6;(\d+);(\d+)t',received);assert match,repr(received)
    height,width=map(int,match.groups())
    rows,cols,xpixels,ypixels=struct.unpack('HHHH',fcntl.ioctl(0,termios.TIOCGWINSZ,b'\0'*8))
    assert xpixels==cols*width and ypixels==rows*height,(cols,rows,xpixels,ypixels,width,height)
    with open(sys.argv[1],'w') as output:json.dump({'hex':received.hex(),'cell':[width,height],'pixels':[xpixels,ypixels]},output)
    os.write(1,b'\x1b_Ga=d,d=I,i=32\x1b\\')
finally:
    termios.tcsetattr(0,termios.TCSANOW,saved)
