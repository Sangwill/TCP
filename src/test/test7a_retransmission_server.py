import time
import os
from common import kill, spawn, quit

# spawn lab-server and lwip-client
# check http request & response

prefix = 'retransmission_server'

kill()

if not os.path.exists('build.ninja'):
    print('Please run in builddir directory!')
    quit(1)

server_name = 'lab-server-retransmission'
client_name = 'lwip-client-retransmission'

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
                print('lwip-client:', line)

    server_http = False
    with open(server_stdout, 'r') as f:
        for line in f:
            line = line.strip()
            if 'GET /index.html' in line:
                server_http = True
                print('lab-server:', line)

    client_closed = False
    with open(client_stdout, 'r') as f:
        for line in f:
            line = line.strip()
            if 'tcp_receive: received FIN.' in line \
              or 'tcp_receive: dequeued FIN.' in line : # out-of-order FIN packet
                client_closed = True
                print('lwip-client:', line)

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
    if len(transitions) >= 5 and client_closed and recv_200 and server_http:
        # find the first CLOSED -> LISTEN
        for i in range(len(transitions) - 2):
            if transitions[i] == ('SYN_RCVD', 'ESTABLISHED') and \
               transitions[i+1] == ('ESTABLISHED', 'FIN_WAIT_1') and \
               transitions[i+2] == ('FIN_WAIT_1', 'FIN_WAIT_2'):
                print('Passed')
                quit(0)
    time.sleep(1)

print('Timeout')
quit(1)
