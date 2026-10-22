#!/usr/bin/env python3
"""Real FLTerm/DomeD integration on private Xvfb, Wayland, D-Bus and control sockets.
Run with uv --no-project --with python-xlib --with pillow python xr/tests/integration.py.
No user display or physical input devices are used.
"""
import json,os,re,signal,socket,subprocess,sys,tempfile,time,urllib.request
from pathlib import Path
from Xlib import X,XK,display
from Xlib.ext import xtest
from PIL import Image
signal.pthread_sigmask(signal.SIG_SETMASK,[])
if os.environ.get('FLTERM_TEST_BUS')!='1':
    os.execvpe('dbus-run-session',['dbus-run-session','--',sys.executable,*sys.argv],{**os.environ,'FLTERM_TEST_BUS':'1'})
root=Path(__file__).resolve().parents[2];out=root/'artifacts/integration';out.mkdir(parents=True,exist_ok=True)
processes=[];logs=[];checks=[];xd=None
(out/'report.json').unlink(missing_ok=True)
def check(ok,name):
    print(('PASS ' if ok else 'FAIL ')+name,flush=True);checks.append({'passed':bool(ok),'check':name});assert ok,name
def wait(fn,timeout=60):
    deadline=time.monotonic()+timeout
    while time.monotonic()<deadline:
        try:
            result=fn()
            if result:return result
        except (OSError,ValueError,KeyError,StopIteration):pass
        time.sleep(.05)
    raise AssertionError('Timed out; see '+str(out))
def start(args,name,env,**kw):
    log=(out/(name+'.log')).open('w');logs.append(log)
    child=subprocess.Popen(args,env=env,stdout=log,stderr=subprocess.STDOUT,cwd=root,**kw);processes.append(child);return child
def ctl(path,message,status=False):
    reply=None
    with socket.socket(socket.AF_UNIX,socket.SOCK_DGRAM) as connection:
        try:
            if status:
                fd,reply=tempfile.mkstemp(prefix='test-',dir=Path(path).parent);os.close(fd);os.unlink(reply);connection.bind(reply);connection.settimeout(2)
            connection.sendto(message.encode(),str(path))
            return json.loads(connection.recv(65536)) if status else None
        finally:
            if reply:os.unlink(reply)
def snapshot(path):return ctl(path,'status',True)
def api(path):
    with urllib.request.urlopen(base+path,timeout=2) as response:return json.load(response)
def windows():return [w for w in api('/api/state')['scene']['windows']if w['app_id']=='org.domed.FLTerm']
def key(name,down=None):
    code=xd.keysym_to_keycode(XK.string_to_keysym(name) if len(name)>1 else ord(name));assert code,name
    for pressed in ([True,False]if down is None else[down]):xtest.fake_input(xd,X.KeyPress if pressed else X.KeyRelease,code);xd.sync();time.sleep(.025)
def chord(mods,name):
    for mod in mods:key(mod,True)
    key(name)
    for mod in reversed(mods):key(mod,False)
def type_text(text):
    for character in text:
        code,level=next(iter(xd.keysym_to_keycodes(ord(character))))
        if level&1:key('Shift_L',True)
        xtest.fake_input(xd,X.KeyPress,code);xtest.fake_input(xd,X.KeyRelease,code);xd.sync()
        if level&1:key('Shift_L',False)
        time.sleep(.015)
def host_image():
    g=host.get_geometry();raw=host.get_image(0,0,g.width,g.height,X.ZPixmap,0xffffffff)
    return Image.frombytes('RGB',(g.width,g.height),raw.data,'raw','BGRX')
try:
 with tempfile.TemporaryDirectory(prefix='flterm-integration-') as directory:
    runtime=Path(directory);runtime.chmod(0o700);config=runtime/'config/dome';config.mkdir(parents=True)
    appearance=config/'appearance.json';appearance.write_text(json.dumps({'font':'Unifont / Sans-Serif','font_size':16,'font_scale':1,'light':False}))
    (config/'keyboard.json').write_text(json.dumps({'ime':False,'layout':'us'}));(runtime/'empty').mkdir()
    env={**os.environ,'XDG_RUNTIME_DIR':directory,'XDG_CONFIG_HOME':str(runtime/'config'),'XDG_DATA_HOME':str(runtime/'empty'),
        'XDG_STATE_HOME':str(runtime/'state'),'XDG_CACHE_HOME':str(runtime/'cache'),'XDG_DATA_DIRS':str(runtime/'empty')+':/usr/local/share:/usr/share','XDG_CURRENT_DESKTOP':'Dome',
        'XR_RUNTIME_JSON':str(runtime/'absent-runtime.json'),'DOME_LOGIN_SESSION':'0','DOME_START_XR':'0','DOME_SESSION_RESTORE':'0',
        'DOME_RENDERER':'gles','DOME_TERMINAL_BACKEND':'flterm','DOME_TERMINAL_MODE':'overlay','DOME_TERMINAL':str(root/'build/flterm-xr'),
        'GSETTINGS_SCHEMA_DIR':'/usr/share/glib-2.0/schemas','GTK_USE_PORTAL':'0','NO_AT_BRIDGE':'1','GIO_USE_VFS':'local',
        'SHELL':'/bin/sh','LIBGL_ALWAYS_SOFTWARE':'1','RUST_LOG':'warn'}
    for name in ['DISPLAY','WAYLAND_DISPLAY','WAYLAND_SOCKET']:env.pop(name,None)
    overlay_path=runtime/'flterm-xr/control.sock';layout=runtime/'overlay-layout.json'
    overlay=start([str(root/'build/flterm-xr'),'--headless','--hidden','--no-keyboard','--layout-file',str(layout)],'overlay',env)
    wait(lambda:overlay_path.exists());initial=wait(lambda:snapshot(overlay_path))
    check(initial['backend']=='FLTerm' and initial['tabs']==1 and not initial['visible'],'Hidden shell starts without a display or OpenXR runtime')
    hidden=runtime/'hidden-output';ctl(overlay_path,"type printf '%s' 'hidden' > "+str(hidden)+'\r')
    check(wait(lambda:hidden.exists() and hidden.read_text()=='hidden'),'Hidden overlay continues running its shell')
    ctl(overlay_path,'type export FLTERM_TAB=one\r');ctl(overlay_path,'tab-new');wait(lambda:snapshot(overlay_path)['tabs']==2)
    second=runtime/'second-tab';ctl(overlay_path,'type printf %s "${FLTERM_TAB-unset}" > '+str(second)+'\r')
    check(wait(lambda:second.exists() and second.read_text()=='unset'),'XR tabs have independent PTY shells')
    ctl(overlay_path,'tab-prev');first=runtime/'first-tab';ctl(overlay_path,'type printf %s "$FLTERM_TAB" > '+str(first)+'\r')
    check(wait(lambda:first.exists() and first.read_text()=='one'),'Switching XR tabs preserves shell state')
    ctl(overlay_path,'position 0.3 -0.2 -1.8');ctl(overlay_path,'size 1.4 0.8');ctl(overlay_path,'opacity 0.3')
    check(wait(lambda:layout.exists() and abs(json.loads(layout.read_text())['width']-1.4)<.01),'XR position and size persist atomically')
    ctl(overlay_path,'quit');overlay.wait(timeout=5)
    overlay=start([str(root/'build/flterm-xr'),'--headless','--hidden','--no-keyboard','--layout-file',str(layout)],'overlay-restart',env)
    check(wait(lambda:overlay_path.exists() and abs(snapshot(overlay_path)['layout']['z']+1.8)<.01),'Restart restores XR layout')
    rd,wr=os.pipe();start(['Xvfb','-displayfd',str(wr),'-screen','0','1600x1000x24','-nolisten','tcp','-noreset'],'xvfb',env,pass_fds=(wr,));os.close(wr)
    with os.fdopen(rd) as pipe:display_name=':'+pipe.readline().strip()
    with socket.socket() as sock:sock.bind(('127.0.0.1',0));port=sock.getsockname()[1]
    base=f'http://127.0.0.1:{port}';env.update({'DISPLAY':display_name,'DOME_PREVIEW_PORT':str(port)})
    compositor=start([os.environ.get('DOME_COMPOSITOR',str(root/'../dome-compositor/target/release/dome-compositor')),'--backend','nested','--terminal'],'compositor',env)
    check(wait(lambda:len(windows())==1),'DomeD starts an independent native FLTK Wayland terminal')
    check(not snapshot(overlay_path)['visible'] and snapshot(overlay_path)['tabs']==1,'Desktop startup leaves the XR overlay untouched')
    xd=display.Display(display_name)
    host=wait(lambda:next((w for w in xd.screen().root.query_tree().children if str(w.get_wm_name()or'').startswith('Dome')),None))
    host.set_input_focus(X.RevertToParent,X.CurrentTime);xd.sync();time.sleep(.4)
    typed=runtime/'typed';type_text('echo domed > '+str(typed));key('Return')
    check(wait(lambda:typed.exists() and typed.read_text().strip()=='domed'),'Real compositor keyboard events reach the FLTerm PTY')
    wayland=re.search(r'Dome Wayland socket: (\S+)',(out/'compositor.log').read_text())[1]
    paste_env={**env,'WAYLAND_DISPLAY':wayland};pasted=runtime/'pasted'
    subprocess.run(['wl-copy','--type','text/plain;charset=utf-8'],input=("printf '%s' 'Žluťoučký 日本語' > "+str(pasted)).encode(),env=paste_env,check=True,timeout=5)
    chord(['Control_L','Shift_L'],'v');key('Return')
    check(wait(lambda:pasted.exists() and pasted.read_text()=='Žluťoučký 日本語'),'FLTK Wayland clipboard paste preserves UTF-8')
    # PRIMARY stays independent of the explicit Copy/Paste clipboard.
    selected='PRIMARY_Žluťoučký'
    primary_socket=next((runtime/'flterm-xr').glob('wayland-*.sock'))
    ctl(primary_socket,"type printf '\\033[2J\\033[H%s' '"+selected+"'\r")
    time.sleep(.3)
    subprocess.run(['wl-copy','--type','text/plain;charset=utf-8'],input=b'CLIPBOARD_SENTINEL',env=paste_env,check=True,timeout=5)
    geometry=windows()[0]['desktop']
    x=int(geometry['x']);y=int(geometry['y'])+34+8
    xtest.fake_input(xd,X.MotionNotify,x=x+1,y=y);xd.sync();time.sleep(.1)
    xtest.fake_input(xd,X.ButtonPress,1);xd.sync();time.sleep(.05)
    xtest.fake_input(xd,X.MotionNotify,x=x+len(selected)*8+1,y=y);xd.sync();time.sleep(.1)
    xtest.fake_input(xd,X.ButtonRelease,1);xd.sync()
    def selection(primary):
        result=subprocess.run(['wl-paste',*(['--primary'] if primary else []),'--no-newline'],env=paste_env,capture_output=True,timeout=2)
        return result.stdout.decode() if result.returncode==0 else None
    check(wait(lambda:selection(True)==selected,timeout=5),'Left-drag selection publishes UTF-8 to Wayland PRIMARY')
    check(selection(False)=='CLIPBOARD_SENTINEL','Primary selection leaves the Copy/Paste clipboard unchanged')
    primary_pasted=runtime/'primary-pasted'
    command="printf '%s' 'PRIMARY_日本語' > "+str(primary_pasted)
    subprocess.run(['wl-copy','--primary','--type','text/plain;charset=utf-8'],input=command.encode(),env=paste_env,check=True,timeout=5)
    time.sleep(.1)
    xtest.fake_input(xd,X.ButtonPress,2);xtest.fake_input(xd,X.ButtonRelease,2);xd.sync();time.sleep(.15);key('Return')
    check(wait(lambda:primary_pasted.exists() and primary_pasted.read_text()=='PRIMARY_日本語'),'Middle-click pastes external UTF-8 PRIMARY into the FLTerm PTY')
    check(selection(False)=='CLIPBOARD_SENTINEL','Middle-click paste keeps the Copy/Paste clipboard independent')
    # Exercise real compositor wheel and keyboard input against a live PTY.
    scroll_ready=runtime/'scroll-ready'
    ctl(primary_socket,"type seq 1 200; printf ready > "+str(scroll_ready)+'\r')
    wait(lambda:scroll_ready.exists() and snapshot(primary_socket)['history_rows']>100)
    geometry=windows()[0]['desktop']
    xtest.fake_input(xd,X.MotionNotify,x=int(geometry['x'])+100,y=int(geometry['y'])+100);xd.sync()
    def wheel(button):
        xtest.fake_input(xd,X.ButtonPress,button);xtest.fake_input(xd,X.ButtonRelease,button);xd.sync()
    wheel(4)
    check(wait(lambda:snapshot(primary_socket)['scroll_offset']>=3),'Mouse wheel up reveals older rows with standard scrolling direction')
    wheel(5)
    check(wait(lambda:snapshot(primary_socket)['scroll_offset']==0),'Mouse wheel down returns to live output')
    chord(['Shift_L'],'Prior')
    check(wait(lambda:snapshot(primary_socket)['scroll_offset']>3),'Shift+PageUp navigates history without sending a terminal key')
    chord(['Shift_L'],'Next')
    check(wait(lambda:snapshot(primary_socket)['scroll_offset']==0),'Shift+PageDown returns toward live output')
    chord(['Shift_L'],'Home')
    check(wait(lambda:snapshot(primary_socket)['scroll_offset']==snapshot(primary_socket)['history_rows']),'Shift+Home jumps to oldest retained output')
    chord(['Shift_L'],'End')
    check(wait(lambda:snapshot(primary_socket)['scroll_offset']==0),'Shift+End returns to live output')
    wheel(4);wait(lambda:snapshot(primary_socket)['scroll_offset']>0)
    type_text(' ')
    check(wait(lambda:snapshot(primary_socket)['scroll_offset']==0),'Typing returns the viewport to the live prompt')
    key('BackSpace')
    chord(['Super_L'],'t');check(wait(lambda:len(windows())==2),'Super+T creates another desktop terminal')
    paths=wait(lambda:list((runtime/'flterm-xr').glob('wayland-*.sock')) if len(list((runtime/'flterm-xr').glob('wayland-*.sock')))==2 else None)
    check(all(snapshot(path)['tabs']==1 for path in paths),'Desktop windows own separate control sockets and tab sets')
    menu=wait(lambda:snapshot(paths[0]) if snapshot(paths[0])['menu_service'] else None)
    layout_text=subprocess.check_output(['gdbus','call','--session','--dest',menu['menu_service'],'--object-path',menu['menu_path'],'--method','com.canonical.dbusmenu.GetLayout','--','0','-1','[]'],text=True,timeout=5)
    check(all(label in layout_text for label in ['New tab','Close tab','Copy','Paste','Show XR terminal']),'FLTK commands are exported as native DomeD universal menus')
    for label,expected in [('New tab',2),('Close tab',1)]:
        item=re.search(r"\((\d+), \{'label': <'"+label+r"'>",layout_text)
        assert item,'Missing exported command: '+label
        subprocess.run(['gdbus','call','--session','--dest',menu['menu_service'],'--object-path',menu['menu_path'],
                        '--method','com.canonical.dbusmenu.Event',item[1],'clicked','<0>','0'],check=True,capture_output=True,text=True,timeout=5)
        check(wait(lambda:snapshot(paths[0])['tabs']==expected),'Universal '+label+' works with the local menu bar hidden')
    chord(['Control_L','Shift_L'],'t')
    check(wait(lambda:sorted(snapshot(path)['tabs'] for path in paths)==[1,2]),'Ctrl+Shift+T adds a tab only to the focused desktop window')
    chord(['Super_L'],'F12');check(wait(lambda:snapshot(overlay_path)['visible']),'Super+F12 shows the existing FLTerm overlay')
    check(snapshot(overlay_path)['tabs']==1,'Overlay show preserves its shell and tab count')
    ctl(overlay_path,'hide');wait(lambda:not snapshot(overlay_path)['visible']);key('F12');time.sleep(.2)
    check(not snapshot(overlay_path)['visible'],'Bare desktop F12 leaves the overlay alone')
    settings=json.loads(appearance.read_text());settings['light']=True;appearance.write_text(json.dumps(settings))
    check(wait(lambda:snapshot(overlay_path)['light'] and all(snapshot(path)['light'] for path in paths)),'DomeD palette changes reach desktop and XR terminals live')
    settings['font_scale']=2;appearance.write_text(json.dumps(settings))
    check(wait(lambda:snapshot(overlay_path)['font_pixels']==32 and all(snapshot(path)['font_pixels']==32 for path in paths)),'Native 2x bitmap fonts propagate without restarting shells')
    check(snapshot(overlay_path)['tabs']==1 and sorted(snapshot(path)['tabs'] for path in paths)==[1,2],'Font changes preserve every desktop and XR tab')
    time.sleep(.6);host_image().save(out/'desktop.png')
    check(compositor.poll() is None and overlay.poll() is None,'Compositor and persistent overlay remain healthy')
finally:
    (out/'report.json').write_text(json.dumps(checks,indent=2)+'\n')
    if xd:xd.close()
    for process in reversed(processes):
        if process.poll() is None:
            process.terminate()
            try:process.wait(timeout=5)
            except subprocess.TimeoutExpired:process.kill();process.wait()
    for log in logs:log.close()
