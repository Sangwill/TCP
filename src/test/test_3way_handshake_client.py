import time
import sys
import subprocess

prefix = '3way_handshake_client'

def kill():
    # kill processes
    subprocess.run("killall lwip-server", shell=True, capture_output=True)
    subprocess.run("killall lab-client", shell=True, capture_output=True)

def quit(code):
    kill()
    sys.exit(code)

kill()

subprocess.Popen(["ninja", "run-lwip-server"], stdout=open(
    f'{prefix}_lwip-server-stdout.log', 'w'), stderr=open(f'{prefix}_lwip-server-stderr.log', 'w'))
subprocess.Popen(["ninja", "run-lab-client"], stdout=open(
    f'{prefix}_lab-client-stdout.log', 'w'), stderr=open(f'{prefix}_lab-client-stderr.log', 'w'))

timeout = 5
file = f'{prefix}_lab-client-stdout.log'
transitions = []
for i in range(timeout):
    with open(file, 'r') as f:
        for line in f:
            if 'TCP state transitioned from' in line:
                line = line.strip()
                parts = line.split(' ')
                old_state = parts[4]
                new_state = parts[6]
                transitions.append((old_state, new_state))

    # check state machine
    if len(transitions) == 2:
        assert(transitions[0] == ('CLOSED', 'SYN_SENT'))
        assert(transitions[1] == ('SYN_SENT', 'ESTABLISHED'))
        print('Passed')
        quit(0)
    time.sleep(1)

print('Timeout')
quit(1)
