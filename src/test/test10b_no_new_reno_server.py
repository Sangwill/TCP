from common import timeout
import time
import os
from common import kill, spawn, quit
from parse_pcap import parse_time

# spawn lab-server and lwip-client
# check the tcp state machine

prefix = 'new_reno'

kill()

if not os.path.exists('build.ninja'):
    print('Please run in builddir directory!')
    quit(1)

server_name = 'lab-server-no-new-reno'
client_name = 'lab-client-new-reno'

spawn(prefix, server_name)
spawn(prefix, client_name)

# timeout = 10
client_stdout = f'{prefix}_{client_name}-stdout.log'
server_stdout = f'{prefix}_{server_name}-stdout.log'
# modified from test5b_termination_server.py
for i in range(timeout):
    print('Reading output:')
    client_closed = False
    with open(client_stdout, 'r') as f:
        for line in f:
            line = line.strip()
            if 'TCP state transitioned from' in line:
                parts = line.split(' ')
                old_state = parts[4]
                new_state = parts[6]
                if new_state == 'CLOSED' or new_state == 'TIME_WAIT':
                    client_closed = True
                    print('lab-client:', line)
                    break

    server_closed = False
    with open(server_stdout, 'r') as f:
        for line in f:
            line = line.strip()
            if 'TCP state transitioned from' in line:
                parts = line.split(' ')
                old_state = parts[4]
                new_state = parts[6]
                if new_state == 'CLOSED' or new_state == 'TIME_WAIT':
                    server_closed = True
                    print('lab-server:', line)

    # check state machine
    if client_closed and server_closed:
        print('Running time:', round(parse_time(
            '../pcap/' + server_name + '.pcap'), 3))
        print('Passed')
        quit(0)
    time.sleep(1)

print('Timeout')
quit(1)
