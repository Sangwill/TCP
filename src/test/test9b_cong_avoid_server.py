from common import timeout
import time
import os
from common import kill, spawn, quit
from parse_pcap import parse_time

# spawn lab-server and lwip-client
# check the tcp state machine

prefix = 'cong_avoid'

kill()

if not os.path.exists('build.ninja'):
    print('Please run in builddir directory!')
    quit(1)

server_name = 'lab-server-cong-avoid-2'
client_name = 'lwip-client-cong-avoid-2'

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
            if 'tcp_receive: received FIN.' in line \
                    or 'tcp_receive: dequeued FIN.' in line:  # out-of-order FIN packet
                client_closed = True
                print('lwip-client:', line)
                break

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
    if len(transitions) >= 5 and client_closed:
        assert (transitions[0] == ('CLOSED', 'LISTEN'))
        assert (transitions[1] == ('CLOSED', 'SYN_RCVD'))
        assert (transitions[2] == ('SYN_RCVD', 'ESTABLISHED'))
        assert (transitions[3] == ('ESTABLISHED', 'FIN_WAIT_1'))
        assert (transitions[4] == ('FIN_WAIT_1', 'FIN_WAIT_2'))
        print('Running time:', round(parse_time(
            '../pcap/' + server_name + '.pcap'), 3))
        print('Passed')
        quit(0)
    time.sleep(1)

print('Timeout')
quit(1)
