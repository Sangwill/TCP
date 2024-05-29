project('tcplab', 'c', 'cpp',
  version : '0.1',
  default_options : ['warning_level=3', 'cpp_std=c++14', 'b_sanitize=address'])

lwip_path = 'thirdparty/lwip-2.1.2/src'
lwip_sources = [
  lwip_path / 'core/init.c',
  lwip_path / 'core/def.c',
  lwip_path / 'core/dns.c',
  lwip_path / 'core/inet_chksum.c',
  lwip_path / 'core/ip.c',
  lwip_path / 'core/mem.c',
  lwip_path / 'core/memp.c',
  lwip_path / 'core/netif.c',
  lwip_path / 'core/pbuf.c',
  lwip_path / 'core/raw.c',
  lwip_path / 'core/stats.c',
  lwip_path / 'core/sys.c',
  lwip_path / 'core/altcp.c',
  lwip_path / 'core/altcp_alloc.c',
  lwip_path / 'core/altcp_tcp.c',
  lwip_path / 'core/tcp.c',
  lwip_path / 'core/tcp_in.c',
  lwip_path / 'core/tcp_out.c',
  lwip_path / 'core/timeouts.c',
  lwip_path / 'core/udp.c',
  lwip_path / 'core/ipv4/autoip.c',
  lwip_path / 'core/ipv4/dhcp.c',
  lwip_path / 'core/ipv4/etharp.c',
  lwip_path / 'core/ipv4/icmp.c',
  lwip_path / 'core/ipv4/igmp.c',
  lwip_path / 'core/ipv4/ip4_frag.c',
  lwip_path / 'core/ipv4/ip4.c',
  lwip_path / 'core/ipv4/ip4_addr.c',
  lwip_path / 'api/api_lib.c',
  lwip_path / 'api/api_msg.c',
  lwip_path / 'api/err.c',
  lwip_path / 'api/if_api.c',
  lwip_path / 'api/netbuf.c',
  lwip_path / 'api/netdb.c',
  lwip_path / 'api/netifapi.c',
  lwip_path / 'api/sockets.c',
  lwip_path / 'api/tcpip.c',
  lwip_path / 'netif/ethernet.c',
  lwip_path / 'apps/http/altcp_proxyconnect.c',
  lwip_path / 'apps/http/fs.c',
  lwip_path / 'apps/http/http_client.c',
  lwip_path / 'apps/http/httpd.c',
]

lwip_incdir = include_directories('include', lwip_path / 'include')

lwip_lib = static_library('lwip',
                          lwip_sources,
                          'src/lwip_common.cpp',
                          'src/common.cpp',
                          c_args : '-fno-common',
                          cpp_args : '-fno-common',
                          include_directories: lwip_incdir)
lab_sources = [
        'src/lab/ip.cpp',
        'src/lab/tcp.cpp',
]

common_sources = [
        'src/lab/timers.cpp',
        'src/common.cpp',
        'src/lab/buffer.cpp',
]

lwip_client = executable('lwip-client',
           common_sources,
           'src/lwip/lwip-client.cpp',
           link_with: lwip_lib,
           include_directories: lwip_incdir)

lwip_server = executable('lwip-server',
           common_sources,
           'src/lwip/lwip-server.cpp',
           link_with: lwip_lib,
           include_directories: lwip_incdir)

incdir = include_directories('include')

tcp_with_nagle = library('tcp_with_nagle',
        lab_sources,
        common_sources,
        include_directories: incdir,
        cpp_args : '-DENABLE_NAGLE')

tcp_new_reno = library('tcp_new_reno',
        lab_sources,
        common_sources,
        include_directories: incdir,
        cpp_args : ['-DENABLE_NAGLE', '-DENABLE_NEW_RENO'])

tcp_no_nagle = library('tcp_no_nagle',
        lab_sources,
        common_sources,
        include_directories: incdir)

lab_client = executable('lab-client',
            'src/lab/lab-client.cpp',
            include_directories: incdir,
            link_with: tcp_with_nagle)

lab_client_with_new_reno = executable('lab-client-with-new-reno',
            'src/lab/lab-client.cpp',
            include_directories: incdir,
            link_with: tcp_new_reno)

lab_client_no_nagle = executable('lab-client-no-nagle',
            'src/lab/lab-client.cpp',
            include_directories: incdir,
            link_with: tcp_no_nagle)

lab_server = executable('lab-server',
            'src/lab/lab-server.cpp',
            include_directories: incdir,
            link_with: tcp_with_nagle)

lab_server_with_new_reno = executable('lab-server-with-new-reno',
            'src/lab/lab-server.cpp',
            include_directories: incdir,
            link_with: tcp_new_reno)

lab_server_no_nagle = executable('lab-server-no-nagle',
            'src/lab/lab-server.cpp',
            include_directories: incdir,
            link_with: tcp_no_nagle)

pcap_path = 'pcap'

run_target('run-lwip-server',
  depends: [lwip_server],
  command : [lwip_server, '-l', 's', '-r', 'c', '-p', pcap_path / 'server.pcap']
)

run_target('run-lwip-client',
  depends: [lwip_client],
  command : [lwip_client, '-l', 'c', '-r', 's', '-p', pcap_path / 'client.pcap']
)

run_target('run-lwip-client-tun',
  depends: [lwip_client],
  command : ['sudo', lwip_client, '-t', 'utun', '-p', pcap_path / 'lwip-client.pcap']
)

run_target('run-lab-client',
  depends: [lab_client],
  command : [lab_client, '-l', 'c', '-r', 's', '-p', pcap_path / 'lab-client.pcap']
)

run_target('run-lab-client-tun',
  depends: [lab_client],
  command : [lab_client, '-t', 'utun', '-p', pcap_path / 'lab-client.pcap']
)

run_target('run-lab-server',
  depends: [lab_server],
  command : [lab_server, '-l', 's', '-r', 'c', '-p', pcap_path / 'lab-server.pcap']
)

run_target('run-lab-server-no-nagle',
  depends: [lab_server_no_nagle],
  command : [lab_server_no_nagle, '-l', 's', '-r', 'c', '-p', pcap_path / 'lab-server-no-nagle.pcap', '-T', 'nagle']
)

run_target('run-lab-server-nagle',
  depends: [lab_server],
  command : [lab_server, '-l', 's', '-r', 'c', '-p', pcap_path / 'lab-server-nagle.pcap', '-T', 'nagle']
)

run_target('run-lwip-client-retransmission',
  depends: [lwip_client],
  command : [lwip_client, '-l', 'c', '-r', 's', '-p', pcap_path / 'client-retransmission.pcap', '-R', '0.4']
)

run_target('run-lab-server-retransmission',
  depends: [lab_server_no_nagle],
  command : [lab_server_no_nagle, '-l', 's', '-r', 'c', '-p', pcap_path / 'lab-server-retransmission.pcap']
)

run_target('run-lab-server-out-of-order',
  depends: [lab_server_no_nagle],
  command : [lab_server_no_nagle, '-l', 's', '-r', 'c', '-p', pcap_path / 'lab-server-out-of-order.pcap']
)

run_target('run-lwip-client-cong-avoid',
  depends: [lwip_client],
  command : [lwip_client, '-l', 'c', '-r', 's', '-p', pcap_path / 'client-cong-avoid.pcap', '-T', 'cong-avoid-client']
)

run_target('run-lwip-client-cong-avoid-2',
  depends: [lwip_client],
  command : [lwip_client, '-l', 'c', '-r', 's', '-p', pcap_path / 'client-cong-avoid-2.pcap', '-T', 'cong-avoid-client-2']
)

run_target('run-lab-client-new-reno',
  depends: [lab_client],
  command : [lab_client, '-l', 'c', '-r', 's', '-p', pcap_path / 'lab-client-new-reno.pcap', '-T', 'new-reno']
)

run_target('run-lab-server-cong-avoid',
  depends: [lab_server_no_nagle],
  command : [lab_server_no_nagle, '-l', 's', '-r', 'c', '-p', pcap_path / 'lab-server-cong-avoid.pcap', '-T', 'cong-avoid-server']
)

run_target('run-lab-server-cong-avoid-2',
  depends: [lab_server_no_nagle],
  command : [lab_server_no_nagle, '-l', 's', '-r', 'c', '-p', pcap_path / 'lab-server-cong-avoid-2.pcap', '-T', 'cong-avoid-server']
)

run_target('run-lab-server-new-reno',
  depends: [lab_server_with_new_reno],
  command : [lab_server_with_new_reno, '-l', 's', '-r', 'c', '-p', pcap_path / 'lab-server-new-reno.pcap', '-T', 'cong-avoid-server']
)

run_target('run-lab-server-no-new-reno',
  depends: [lab_server],
  command : [lab_server, '-l', 's', '-r', 'c', '-p', pcap_path / 'lab-server-no-new-reno.pcap', '-T', 'cong-avoid-server']
)

# testing
test1_seq_compare = executable('test1-seq-compare',
            lab_sources,
            common_sources,
            'src/test/test1_seq_compare.cpp',
            include_directories: incdir)
test('test1 seq comparison', test1_seq_compare, timeout: 100)



root = meson.source_root()
python3 = import('python').find_installation('python3')
sudo = find_program('sudo')

test('test3a 3-way handshake (client)',
    python3,
    args: [root / 'src/test/test3a_3way_handshake_client.py'],
    is_parallel : false,
    timeout: 100)

test('test3b 3-way handshake (server)',
    python3,
    args: [root / 'src/test/test3b_3way_handshake_server.py'],
    is_parallel : false,
    timeout: 100)



test('test7b out-of-order (client)',
    python3,
    args: [root / 'src/test/test7b_out_of_order_client.py'],
    is_parallel : false,
    timeout: 100)

