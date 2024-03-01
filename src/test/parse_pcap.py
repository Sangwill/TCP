import socket
import datetime
import dpkt
from dpkt.compat import compat_ord

def parse_time(pcap_path):
    with open(pcap_path, 'rb') as f:
        pcap = dpkt.pcap.Reader(f)
        pcap = list(pcap)
        return (pcap[-1][0] - pcap[0][0])
        # print(pcap[0][0], pcap[-1][0])
        # print("time:", (pcap[-1][0] - pcap[0][0]))
