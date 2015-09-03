/*
OpenIO SDS proxy
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

#ifndef OIO_SDS__proxy__transport_http_h
# define OIO_SDS__proxy__transport_http_h 1

# include <server/slab.h>

struct network_client_s;

/* Hidden type, internally defined */
struct http_request_dispatcher_s;

struct http_request_s
{
	struct network_client_s *client;
	void (*notify_body) (struct http_request_s *);

	/* unpacked request line */
	gchar *cmd;
	gchar *req_uri;
	gchar *version;

	/* all the headers mapped as <gchar*,gchar*> */
	GTree *tree_headers;
	GByteArray *body;
};

struct http_reply_ctx_s
{
	void (*set_status) (int code, const gchar *msg);
	void (*set_content_type)(const gchar *type);
	void (*add_header) (const gchar *name, gchar *v);
	void (*add_header_gstr) (const gchar *name, GString *value);
	void (*set_body) (guint8 *d, gsize l);
	void (*set_body_gstr) (GString *gstr);
	void (*set_body_gba) (GByteArray *gstr);

	void (*finalize) (void);
	void (*access_tail) (const char *fmt, ...);
};

enum http_rc_e { HTTPRC_DONE, HTTPRC_NEXT, HTTPRC_ABORT };

struct http_request_descr_s
{
	const gchar *name;

	enum http_rc_e (*handler) (gpointer u,
		 struct http_request_s *request,
		 struct http_reply_ctx_s *reply);
};

/* Associates the given client to the given request dispatcher into
 * a transport object. */
void transport_http_factory0(struct http_request_dispatcher_s *dispatcher,
		struct network_client_s *client);

/* Wrapper over transport_http_factory0() to provide a factory function
 * without having to cast transport_http_factory0(). */
static inline void
transport_http_factory(gpointer dispatcher, struct network_client_s *client)
{
	transport_http_factory0(dispatcher, client);
}

void http_request_dispatcher_clean(struct http_request_dispatcher_s *d);

struct http_request_dispatcher_s * transport_http_build_dispatcher(
		gpointer u, const struct http_request_descr_s *descr);

const gchar * http_request_get_header(struct http_request_s *req,
		const gchar *n);

#endif /*OIO_SDS__proxy__transport_http_h*/
