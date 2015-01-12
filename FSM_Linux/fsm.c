//
// fsm.c
//
// Created by Minsuk Lee, 2014.11.1.
// Updated by Younggi YUN, 2015. 1. 10
// Copyright (c) 2014. Minsuk Lee All rights reserved.
// see LICENSE

#include "util.h"

#define CONNECT_TIMEOUT      1
#define SENDING_DATA_TIMEOUT 2
#define RANGE_OF_RESENDING   3

#define NUM_STATE   4
#define NUM_EVENT   9

enum pakcet_type { F_CON = 0, F_FIN = 1, F_ACK = 2, F_DATA = 3 };                  // Packet Type
enum proto_state { wait_CON = 0, CON_sent = 1, CONNECTED = 2, DATA_sending = 3 };   // States

// Events
enum proto_event {
	RCV_CON = 0, RCV_FIN = 1, RCV_ACK = 2, RCV_DATA = 3,
	CONNECT = 4, CLOSE = 5, SEND = 6, TIMEOUT = 7, TIMEOUT_SENDING = 8
};

char *pkt_name[] = { "F_CON", "F_FIN", "F_ACK", "F_DATA" };
char *st_name[] = { "wait_CON", "CON_sent", "CONNECTED", "CON_sending" };
char *ev_name[] = { "RCV_CON", "RCV_FIN", "RCV_ACK", "RCV_DATA",
"CONNECT", "CLOSE", "SEND", "TIMEOUT", "TIMEOUT_SENDING" };

struct state_action {            // Protocol FSM Structure
	void(*action)(void *p);
	enum proto_state next_state;
};

#define MAX_DATA_SIZE   (500)

struct packet {                  // 504 Byte Packet to & from Simulator
	unsigned short type;         // enum packet_type
	unsigned short size;
	char data[MAX_DATA_SIZE];
};

struct p_event {                 // Event Structure
	enum proto_event event;
	struct packet packet;
	int size;
};

enum proto_state c_state = wait_CON;         // Initial State
volatile int timedout = 0;

static void timer_handler(int signum)
{
	printf("Timedout\n");
	timedout = 1;
}

static void timer_init(void)
{
	struct sigaction sa;

	memset(&sa, 0, sizeof (sa));
	sa.sa_handler = &timer_handler;
	sigaction(SIGALRM, &sa, NULL);
}

void set_timer(int sec)
{
	struct itimerval timer;

	timedout = 0;
	timer.it_value.tv_sec = sec;
	timer.it_value.tv_usec = 0;
	timer.it_interval.tv_sec = 0;   // Non Periodic timer
	timer.it_interval.tv_usec = 0;
	setitimer(ITIMER_REAL, &timer, NULL);
}

void send_packet(int flag, void *p, int size)
{
	struct packet pkt;
	printf("SEND %s\n", pkt_name[flag]);

	pkt.type = flag;
	pkt.size = size;

	if (size)
		memcpy(pkt.data, ((struct p_event *)p)->packet.data, (size > MAX_DATA_SIZE) ? MAX_DATA_SIZE : size);

	Send((char *)&pkt, sizeof(struct packet) - MAX_DATA_SIZE + size);
}

static void report_connect(void *p)
{
	set_timer(0);           // Stop Timer
	printf("Connected\n");
}

static void passive_con(void *p)
{
	send_packet(F_ACK, NULL, 0);
	report_connect(NULL);
}

static void active_con(void *p)
{
	send_packet(F_CON, NULL, 0);
	set_timer(CONNECT_TIMEOUT);
}

static void close_con(void *p)
{
	send_packet(F_FIN, NULL, 0);
	printf("Connection Closed\n");
}

char recent_sent_data[MAX_DATA_SIZE] = "";
int  recent_sent_data_size;

static void send_data(void *p)
{
	printf("Send Data to peer '%s' size:%d \n",
		((struct p_event*)p)->packet.data, ((struct p_event*)p)->size);
	send_packet(F_DATA, (struct p_event *)p, ((struct p_event *)p)->size);
	memcpy(recent_sent_data, ((struct p_event*)p)->packet.data, strlen(((struct p_event*)p)->packet.data) + 1);
	recent_sent_data_size = ((struct p_event*)p)->size;
	set_timer(SENDING_DATA_TIMEOUT);
}

int number_of_resending = 0;

static void resend_data(void *p)
{
	set_timer(0);           // Stop Timer
	printf("Resend Data to peer '%s' size:%d try:%d/%d \n",
		recent_sent_data, recent_sent_data_size, ++number_of_resending, RANGE_OF_RESENDING);

	memcpy(((struct p_event*)p)->packet.data, recent_sent_data, strlen(recent_sent_data) + 1);
	((struct p_event*)p)->size = recent_sent_data_size;

	send_packet(F_DATA, (struct p_event *)p, ((struct p_event *)p)->size);
	set_timer(SENDING_DATA_TIMEOUT);
}

static void stop_resending(void *p)
{
	set_timer(0);
	number_of_resending = 0;
	fputs("resending is over\n", stdout);
}

static void activate_resend(void *p)
{
	set_timer(SENDING_DATA_TIMEOUT);
}

char duplicated_data[MAX_DATA_SIZE] = "";

static void report_data(void *p)
{
	send_packet(F_ACK, NULL, 0);

	if (!strcmp(((struct p_event*)p)->packet.data, duplicated_data))
		return;

	printf("Data Arrived data='%s' size:%d\n",
		((struct p_event*)p)->packet.data, ((struct p_event*)p)->packet.size);

	sprintf(duplicated_data, "%s", ((struct p_event*)p)->packet.data);
}

struct state_action p_FSM[NUM_STATE][NUM_EVENT] = {
	//  for each event:
	//  RCV_CON,                 RCV_FIN,                 RCV_ACK,                       RCV_DATA,
	//  CONNECT,                 CLOSE,                   SEND,                          TIMEOUT
	//  TIMEOUT_SENDING

	// - wait_CON state
	{ { passive_con, CONNECTED }, { NULL, wait_CON }, { NULL, wait_CON }, { NULL, wait_CON },
	{ active_con, CON_sent }, { NULL, wait_CON }, { NULL, wait_CON }, { NULL, wait_CON },
	{ NULL, wait_CON } },

	// - CON_sent state
	{ { passive_con, CONNECTED }, { close_con, wait_CON }, { report_connect, CONNECTED }, { NULL, CON_sent },
	{ NULL, CON_sent }, { close_con, wait_CON }, { NULL, CON_sent }, { close_con, wait_CON },
	{ NULL, CON_sent } },

	// - CONNECTED state
	{ { NULL, CONNECTED }, { close_con, wait_CON }, { NULL, CONNECTED }, { report_data, CONNECTED },
	{ NULL, CONNECTED }, { close_con, wait_CON }, { send_data, DATA_sending }, { activate_resend, DATA_sending },
	{ NULL, CONNECTED } },

	// - DATA_sending 
	{ { NULL, DATA_sending }, { NULL, DATA_sending }, { stop_resending, CONNECTED }, { report_data, CONNECTED },
	{ NULL, DATA_sending }, { close_con, wait_CON }, { NULL, DATA_sending }, { resend_data, DATA_sending },
	{ stop_resending, CONNECTED } },
};

int data_count = 0;

struct p_event *get_event(void)
{
	static struct p_event event;    // not thread-safe

loop:
	// Check if there is user command
	if (!kbhit()) {
		// Check if timer is timed-out
		if (timedout) {
			if (number_of_resending < RANGE_OF_RESENDING) {
				event.event = TIMEOUT;
			}
			else {
				number_of_resending = 0;
				event.event = TIMEOUT_SENDING;
			}
			timedout = 0;
		}
		else {
			// Check Packet arrival by event_wait()
			ssize_t n = Recv((char*)&event.packet, sizeof(struct packet));
			if (n > 0) {
				// if then, decode header to make event
				switch (event.packet.type) {
				case F_CON:  event.event = RCV_CON;  break;
				case F_ACK:  event.event = RCV_ACK;  break;
				case F_FIN:  event.event = RCV_FIN;  break;
				case F_DATA:
					event.event = RCV_DATA;
					event.size = event.packet.size;
					break;
				default:
					goto loop;
				}
			}
			else
				goto loop;
		}
	}
	else {
		int n = getchar();
		switch (n) {
		case '0': event.event = CONNECT; break;
		case '1': event.event = CLOSE;   break;
		case '2':
			event.event = SEND;
			sprintf(event.packet.data, "%09d", data_count++);
			event.size = strlen(event.packet.data) + 1;
			break;
		case '3': return NULL;  // QUIT
		default:
			goto loop;
		}
	}
	return &event;
}

void
Protocol_Loop(void)
{
	struct p_event *eventp;

	timer_init();

	while (1) {
		printf("Current State = %s\n", st_name[c_state]);

		/* Step 0: Get Input Event */
		if ((eventp = get_event()) == NULL)
			break;
		printf("EVENT : %s\n", ev_name[eventp->event]);

		/* Step 1: Do Action */
		if (p_FSM[c_state][eventp->event].action)
			p_FSM[c_state][eventp->event].action(eventp);
		else
			printf("No Action for this event\n");

		/* Step 2: Set Next State */
		c_state = p_FSM[c_state][eventp->event].next_state;
	}
}

int
main(int argc, char *argv[])
{
	ChannelNumber channel;
	ID id;
	int rate_of_packet_loss;

	printf("Channel : ");
	scanf("%d", &channel);
	printf("ID : ");
	scanf("%d", &id);
	printf("Rate of Packet Loss (0 ~ 100)%% : ");
	scanf("%d", &rate_of_packet_loss);
	if (rate_of_packet_loss < 0)
		rate_of_packet_loss = 0;
	else if (rate_of_packet_loss > 100)
		rate_of_packet_loss = 100;

	// Login to SIMULATOR

	if (Login(channel, id, rate_of_packet_loss) == -1) {
		printf("Login Failed\n");
		return -1;
	}

	printf("Entering protocol loop...\n");
	printf("type number '[0]CONNECT', '[1]CLOSE', '[2]SEND', or '[3]QUIT'\n");
	Protocol_Loop();

	// SIMULATOR_CLOSE

	return 0;
}
