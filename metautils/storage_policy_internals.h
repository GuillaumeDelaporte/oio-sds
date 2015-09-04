/*
OpenIO SDS metautils
Copyright (C) 2014 Worldine, original work as part of Redcurrant
Copyright (C) 2015 OpenIO, modified as part of OpenIO Software Defined Storage

This library is free software; you can redistribute it and/or
modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation; either
version 3.0 of the License, or (at your option) any later version.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public
License along with this library.
*/

#ifndef OIO_SDS__metautils__lib__storage_policy_internals_h
# define OIO_SDS__metautils__lib__storage_policy_internals_h 1

#include <glib.h>

#include "storage_policy.h"

struct data_security_s
{
	gchar *name;
	enum data_security_e type;
	GHashTable *params;
};

struct data_treatments_s
{
	gchar *name;
	enum data_treatments_e type;
	GHashTable *params;
};

struct storage_class_s
{
	gchar *name;
	GSList *fallbacks;
};

struct storage_policy_s
{
	gchar *name;
	struct data_security_s *datasec;
	struct data_treatments_s *datatreat;
	struct storage_class_s *stgclass;
};

#endif /*OIO_SDS__metautils__lib__storage_policy_internals_h*/