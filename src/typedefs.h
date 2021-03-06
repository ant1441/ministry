/**************************************************************************
* This code is licensed under the Apache License 2.0.  See ../LICENSE     *
* Copyright 2015 John Denholm                                             *
*                                                                         *
* typedefs.h - major typedefs for other structures                        *
*                                                                         *
* Updates:                                                                *
**************************************************************************/

#ifndef MINISTRY_TYPEDEFS_H
#define MINISTRY_TYPEDEFS_H

typedef struct ministry_control		MIN_CTL;
typedef struct log_control			LOG_CTL;
typedef struct lock_control			LOCK_CTL;
typedef struct mem_control			MEM_CTL;
typedef struct network_control		NET_CTL;
typedef struct stats_control		STAT_CTL;
typedef struct synth_control        SYN_CTL;

typedef struct net_in_port			NET_PORT;
typedef struct net_type				NET_TYPE;
typedef struct stat_config			ST_CFG;
typedef struct stat_thread_ctl		ST_THR;
typedef struct stat_threshold		ST_THOLD;
typedef struct config_context		CCTXT;
typedef struct points_list			PTLIST;
typedef struct path_sum				PTSUM;
typedef struct mem_type_blank		MTBLANK;
typedef struct ip_network			IPNET;
typedef struct ip_check				IPCHK;
typedef struct mem_type_control		MTYPE;
typedef union  data_hash_vals		DVAL;
typedef struct data_hash_entry		DHASH;
typedef struct io_buffer			IOBUF;
typedef struct io_buffer_list		IOLIST;
typedef struct net_socket			NSOCK;
typedef struct network_target		TARGET;
typedef struct host_data			HOST;
typedef struct thread_data			THRD;
typedef struct synth_data			SYNTH;
typedef struct words_data			WORDS;
typedef struct av_pair				AVP;

// function types
typedef void loop_call_fn ( int64_t, void * );
typedef void * throw_fn ( void * );
typedef void add_fn ( char *, int, char * );
typedef void line_fn ( HOST *, char *, int );
typedef void synth_fn( SYNTH * );

#endif
