/*
OpenIO SDS gridd
Copyright (C) 2014 Worldine, original work as part of Redcurrant
Copyright (C) 2015 OpenIO, modified as part of OpenIO Software Defined Storage

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU Affero General Public License as
published by the Free Software Foundation, either version 3 of the
License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU Affero General Public License for more details.

You should have received a copy of the GNU Affero General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef OIO_SDS__gridd__main__srvtimer_h
# define OIO_SDS__gridd__main__srvtimer_h 1

#include <glib.h>

typedef void (*srvtimer_f) (gpointer udata);

gboolean srvtimer_register_regular (const char *name, srvtimer_f fire,
	srvtimer_f close, gpointer udata, guint64 freq);

void srvtimer_init (void);

void srvtimer_fini (void);

void srvtimer_fire (guint64 ticks);

#endif /*OIO_SDS__gridd__main__srvtimer_h*/