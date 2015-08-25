#include "ministry.h"


static uint32_t data_cksum_primes[8] =
{
	2909, 3001, 3083, 3187, 3259, 3343, 3517, 3581
};

uint32_t data_path_cksum( char *str, int len )
{
#ifdef CKSUM_64BIT
	register uint64_t *p, sum = 0xbeef;
#else
	register uint32_t *p, sum = 0xbeef;
#endif
	int rem;

#ifdef CKSUM_64BIT
	rem = len & 0x7;
	len = len >> 3;

	for( p = (uint64_t *) str; len > 0; len-- )
#else
	rem = len & 0x3;
	len = len >> 2;

	for( p = (uint32_t *) str; len > 0; len-- )
#endif
		sum ^= *p++;

	// and capture the rest
	str = (char *) p;
	while( rem-- > 0 )
		sum += *str++ * data_cksum_primes[rem];

#ifdef CKSUM_64BIT
	sum = ( ( sum >> 32 ) ^ ( sum & 0xffffffff ) ) & 0xffffffff;
#endif

	return sum;
}


inline DSTAT *data_find_stat( uint32_t hval, uint32_t indx, char *path, int len )
{
	DSTAT *d;

	for( d = ctl->data->stats[indx]; d; d = d->next )
	{
		if( d->sum == hval
		 && d->len == len
		 && !memcmp( d->path, path, len ) )
			break;
	}

	return d;
}

inline DADD *data_find_add( uint32_t hval, uint32_t indx, char *path, int len )
{
	DADD *d;

	for( d = ctl->data->add[indx]; d; d = d->next )
	{
		if( d->sum == hval
		 && d->len == len
		 && !memcmp( d->path, path, len ) )
			break;
	}

	return d;
}



void data_point_add( char *path, int len, unsigned long long val )
{
	uint32_t hval, indx;
	DADD *d;

	hval = data_path_cksum( path, len );
	indx = hval % ctl->data->hsize;

	if( !( d = data_find_add( hval, indx, path, len ) ) )
	{
		pthread_mutex_lock( &(ctl->locks->addtable ) );

		if( !( d = data_find_add( hval, indx, path, len ) ) )
		{
			d = mem_new_dadd( path, len );
			d->id   = ++(ctl->data->apaths);
			d->len  = len;
			d->path = str_copy( path, len );
			d->sum  = hval;

			d->next = ctl->data->add[indx];
			ctl->data->add[indx] = d;
		}

		pthread_mutex_unlock( &(ctl->locks->addtable) );
	}

	// lock that path
	lock_adder( d );

	// add in that data point
	d->total += val;

	// and unlock
	unlock_adder( d );
}


void data_point_stat( char *path, int len, float val )
{
	uint32_t hval, indx;
	PTLIST *p;
	DSTAT *d;

	hval = data_path_cksum( path, len );
	indx = hval % ctl->data->hsize;

	// there is a theoretical race condition here
	// so we check again in a moment under lock
	if( !( d = data_find_stat( hval, indx, path, len ) ) )
	{
		pthread_mutex_lock( &(ctl->locks->stattable) );

		if( !( d = data_find_stat( hval, indx, path, len ) ) )
		{
			d = mem_new_dstat( path, len );
			d->id   = ++(ctl->data->spaths);
			d->len  = len;
			d->path = str_copy( path, len );
			d->sum  = hval;

			d->next = ctl->data->stats[indx];
			ctl->data->stats[indx] = d;
		}

		pthread_mutex_unlock( &(ctl->locks->stattable) );
	}

	// lock that path
	lock_stats( d );

	// find where to store the points
	for( p = d->points; p; p = p->next )
		if( p->count < PTLIST_SIZE )
			break;

	// make a new one if need be
	if( !p )
	{
		p = mem_new_point( );
		p->next   = d->points;
		d->points = p;
	}

	// keep that data point
	p->vals[p->count++] = val;
	d->total++;

	// and unlock
	unlock_stats( d );
}


// support the statsd format
// path:<val>|<c or ms>
void data_line_statsd( HOST *h, char *line, int len )
{
	char *cl, *vb;
	int plen;

	if( !( cl = memchr( line, ':', len ) ) )
	{
		h->invalid++;
		return;
	}

	// stomp on that
	*cl  = '\0';
	plen = cl - line;

	cl++;
	len -= plen + 1;

	if( !( vb = memchr( cl, '|', len ) ) )
	{
		h->invalid++;
		return;
	}

	// and stomp on that
	*vb++ = '\0';

	switch( *vb )
	{
		case 'c':
			// counter - integer
			data_point_add( line, plen, strtoull( cl, NULL, 10 ) );
			h->points++;
			break;
		case 'm':
			// msec - double
			data_point_stat( line, plen, strtod( cl, NULL ) );
			h->points++;
			break;
		default:
			h->invalid++;
			break;
	}
}


void data_line_data( HOST *h, char *line, int len )
{
	// break that up
	strwords( h->val, line, len, FIELD_SEPARATOR );

	// broken?
	if( h->val->wc < STAT_FIELD_MIN )
	{
		h->invalid++;
		return;
	}

	// looks OK
	h->points++;

	data_point_stat( h->val->wd[0], h->val->len[0],
	                 strtod( h->val->wd[1], NULL ) );
}


void data_line_adder( HOST *h, char *line, int len )
{
	// break it up
	strwords( h->val, line, len, FIELD_SEPARATOR );

	// broken?
	if( h->val->wc < STAT_FIELD_MIN )
	{
		h->invalid++;
		return;
	}

	// looks ok
	h->points++;

	// and put that in
	data_point_add( h->val->wd[0], h->val->len[0],
				strtoull( h->val->wd[1], NULL, 10 ) );
}




void data_handle_connection( HOST *h )
{
	struct pollfd p;
	int rv, i;

	p.fd     = h->net->sock;
	p.events = POLL_EVENTS;

	while( ctl->run_flags & RUN_LOOP )
	{
		if( ( rv = poll( &p, 1, 500 ) ) < 0 )
		{
			// don't sweat interruptions
			if( errno == EINTR )
			{
				debug( "Poll call was interrupted." );
				continue;
			}

			warn( "Poll error talk to host %s -- %s",
				h->net->name, Err );
			break;
		}
		if( !rv )
			continue;

		// they went away?
		if( p.revents & POLLHUP )
			break;

		// 0 means gone away
		if( ( rv = net_read_lines( h ) ) <= 0 )
		{
			h->flags |= HOST_CLOSE;
			break;
		}

		// mark them active
		h->last = ctl->curr_time.tv_sec;

		// and handle them
		for( i = 0; i < h->all->wc; i++ )
			if( h->all->len[i] > 0 )
				(*(h->type->handler))( h, h->all->wd[i], h->all->len[i] );

		// allow data fetch to tell us to close this host down
		if( h->flags & HOST_CLOSE )
			break;
	}
}



void *data_connection( void *arg )
{
	THRD *t;
	HOST *h;

	t = (THRD *) arg;
	h = (HOST *) t->arg;

	info( "Accepted data connection from host %s", h->net->name );

	// make sure we can be cancelled
	pthread_setcancelstate( PTHREAD_CANCEL_ENABLE, NULL );
	pthread_setcanceltype( PTHREAD_CANCEL_ASYNCHRONOUS, NULL );

	data_handle_connection( h );

	info( "Closing connection to host %s after %lu data points.",
			h->net->name, h->points );

	net_close_host( h );

	free( t );
	return NULL;
}



void *data_loop_udp( void *arg )
{
	socklen_t sl;
	NET_PORT *n;
	THRD *t;
	NBUF *b;
	HOST *h;
	int i;

	t = (THRD *) arg;
	n = (NET_PORT *) t->arg;
	h = mem_new_host( );
	b = h->net->in;

	h->type = n->type;

	loop_mark_start( "udp" );

	while( ctl->run_flags & RUN_LOOP )
	{
		// get a packet, set the from
		sl = sizeof( struct sockaddr_in );
		if( ( b->len = recvfrom( n->sock, b->buf, b->sz, 0,
						(struct sockaddr *) &(h->peer), &sl ) ) < 0 )
		{
			if( errno == EINTR || errno == EAGAIN )
				continue;

			err( "Recvfrom error -- %s", Err );
			loop_end( "receive error on udp socket" );
			break;
		}
		else if( !b->len )
			continue;

		if( b->len < b->sz )
			b->buf[b->len] = '\0';

		// break that up
		if( !strwords( h->all, (char *) b->buf, b->len, LINE_SEPARATOR ) )
			continue;

		// and handle them
		for( i = 0; i < h->all->wc; i++ )
			if( h->all->len[i] > 0 )
				(*(h->type->handler))( h, h->all->wd[i], h->all->len[i] );
	}

	loop_mark_done( "udp" );

	mem_free_host( &h );

	free( t );
	return NULL;
}




void *data_loop_tcp( void *arg )
{
	struct pollfd p;
	NET_PORT *n;
	THRD *t;
	HOST *h;
	int rv;

	t = (THRD *) arg;
	n = (NET_PORT *) t->arg;

	p.fd     = n->sock;
	p.events = POLL_EVENTS;

	loop_mark_start( "tcp" );

	while( ctl->run_flags & RUN_LOOP )
	{
		if( ( rv = poll( &p, 1, 500 ) ) < 0 )
		{
			if( errno != EINTR )
			{
				err( "Poll error on %s -- %s", n->type->label, Err );
				loop_end( "polling error on tcp socket" );
				break;
			}
		}
		else if( !rv )
			continue;

		// go get that then
		if( p.revents & POLL_EVENTS )
		{
			h = net_get_host( p.fd, n->type );
			thread_throw_watched( data_connection, h );
		}
	}

	loop_mark_done( "tcp" );

	free( t );
	return NULL;
}



void data_start( NET_TYPE *nt )
{
	int i;

	if( !( nt->flags & NTYPE_ENABLED ) )
		return;

	if( nt->flags & NTYPE_TCP_ENABLED )
		thread_throw( data_loop_tcp, nt->tcp );

	if( nt->flags & NTYPE_UDP_ENABLED )
		for( i = 0; i < nt->udp_count; i++ )
			thread_throw( data_loop_udp, nt->udp[i] );

	info( "Started listening for data on %s", nt->label );
}


// allocate the hash structures now that hsize is finalized
void data_init( void )
{
	ctl->data->stats = (DSTAT **) allocz( ctl->data->hsize * sizeof( DSTAT * ) );
	ctl->data->add   = (DADD **)  allocz( ctl->data->hsize * sizeof( DADD * )  );
}


DATA_CTL *data_config_defaults( void )
{
	DATA_CTL *d;

	d              = (DATA_CTL *) allocz( sizeof( DATA_CTL ) );
	d->hsize       = DATA_HASH_SIZE;
	d->submit_intv = DATA_SUBMIT_INTV;

	return d;
}


int data_config_line( AVP *av )
{
	if( attIs( "hashsize" ) )
		ctl->data->hsize = atoi( av->val );
	else if( attIs( "submit" ) )
	{
		ctl->data->submit_intv = atoi( av->val );
		// we assume a value too low is in seconds
		if( ctl->data->submit_intv < 500 )
		{
			info( "Converting submit interval %d to usec.",
				ctl->data->submit_intv );
			ctl->data->submit_intv *= 1000000;
		}
	}
	else
		return -1;

	return 0;
}


