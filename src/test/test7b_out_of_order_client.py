import time
import os
from common import kill, spawn, quit

# spawn lab-server and lwip-client
# check http request & response

prefix = 'out_of_order'

kill()

if not os.path.exists('build.ninja'):
    print('Please run in builddir directory!')
    quit(1)

server_name = 'lab-server-out-of-order'
client_name = 'lab-client'

spawn(prefix, server_name)
spawn(prefix, client_name)

# timeout = 10
from common import timeout
client_stdout = f'{prefix}_{client_name}-stdout.log'
server_stdout = f'{prefix}_{server_name}-stdout.log'
for i in range(timeout):
    print('Reading output:')
    recv_200 = False
    with open(client_stdout, 'r') as f:
        for line in f:
            line = line.strip()
            if 'HTTP/1.1 200 OK' in line:
                recv_200 = True
                print('lab-client:', line)

    server_http = False
    with open(server_stdout, 'r') as f:
        for line in f:
            line = line.strip()
            if 'GET /index.html' in line:
                server_http = True
                print('lab-server:', line)

    server_closed = False
    with open(server_stdout, 'r') as f:
        for line in f:
            line = line.strip()
            if 'Connection closing' in line:
                server_closed = True
                print('lab-server:', line)

    transitions = []
    with open(server_stdout, 'r') as f:
        for line in f:
            line = line.strip()
            if 'TCP state transitioned from' in line:
                parts = line.split(' ')
                old_state = parts[4]
                new_state = parts[6]
                transitions.append((old_state, new_state))
                print('lab-server:', line)

    # check state machine
    if len(transitions) >= 5 and recv_200 and server_http and server_closed:
        assert(transitions[0] == ('CLOSED', 'LISTEN'))
        assert(transitions[1] == ('CLOSED', 'SYN_RCVD'))
        assert(transitions[2] == ('SYN_RCVD', 'ESTABLISHED'))
        assert(transitions[3] == ('ESTABLISHED', 'FIN_WAIT_1') or transitions[3] == ('ESTABLISHED', 'CLOSE_WAIT'))
        assert(transitions[4] == ('FIN_WAIT_1', 'FIN_WAIT_2') or transitions[4] == ('FIN_WAIT_1', 'CLOSING')
               or transitions[4] == ('CLOSE_WAIT', 'LAST_ACK'))
        print('Passed')
        quit(0)
    time.sleep(1)

print('Timeout')
quit(1)
