#include "ministry.h"


/*
 * Watched sockets
 *
 * This works by thread_throw_network creating a watcher
 * thread.  As there are various lock-up issues with
 * networking, we have a watcher thread that keeps a check
 * on the networking thread.  It watches for the thread to
 * still exist and the network start time to match what it
 * captured early on.
 *
 * If it sees the time since last activity get too high, we
 * assume that the connection has died.  So we kill the
 * thread and close the connection.
 */

void *net_watched_socket( void *arg )
{
	double stime;
	//pthread_t nt;
	THRD *t;
	HOST *h;

	t = (THRD *) arg;
	h = (HOST *) t->arg;

	// capture the start time - it's sort of a thread id
	stime = h->started;
	debug( "Connection from %s starts at %.6f", h->net->name, stime );

	// capture the thread ID of the watched thread
	// when throwing the handler function
	//nt = thread_throw( t->fp, t->arg );
	thread_throw( t->fp, t->arg );

	while( ctl->run_flags & RUN_LOOP )
	{
		// safe because we never destroy host structures
		if( h->started != stime )
		{
            debug( "Socket has been freed or re-used." );
			break;
		}

		// just use our maintained clock
		if( ( ctl->curr_time.tv_sec - h->last ) > ctl->net->dead_time )
		{
			// cancel that thread
			//pthread_cancel( nt );
			notice( "Connection from host %s timed out.", h->net->name );
            h->net->flags |= HOST_CLOSE;
			// net_close_host( h );
			break;
		}

		// we are not busy threads around these parts
		usleep( 200000 );
	}

	debug( "Watcher of thread %lu, exiting." );
	free( t );
	return NULL;
}


void net_disconnect( int *sock, char *name )
{
	if( shutdown( *sock, SHUT_RDWR ) )
		err( "Shutdown error on connection with %s -- %s",
			name, Err );

	close( *sock );
	*sock = -1;
}


void net_close_host( HOST *h )
{
	net_disconnect( &(h->net->sock), h->net->name );
	debug( "Closed connection from host %s.", h->net->name );

	mem_free_host( &h );
}


HOST *net_get_host( int sock, NET_TYPE *type )
{
	struct sockaddr_in from;
	socklen_t sz;
	char buf[32];
	int d, l;
	HOST *h;

	sz = sizeof( from );

	if( ( d = accept( sock, (struct sockaddr *) &from, &sz ) ) < 0 )
	{
		// broken
		err( "Accept error -- %s", Err );
		return NULL;
	}

	l = snprintf( buf, 32, "%s:%hu", inet_ntoa( from.sin_addr ),
		ntohs( from.sin_port ) );

	h            = mem_new_host( );
	h->peer      = from;
	h->net->name = str_copy( buf, l );
	h->net->sock = d;
	// should be a unique timestamp
	h->started   = ctl->curr_time.tv_sec;
	h->last      = h->started;
	h->type      = type;

	return h;
}




NSOCK *net_make_sock( int insz, int outsz, char *name, struct sockaddr_in *peer )
{
	NSOCK *ns;

	ns = (NSOCK *) allocz( sizeof( NSOCK ) );

	if( name )
		ns->name = strdup( name );

	ns->peer = peer;
	ns->keep = mem_new_buf( 0 );

	if( insz )
	{
		ns->in = mem_new_buf( insz );
		ns->keep->buf  = ns->in->buf;
		ns->keep->sz   = insz;
		ns->keep->hwmk = ns->in->hwmk;
	}
	if( outsz )
		ns->out = mem_new_buf( outsz );

	// no socket yet
	ns->sock = -1;

	return ns;
}





int net_listen_tcp( unsigned short port, uint32_t ip, int backlog )
{
	struct sockaddr_in sa;
	int s, so;

	if( ( s = socket( AF_INET, SOCK_STREAM, 0 ) ) < 0 )
	{
		err( "Unable to make tcp listen socket -- %s", Err );
		return -1;
	}

	so = 1;
	if( setsockopt( s, SOL_SOCKET, SO_REUSEADDR, &so, sizeof( int ) ) )
	{
		err( "Set socket options error for listen socket -- %s", Err );
		close( s );
		return -2;
	}

	memset( &sa, 0, sizeof( struct sockaddr_in ) );
	sa.sin_family = AF_INET;
	sa.sin_port   = htons( port );

	// ip as well?
	sa.sin_addr.s_addr = ( ip ) ? ip : INADDR_ANY;

	// try to bind
	if( bind( s, (struct sockaddr *) &sa, sizeof( struct sockaddr_in ) ) < 0 )
	{
		err( "Bind to %s:%hu failed -- %s",
			inet_ntoa( sa.sin_addr ), port, Err );
		close( s );
		return -3;
	}

	if( !backlog )
		backlog = 5;

	if( listen( s, backlog ) < 0 )
	{
		err( "Listen error -- %s", Err );
		close( s );
		return -4;
	}

	info( "Listening on tcp port %s:%hu for connections.", inet_ntoa( sa.sin_addr ), port );

	return s;
}


int net_listen_udp( unsigned short port, uint32_t ip )
{
	struct sockaddr_in sa;
	struct timeval tv;
	int s;

	if( ( s = socket( AF_INET, SOCK_DGRAM, IPPROTO_UDP ) ) < 0 )
	{
		err( "Unable to make udp listen socket -- %s", Err );
		return -1;
	}

	tv.tv_sec  = ctl->net->rcv_tmout;
	tv.tv_usec = 0;

	debug( "Setting receive timeout to %ld.%06ld", tv.tv_sec, tv.tv_usec );

	if( setsockopt( s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof( struct timeval ) ) < 0 )
	{
		err( "Set socket options error for listen socket -- %s", Err );
		close( s );
		return -2;
	}

	memset( &sa, 0, sizeof( struct sockaddr_in ) );
	sa.sin_family = AF_INET;
	sa.sin_port   = htons( port );

	// ip as well?
	sa.sin_addr.s_addr = ( ip ) ? ip : INADDR_ANY;

	// try to bind
	if( bind( s, (struct sockaddr *) &sa, sizeof( struct sockaddr_in ) ) < 0 )
	{
		err( "Bind to %s:%hu failed -- %s",
			inet_ntoa( sa.sin_addr ), port, Err );
		close( s );
		return -1;
	}

	info( "Bound to udp port %s:%hu for packets.", inet_ntoa( sa.sin_addr ), port );

	return s;
}







int net_startup( NET_TYPE *nt )
{
	int i, j;

	if( !( nt->flags & NTYPE_ENABLED ) )
		return 0;

	if( nt->flags & NTYPE_TCP_ENABLED )
	{
		nt->tcp->sock = net_listen_tcp( nt->tcp->port, nt->tcp->ip, nt->tcp->back );
		if( nt->tcp->sock < 0 )
			return -1;
	}

	if( nt->flags & NTYPE_UDP_ENABLED )
	{
		for( i = 0; i < nt->udp_count; i++ )
		{
			// grab the udp ip variable
			nt->udp[i]->ip   = nt->udp_bind;
			nt->udp[i]->sock = net_listen_udp( nt->udp[i]->port, nt->udp[i]->ip );
			if( nt->udp[i]->sock < 0 )
			{
				if( nt->flags & NTYPE_TCP_ENABLED )
				{
					close( nt->tcp->sock );
					nt->tcp->sock = -1;
				}

				for( j = 0; j < i; j++ )
				{
					close( nt->udp[j]->sock );
					nt->udp[j]->sock = -1;
				}
				return -2;
			}
			debug( "Bound udp port %hu with socket %d",
					nt->udp[i]->port, nt->udp[i]->sock );
		}
	}

	notice( "Started up %s", nt->label );
	return 0;
}



int net_start( void )
{
	int ret = 0;

	notice( "Starting networking." );

	ret += net_startup( ctl->net->data );
	ret += net_startup( ctl->net->statsd );
	ret += net_startup( ctl->net->adder );

	return ret;
}


void net_shutdown( NET_TYPE *nt )
{
	int i;

	if( !( nt->flags & NTYPE_ENABLED ) )
		return;

	if( ( nt->flags & NTYPE_TCP_ENABLED ) && nt->tcp->sock > -1 )
	{
		close( nt->tcp->sock );
		nt->tcp->sock = -1;
	}

	if( nt->flags & NTYPE_UDP_ENABLED )
		for( i = 0; i < nt->udp_count; i++ )
			if( nt->udp[i]->sock > -1 )
			{
				close( nt->udp[i]->sock );
				nt->udp[i]->sock = -1;
			}

	notice( "Stopped %s", nt->label );
}



void net_stop( void )
{
	notice( "Stopping networking." );

	net_shutdown( ctl->net->data );
	net_shutdown( ctl->net->statsd );
	net_shutdown( ctl->net->adder );
}


NET_TYPE *net_type_defaults( unsigned short port, line_fn *lfn, char *label )
{
	NET_TYPE *nt;

	nt            = (NET_TYPE *) allocz( sizeof( NET_TYPE ) );
	nt->tcp       = (NET_PORT *) allocz( sizeof( NET_PORT ) );
	nt->tcp->ip   = INADDR_ANY;
	nt->tcp->back = 20;
	nt->tcp->port = port;
	nt->tcp->type = nt;
	nt->handler   = lfn;
	nt->udp_bind  = INADDR_ANY;
	nt->label     = strdup( label );
	nt->flags     = NTYPE_ENABLED|NTYPE_TCP_ENABLED|NTYPE_UDP_ENABLED;

	return nt;
}



NET_CTL *net_config_defaults( void )
{
	NET_CTL *net;

	net            = (NET_CTL *) allocz( sizeof( NET_CTL ) );
	net->dead_time = NET_DEAD_CONN_TIMER;
	net->rcv_tmout = NET_RCV_TMOUT;
	net->reconn    = 1000 * NET_RECONN_MSEC;
	net->io_usec   = 1000 * NET_IO_MSEC;

	net->data      = net_type_defaults( DEFAULT_DATA_PORT,   &data_line_data,   "ministry data socket" );
	net->statsd    = net_type_defaults( DEFAULT_STATSD_PORT, &data_line_statsd, "stats compat socket" );
	net->adder     = net_type_defaults( DEFAULT_ADDER_PORT,  &data_line_adder,  "combiner socket" );

	// default graphite target, localhost 2003
	net->host      = strdup( DEFAULT_TARGET_HOST );
	net->port      = DEFAULT_TARGET_PORT;

	return net;
}


#define ntflag( f )			if( atoi( av->val ) ) nt->flags |= NTYPE_##f; else nt->flags &= ~NTYPE_##f


int net_config_line( AVP *av )
{
	struct in_addr ina;
	char *d, *p, *cp;
	NET_TYPE *nt;
	int i, tcp;
	WORDS *w;


	if( !( d = strchr( av->att, '.' ) ) )
	{
		if( attIs( "timeout" ) )
		{
			ctl->net->dead_time = (time_t) atoi( av->val );
			debug( "Dead connection timeout set to %d sec.", ctl->net->dead_time );
		}
		else if( attIs( "rcv_tmout" ) )
		{
			ctl->net->rcv_tmout = (unsigned int) atoi( av->val );
			debug( "Receive timeout set to %u sec.", ctl->net->rcv_tmout );
		}
		else if( attIs( "reconn_msec" ) )
		{
			ctl->net->reconn = 1000 * atoi( av->val );
			if( ctl->net->reconn )
				ctl->net->reconn = 1000 * NET_RECONN_MSEC;
			debug( "Reconnect time set to %d usec.", ctl->net->reconn );
		}
		else if( attIs( "io_msec" ) )
		{
			ctl->net->io_usec = 1000 * atoi( av->val );
			if( ctl->net->io_usec )
				ctl->net->io_usec = 1000 * NET_IO_MSEC;
			debug( "Io loop time set to %d usec.", ctl->net->io_usec );
		}
		else
			return -1;

		return 0;
	}

	/* then it's data., statsd. or adder. */
	p = d + 1;

	if( !strncasecmp( av->att, "data.", 5 ) )
		nt = ctl->net->data;
	else if( !strncasecmp( av->att, "statsd.", 7 ) )
		nt = ctl->net->statsd;
	else if( !strncasecmp( av->att, "adder.", 6 ) )
		nt = ctl->net->adder;
	else if( !strncasecmp( av->att, "target.", 7 ) )
	{
		if( !strcasecmp( d, "host" ) )
		{
			free( ctl->net->host );
			ctl->net->host = strdup( av->val );
		}
		else if( !strcasecmp( d, "port" ) )
		{
			ctl->net->port = (unsigned short) strtoul( av->val, NULL, 10 );
			if( ctl->net->port == 0 )
				ctl->net->port = DEFAULT_TARGET_PORT;
		}
		else
			return -1;

		return 0;
	}
	else
		return -1;

	// only a few single-words per connection type
	if( !( d = strchr( p, '.' ) ) )
	{
		if( !strcasecmp( p, "enable" ) )
		{
			ntflag( ENABLED );
			debug( "Networking %s for %s", ( nt->flags & NTYPE_ENABLED ) ? "enabled" : "disabled", nt->label );
		}
		else if( !strcasecmp( p, "enable_tcp" ) )
		{
			ntflag( TCP_ENABLED );
		}
		else if( !strcasecmp( p, "enable_udp" ) )
		{
			ntflag( UDP_ENABLED );
		}
		else if( !strcasecmp( p, "label" ) )
		{
			free( nt->label );
			nt->label = strdup( av->val );
		}
		else
			return -1;

		return 0;
	}

	d++;

	// which port struct are we working with?
	if( !strncasecmp( p, "udp.", 4 ) )
		tcp = 0;
	else if( !strncasecmp( p, "tcp.", 4 ) )
		tcp = 1;
	else
		return -1;
	
	if( !strcasecmp( d, "bind" ) )
	{
		inet_aton( av->val, &ina );
		if( tcp )
			nt->tcp->ip = ina.s_addr;
		else
			nt->udp_bind = ina.s_addr;
	}
	else if( !strcasecmp( d, "backlog" ) )
	{
		if( tcp )
			nt->tcp->back = (unsigned short) strtoul( av->val, NULL, 10 );
		else
			warn( "Cannot set a backlog for a udp connection." );
	}
	else if( !strcasecmp( d, "port" ) )
	{
		if( tcp )
			nt->tcp->port = (unsigned short) strtoul( av->val, NULL, 10 );
		else
		{
			// work out how many ports we have
			w  = (WORDS *) allocz( sizeof( WORDS ) );
			cp = strdup( av->val );
			strwords( w, cp, 0, ',' );
			if( w->wc > 0 )
			{
				nt->udp_count = w->wc;
				nt->udp       = (NET_PORT **) allocz( w->wc * sizeof( NET_PORT * ) );

				debug( "Discovered %d udp ports for %s", w->wc, nt->label );

				for( i = 0; i < w->wc; i++ )
				{
					nt->udp[i]       = (NET_PORT *) allocz( sizeof( NET_PORT ) );
					nt->udp[i]->port = (unsigned short) strtoul( w->wd[i], NULL, 10 );
					nt->udp[i]->type = nt;
				}
			}

			free( cp );
			free( w );
		}
	}
	else
		return -1;

	return 0;
}

#undef ntflag

