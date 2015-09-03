/*
OpenIO SDS sqliterepo
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

#ifndef OIO_SDS__sqliterepo__replication_dispatcher_h
# define OIO_SDS__sqliterepo__replication_dispatcher_h 1

struct gridd_request_descr_s;

const struct gridd_request_descr_s* sqlx_repli_gridd_get_requests(void);

const struct gridd_request_descr_s * sqlx_sql_gridd_get_requests(void);

#endif /*OIO_SDS__sqliterepo__replication_dispatcher_h*/
