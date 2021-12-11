import subprocess
import time
import sys
import os


def kill():
    # kill processes
    subprocess.run("killall lwip-server", shell=True, capture_output=True)
    subprocess.run("killall lwip-client", shell=True, capture_output=True)
    subprocess.run("killall lab-server", shell=True, capture_output=True)
    subprocess.run("killall lab-client", shell=True, capture_output=True)


def spawn(prefix, target):
    stdout = os.path.join(os.getcwd(), f'{prefix}_{target}-stdout.log')
    stderr = os.path.join(os.getcwd(), f'{prefix}_{target}-stderr.log')
    print(
        f'Spawning {target}, stdout redirected to {stdout}, stderr redirected to {stderr}')
    subprocess.Popen(["ninja", f"run-{target}"], stdout=open(
        stdout, 'w'), stderr=open(stderr, 'w'))
    time.sleep(1)


def spawn_lwip_server(prefix):
    spawn(prefix, 'lwip-server')


def spawn_lab_client(prefix):
    spawn(prefix, 'lab-client')


def quit(code):
    kill()
    sys.exit(code)
