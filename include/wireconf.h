
/*
 * Copyright (c) 2006-2009 The StarBED Project  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the project nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE PROJECT AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE PROJECT OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/************************************************************************
 *
 * QOMET Emulator Implementation
 *
 * File name: wireconf.h
 * Function: Header file of wired-network emulator configuration
 *
 * Authors: Junya Nakata, Razvan Beuran
 * Changes: Kunio AKASHI
 *
 ***********************************************************************/

#ifdef __FreeBSD__
#include <netinet/ip_fw.h>
#include <netinet/ip_dummynet.h>
#endif


#ifndef	__WIRECONF_H
#define	__WIRECONF_H_

#include <sys/queue.h>
#include <sys/socket.h>

#include <sys/types.h>

#include <net/if.h>
#include <netinet/in.h>


#define DIRECTION_BOTH                  0
#define DIRECTION_IN                    1
#define DIRECTION_OUT                   2

#define MAX_NODES                       500

int get_socket(void);
void close_socket(int socket_id);


#ifdef __FreeBSD__
int get_rule(uint s, int16_t rulenum);
#elif __linux
int get_rule_linux(void);
#endif

int init_rule(char *dst);
int add_rule(int s, uint16_t rulenum, int pipe_nr, char *src, char *dst, int direction);
int configure_rule(int s, char* dst, int handle, int bandwidth, float delay, double lossrate);
int delete_rule(uint s, char *dst, u_int32_t rule_number);

// print a rule structure
#ifdef __FreeBSD__
void print_rule(struct ip_fw *rule);
#endif

// print a pipe structure
#ifdef __FreeBSD__
void print_pipe(struct dn_pipe *pipe);
#endif


struct wireconf_class {
    struct timespec start_time;
    int my_id;
    struct in_addr *IP_addresses;
    int node_count;
    pthread_t stats_send_thread;
    int do_interrupt_send;
    int stats_provide_pipe[2];
    pthread_t stats_listen_thread;
    int do_interrupt_listen;
    float *total_channel_utilizations;
    float *total_transmission_probabilities;
    float *self_channel_utilizations;
    float *self_transmission_probabilities;
    float total_channel_utilization_others;
    float total_transmission_probability_others;
    float total_self_number_packets;
    float **channel_utilization_per_source;
    /* NOTE: the code below intended for the next version of wireconf,
       that will be able to read QOMET binary output

    // data queue of this chanel
    data_queue_class data_queue;

    // array of flags showing which destinations are
    // active (TRUE=>active, FALSE=>inactive)
    int active_destinations[MAX_DESTINATIONS];

    // configuration array
    deltaQ_class configurations[MAX_DESTINATIONS];
     */

};
#endif