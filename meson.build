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

client = executable('client',
           'src/client/client.cpp',
           link_with: lwip_lib,
           include_directories: lwip_incdir)

server = executable('server',
           'src/server/server.cpp',
           link_with: lwip_lib,
           include_directories: lwip_incdir)

incdir = include_directories('include')

lab_sources = [
        'src/lab/timers.cpp',
        'src/lab/ip.cpp',
        'src/lab/tcp.cpp',
        'src/common.cpp',
]

lab_client = executable('lab-client',
            lab_sources,
           'src/lab/lab-client.cpp',
           include_directories: incdir)

lab_server = executable('lab-server',
            lab_sources,
           'src/lab/lab-server.cpp',
           include_directories: incdir)

run_target('run-server',
  depends: [server],
  command : [server, '-l', 's', '-r', 'c', '-p', 'server.pcap']
)

run_target('run-client',
  depends: [client],
  command : [client, '-l', 'c', '-r', 's', '-p', 'client.pcap']
)

run_target('run-lab-client',
  depends: [lab_client],
  command : [lab_client, '-l', 'c', '-r', 's', '-p', 'lab.pcap']
)

run_target('run-lab-server',
  depends: [lab_server],
  command : [lab_server, '-l', 'c', '-r', 's', '-p', 'lab.pcap']
)
