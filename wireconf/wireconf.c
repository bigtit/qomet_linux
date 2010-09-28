
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
 * File name: wireconf.c
 * Function: Main source file of the wired-network emulator 
 *           configurator library
 *
 * Authors: Junya Nakata, Razvan Beuran
 *
 ***********************************************************************/

#include <sys/queue.h>
#include <sys/socket.h>

#include <net/if.h>
#include <netinet/in.h>

#ifdef __FreeBSD__
#include <netinet/ip_fw.h>
#include <netinet/ip_dummynet.h>
#endif

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "global.h"
#include "wireconf.h"
#ifdef __linux
#include "utils.h"
#include "ip_common.h"
//#include "iproute.h"
#include "tc_common.h"
#include "tc_util.h"
#endif



/////////////////////////////////////////////
// Special defines
/////////////////////////////////////////////

// this is only used in conjunction with the new
// modified version of dummynet that is still in
// experimental phase
//#define NEW_DUMMYNET #(version 6??)


/////////////////////////////////////////////
// Message-handling functions
/////////////////////////////////////////////

#ifdef MESSAGE_WARNING
#define WARNING(message...) do {                                             \
  fprintf(stdout, "libwireconf WARNING: %s, line %d: ", __FILE__, __LINE__); \
  fprintf(stdout, message); fprintf(stdout,"\n");                            \
} while(0)
#else
#define WARNING(message...) /* message */
#endif

#ifdef MESSAGE_DEBUG
#define DEBUG(message...) do {                                             \
  fprintf(stdout, "libwireconf DEBUG: %s, line %d: ", __FILE__, __LINE__); \
  fprintf(stdout, message); fprintf(stdout,"\n");                          \
} while(0)
#else
#define DEBUG(message...) /* message */
#endif

#ifdef MESSAGE_INFO
#define INFO(message...) do {                                             \
  fprintf(stdout, "libwireconf INFO: %s, line %d: ", __FILE__, __LINE__); \
  fprintf(stdout, message); fprintf(stdout,"\n");                         \
} while(0)
#else
#define INFO(message...) /* message */
#endif


//static struct 
//{
//	char netem_child[20];
//	char tbf_child[20];
//}rootid;

///////////////////////////////////////////////
// IPv4 address data structure and manipulation
///////////////////////////////////////////////

// IPv4 address structure
typedef union
{
  uint8_t octet[4];
  uint32_t word;
} in4_addr;

// convert a string containing an IPv4 address 
// to an IPv4 data structure;
// return SUCCESS on success, ERROR on error
#ifdef __FreeBSD__
static int atoaddr(char *array_address, in4_addr *ipv4_address, uint16_t *port)
{
  int c, o;
  
  c = 0, o = 0, ipv4_address->word = 0, *port = 0;

  for(;;)
    {
      if(array_address[c] == '.')
	{
	  c++, o++;
	  continue;
	} 
      else if(isdigit(array_address[c]) == 0)
	break;
		
      ipv4_address->octet[o] = ipv4_address->octet[o] * 10 + 
	array_address[c] - '0';
      c++;
    }

  if(array_address[c] == '\0')
    return SUCCESS;
  else if((o != 3) || (array_address[c] != ':'))
    return ERROR;
  c++;

  for(;;)
    {
      if(isdigit(array_address[c]) == 0)
	break;
      *port = *port * 10 + array_address[c] - '0';
      c++;
    }
  return SUCCESS;
}
#endif


/////////////////////////////////////////////
// Socket manipulation functions
/////////////////////////////////////////////

// get a socket identifier;
// return socket identifier on succes, ERROR on error
int get_socket(void)
{
  int socket_id;
  
  if((socket_id = socket(AF_INET, SOCK_RAW, IPPROTO_RAW)) < 0)
    {
      WARNING("Error creating socket");
      perror("socket");
      return ERROR;
    }

  return socket_id;
}

// close socket specified by socket_id
void close_socket(int socket_id)
{
  close(socket_id);
}

// apply socket options related to ipfw commands
#ifdef __FreeBSD__
static int apply_socket_options(int s, int option_id, void *option, 
				socklen_t option_length)
{
  // choose the action corresponding to the given command
  switch(option_id)
    {
      // add a rule to the firewall (the rule contains dummynet pipes);
      // option should be of type "struct ip_fw*"
    case IP_FW_ADD:
#ifdef MESSAGE_DEBUG
      DEBUG("Add ipfw rule: "); 
      print_rule((struct ip_fw*)option);
#endif

      if(getsockopt(s, IPPROTO_IP, option_id, option, &option_length) < 0) 
	{
	  WARNING("Error getting socket options");
	  perror("getsockopt");
	  return ERROR;
	}
      break;

      // delete a firewall rule (the rule contains dummynet pipes);
      // option should be of type "u_int32_t *"
    case IP_FW_DEL:
      DEBUG("Delete ipfw rule #%d", (*(u_int32_t *)option));

      if(setsockopt(s, IPPROTO_IP, option_id, option, option_length)<0)
	{
	  WARNING("Error setting socket options");
	  perror("setsockopt");
	  return ERROR;
	}
      break;

      // configure a dummynet pipe;
      // option should be of type "dn_pipe *"
    case IP_DUMMYNET_CONFIGURE:
#ifdef MESSAGE_DEBUG
      DEBUG("Configure ipfw dummynet pipe: "); 
      print_pipe((struct dn_pipe*)option);
#endif

      if(setsockopt(s, IPPROTO_IP, option_id, option, option_length)<0)
	{
	  WARNING("Error setting socket options");
	  perror("setsockopt");
	  return ERROR;
	}
      break;

      /* FUTURE PLAN: DELETE _PIPES_ INSTEAD OF RULES
    case IP_DUMMYNET_DEL:
      printf("delete pipe: option_id=%d option=%d option_length=%d\n",
	     option_id, (*(int16_t*)(option)), option_length);
      if(setsockopt(s, IPPROTO_IP, option_id, option, option_length)<0)
	{
	  WARNING("Error setting socket options");
	  perror("setsockopt");
	  return ERROR;
	}
      break;
      */

    default:
      WARNING("Unrecognized ipfw socket option: %d", option_id);
      return -1;
    }
 
   return 0;
}
#endif


////////////////////////////////////////////////
// Functions implemented by the wireconf library
////////////////////////////////////////////////

// get a rule
// NOT WORKING PROPERLY YET!!!!!!!!!!!!!
int
get_rule(uint s, int16_t rulenum)
{
#ifdef __FreeBSD__
  //int RULES=100;
  int i=0;
  struct ip_fw rules[10];

  socklen_t rules_size;

  bzero(&rules, sizeof(rules));
  rules_size=sizeof(rules);
  //rule.rulenum = rulenum;

  //  printf("Get rule: "); print_rule(&rule);
  if(getsockopt(s, IPPROTO_IP, IP_FW_GET, &rules, &rules_size) < 0) 
    {
      WARNING("Error getting socket options");
      perror("getsockopt");
      return -1;
    }
  printf("Got rules:\n");
  for(i=0; i<10; i++)
    print_rule(&rules[i]);

#elif __linux
	system("tc qdisc show");
#endif
  return 0;
}

// add an ipfw rule containing a dummynet pipe to the firewall
// return SUCCESS on succes, ERROR on error
#ifdef __FreeBSD__
int add_rule(int s, uint16_t rulenum, int pipe_nr, char *src, char *dst, 
	     int direction)
{
  int clen;
  in4_addr addr;
  uint16_t port;
  ipfw_insn cmd[10];
  struct ip_fw *p;
  
  // additional rule length counter
  clen = 0;

  // process source address

  if(strncmp(src, "any", strlen(src))==0)
    {
      // source address was "any"
      cmd[clen].opcode = O_IP_SRC;
      cmd[clen].len = 0;
      cmd[clen].arg1 = 0;
    }
  else
    {
      if(atoaddr(src, &addr, &port) < 0)
	{
	  WARNING("Invalid argument to add_rule: %s\n", src);
	  return ERROR;
	}
      cmd[clen].opcode = O_IP_SRC;
      cmd[clen].len = 2;
      cmd[clen].arg1 = 0;
      ((uint32_t *)cmd)[clen + 1] = addr.word;
      clen += 2;
      if(port > 0)
	{
	  cmd[clen].opcode = O_IP_SRCPORT;
	  cmd[clen].len = 2;
	  cmd[clen].arg1 = 0;
	  ((uint32_t *)cmd)[clen + 1]
	    = port | port << 16;
	  clen += 2;
	}
    }

  // process destination address
  if(strncmp(dst, "any", strlen(dst))==0)
    {
      // destination address was "any"
      cmd[clen].opcode = O_IP_DST;
      cmd[clen].len = 0;
      cmd[clen].arg1 = 0;
    }
  else
    {
      if(atoaddr(dst, &addr, &port) < 0)
	{
	  WARNING("Invalid argument to add_rule: %s", dst);
	  return ERROR;
	}

      cmd[clen].opcode = O_IP_DST;
      cmd[clen].len = 2;
      cmd[clen].arg1 = 0;
      ((uint32_t *)cmd)[clen + 1] = addr.word;
      clen += 2;
      if(port > 0)
	{
	  cmd[clen].opcode = O_IP_DSTPORT;
	  cmd[clen].len = 2;
	  cmd[clen].arg1 = 0;
	  ((uint32_t *)cmd)[clen + 1]
	    = port | port << 16;
	  clen += 2;
	}
    }

  // use in/out direction indicators
  if(direction != DIRECTION_BOTH)
    {
      // basic command code for in/out operation
      cmd[clen].opcode = O_IN; 
      cmd[clen].len = 1;

      // a negation mask is used for meaning "out"
      if(direction == DIRECTION_OUT)
	cmd[clen].len |= F_NOT;
      //printf("len=0x%x len&F_NOT=0x%x flen=%d\n", cmd[clen].len, cmd[clen].len & F_NOT, F_LEN(cmd+clen));
      clen += 1;
    }

  // configure pipe
  cmd[clen].opcode = O_PIPE;
  cmd[clen].len = 2;
  cmd[clen].arg1 = pipe_nr;
  ((uint32_t *)cmd)[clen + 1] = 0;
  clen += 1;	/* trick! */
  if((p = (struct ip_fw *)malloc(sizeof(struct ip_fw) + clen * 4)) == NULL) 
    {
      WARNING("Could not allocate memory for a new rule");
      return ERROR;
    }
  bzero(p, sizeof(struct ip_fw));
  p->act_ofs = clen - 1;
  p->cmd_len = clen + 1;
  p->rulenum = rulenum;
  bcopy(cmd, &p->cmd, clen * 4);

  // apply appropriate socket options
  if(apply_socket_options(s, IP_FW_ADD, p, sizeof(struct ip_fw) + clen*4) < 0)
    {
      WARNING("Adding rule operation failed");
      return ERROR;
    }
  return 0;
}
#elif __linux
int
add_rule(int s, uint16_t rulenum, int handle_nr, char *src, char *dst, int direction)
{
	//system("tc qdisc add dev lo root handle 10: netem delay 1us");
	//system("tc qdisc add dev lo parent 10:1 handle 100: tbf rate 100mbit limit 10mb buffer 10kb/8");
	struct qdisc_parameter qp;
	char handleid[10];

	// netem parameter set
	memset(&qp, 0, sizeof(qp));

	sprintf(handleid, "%d", handle_nr);
	printf("rulenum = %s\n", handleid);

	qp.limit = "1000";
	qp.delay = "1us";
	qp.jitter = "0";
    qp.delay_corr = "0";
    qp.loss = "0";
    qp.loss_corr = "0";
    qp.reorder_prob = "0";
    qp.reorder_corr = "0";
	qp.rate = "1Gbit";
	qp.buffer = "1Mbit";

	tc_cmd(RTM_NEWQDISC, NLM_F_EXCL|NLM_F_CREATE, (char* )get_route_info("dev", dst), handleid, "1", qp, "netem");
	//tc_cmd(RTM_NEWQDISC, NLM_F_EXCL|NLM_F_CREATE, (char* )get_route_info("dev", dst), handleid, "1", qp, "tbf");

	return 0;
}
#endif

// delete an ipfw rule;
// return SUCCESS on succes, ERROR on error
#ifdef __FreeBSD__
int delete_rule(uint s, u_int32_t rule_number)
{
  // Note: rule number is of type u_int16_t in ip_fw.h,
  // but a comment in ip_fw2.c shows that the expected size
  // when applying socket options is u_int32_t (see comment below)

  /* COMMENT FROM IP_FW2.C
   *
   * IP_FW_DEL is used for deleting single rules or sets,
   * and (ab)used to atomically manipulate sets. Argument size
   * is used to distinguish between the two:
   *    sizeof(u_int32_t)
   *	delete single rule or set of rules,
   *	or reassign rules (or sets) to a different set.
   *    2*sizeof(u_int32_t)
   *	atomic disable/enable sets.
   *	first u_int32_t contains sets to be disabled,
   *	second u_int32_t contains sets to be enabled.
   */

  // do delete rule
  if(apply_socket_options(s, IP_FW_DEL, &rule_number, sizeof(rule_number)) < 0)
    {
      WARNING("Delete rule operation failed");
      return ERROR;
    }

  return SUCCESS;
}
#elif __linux
int delete_netem(uint s, char* dst, u_int32_t rule_number)
{
	printf("delete qdisc\n");
	struct qdisc_parameter qp;

	memset(&qp, 0, sizeof(qp));

/*
	qp.limit = "1000";
	qp.delay = "1us";
	qp.jitter = "0";
	qp.delay_corr = "0";
	qp.loss = "0";
	qp.loss_corr = "0";
	qp.reorder_prob = "0";
	qp.reorder_corr = "0";
	qp.rate = "1Gbit";
	qp.buffer = "1Mbit";
*/

	//system("tc qdisc del dev lo root");
	tc_cmd(RTM_DELQDISC, 0, (char* )get_route_info("dev", dst), "1", "0", qp, "netem");
	//tc_cmd(RTM_DELQDISC, 0, (char* )get_route_info("dev", dst), "1", "0", qp, "netem");
	//tc_cmd(RTM_DELQDISC, 0, (char* )get_route_info("dev", "127.0.0.1"), "1", "0", qp, "netem");
#endif

  return SUCCESS;
}

// print a rule structure
#ifdef __FreeBSD__
void print_rule(struct ip_fw *rule)
{
  printf("Rule #%d (size=%d):\n", rule->rulenum, sizeof(*rule));
  printf("\tnext=%p next_rule=%p\n", rule->next, rule->next_rule);
  printf("\tact_ofs=%u cmd_len=%u rulenum=%u set=%u _pad=%u\n", 
	 rule->act_ofs, rule->cmd_len, rule->rulenum, rule->set, rule->_pad);
  printf("\tpcnt=%llu bcnt=%llu timestamp=%u\n", rule->pcnt, rule->bcnt, 
	 rule->timestamp);
}
#endif

// print a pipe structure
#ifdef __FreeBSD__
void print_pipe(struct dn_pipe *pipe)
{
  printf("Pipe #%d (size=%d):\n", pipe->pipe_nr, sizeof(*pipe));
  //printf("\tnext=%p pipe_nr=%u\n", pipe->next.sle_next, pipe->pipe_nr);

#ifdef NEW_DUMMYNET
  printf("\tbandwidth=%u delay=%u delay_type=%u fs.plr=%u fs.qsize=%u\n", 
	 pipe->bandwidth, pipe->delay, pipe->delay_type, pipe->fs.plr, 
	 pipe->fs.qsize);
#else
  printf("\tbandwidth=%u delay=%u fs.plr=%u fs.qsize=%u\n", 
	 pipe->bandwidth, pipe->delay, pipe->fs.plr, pipe->fs.qsize);
#endif
}
#endif

// configure a dummynet pipe;
// return SUCCESS on succes, ERROR on error
#ifdef __FreeBSD__
int configure_pipe(int s, int pipe_nr, int bandwidth, int delay, int lossrate)
{
  struct dn_pipe p;

  // reset data structure  
  bzero(&p, sizeof(p));

  // initialize appropriate fields
  p.pipe_nr = pipe_nr;
  p.fs.plr = lossrate;
  p.bandwidth = bandwidth;
  p.delay = delay;

  // set queue size to a small value to avoid large delays 
  // in case bandwidth limitation is enforced
  p.fs.qsize = 2;

  if(apply_socket_options(s, IP_DUMMYNET_CONFIGURE, &p, sizeof(p)) < 0)
    {
      WARNING("Pipe configuration could not be applied");
      return ERROR;
    }
  return SUCCESS;
}
#elif __linux
int convert_netemid(int handle)
{
	int id;

	id = handle;
	id <<= 16;
	id |= handle;

	return id;
}
int configure_qdisc(int s, char* dst, int handle, int bandwidth, int delay, double lossrate)
{
	struct qdisc_parameter qp;
	char delaystr[20];
	char loss[20];
	char handleid[10];
	char rate[20];
	char buffer[20];
	//char bw_cmd[100];

	memset(&qp, 0, sizeof(qp));

	int netemid;
	netemid = convert_netemid(handle);
	printf("netem id = %d\n", netemid);
	netemid |= handle;
	int parent;
	parent = netemid;
	parent >>= 16;
	int child;
	child = netemid;
	child <<= 16;
	child >>= 16;
	printf("parent id = %d\n", parent);
	printf("child id = %d\n", child);
	
	printf("rulenum = %d\n", handle);

	sprintf(handleid, "%d", handle);
	sprintf(delaystr, "%dms", delay);
	sprintf(loss, "%f", lossrate);
	sprintf(rate, "%d", bandwidth);
	sprintf(buffer, "%d", bandwidth / 1024);

	qp.limit = "1000";
	qp.delay = delaystr;
	qp.jitter = "0";
    qp.delay_corr = "0";
	qp.loss = loss;
    qp.loss_corr = "0";
    qp.reorder_prob = "0";
    qp.reorder_corr = "0";
	qp.rate = rate;
	qp.buffer = buffer;
	
	//tc_cmd(RTM_NEWQDISC, 0, (char* )get_route_info("dev", "127.0.0.1"), handleid, "1", qp, "netem");
	tc_cmd(RTM_NEWQDISC, 0, (char* )get_route_info("dev", dst), handleid, "1", qp, "netem");
	//tc_cmd(RTM_NEWQDISC, 0, (char* )get_route_info("dev", argv[2]), "1", "1", qp, "netem");
	//sprintf(bw_cmd, "tc qdisc change dev lo parent 1:1 handle 100: tbf rate %dbit limit 15kb buffer 10kb/8", 
	//	bandwidth);

	//system(bw_cmd);

	return SUCCESS;
}
#endif
