#!/usr/bin/env python3
"""Native FLTerm OpenXR overlay on a private simulated Monado headset.
Requires monado-service, Xvfb and Mesa Lavapipe. Never grabs physical input.
"""
import json,os,shutil,signal,socket,subprocess,tempfile,time
from pathlib import Path
signal.pthread_sigmask(signal.SIG_SETMASK,[])
root=Path(__file__).resolve().parents[2];out=root/'artifacts/xr';out.mkdir(parents=True,exist_ok=True)
processes=[];logs=[];checks=[]
def check(ok,name):
    print(('PASS 'if ok else'FAIL ')+name,flush=True);checks.append({'passed':bool(ok),'check':name});assert ok,name
def wait(fn,timeout=40):
    end=time.monotonic()+timeout
    while time.monotonic()<end:
        try:
            value=fn()
            if value:return value
        except (OSError,ValueError,KeyError):pass
        time.sleep(.05)
    raise AssertionError('Timed out; see '+str(out))
def start(args,name,env,**kw):
    log=(out/(name+'.log')).open('w');logs.append(log);child=subprocess.Popen(args,env=env,stdout=log,stderr=subprocess.STDOUT,**kw);processes.append(child);return child
def ctl(path,message,reply=False):
    address=None
    with socket.socket(socket.AF_UNIX,socket.SOCK_DGRAM) as sock:
        try:
            if reply:
                fd,address=tempfile.mkstemp(prefix='reply-',dir=path.parent);os.close(fd);os.unlink(address);sock.bind(address);sock.settimeout(3)
            sock.sendto(message.encode(),str(path))
            return json.loads(sock.recv(65536))if reply else None
        finally:
            if address:os.unlink(address)
try:
 with tempfile.TemporaryDirectory(prefix='flterm-xr-smoke-')as directory:
    runtime=Path(directory);runtime.chmod(0o700)
    env={**os.environ,'XDG_RUNTIME_DIR':directory,'XDG_CONFIG_HOME':directory+'/config','XDG_STATE_HOME':directory+'/state',
         'XDG_CACHE_HOME':directory+'/cache','XDG_DATA_DIRS':'/usr/local/share:/usr/share','LD_LIBRARY_PATH':'/usr/lib/x86_64-linux-gnu',
         'SIMULATED_ENABLE':'1','XRT_COMPOSITOR_FORCE_XCB':'1','IPC_EXIT_WHEN_IDLE':'false',
         'VK_DRIVER_FILES':'/usr/share/vulkan/icd.d/lvp_icd.json','LIBGL_ALWAYS_SOFTWARE':'1',
         'XR_RUNTIME_JSON':'/usr/share/openxr/1/openxr_monado.json','XDG_CURRENT_DESKTOP':'Dome'}
    layers=root/'build/validation/layers'
    if layers.exists():
        env['XR_API_LAYER_PATH']=str(layers)
        env['XR_ENABLE_API_LAYERS']='XR_APILAYER_LUNARG_core_validation'
    config=runtime/'config/dome';config.mkdir(parents=True)
    appearance=config/'appearance.json';appearance.write_text(json.dumps({'font':'Unifont / Sans-Serif','font_size':16,'font_scale':1,'light':False}))
    for name in ['WAYLAND_DISPLAY','WAYLAND_SOCKET','DISPLAY']:env.pop(name,None)
    rd,wr=os.pipe();start(['Xvfb','-displayfd',str(wr),'-screen','0','1600x1000x24','-nolisten','tcp','-noreset'],'xvfb',env,pass_fds=(wr,));os.close(wr)
    with os.fdopen(rd)as pipe:env['DISPLAY']=':'+pipe.readline().strip()
    binary=runtime/'monado-service';shutil.copyfile(shutil.which('monado-service'),binary);binary.chmod(0o700)
    monado=start([str(binary)],'monado',env,stdin=subprocess.PIPE);wait(lambda:(runtime/'monado_comp_ipc').exists())
    probe=subprocess.run([str(root/'build/flterm-xr'),'--probe'],env=env,capture_output=True,text=True,timeout=20)
    (out/'probe.log').write_text(probe.stdout+probe.stderr)
    check(probe.returncode==0 and 'XR_EXTX_overlay' in probe.stdout+probe.stderr,'Simulated runtime advertises native overlay support')
    app=start([str(root/'build/flterm-xr'),'--hidden','--no-keyboard','--no-save-layout','--placement','20',
               '--screenshot',str(out/'terminal.ppm'),'--report',str(out/'terminal.json')],'flterm',env)
    path=runtime/'flterm-xr/control.sock';wait(lambda:path.exists())
    status=lambda:ctl(path,'status',True)
    check(wait(lambda:status()['hidden_frames']>5),'Hidden native overlay submits zero layers while the XR session runs')
    if layers.exists():check('libXrApiLayer_core_validation' in Path(f'/proc/{app.pid}/maps').read_text(),'Khronos core validation layer is loaded in the native overlay')
    marker=runtime/'hidden-output';ctl(path,"type printf %s hidden > "+str(marker)+'\r')
    check(wait(lambda:marker.exists() and marker.read_text()=='hidden'),'Native XR shell remains usable while hidden')
    ctl(path,'show');check(wait(lambda:status()['visible_frames']>5),'Native OpenXR quad receives rendered FLTerm frames')
    ctl(path,'position 0.2 -0.1 -1.6');ctl(path,'size 1.4 0.8');ctl(path,'tab-new')
    check(wait(lambda:status()['tabs']==2 and abs(status()['layout']['z']+1.6)<.01),'Native overlay accepts live geometry and tab commands')
    ctl(path,'hide');before=status();time.sleep(.3);after=status()
    check(after['hidden_frames']>before['hidden_frames'] and after['visible_frames']<=before['visible_frames']+1,'Hide stops visible layer submission and keeps the session alive')
    ctl(path,'show');check(wait(lambda:status()['visible_frames']>after['visible_frames']+5),'Show resumes the existing native overlay')
    appearance.write_text(json.dumps({'font':'Unifont / Sans-Serif','font_size':16,'font_scale':2,'light':True}))
    check(wait(lambda:status()['font_pixels']==32 and status()['light'] and status()['visible_frames']>after['visible_frames']+10),'Live DomeD font and palette changes recreate the native XR swapchain')
    ctl(path,'quit');check(app.wait(timeout=8)==0,'Native overlay shuts down and releases its OpenXR resources')
    check(not path.exists(),'Native control socket is removed on exit')
    log=(out/'flterm.log').read_text()
    check(not any(error in log for error in ['Validation Error','VALIDATION_ERROR','VALID_ERROR']),'Native overlay log contains no validation errors')
finally:
    (out/'report.json').write_text(json.dumps(checks,indent=2)+'\n')
    for child in reversed(processes):
        if child.poll()is None:
            child.terminate()
            try:child.wait(timeout=5)
            except subprocess.TimeoutExpired:child.kill();child.wait()
    for log in logs:log.close()
