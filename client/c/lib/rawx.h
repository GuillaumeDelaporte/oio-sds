/*
OpenIO SDS client
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

#ifndef OIO_SDS__client__c__lib__rawx_h
# define OIO_SDS__client__c__lib__rawx_h 1

# include "./gs_internals.h"

#define RAWX_ATTR_CHUNK_POSITION "chunkpos"
#define RAWX_ATTR_CONTENT_CHUNKNB "chunknb"
#define RAWX_ATTR_CONTENT_SIZE "contentsize"

typedef struct ne_request_param_s {
	ne_session *session;
	const char *method;
	char *cPath;
	const char *containerid;
	const char *contentpath;
	chunk_position_t chunkpos;
	guint32 chunknb;
	chunk_size_t chunksize;
	int64_t contentsize;
} ne_request_param_t;

/**
 * Chunk
 */
struct chunk_attr_s {
	const char *key;
	const char *val;
};

/*  */
gboolean rawx_download (gs_chunk_t *chunk, GError **err,
		struct dl_status_s *status, GSList **p_broken_rawx_list);
int rawx_init (void);

gboolean rawx_update_chunk_attr(struct meta2_raw_chunk_s *c, const char *name,
		const char *val, GError **err);

/**
 * Update chunk extended attributes.
 *
 * @param url The URL of the chunk (a.k.a "chunk id")
 * @param attrs A list of (struct chunk_attr_s *)
 * @param err
 * @return TRUE on success, FALSE otherwise
 */
gboolean rawx_update_chunk_attrs(const gchar *chunk_url, GSList *attrs,
		GError **err);

ne_request_param_t* new_request_param(void);
void free_request_param(ne_request_param_t *param);

char* create_rawx_request_common(ne_request **req, ne_request_param_t *param,
		GError **err);
char* create_rawx_request_from_chunk(ne_request **req, ne_session *session,
		const char *method, gs_chunk_t *chunk, GByteArray *system_metadata,
		GError **err);

ne_session *opensession_common(const addr_info_t *addr_info,
		int connect_timeout, int read_timeout, GError **err);

/**
 * Generate an request id
 *
 * @param dst destination buffer (will be nul-terminated)
 * @param dst_size size of the destination buffer
 */
void gen_req_id_header(gchar *dst, gsize dst_size);

#endif /*OIO_SDS__client__c__lib__rawx_h*/