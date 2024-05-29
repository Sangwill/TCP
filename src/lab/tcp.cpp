#include "../include/tcp.h"
#include "common.h"
#include "timers.h"
#include <assert.h>
#include <map>
#include <stdio.h>
// mapping from fd to TCP connection
std::map<int, TCP *> tcp_fds;
struct Retransmission {
  int fd;
  size_t operator()() {
    
    TCP *tcp = tcp_fds[fd];
    assert(tcp);
    if (tcp->retransmission_queue.empty()) {
      return -1;
    } else {
      printf("TRY RETRANSMIT\n");
      tcp->retransmission();
      return 2000;
    }
  }
};

struct Nagle
{
  int fd;
  size_t operator()(){
     
    TCP *tcp = tcp_fds[fd];
    assert(tcp);
    if (tcp->nagle_buffer_size!=0) {
      if (current_ts_msec()-tcp->nagle_timer){
        tcp->send_nagle();
        return -1;
      }
    } else {
      return 2000;
    }
  }
};

// some helper functions
const char *tcp_state_to_string(TCPState state) {
  switch (state) {
  case TCPState::LISTEN:
    return "LISTEN";
  case TCPState::SYN_SENT:
    return "SYN_SENT";
  case TCPState::SYN_RCVD:
    return "SYN_RCVD";
  case TCPState::ESTABLISHED:
    return "ESTABLISHED";
  case TCPState::FIN_WAIT_1:
    return "FIN_WAIT_1";
  case TCPState::FIN_WAIT_2:
    return "FIN_WAIT_2";
  case TCPState::CLOSE_WAIT:
    return "CLOSE_WAIT";
  case TCPState::CLOSING:
    return "CLOSING";
  case TCPState::LAST_ACK:
    return "LAST_ACK";
  case TCPState::TIME_WAIT:
    return "TIME_WAIT";
  case TCPState::CLOSED:
    return "CLOSED";
  default:
    printf("Invalid TCPState\n");
    exit(1);
  }
}

void TCP::set_state(TCPState new_state) {
  // for unit tests
  printf("TCP state transitioned from %s to %s\n", tcp_state_to_string(state),
         tcp_state_to_string(new_state));
  fflush(stdout);
  state = new_state;
}

const char *tcp_reno_state_to_string(RenoState state) {
  switch (state) {
  case RenoState::SLOW_START:
    return "SLOW_START";
  case RenoState::CONGESTION_AVOIDANCE:
    return "CONGESTION_AVOIDANCE";
  case RenoState::FAST_RECOVERY:
    return "FAST_RECOVERY";
  default:
    printf("Invalid TCPState\n");
    exit(1);
  }
}

void TCP::set_reno_state(RenoState new_state) {
  // for unit tests
  printf("TCP Reno state transitioned from %s to %s\n", tcp_reno_state_to_string(reno_state),
         tcp_reno_state_to_string(new_state));
  fflush(stdout);
  reno_state = new_state;
}

// update retransmission queue
void TCP::push_to_retransmission_queue(const uint8_t *buffer, const size_t header_len, const size_t body_len) {
  TCPHeader *tcp_hdr = (TCPHeader *)&buffer[20];
  uint32_t seq = ntohl(tcp_hdr->seq);
  printf("|* Push Retransmission Queue, seq = %lu *|\n", seq);
  for (auto seg : retransmission_queue) {
    TCPHeader *seg_tcp_hdr = (TCPHeader *)&seg.buffer[20];
    uint32_t seg_seq = ntohl(seg_tcp_hdr->seq);
    if (seg_seq == seq) {
      printf("|* Already in Retransmission Queue *|\n");
      return;
    }
  }
  printf("|* Push Packet *|\n");
  Segment new_seg = Segment(buffer, header_len, body_len, current_ts_msec());
  retransmission_queue.push_back(new_seg);
  printf("retransmission queue size = %lu\n", retransmission_queue.size());
}

void TCP::pop_from_retransmission_queue(const uint32_t seg_ack) {
  printf("|* Pop Retransmission Queue *|\n");
  ssize_t index = -1;
  for (ssize_t i = 0, iEnd = retransmission_queue.size(); i < iEnd; i++) {
    auto& seg = retransmission_queue[i];
    TCPHeader *tcp_hdr = (TCPHeader *)&seg.buffer[20];
    uint32_t seg_seq = ntohl(tcp_hdr->seq);
     if (seg_seq == seg_ack) {
      if (reno_state == RenoState::SLOW_START ||
          reno_state == RenoState::CONGESTION_AVOIDANCE) {
        // dupACKcount++
        seg.dup_ack_cnt++;
        printf("seg_seq = %u seg_dup_ack_cnt = %zu\n", seg_seq, seg.dup_ack_cnt);
        // when duplicate ACKs count == 3
        if (seg.dup_ack_cnt == 3) {
          set_reno_state(RenoState::FAST_RECOVERY);
          // ssthresh = cwnd / 2; cwnd = ssthresh + 3 * MSS
          ssthresh = cwnd >> 1;
          cwnd = ssthresh + 3 * DEFAULT_MSS;
          // retransmit missing segment
          send_packet(seg.buffer, seg.header_len + seg.body_len);
        }
      } else {
        // cwnd = cwnd + MSS
        cwnd += DEFAULT_MSS;
      }
    }
    // match packet
    if (0 < seg.body_len) {
      if (seg_seq + seg.body_len == seg_ack) {
        index = i;
        break;
      }
    } else {
      if (seg_seq + 1 == seg_ack) {
        index = i;
        break;
      }
    }
  }
  // new ACK
  if (index != -1) {
    printf("|* Pop Packet *|\n");
    retransmission_queue.erase(retransmission_queue.begin(), retransmission_queue.begin() + index + 1);
    if (state != TCPState::SYN_SENT && state != TCPState::SYN_RCVD) {
      // update cwnd
      if (reno_state == RenoState::SLOW_START) {
        cwnd += DEFAULT_MSS;
        printf("slow start: cwnd = %u ssthresh = %u\n", cwnd, ssthresh);
        if (cwnd == ssthresh) {
          set_reno_state(RenoState::CONGESTION_AVOIDANCE);
        }
      } else if (reno_state == RenoState::CONGESTION_AVOIDANCE) {
        delta_cwnd += DEFAULT_MSS;
        if (delta_cwnd == cwnd) {
          cwnd += DEFAULT_MSS;
          delta_cwnd = 0;
          printf("congestion avoidance: cwnd = %u ssthresh = %u\n", cwnd, ssthresh); 
        }
      } else {
        cwnd = ssthresh;
        set_reno_state(RenoState::CONGESTION_AVOIDANCE);
      }
      // dupACKcount = 0
      clear_dup_ack_cnt();
    }
  }
  printf("retransmission queue size = %lu\n", retransmission_queue.size());
}

void TCP::clear_dup_ack_cnt() {
  for (auto seg : retransmission_queue) {
    seg.dup_ack_cnt = 0;
  }
}

void TCP::retransmission() {
  uint64_t current_ms = current_ts_msec();
  // printf("current_ms = %llu\n", current_ms);
  for (auto seg : retransmission_queue) {
    // printf("start_ms = %llu\n", seg.start_ms);
    if (RTO + seg.start_ms < current_ms) {
      printf("|* Retransmission *|\n");
      printf("|* Retransmission Packet *|\n");
      set_reno_state(RenoState::SLOW_START);
      // cwnd = MSS; ssthresh = cwnd / 2;
      ssthresh = cwnd >> 1;
      cwnd = DEFAULT_MSS;
      // dupACKcount = 0
      clear_dup_ack_cnt();
      send_packet(seg.buffer, seg.header_len + seg.body_len);
    }
  }
}

void TCP::push_to_out_of_order_queue(const uint8_t *data, const size_t len, const uint32_t seg_seq, bool fin) {
  printf("|* Push Out of Order Queue *|\n");
  if (fin){
    printf("PUSING FIN\n");
  }
  Payload payload = Payload(data, len, seg_seq,fin);
  out_of_order_queue.push_back(payload);
  printf("out_of_order queue size = %lu\n", out_of_order_queue.size());
}

// construct ip header from tcp connection
void construct_ip_header(uint8_t *buffer, const TCP *tcp,
                         uint16_t total_length) {
  IPHeader *ip_hdr = (IPHeader *)buffer;
  memset(ip_hdr, 0, 20);
  ip_hdr->ip_v = 4;
  ip_hdr->ip_hl = 5;
  ip_hdr->ip_len = htons(total_length);
  ip_hdr->ip_ttl = 64;
  ip_hdr->ip_p = 6; // TCP
  ip_hdr->ip_src = tcp->local_ip;
  ip_hdr->ip_dst = tcp->remote_ip;
}

// update tcp & ip checksum
void update_tcp_ip_checksum(uint8_t *buffer) {
  IPHeader *ip_hdr = (IPHeader *)buffer;
  TCPHeader *tcp_hdr = (TCPHeader *)(buffer + ip_hdr->ip_hl * 4);
  update_tcp_checksum(ip_hdr, tcp_hdr);
  update_ip_checksum(ip_hdr);
}

void TCP::reorder(const uint32_t seg_seq) {
  printf("|* Reorder *|\n");
  ssize_t index = -1;
  for (ssize_t i = 0, iEnd = out_of_order_queue.size(); i < iEnd; i++) {
    Payload payload = out_of_order_queue[i];
    if (payload.fin){
      if(rcv_nxt==payload.seg_seq+payload.len){
        set_state(TCPState::CLOSE_WAIT);
      }
    }
    if (seg_seq == payload.seg_seq) {
      size_t res = recv.write(payload.data, payload.len, 0);
      rcv_nxt = rcv_nxt + res;
      rcv_wnd = recv.free_bytes();
      index = i;
      break;
    }
  }
  if (index != -1) {
    const uint32_t new_seg_seq = out_of_order_queue[index].seg_seq + out_of_order_queue[index].len;
    out_of_order_queue.erase(out_of_order_queue.begin() + index);
    printf("out_of_order queue size = %lu\n", out_of_order_queue.size());
    if (!out_of_order_queue.empty()) {
      reorder(new_seg_seq);
    }
  }
}





void TCP::send_nagle(){
  printf("|* Send Nagle *|\n");
  size_t segment_len = nagle_buffer_size;
  if (segment_len>0){
    uint16_t total_len = 20 + 20 + segment_len;
    uint8_t buffer[MTU];
    construct_ip_header(buffer, this, total_len);
    TCPHeader *tcp_hdr = (TCPHeader *)&buffer[20];
    memset(tcp_hdr, 0, 20);
    tcp_hdr->source = htons(local_port);
    tcp_hdr->dest = htons(remote_port);
    tcp_hdr->seq = htonl(snd_nxt);
    snd_nxt += segment_len;
    tcp_hdr->doff = 20 / 4;
    tcp_hdr->ack = 1;
    tcp_hdr->ack_seq = htonl(rcv_nxt);
    tcp_hdr->window = htons(recv.free_bytes());
    memcpy(&buffer[40], nagle_buffer, segment_len);
    memset(nagle_buffer, 0, segment_len);
    nagle_buffer_size = 0;
    update_tcp_ip_checksum(buffer);
    send_packet(buffer, total_len);

  }
}

uint32_t generate_initial_seq() {
  // TODO(step 1: sequence number comparison and generation)
  // initial sequence number based on timestamp
  // rfc793 page 27 or rfc6528
  // https://www.rfc-editor.org/rfc/rfc793.html#page-27
  // "The generator is bound to a (possibly fictitious) 32
  // bit clock whose low order bit is incremented roughly every 4
  // microseconds."
  return ((uint32_t)current_ts_usec() >> 2) % 0xffffffff;
  
}

void process_tcp(const IPHeader *ip, const uint8_t *data, size_t size) {
  TCPHeader *tcp_header = (TCPHeader *)data;
  if (!verify_tcp_checksum(ip, tcp_header)) {
    printf("Bad TCP checksum\n");
    return;
  }

  // SEG.SEQ
  uint32_t seg_seq = ntohl(tcp_header->seq);
  // SEG.ACK
  uint32_t seg_ack = ntohl(tcp_header->ack_seq);
  // SEG.WND
  uint16_t seg_wnd = ntohs(tcp_header->window);
  // segment(payload) length
  uint32_t seg_len = ntohs(ip->ip_len) - ip->ip_hl * 4 - tcp_header->doff * 4;
  const uint8_t *payload = data + tcp_header->doff * 4;

  // iterate tcp connections in two pass
  // first pass: only exact matches
  // second pass: allow wildcard matches for listening socket
  // this gives priority to connected sockets
  for (int pass = 1; pass <= 2; pass++) {
    for (auto &pair : tcp_fds) {
      TCP *tcp = pair.second;
      if (tcp->state == TCPState::CLOSED) {
        // ignore closed sockets
        continue;
      }

      if (pass == 1) {
        // first pass: exact match
        if (tcp->local_ip != ip->ip_dst || tcp->remote_ip != ip->ip_src ||
            tcp->local_port != ntohs(tcp_header->dest) ||
            tcp->remote_port != ntohs(tcp_header->source)) {
          continue;
        }
      } else {
        // second pass: allow wildcard
        if (tcp->local_ip != 0 && tcp->local_ip != ip->ip_dst) {
          continue;
        }
        if (tcp->remote_ip != 0 && tcp->remote_ip != ip->ip_src) {
          continue;
        }
        if (tcp->local_port != 0 &&
            tcp->local_port != ntohs(tcp_header->dest)) {
          continue;
        }
        if (tcp->remote_port != 0 &&
            tcp->remote_port != ntohs(tcp_header->source)) {
          continue;
        }
      }

      // matched
      if (tcp_header->doff > 20 / 4) {
        // options exists
        // check TCP option header: MSS
        uint8_t *opt_ptr = (uint8_t *)data + 20;
        uint8_t *opt_end = (uint8_t *)data + tcp_header->doff * 4;
        while (opt_ptr < opt_end) {
          if (*opt_ptr == 0x00) {
            // End Of Option List
            break;
          } else if (*opt_ptr == 0x01) {
            // No-Operation
            opt_ptr++;
          } else if (*opt_ptr == 0x02) {
            // MSS
            uint8_t len = opt_ptr[1];
            if (len != 4) {
              printf("Bad TCP option len: %d\n", len);
              break;
            }

            uint16_t mss = ((uint16_t)opt_ptr[2] << 8) + opt_ptr[3];
            if (tcp_header->syn) {
              tcp->remote_mss = mss;
              printf("Remote MSS is %d\n", mss);
            } else {
              printf("Remote sent MSS option header in !SYN packet\n");
            }
            opt_ptr += len;
          } else {
            printf("Unrecognized TCP option: %d\n", *opt_ptr);
            break;
          }
        }
      }

      if (tcp->state == TCPState::LISTEN) {
        // rfc793 page 65
        // https://www.rfc-editor.org/rfc/rfc793.html#page-65
        // "If the state is LISTEN then"

        // "first check for an RST
        // An incoming RST should be ignored.  Return."
        if (tcp_header->rst) {
          return;
        }

        // "second check for an ACK"
        if (tcp_header->ack) {
          // "Any acknowledgment is bad if it arrives on a connection still in
          // the LISTEN state.  An acceptable reset segment should be formed
          // for any arriving ACK-bearing segment.  The RST should be
          // formatted as follows:
          // <SEQ=SEG.ACK><CTL=RST>
          // Return."
          printf("++++++++++++AT HERE++++++++++++\n");
          UNIMPLEMENTED()
          return;
        }

        // "third check for a SYN"
        if (tcp_header->syn) {
          // create a new socket for the connection
          int new_fd = tcp_socket();
          TCP *new_tcp = tcp_fds[new_fd];
          new_tcp->nagle = tcp->nagle;
          tcp->accept_queue.push_back(new_fd);

          // initialize
          new_tcp->local_ip = tcp->local_ip;
          new_tcp->remote_ip = ip->ip_src;
          new_tcp->local_port = tcp->local_port;
          new_tcp->remote_port = ntohs(tcp_header->source);

          // "Set RCV.NXT to SEG.SEQ+1, IRS is set to SEG.SEQ and any other
          // control or text should be queued for processing later.  ISS
          // should be selected"
          new_tcp->rcv_nxt = seg_seq + 1;
          new_tcp->irs = seg_seq;

          uint32_t initial_seq = generate_initial_seq();
          new_tcp->iss = initial_seq;

          // initialize params
          // assume maximum mss for remote by default
          new_tcp->local_mss = new_tcp->remote_mss = DEFAULT_MSS;

          // TODO(step 2: 3-way handshake)
          // send SYN,ACK to remote
          // 44 = 20(IP) + 24(TCP)
          // with 4 bytes option(MSS)
          // "a SYN segment sent of the form:
          // <SEQ=ISS><ACK=RCV.NXT><CTL=SYN,ACK>"
          //UNIMPLEMENTED()
          uint8_t buffer[44];
          construct_ip_header(buffer, new_tcp, sizeof(buffer));
          TCPHeader *tcp_hdr = (TCPHeader *)&buffer[20];
          memset(tcp_hdr, 0, 20);
          tcp_hdr->source = htons(new_tcp->local_port);
          tcp_hdr->dest = htons(new_tcp->remote_port);
          tcp_hdr->seq = htonl(initial_seq);
          tcp_hdr->ack_seq = htonl(new_tcp->rcv_nxt);
          tcp_hdr->syn = 1;
          tcp_hdr->ack = 1;
          tcp_hdr->doff = 24 / 4;
          tcp_hdr->window = htons(tcp->recv.free_bytes());
          // buffer[40] = 0x02; // kind
          // buffer[41] = 0x04; // length
          // buffer[42] = tcp->local_mss >> 8;
          // buffer[43] = tcp->local_mss;
          update_tcp_ip_checksum(buffer);
          send_packet(buffer, sizeof(buffer));
          // "SND.NXT is set to ISS+1 and SND.UNA to ISS.  The connection
          // state should be changed to SYN-RECEIVED."
          if (!new_tcp->nagle){
            new_tcp->push_to_retransmission_queue(buffer, sizeof(buffer), 0);
            // start retransmission timer
            Retransmission retransmission_fn;
            retransmission_fn.fd = new_fd;
            TIMERS.add_job(retransmission_fn, current_ts_msec());
          }
          new_tcp->snd_nxt = initial_seq + 1;
          new_tcp->snd_una = initial_seq;
          new_tcp->snd_wnd = seg_wnd;
          new_tcp->rcv_wnd = new_tcp->recv.free_bytes();
          new_tcp->snd_wl2 = initial_seq - 1;
          printf("+++++++++++++NOW SEND SYN+ACK TO CLIENT++++++++++++++++\n");
          // NOTE THAT IF THE DROP RATE TOO HIGH, WHEN CLIENT MAX SYN RETRY REACHED,
          // CONNECTION WILL TERMINATE
          new_tcp->set_state(TCPState::SYN_RCVD);
          return;
        }
      }

      if (tcp->state == TCPState::SYN_SENT) {
        // rfc793 page 66
        // https://www.rfc-editor.org/rfc/rfc793.html#page-66
        // "If the state is SYN-SENT then"
        // "If the ACK bit is set"
        if (tcp_header->ack) {
          tcp->pop_from_retransmission_queue(seg_ack);
          // "If SEG.ACK =< ISS, or SEG.ACK > SND.NXT, send a reset (unless
          // the RST bit is set, if so drop the segment and return)
          //<SEQ=SEG.ACK><CTL=RST>
          // and discard the segment.  Return."
          if (tcp_seq_le(seg_ack, tcp->iss) ||
              tcp_seq_gt(seg_ack, tcp->snd_nxt)) {
            // send a reset when !RST
            printf("++++++++++++AT HERE++++++++++++\n");
            UNIMPLEMENTED()
            return;
          }
        }

        // "second check the RST bit"
        // "If the RST bit is set"
        if (tcp_header->rst) {
          // "If the ACK was acceptable then signal the user "error:
          // connection reset", drop the segment, enter CLOSED state,
          // delete TCB, and return.  Otherwise (no ACK) drop the segment
          // and return."
          printf("Connection reset\n");
          tcp->set_state(TCPState::CLOSED);
          return;
        }

        // "fourth check the SYN bit"
        if (tcp_header->syn) {
          // TODO(step 2: 3-way handshake)
          // "RCV.NXT is set to SEG.SEQ+1, IRS is set to
          // SEG.SEQ.  SND.UNA should be advanced to equal SEG.ACK (if there
          // is an ACK), and any segments on the retransmission queue which
          // are thereby acknowledged should be removed."
          
          //UNIMPLEMENTED()
          tcp->rcv_nxt = seg_seq + 1;
          tcp->irs = seg_seq;
          if(tcp_header->ack){
            tcp->snd_una = seg_ack;
          }
          if (tcp_seq_gt(tcp->snd_una, tcp->iss)) {
            // "If SND.UNA > ISS (our SYN has been ACKed), change the connection
            // state to ESTABLISHED,"
            printf("+++++++++++++INTO IT++++++++++++++++\n");
            tcp->set_state(TCPState::ESTABLISHED);

            // TODO(step 2: 3-way handshake)
            // "form an ACK segment
            // <SEQ=SND.NXT><ACK=RCV.NXT><CTL=ACK>
            // and send it."
            //UNIMPLEMENTED()
            uint8_t buffer[40];
            construct_ip_header(buffer, tcp, sizeof(buffer));
            TCPHeader *tcp_hdr = (TCPHeader *)&buffer[20];
            memset(tcp_hdr, 0, 20);
            tcp_hdr->source = htons(tcp->local_port);
            tcp_hdr->dest = htons(tcp->remote_port);
            tcp_hdr->ack_seq = htonl(tcp->rcv_nxt);
            tcp_hdr->seq = htonl(tcp->snd_nxt);
            tcp_hdr->doff = 20 / 4;
            tcp_hdr->ack = 1;
            tcp_hdr->window = htons(tcp->recv.free_bytes());
            update_tcp_ip_checksum(buffer);
            send_packet(buffer, sizeof(buffer));
            // tcp->push_to_retransmission_queue(buffer, sizeof(buffer), 0);
            // // start retransmission timer
            // Retransmission retransmission_fn;
            // retransmission_fn.fd = pair.first;
            // TIMERS.add_job(retransmission_fn, current_ts_msec());
            // TODO(step 2: 3-way handshake)
            // https://www.rfc-editor.org/rfc/rfc1122#page-94
            // "When the connection enters ESTABLISHED state, the following
            // variables must be set:
            // SND.WND <- SEG.WND
            // SND.WL1 <- SEG.SEQ
            // SND.WL2 <- SEG.ACK"
            //UNIMPLEMENTED()
            tcp->snd_wnd = seg_wnd;
            tcp->snd_wl1 = seg_seq;
            tcp->snd_wl2 = seg_ack;
          } else {
            // "Otherwise enter SYN-RECEIVED"
            // "form a SYN,ACK segment
            //<SEQ=ISS><ACK=RCV.NXT><CTL=SYN,ACK>
            // and send it."
            //UNIMPLEMENTED()
            printf("+++++++++++++INTO IT++++++++++++++++\n");
            tcp->set_state(TCPState::SYN_RCVD);
            uint8_t buffer[40];
            construct_ip_header(buffer, tcp, sizeof(buffer));
            TCPHeader *tcp_hdr = (TCPHeader *)&buffer[20];
            memset(tcp_hdr, 0, 20);
            tcp_hdr->source = htons(tcp->local_port);
            tcp_hdr->dest = htons(tcp->remote_port);
            tcp_hdr->ack_seq = htonl(tcp->rcv_nxt);
            tcp_hdr->seq = htonl(tcp->iss);
            tcp_hdr->doff = 20 / 4;
            tcp_hdr->ack = 1;
            tcp_hdr->syn = 1;
            tcp_hdr->window = htons(tcp->recv.free_bytes());
            update_tcp_ip_checksum(buffer);
            send_packet(buffer, sizeof(buffer));
            // tcp->push_to_retransmission_queue(buffer, sizeof(buffer), 0);
            // // start retransmission timer
            // Retransmission retransmission_fn;
            // retransmission_fn.fd = pair.first;
            // TIMERS.add_job(retransmission_fn, current_ts_msec());
          }
        }

        // "fifth, if neither of the SYN or RST bits is set then drop the
        // segment and return."
        if (!tcp_header->syn || !tcp_header->ack) {
          printf("Received unexpected !SYN || !ACK packet in SYN_SENT state\n");
          return;
        }
      }

      // rfc793 page 69
      // https://www.rfc-editor.org/rfc/rfc793.html#page-69
      // "Otherwise,"
      if (tcp->state == TCPState::SYN_RCVD ||
          tcp->state == TCPState::ESTABLISHED ||
          tcp->state == TCPState::FIN_WAIT_1 ||
          tcp->state == TCPState::FIN_WAIT_2 ||
          tcp->state == TCPState::CLOSE_WAIT ||
          tcp->state == TCPState::CLOSING || tcp->state == TCPState::LAST_ACK ||
          tcp->state == TCPState::TIME_WAIT) {

        // "first check sequence number"

        // "There are four cases for the acceptability test for an incoming
        // segment:"
        bool acceptable = false;
        //UNIMPLEMENTED_WARN();
        if (seg_len == 0 && tcp->rcv_wnd == 0) {
          if (seg_seq == tcp->rcv_nxt) {
            acceptable = true;
          }
        } else if (seg_len == 0 && tcp->rcv_wnd > 0) {
          if (tcp_seq_le(tcp->rcv_nxt, seg_seq) && 
              tcp_seq_lt(seg_seq, tcp->rcv_nxt + tcp->rcv_wnd)) {
            acceptable = true;
          }
        } else if (seg_len > 0 && tcp->rcv_wnd > 0) {
          if ((tcp_seq_le(tcp->rcv_nxt, seg_seq) && tcp_seq_lt(seg_seq, tcp->rcv_nxt + tcp->rcv_wnd)) ||
              (tcp_seq_le(tcp->rcv_nxt, seg_seq + seg_len - 1) && tcp_seq_lt(seg_seq + seg_len - 1, tcp->rcv_nxt + tcp->rcv_wnd))) {
            acceptable = true;
          }
        } 

        // "If an incoming segment is not acceptable, an acknowledgment
        // should be sent in reply (unless the RST bit is set, if so drop
        // the segment and return):"
        if (!acceptable) {
          //UNIMPLEMENTED_WARN();
          if (!tcp_header->rst) {
            // <SEQ=SND.NXT><ACK=RCV.NXT><CTL=ACK>
            uint8_t buffer[40];
            construct_ip_header(buffer, tcp, sizeof(buffer));
            // tcp
            TCPHeader *tcp_hdr = (TCPHeader *)&buffer[20];
            memset(tcp_hdr, 0, 20);
            tcp_hdr->source = htons(tcp->local_port);
            tcp_hdr->dest = htons(tcp->remote_port);
            tcp_hdr->seq = htonl(tcp->snd_nxt);
            tcp_hdr->ack_seq = htonl(tcp->rcv_nxt);
            // flags
            tcp_hdr->doff = 20 / 4;
            tcp_hdr->ack = 1;
            // window size
            tcp_hdr->window = htons(tcp->recv.free_bytes());
            
            update_tcp_ip_checksum(buffer);
            send_packet(buffer, sizeof(buffer));
            // tcp->push_to_retransmission_queue(buffer, sizeof(buffer), 0);
            // // start retransmission timer
            // Retransmission retransmission_fn;
            // retransmission_fn.fd = pair.first;
            // TIMERS.add_job(retransmission_fn, current_ts_msec());
            
          }else{
            return;
          }
        }

        // "second check the RST bit,"
        if (tcp_header->rst) {
        }

        // "fourth, check the SYN bit,"
        if (tcp_header->syn) {
        }

        // "fifth check the ACK field,"
        if (tcp_header->ack) {
          // "if the ACK bit is on"
          // "SYN-RECEIVED STATE"
          tcp->pop_from_retransmission_queue(seg_ack);
          if (tcp->state == SYN_RCVD) {
            // "If SND.UNA =< SEG.ACK =< SND.NXT then enter ESTABLISHED state
            // and continue processing."
            if (tcp_seq_le(tcp->snd_una, seg_ack) &&
                tcp_seq_le(seg_ack, tcp->snd_nxt)) {
              printf("+++++++++++++INTO IT++++++++++++++++\n");
              tcp->set_state(TCPState::ESTABLISHED);
            }
          }

          // "ESTABLISHED STATE"
          if (tcp->state == ESTABLISHED) {
            // TODO(step 3: send & receive)
            // "If SND.UNA < SEG.ACK =< SND.NXT then, set SND.UNA <- SEG.ACK."
            //UNIMPLEMENTED()
            if (tcp_seq_lt(tcp->snd_una,seg_ack) && tcp_seq_le(seg_ack,tcp->snd_nxt)){
              tcp->snd_una = seg_ack;
            }
            // TODO(step 3: send & receive)
            // "If SND.UNA < SEG.ACK =< SND.NXT, the send window should be
            // updated.  If (SND.WL1 < SEG.SEQ or (SND.WL1 = SEG.SEQ and
            // SND.WL2 =< SEG.ACK)), set SND.WND <- SEG.WND, set
            // SND.WL1 <- SEG.SEQ, and set SND.WL2 <- SEG.ACK."
            if (tcp_seq_lt(tcp->snd_wl1, seg_seq) || 
                ((tcp->snd_wl1 == seg_seq) && tcp_seq_le(tcp->snd_wl2, seg_ack))) {
                tcp->snd_wnd = seg_wnd;
                // tcp->snd_wnd = seg_wnd << tcp->wnd_shift_cnt;
                tcp->snd_wl1 = seg_seq;
                tcp->snd_wl2 = seg_ack;
            }
          }

          // "FIN-WAIT-1 STATE"
          if (tcp->state == FIN_WAIT_1) {
            // "In addition to the processing for the ESTABLISHED state, if
            // our FIN is now acknowledged then enter FIN-WAIT-2 and continue
            // processing in that state."
            tcp->set_state(FIN_WAIT_2);
          }

          // "FIN-WAIT-2 STATE"
          if (tcp->state == FIN_WAIT_2) {
            // "In addition to the processing for the ESTABLISHED state, if
            // the retransmission queue is empty, the user's CLOSE can be
            // acknowledged ("ok") but do not delete the TCB."
          }

          // LAST-ACK STATE
          if (tcp->state == LAST_ACK) {
            // "The only thing that can arrive in this state is an
            // acknowledgment of our FIN.  If our FIN is now acknowledged,
            // delete the TCB, enter the CLOSED state, and return."
            tcp->set_state(CLOSED);
            return;
          }
        }

        // "seventh, process the segment text,"
        if (seg_len > 0) {
          if (tcp->state == ESTABLISHED) {
            // "Once in the ESTABLISHED state, it is possible to deliver
            // segment text to user RECEIVE buffers."
            printf("Received %d bytes from server\n", seg_len);

            // TODO(step 3: send & receive)
            // write to recv buffer
            // "Once the TCP takes responsibility for the data it advances
            // RCV.NXT over the data accepted, and adjusts RCV.WND as
            // appropriate to the current buffer availability.  The total of
            // RCV.NXT and RCV.WND should not be reduced."
            //UNIMPLEMENTED()
            if (tcp->rcv_nxt != seg_seq) {
              printf("OUT OF ORDER\n");
              tcp->push_to_out_of_order_queue(payload, seg_len, seg_seq,false);
            } else {
              size_t res = tcp->recv.write(payload, seg_len, 0);
              tcp->rcv_nxt = tcp->rcv_nxt + res;
              tcp->rcv_wnd = tcp->recv.free_bytes();
              if (!tcp->out_of_order_queue.empty()) {
                tcp->reorder(seg_seq + seg_len);
              }
            }
            // "Send an acknowledgment of the form:
            // <SEQ=SND.NXT><ACK=RCV.NXT><CTL=ACK>"
            // UNIMPLEMENTED()
            uint8_t buffer[40];
            construct_ip_header(buffer, tcp, sizeof(buffer));
            TCPHeader *tcp_hdr = (TCPHeader *)&buffer[20];
            memset(tcp_hdr, 0, 20);
            tcp_hdr->source = htons(tcp->local_port);
            tcp_hdr->dest = htons(tcp->remote_port);
            tcp_hdr->ack_seq = htonl(tcp->rcv_nxt);
            tcp_hdr->seq = htonl(tcp->snd_nxt);
            tcp_hdr->doff = 20 / 4;
            tcp_hdr->ack = 1;
            tcp_hdr->window = htons(tcp->recv.free_bytes());
            update_tcp_ip_checksum(buffer);
            send_packet(buffer, sizeof(buffer));
            // tcp->push_to_retransmission_queue(buffer, sizeof(buffer), 0);
            // // start retransmission timer
            // Retransmission retransmission_fn;
            // retransmission_fn.fd = pair.first;
            // TIMERS.add_job(retransmission_fn, current_ts_msec());
          }
        }

        // "eighth, check the FIN bit,"
        if (tcp_header->fin) {
          // "Do not process the FIN if the state is CLOSED, LISTEN or SYN-SENT
          // since the SEG.SEQ cannot be validated; drop the segment and
          // return."
          if (tcp->state == CLOSED || tcp->state == LISTEN ||
              tcp->state == SYN_SENT) {
            return;
          }

          // TODO(step 4: connection termination)
          // "If the FIN bit is set, signal the user "connection closing" and
          // return any pending RECEIVEs with same message, advance RCV.NXT
          // over the FIN, and send an acknowledgment for the FIN.  Note that
          // FIN implies PUSH for any segment text not yet delivered to the
          // user."
          //UNIMPLEMENTED();
          // wait till all the data has been received,then send ack
          // if fin arrive before data, cannot close
          // store fin header in buffer, wait some time , check if the ooo buffer
          // has been cleared
          bool ooo = false;// out of order
          if (true) {
            printf("RCV_NXT=%u\n", tcp->rcv_nxt);
            printf("SEG_SEQ=%u\n", seg_seq);
            printf("SEG_LEN=%u\n", seg_len);
            if (tcp->rcv_nxt != seg_seq + seg_len){
              ooo = true;
              printf("OUT OF ORDER\n");
              tcp->push_to_out_of_order_queue(payload, seg_len, seg_seq, true);
            }
            tcp->rcv_nxt = seg_seq + seg_len + 1;
            uint8_t buffer[40];
            construct_ip_header(buffer, tcp, sizeof(buffer));

            TCPHeader *tcp_hdr = (TCPHeader *)&buffer[20];
            memset(tcp_hdr, 0, 20);
            tcp_hdr->source = htons(tcp->local_port);
            tcp_hdr->dest = htons(tcp->remote_port);
            tcp_hdr->seq = htonl(tcp->snd_nxt);
            tcp_hdr->ack_seq = htonl(tcp->rcv_nxt);
            tcp_hdr->doff = 20 / 4;
            tcp_hdr->ack = 1;
            tcp_hdr->window = htons(tcp->recv.free_bytes());
            update_tcp_ip_checksum(buffer);
            send_packet(buffer, sizeof(buffer));
            printf("Connection closing\n");
          }
          // if seg_seq!=rcv_nxt,add into ooo buffer

          if (tcp->state == SYN_RCVD || tcp->state == ESTABLISHED) {
            // Enter the CLOSE-WAIT state
            // until all the data received, enter 
            if(!ooo)
              tcp->set_state(TCPState::CLOSE_WAIT);
          } else if (tcp->state == FIN_WAIT_1) {
            // FIN-WAIT-1 STATE
            // "If our FIN has been ACKed (perhaps in this segment), then
            // enter TIME-WAIT, start the time-wait timer, turn off the other
            // timers; otherwise enter the CLOSING state."

            tcp->set_state(TCPState::TIME_WAIT);
          } else if (tcp->state == FIN_WAIT_2) {
            // FIN-WAIT-2 STATE
            // "Enter the TIME-WAIT state.  Start the time-wait timer, turn
            // off the other timers."

            tcp->set_state(TCPState::TIME_WAIT);
          }
        }
      }
      return;
    }
  }

  printf("No matching TCP connection found\n");
  // send RST
  // rfc793 page 65 CLOSED state
  // https://www.rfc-editor.org/rfc/rfc793.html#page-65
  if (tcp_header->rst) {
    // "An incoming segment containing a RST is discarded."
    return;
  }

  // send RST segment
  // 40 = 20(IP) + 20(TCP)
  uint8_t buffer[40];
  IPHeader *ip_hdr = (IPHeader *)buffer;
  memset(ip_hdr, 0, 20);
  ip_hdr->ip_v = 4;
  ip_hdr->ip_hl = 5;
  ip_hdr->ip_len = htons(sizeof(buffer));
  ip_hdr->ip_ttl = 64;
  ip_hdr->ip_p = 6; // TCP
  ip_hdr->ip_src = ip->ip_dst;
  ip_hdr->ip_dst = ip->ip_src;

  // tcp
  TCPHeader *tcp_hdr = (TCPHeader *)&buffer[20];
  memset(tcp_hdr, 0, 20);
  tcp_hdr->source = tcp_header->dest;
  tcp_hdr->dest = tcp_header->source;
  if (!tcp_header->ack) {
    // "If the ACK bit is off, sequence number zero is used,"
    // "<SEQ=0>"
    tcp_hdr->seq = 0;
    // "<ACK=SEG.SEQ+SEG.LEN>"
    tcp_hdr->ack_seq = htonl(seg_seq + seg_len);
    // "<CTL=RST,ACK>"
    tcp_hdr->rst = 1;
    tcp_hdr->ack = 1;
  } else {
    // "If the ACK bit is on,"
    // "<SEQ=SEG.ACK>"
    tcp_hdr->seq = htonl(seg_ack);
    // "<CTL=RST>"
    tcp_hdr->rst = 1;
  }
  // flags
  tcp_hdr->doff = 20 / 4; // 20 bytes

  update_tcp_ip_checksum(buffer);
  send_packet(buffer, sizeof(buffer));
}

void update_tcp_checksum(const IPHeader *ip, TCPHeader *tcp) {
  uint32_t checksum = 0;

  // pseudo header
  // rfc793 page 17
  // https://www.rfc-editor.org/rfc/rfc793.html#page-17
  // "This pseudo header contains the Source
  // Address, the Destination Address, the Protocol, and TCP length."
  uint8_t pseudo_header[12];
  memcpy(&pseudo_header[0], &ip->ip_src, 4);
  memcpy(&pseudo_header[4], &ip->ip_dst, 4);
  // zero
  pseudo_header[8] = 0;
  // proto tcp
  pseudo_header[9] = 6;
  // TCP length (header + payload)
  be16_t tcp_len = htons(ntohs(ip->ip_len) - ip->ip_hl * 4);
  memcpy(&pseudo_header[10], &tcp_len, 2);
  for (int i = 0; i < 6; i++) {
    checksum +=
        (((uint32_t)pseudo_header[i * 2]) << 8) + pseudo_header[i * 2 + 1];
  }

  // "The checksum field is the 16 bit one's complement of the one's
  // complement sum of all 16 bit words in the header and text."

  // TCP header
  uint8_t *tcp_data = (uint8_t *)tcp;
  tcp->checksum = 0;
  for (int i = 0; i < tcp->doff * 2; i++) {
    checksum += (((uint32_t)tcp_data[i * 2]) << 8) + tcp_data[i * 2 + 1];
  }

  // TCP payload
  uint8_t *payload = tcp_data + tcp->doff * 4;
  int payload_len = ntohs(ip->ip_len) - ip->ip_hl * 4 - tcp->doff * 4;
  for (int i = 0; i < payload_len; i++) {
    if ((i % 2) == 0) {
      checksum += (((uint32_t)payload[i]) << 8);
    } else {
      checksum += payload[i];
    }
  }

  while (checksum >= 0x10000) {
    checksum -= 0x10000;
    checksum += 1;
  }
  // update
  tcp->checksum = htons(~checksum);
}

bool verify_tcp_checksum(const IPHeader *ip, const TCPHeader *tcp) {
  uint32_t checksum = 0;

  // pseudo header
  // rfc793 page 17
  // https://www.rfc-editor.org/rfc/rfc793.html#page-17
  // "This pseudo header contains the Source
  // Address, the Destination Address, the Protocol, and TCP length."
  uint8_t pseudo_header[12];
  memcpy(&pseudo_header[0], &ip->ip_src, 4);
  memcpy(&pseudo_header[4], &ip->ip_dst, 4);
  // zero
  pseudo_header[8] = 0;
  // proto tcp
  pseudo_header[9] = 6;
  // TCP length (header + payload)
  be16_t tcp_len = htons(ntohs(ip->ip_len) - ip->ip_hl * 4);
  memcpy(&pseudo_header[10], &tcp_len, 2);
  for (int i = 0; i < 6; i++) {
    checksum +=
        (((uint32_t)pseudo_header[i * 2]) << 8) + pseudo_header[i * 2 + 1];
  }

  // "The checksum field is the 16 bit one's complement of the one's
  // complement sum of all 16 bit words in the header and text."

  // TCP header
  uint8_t *tcp_data = (uint8_t *)tcp;
  for (int i = 0; i < tcp->doff * 2; i++) {
    checksum += (((uint32_t)tcp_data[i * 2]) << 8) + tcp_data[i * 2 + 1];
  }

  // TCP payload
  uint8_t *payload = tcp_data + tcp->doff * 4;
  int payload_len = ntohs(ip->ip_len) - ip->ip_hl * 4 - tcp->doff * 4;
  for (int i = 0; i < payload_len; i++) {
    if ((i % 2) == 0) {
      checksum += (((uint32_t)payload[i]) << 8);
    } else {
      checksum += payload[i];
    }
  }

  while (checksum >= 0x10000) {
    checksum -= 0x10000;
    checksum += 1;
  }
  return checksum == 0xffff;
}

// TODO(step 1: sequence number comparison and generation)
bool tcp_seq_lt(uint32_t a, uint32_t b) {
  // UNIMPLEMENTED()
  // return true;
  return (int)(a - b) < 0;
}

bool tcp_seq_le(uint32_t a, uint32_t b) {
  // UNIMPLEMENTED()
  // return true;
  return (int)(a - b) <= 0;
}

bool tcp_seq_gt(uint32_t a, uint32_t b) {
  // UNIMPLEMENTED()
  // return true;
  return (int)(a - b) > 0;
}

bool tcp_seq_ge(uint32_t a, uint32_t b) {
  // UNIMPLEMENTED()
  // return true;
  return (int)(a - b) >= 0;
}

// returns fd
int tcp_socket() {
  for (int i = 0;; i++) {
    if (tcp_fds.find(i) == tcp_fds.end()) {
      // found free fd, create one
      TCP *tcp = new TCP;
      tcp_fds[i] = tcp;

      // add necessary initialization here
      return i;
    }
  }
}

void set_nagle(bool nagle, int listen_fd) { 
  tcp_fds[listen_fd]->nagle = nagle;
}

void tcp_connect(int fd, uint32_t dst_addr, uint16_t dst_port) {
  TCP *tcp = tcp_fds[fd];

  tcp->local_ip = client_ip;
  // random local port
  tcp->local_port = 40000 + (rand() % 10000);
  tcp->remote_ip = dst_addr;
  tcp->remote_port = dst_port;

  // initialize params
  // assume maximum mss for remote by default
  tcp->local_mss = tcp->remote_mss = DEFAULT_MSS;

  uint32_t initial_seq = generate_initial_seq();
  // rfc793 page 54 OPEN Call CLOSED STATE
  // https://www.rfc-editor.org/rfc/rfc793.html#page-54
  tcp->iss = initial_seq;
  // only one unacknowledged number: initial_seq
  // "Set SND.UNA to ISS, SND.NXT to ISS+1, enter SYN-SENT
  // state, and return."
  tcp->snd_una = initial_seq;
  tcp->snd_nxt = initial_seq + 1;
  tcp->snd_wnd = 0;
  tcp->rcv_wnd = tcp->recv.free_bytes();
  tcp->snd_wl2 = initial_seq - 1;
  tcp->set_state(TCPState::SYN_SENT);

  // send SYN to remote
  // 44 = 20(IP) + 24(TCP)
  // with 4 bytes option(MSS)
  uint8_t buffer[44];
  construct_ip_header(buffer, tcp, sizeof(buffer));

  // tcp
  TCPHeader *tcp_hdr = (TCPHeader *)&buffer[20];
  memset(tcp_hdr, 0, 20);
  tcp_hdr->source = htons(tcp->local_port);
  tcp_hdr->dest = htons(tcp->remote_port);
  tcp_hdr->seq = htonl(initial_seq);
  // flags
  tcp_hdr->doff = 24 / 4; // 24 bytes
  tcp_hdr->syn = 1;

  // TODO(step 3: send & receive)
  // window size: size of empty bytes in recv buffer
  tcp_hdr->window = htons(tcp->recv.free_bytes());
  //UNIMPLEMENTED_WARN();

  // mss option, rfc793 page 18
  // https://www.rfc-editor.org/rfc/rfc793.html#page-18
  buffer[40] = 0x02; // kind
  buffer[41] = 0x04; // length
  buffer[42] = tcp->local_mss >> 8;
  buffer[43] = tcp->local_mss;

  update_tcp_ip_checksum(buffer);
  send_packet(buffer, sizeof(buffer));
  if (!tcp->nagle){
  tcp->push_to_retransmission_queue(buffer, sizeof(buffer), 0);
  // start retransmission timer
  Retransmission retransmission_fn;
  retransmission_fn.fd = fd;
  TIMERS.add_job(retransmission_fn, current_ts_msec());
  }
  return;
}

ssize_t tcp_write(int fd, const uint8_t *data, size_t size) {
  TCP *tcp = tcp_fds[fd];
  assert(tcp);

  // rfc793 page 56 SEND Call
  // https://www.rfc-editor.org/rfc/rfc793.html#page-56
  if (tcp->state == TCPState::SYN_SENT || tcp->state == TCPState::SYN_RCVD) {
    // queue data for transmission
    return tcp->send.write(data, size);
  } else if (tcp->state == TCPState::ESTABLISHED ||
             tcp->state == TCPState::CLOSE_WAIT) {
    // queue data for transmission
    size_t res = tcp->send.write(data, size);
    //printf("TCP SEND SIZE=%zu \n", tcp->send.size);

    // send data to remote
    size_t bytes_to_send = tcp->send.size;
    bytes_to_send -= tcp->send.sent_size;
    // TODO(step 3: send & receive)
    // consider mss and send sequence space
    // send sequence space: https://www.rfc-editor.org/rfc/rfc793.html#page-20
    // figure 4 compute the segment length to send
    #ifdef ENABLE_NAGLE

    if (bytes_to_send < NAGLE_SIZE && tcp->nagle) {
      tcp->nagle_timer = current_ts_msec();
      size_t bytes_read = tcp->send.read(
          &tcp->nagle_buffer[tcp->nagle_buffer_size], bytes_to_send);
      tcp->nagle_buffer_size += bytes_read;
      Nagle nagle_fn;
      nagle_fn.fd = fd;
      TIMERS.add_job(nagle_fn, current_ts_msec());
      if (tcp->nagle_buffer_size>NAGLE_SIZE){
        tcp->send_nagle();
      }else{
        printf("|* Nagle Saved %zu *|\n", tcp->nagle_buffer_size);
      }
    } else {
      while (bytes_to_send) {
        //printf("BYTES TO SEND TO REMOTE=%zu \n", bytes_to_send);
        
        size_t segment_len =
          bytes_to_send < tcp->remote_mss ? bytes_to_send : tcp->remote_mss;
        bytes_to_send -= segment_len;
        // UNIMPLEMENTED()
        if (segment_len > 0) {
          printf("Sending segment of len %zu to remote\n", segment_len);
          // send data now

          // 20 IP header & 20 TCP header
          uint16_t total_length = 20 + 20 + segment_len;
          uint8_t buffer[MTU];
          construct_ip_header(buffer, tcp, total_length);

          // tcp
          TCPHeader *tcp_hdr = (TCPHeader *)&buffer[20];
          memset(tcp_hdr, 0, 20);
          tcp_hdr->source = htons(tcp->local_port);
          tcp_hdr->dest = htons(tcp->remote_port);
          // this segment occupies range:
          // [snd_nxt, snd_nxt+seg_len)
          tcp_hdr->seq = htonl(tcp->snd_nxt);
          tcp->snd_nxt += segment_len;
          // flags
          tcp_hdr->doff = 20 / 4; // 20 bytes

          // TODO(step 3: send & receive)
          // set ack bit and ack_seq
          tcp_hdr->ack = 1;
          tcp_hdr->ack_seq = htonl(tcp->rcv_nxt);
          // TODO(step 3: send & receive)
          // window size: size of empty bytes in recv buffer
          tcp_hdr->window = htons(tcp->recv.free_bytes());
          //UNIMPLEMENTED();
          //If the urgent flag is set, then SND.UP <- SND.NXT-1 and set the
          //urgent pointer in the outgoing segments.
          // if (tcp_hdr->urg){
          //   tcp_hdr->urg_ptr = htons(tcp->snd_nxt - 1);
          // }
          // payload
          size_t bytes_read = tcp->send.read(&buffer[40], segment_len);
          // should never fail


          update_tcp_ip_checksum(buffer);
          send_packet(buffer, total_length);
          if (!tcp->nagle){
            tcp->push_to_retransmission_queue(buffer, 52, segment_len);
            // overflow
            // start retransmission timer
            Retransmission retransmission_fn;
            retransmission_fn.fd = fd;
            TIMERS.add_job(retransmission_fn, current_ts_msec());
          }
        }

      }
    }
    #else
    printf("NO NAGLE\n");
    
      while (bytes_to_send) {
      //printf("BYTES TO SEND TO REMOTE=%zu \n", bytes_to_send);
        size_t send_sequence_space = tcp->snd_una + tcp->snd_wnd - tcp->snd_nxt;
        size_t segment_len =
            bytes_to_send < tcp->remote_mss ? bytes_to_send : tcp->remote_mss;
        segment_len = send_sequence_space < segment_len ? send_sequence_space
                                                        : segment_len;
        segment_len = tcp->cwnd < segment_len ? tcp->cwnd : segment_len;
        bytes_to_send -= segment_len;
        // UNIMPLEMENTED()
        if (segment_len > 0) {
          printf("Sending segment of len %zu to remote\n", segment_len);
          // send data now

          // 20 IP header & 20 TCP header
          uint16_t total_length = 20 + 20 + segment_len;
          uint8_t buffer[MTU];
          construct_ip_header(buffer, tcp, total_length);

          // tcp
          TCPHeader *tcp_hdr = (TCPHeader *)&buffer[20];
          memset(tcp_hdr, 0, 20);
          tcp_hdr->source = htons(tcp->local_port);
          tcp_hdr->dest = htons(tcp->remote_port);
          // this segment occupies range:
          // [snd_nxt, snd_nxt+seg_len)
          tcp_hdr->seq = htonl(tcp->snd_nxt);
          tcp->snd_nxt += segment_len;
          // flags
          tcp_hdr->doff = 20 / 4; // 20 bytes

          // TODO(step 3: send & receive)
          // set ack bit and ack_seq
          tcp_hdr->ack = 1;
          tcp_hdr->ack_seq = htonl(tcp->rcv_nxt);
          // TODO(step 3: send & receive)
          // window size: size of empty bytes in recv buffer
          tcp_hdr->window = htons(tcp->recv.free_bytes());
          //UNIMPLEMENTED();
          //If the urgent flag is set, then SND.UP <- SND.NXT-1 and set the
          //urgent pointer in the outgoing segments.
          // if (tcp_hdr->urg){
          //   tcp_hdr->urg_ptr = htons(tcp->snd_nxt - 1);
          // }
          // payload
          size_t bytes_read = tcp->send.read(&buffer[40], segment_len);
          // should never fail


          update_tcp_ip_checksum(buffer);
          send_packet(buffer, total_length);
          if (!tcp->nagle){
            tcp->push_to_retransmission_queue(buffer, 52, segment_len);
            // start retransmission timer
            Retransmission retransmission_fn;
            retransmission_fn.fd = fd;
            TIMERS.add_job(retransmission_fn, current_ts_msec());
          }
          
        }

      }
    
    #endif
    
    
    return res;
  }
  return -1;
}

ssize_t tcp_read(int fd, uint8_t *data, size_t size) {
  TCP *tcp = tcp_fds[fd];
  assert(tcp);

  // TODO(step 3: send & receive)
  // copy from recv_buffer to user data
  //UNIMPLEMENTED();
  auto bytes = tcp->recv.read(data, size);
  return bytes;
}

void tcp_shutdown(int fd) {
  TCP *tcp = tcp_fds[fd];
  assert(tcp);

  // CLOSE Call
  // ESTABLISHED STATE
  if (tcp->state == TCPState::ESTABLISHED) {
    // TODO(step 4: connection termination)
    // "Queue this until all preceding SENDs have been segmentized, then
    // form a FIN segment and send it. In any case, enter FIN-WAIT-1 state."
    //UNIMPLEMENTED();
    uint8_t buffer[40];
    construct_ip_header(buffer, tcp, sizeof(buffer));
    TCPHeader *tcp_hdr = (TCPHeader *)&buffer[20];
    memset(tcp_hdr, 0, 20);
    tcp_hdr->source = htons(tcp->local_port);
    tcp_hdr->dest = htons(tcp->remote_port);
    tcp_hdr->seq = htonl(tcp->snd_nxt);

    tcp_hdr->doff = 5;
    tcp_hdr->fin = 1;
    tcp_hdr->window = htons(tcp->recv.free_bytes());
    tcp->snd_nxt += 1;
    update_tcp_ip_checksum(buffer);

    send_packet(buffer, sizeof(buffer));
    if (!tcp->nagle){
    tcp->push_to_retransmission_queue(buffer, sizeof(buffer), 0);
    // start retransmission timer
    Retransmission retransmission_fn;
    retransmission_fn.fd = fd;
    TIMERS.add_job(retransmission_fn, current_ts_msec());
    }

    printf("we are here\n");
    printf("Connection closing\n");
    tcp->set_state(TCPState::FIN_WAIT_1);
  } else if (tcp->state == TCPState::CLOSE_WAIT) {
    // TODO(step 4: connection termination)
    // CLOSE_WAIT STATE
    // "Queue this request until all preceding SENDs have been
    // segmentized; then send a FIN segment, enter LAST-ACK state."
    //UNIMPLEMENTED();
    uint8_t buffer[40];
    construct_ip_header(buffer, tcp, sizeof(buffer));
    TCPHeader *tcp_hdr = (TCPHeader *)&buffer[20];
    memset(tcp_hdr, 0, 20);
    tcp_hdr->source = htons(tcp->local_port);
    tcp_hdr->dest = htons(tcp->remote_port);
    tcp_hdr->seq = htonl(tcp->snd_nxt);
    tcp_hdr->doff = 5;
    tcp_hdr->fin = 1;
    tcp->snd_nxt += 1;
    tcp_hdr->window = htons(tcp->recv.free_bytes());

    update_tcp_ip_checksum(buffer);
    send_packet(buffer, sizeof(buffer));
    if (!tcp->nagle){
      tcp->push_to_retransmission_queue(buffer, sizeof(buffer), 0);
      // start retransmission timer
      Retransmission retransmission_fn;
      retransmission_fn.fd = fd;
      TIMERS.add_job(retransmission_fn, current_ts_msec());
    }
    tcp->set_state(TCPState::LAST_ACK);
  }
}

void tcp_close(int fd) {
  // shutdown first
  tcp_shutdown(fd);

  TCP *tcp = tcp_fds[fd];
  assert(tcp);

  // remove connection if closed
  if (tcp->state == TCPState::CLOSED) {
    printf("Removing TCP connection fd=%d\n", fd);
    tcp_fds.erase(fd);
    delete tcp;
  }
}

void tcp_bind(int fd, be32_t addr, uint16_t port) {
  TCP *tcp = tcp_fds[fd];
  assert(tcp);

  tcp->local_ip = addr;
  tcp->local_port = port;
  // wildcard
  tcp->remote_ip = 0;
  tcp->remote_port = 0;
}

void tcp_listen(int fd) {
  TCP *tcp = tcp_fds[fd];
  assert(tcp);

  // enter listen state
  tcp->set_state(TCPState::LISTEN);
}

int tcp_accept(int fd) {
  TCP *tcp = tcp_fds[fd];
  assert(tcp);

  // pop fd from accept queue
  if (tcp->accept_queue.empty()) {
    return -1;
  } else {
    int fd = tcp->accept_queue.front();
    tcp->accept_queue.pop_front();
    return fd;
  }
}

TCPState tcp_state(int fd) {
  TCP *tcp = tcp_fds[fd];
  assert(tcp);
  return tcp->state;
}