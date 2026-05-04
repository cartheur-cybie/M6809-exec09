/*
 * Copyright 2009 by Brian Dominy <brian@oddchange.com>
 *
 * This file is part of GCC6809.
 *
 * GCC6809 is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * GCC6809 is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with GCC6809; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <sys/types.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include "machine.h"
#include "serial.h"

/* Emulate a serial port.  Basically this driver can be used for any byte-at-a-time
input/output interface. */
static struct serial_port *default_port = NULL;
static int tx_trace_enabled = 0;

static int serial_queue_push (struct serial_port *port, U8 val)
{
	if (port->rx_count >= sizeof (port->rx_buf))
		return -1;
	port->rx_buf[port->rx_wr] = val;
	port->rx_wr = (port->rx_wr + 1) % sizeof (port->rx_buf);
	port->rx_count++;
	return 0;
}

static int serial_queue_pop (struct serial_port *port, U8 *val)
{
	if (port->rx_count == 0)
		return -1;
	*val = port->rx_buf[port->rx_rd];
	port->rx_rd = (port->rx_rd + 1) % sizeof (port->rx_buf);
	port->rx_count--;
	return 0;
}

void serial_update (struct serial_port *port)
{
	fd_set infds, outfds;
	struct timeval timeout;

	FD_ZERO (&infds);
	FD_SET (port->fin, &infds);
	FD_ZERO (&outfds);
	FD_SET (port->fout, &outfds);
	timeout.tv_sec = 0;
	timeout.tv_usec = 0;
	select (2, &infds, &outfds, NULL, &timeout);
	if ((port->rx_count > 0) || FD_ISSET (port->fin, &infds))
		port->status |= SER_STAT_READOK;
	else
		port->status &= ~SER_STAT_READOK;
	if (FD_ISSET (port->fout, &outfds))
		port->status |= SER_STAT_WRITEOK;
	else
		port->status &= ~SER_STAT_WRITEOK;
}

U8 serial_read (struct hw_device *dev, unsigned long addr)
{
	struct serial_port *port = (struct serial_port *)dev->priv;
	int retval;
	serial_update (port);
	switch (addr)
	{
		case SER_DATA:
		{
			U8 val;
			if (serial_queue_pop (port, &val) == 0)
				return val;
			if (!(port->status & SER_STAT_READOK))
				return 0xFF;
			retval = read (port->fin, &val, 1);
			assert(retval != -1);
			return val;
		}
		case SER_CTL_STATUS:
                        return port->status;
                default:
                        fprintf(stderr, "serial_read() from undefined addr\n");
        }
        return 0x42;
}

void serial_write (struct hw_device *dev, unsigned long addr, U8 val)
{
	struct serial_port *port = (struct serial_port *)dev->priv;
	int retval;
	switch (addr)
	{
		case SER_DATA:
		{
			U8 v = val;
			retval = write (port->fout, &v, 1);
			assert(retval != -1);
			if (tx_trace_enabled)
			{
				if (v >= 32 && v <= 126)
					fprintf (stderr, "[serial tx 0x%02X '%c']\n", v, v);
				else
					fprintf (stderr, "[serial tx 0x%02X '.']\n", v);
			}
			break;
		}
		case SER_CTL_STATUS:
			port->ctrl = val;
			break;
	}
}

void serial_reset (struct hw_device *dev)
{
	struct serial_port *port = (struct serial_port *)dev->priv;
	port->ctrl = 0;
	port->status = 0;
	port->rx_rd = 0;
	port->rx_wr = 0;
	port->rx_count = 0;
}

struct hw_class serial_class =
{
	.name = "serial",
	.readonly = 0,
	.reset = serial_reset,
	.read = serial_read,
	.write = serial_write,
};

extern U8 null_read (struct hw_device *dev, unsigned long addr);

struct hw_device* serial_create (void)
{
	struct serial_port *port = malloc (sizeof (struct serial_port));
	port->fin = STDIN_FILENO;
	port->fout = STDOUT_FILENO;
	port->rx_rd = 0;
	port->rx_wr = 0;
	port->rx_count = 0;
	if (!default_port)
		default_port = port;
	return device_attach (&serial_class, 4, port);
}

struct hw_device* hostfile_create (const char *filename, int flags)
{
	struct serial_port *port = malloc (sizeof (struct serial_port));
	port->fin = port->fout = open(filename, O_CREAT | flags, S_IRUSR | S_IWUSR);
	port->rx_rd = 0;
	port->rx_wr = 0;
	port->rx_count = 0;
	if (!default_port)
		default_port = port;
	return device_attach (&serial_class, 4, port);
}

int serial_inject_byte (U8 val)
{
	if (!default_port)
		return -1;
	return serial_queue_push (default_port, val);
}

int serial_inject_bytes (const U8 *vals, unsigned int count)
{
	unsigned int i;
	for (i = 0; i < count; i++)
		if (serial_inject_byte (vals[i]) != 0)
			return -1;
	return 0;
}

int serial_get_rx_pending (void)
{
	if (!default_port)
		return -1;
	return default_port->rx_count;
}

void serial_set_tx_trace (int enabled)
{
	tx_trace_enabled = !!enabled;
}

int serial_get_tx_trace (void)
{
	return tx_trace_enabled;
}
