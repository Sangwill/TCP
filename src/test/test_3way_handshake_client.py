import time
from common import kill, spawn_lab_client, spawn_lwip_server, quit

prefix = '3way_handshake_client'

kill()

spawn_lwip_server(prefix)
spawn_lab_client(prefix)

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
                print(line)

    # check state machine
    if len(transitions) == 2:
        assert(transitions[0] == ('CLOSED', 'SYN_SENT'))
        assert(transitions[1] == ('SYN_SENT', 'ESTABLISHED'))
        print('Passed')
        quit(0)
    time.sleep(1)

print('Timeout')
quit(1)
