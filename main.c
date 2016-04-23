#define _GNU_SOURCE
#include <signal.h>
#include <poll.h>
#include <stdio.h>
#include <pcap.h>
#include <time.h>
#include <net/ethernet.h>
#include <arpa/inet.h>
#include <ncurses.h>

#include "utlist.h"
#include "uthash.h"
#include "flow.h"
#include "decode.h"
#include "timeywimey.h"
#include "intervals.h"

static const char const *protos[IPPROTO_MAX] = {[IPPROTO_TCP] = "TCP",
	                                        [IPPROTO_UDP] = "UDP",
	                                        [IPPROTO_ICMP] = "ICMP",
	                                        [IPPROTO_ICMPV6] = "ICMP6",
	                                        [IPPROTO_IP] = "IP",
	                                        [IPPROTO_IGMP] = "IGMP" };

#define ERR_LINE_OFFSET 2
#define TOP_N_LINE_OFFSET 5
#define TP1_COL 48
#define TP2_COL 59

#define HEADER1 \
"                                 Source|SPort|Proto"
#define HEADER2 \
"                            Destination|DPort|B/s @%3dms|B/s @%3dms"

int print_tp_hdrs(int tp1, int interval1, int tp2, int interval2)
{
	enum speeds { BPS, KBPS, MBPS, GBPS };
	static char * const units[] = {
		[BPS]  = "B/s",
		[KBPS] = "kB/s",
		[MBPS] = "MB/s",
		[GBPS] = "GB/s"
	};

	char *unit;
	int div;

	if (tp1 > 1E9) {
		unit = units[GBPS];
		div = 1E9;
	} else if (tp1 > 1E6) {
		unit = units[MBPS];
		div = 1E6;
	} else if (tp1 > 1E3) {
		unit = units[KBPS];
		div = 1E3;
	} else {
		unit = units[BPS];
		div = 1;
	}

	attron(A_BOLD);
	mvprintw(TOP_N_LINE_OFFSET, 1, HEADER1);
	mvprintw(TOP_N_LINE_OFFSET + 1, 1,
"                            Destination|DPort|%4s@%3dms|%4s@%3dms          ",
	         unit, interval1 / 1000,
	         unit, interval2 / 1000);

	attroff(A_BOLD);
	return div;
}

void print_top_n(int stop)
{
	int row = 3, flowcnt = stop;
	char ip_src[16];
	char ip_dst[16];
	char ip6_src[40];
	char ip6_dst[40];

	flowcnt = get_flow_count();
	mvprintw(0, 50, "%5d active flows", flowcnt);

	const int interval1 = 7, interval2 = 3;

	/* Clear the table */
	for (int i = TOP_N_LINE_OFFSET + row;
	     i <= TOP_N_LINE_OFFSET + row + 3 * stop; i++) {
		mvprintw(i, 0, "%80s", " ");
	}

	struct top_flows *t5 = malloc(sizeof(struct top_flows));
	memset(t5, 0, sizeof(struct top_flows));
	get_top5(t5);

	for (int i = 0; i < flowcnt && i < 5; i++) {
		int div;

		struct flow_record *fte1 = &(t5->flow[i][interval1]);
		struct flow_record *fte2 = &(t5->flow[i][interval2]);

		sprintf(ip_src, "%s", inet_ntoa(fte1->flow.src_ip));
		sprintf(ip_dst, "%s", inet_ntoa(fte1->flow.dst_ip));
		inet_ntop(AF_INET6, &(fte1->flow.src_ip6), ip6_src,
		          sizeof(ip6_src));
		inet_ntop(AF_INET6, &(fte1->flow.dst_ip6), ip6_dst,
		          sizeof(ip6_dst));

		if (0 == i) {
			div = print_tp_hdrs(fte1->size,
			                    intervals[interval1],
			                    fte2->size,
			                    intervals[interval2]);
		}

		switch (fte1->flow.ethertype) {
		case ETHERTYPE_IP:
			mvaddch(TOP_N_LINE_OFFSET + row + 0, 0, ACS_ULCORNER);
			mvaddch(TOP_N_LINE_OFFSET + row + 1, 0, ACS_LLCORNER);
			mvprintw(TOP_N_LINE_OFFSET + row + 0, 1, "%39s",
			         ip_src);
			mvprintw(TOP_N_LINE_OFFSET + row + 1, 1, "%39s",
			         ip_dst);
			mvprintw(TOP_N_LINE_OFFSET + row + 0, 40, "%6d",
			         fte1->flow.sport);
			mvprintw(TOP_N_LINE_OFFSET + row + 1, 40, "%6d",
			         fte1->flow.dport);
			mvprintw(TOP_N_LINE_OFFSET + row + 0, 47, "%s",
			         protos[fte1->flow.proto]);
			mvprintw(TOP_N_LINE_OFFSET + row + 1, 47, "%10d %10d",
			         fte1->size / div, fte2->size / div);
			mvprintw(TOP_N_LINE_OFFSET + row + 2, 0, "%80s", " ");
			row += 3;
			break;

		case ETHERTYPE_IPV6:
			mvaddch(TOP_N_LINE_OFFSET + row + 0, 0, ACS_ULCORNER);
			mvaddch(TOP_N_LINE_OFFSET + row + 1, 0, ACS_LLCORNER);
			mvprintw(TOP_N_LINE_OFFSET + row + 0, 1, "%39s",
			         ip6_src);
			mvprintw(TOP_N_LINE_OFFSET + row + 1, 1, "%39s",
			         ip6_dst);
			mvprintw(TOP_N_LINE_OFFSET + row + 0, 40, "%6d",
			         fte1->flow.sport);
			mvprintw(TOP_N_LINE_OFFSET + row + 1, 40, "%6d",
			         fte1->flow.dport);
			mvprintw(TOP_N_LINE_OFFSET + row + 0, 47, "%s",
			         protos[fte1->flow.proto]);
			mvprintw(TOP_N_LINE_OFFSET + row + 1, 47, "%10d %10d",
			         fte1->size / div, fte2->size / div);
			mvprintw(TOP_N_LINE_OFFSET + row + 2, 0, "%80s", " ");
			row += 3;
			break;
		default:
			mvprintw(ERR_LINE_OFFSET, 0, "%80s", " ");
			mvprintw(ERR_LINE_OFFSET, 0, "Unknown ethertype: %d",
			         fte1->flow.ethertype);
		}
	}
	free(t5);
}

void handle_packet(uint8_t *user, const struct pcap_pkthdr *pcap_hdr,
                   const uint8_t *wirebits)
{
	static const struct flow_pkt zp = { 0 };
	struct flow_pkt *pkt;
	char errstr[DECODE_ERRBUF_SIZE];

	pkt = malloc(sizeof(struct flow_pkt));
	*pkt = zp;

	if (0 == decode_ethernet(pcap_hdr, wirebits, pkt, errstr)) {
		update_stats_tables(pkt);
	} else {
		mvprintw(ERR_LINE_OFFSET, 0, "%-80s", errstr);
	}

	free(pkt);
}

void grab_packets(int fd, pcap_t *handle)
{
	struct timespec poll_timeout = {.tv_sec = 0, .tv_nsec = 5E6 };
	struct timespec print_timeout, now;
	int ch;

	struct pollfd fds[] = {
		{.fd = fd, .events = POLLIN, .revents = POLLHUP }
	};

	clock_gettime(CLOCK_MONOTONIC, &print_timeout);
	print_timeout = ts_add(print_timeout, poll_timeout);

	while (1) {
		if (ppoll(fds, 1, &poll_timeout, NULL)) {
			pcap_dispatch(handle, 100000, handle_packet, NULL);
		}

		if ((ch = getch()) == ERR) {
			/* normal case - no input */
			;
		} else {
			switch (ch) {
			case 'q':
				endwin(); /* End curses mode */
				return;
			}
		}

	        clock_gettime(CLOCK_MONOTONIC, &now);

		if (0 > ts_cmp(print_timeout, now)) {
			print_timeout = ts_add(print_timeout, poll_timeout);
			print_top_n(5);
			refresh(); /* ncurses screen update */
		}
	}
}

void init_curses()
{
	initscr();            /* Start curses mode              */
	raw();                /* Line buffering disabled        */
	keypad(stdscr, TRUE); /* We get F1, F2 etc..            */
	noecho();             /* Don't echo() while we do getch */
	nodelay(stdscr, TRUE);
}

int main(int argc, char *argv[])
{
	char *dev, errbuf[PCAP_ERRBUF_SIZE];
	pcap_t *handle;
	int selectable_fd;

	if (argc == 2) {
		dev = argv[1];
	} else {
		dev = pcap_lookupdev(errbuf);
	}

	if (dev == NULL) {
		fprintf(stderr, "Couldn't find default device: %s\n", errbuf);
		return (2);
	}

	handle = pcap_open_live(dev, BUFSIZ, 1, 0, errbuf);
	if (handle == NULL) {
		fprintf(stderr, "Couldn't open device %s: %s\n", dev, errbuf);
		return (2);
	}

	if (pcap_datalink(handle) != DLT_EN10MB) {
		fprintf(stderr, "Device %s doesn't provide Ethernet headers - "
		                "not supported\n",
		        dev);
		return (2);
	}

	if (pcap_setnonblock(handle, 1, errbuf) != 0) {
		fprintf(stderr, "Non-blocking mode failed: %s\n", errbuf);
		return (2);
	}

	selectable_fd = pcap_get_selectable_fd(handle);
	if (-1 == selectable_fd) {
		fprintf(stderr, "pcap handle not selectable.\n");
		return (2);
	}

	init_curses();
	mvprintw(0, 0, "Device:");
	attron(A_BOLD);
	mvprintw(0, 10, "%s\n", dev);
	attroff(A_BOLD);

	grab_packets(selectable_fd, handle);

	/* And close the session */
	pcap_close(handle);
	return 0;
}
