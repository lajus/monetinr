import subprocess
import threading
import time

def find_pid(pname):
    try:
        tmp = subprocess.check_output("pgrep %s"%pname, shell=True).split('\n')
    except:
        return 0
    if len(tmp) is not 2:
        raise ValueError("pgrep: Not unique %s process: %d processes found"%(pname, len(tmp)-1))
    return int(tmp[0])
    
def watch_pid(pname, interval, output):
    already_run = False
    i = 0
    while True:
        try:
            pid = find_pid(pname)
            if pid is 0: 
                if already_run or i > 100: 
                    return
                else:
                    i += 1
                    time.sleep(interval)
                    continue
            already_run = True
            output.write(subprocess.check_output("ps -p %d -o %%cpu,%%mem | tail -n +2"%pid, shell=True))
            time.sleep(interval)
        except:
            return

def get_watcher(pname, interval, output):
    return threading.Thread(target=watch_pid, args=[pname, interval, output])
