/**************************************************************************
* This code is licensed under the Apache License 2.0.  See ../LICENSE     *
* Copyright 2015 John Denholm                                             *
*                                                                         *
* io.h - defines network buffers and io routines                          *
*                                                                         *
* Updates:                                                                *
**************************************************************************/

#ifndef MINISTRY_IO_H
#define MINISTRY_IO_H


#define IO_BUF_SZ				0x40000		// 256k
#define IO_BUF_HWMK				0x3c000		// 240k
#define IO_MAX_WAITING			128			// makes for 128 * 256k = 32M


struct io_buffer
{
	IOBUF		*	next;
	char		*	ptr;		// holds memory even if not requested
	char		*	buf;
	char		*	hwmk;
	int				len;
	int				sz;
	int				refs;		// how many outstanding to send?
};

struct io_buffer_list
{
	IOLIST		*	next;
	IOLIST		*	prev;
	IOBUF		*	buf;
};



// r/w
int io_read_data( NSOCK *s );
int io_read_lines( HOST *h );
int io_write_data( NSOCK *s, int off );

int io_connected( NSOCK *s );
int io_connect( TARGET *t );

void io_buf_send( IOBUF *buf );

throw_fn io_loop;

void io_start( void );
void io_stop( void );


#endif
