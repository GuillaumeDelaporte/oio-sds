/*
OpenIO SDS meta1v2
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

#include <metautils/lib/metautils.h>

#include "./internals.h"
#include "./meta1_remote.h"

static gboolean
on_reply(gpointer ctx, MESSAGE reply)
{
	GByteArray *out = ctx;
	gsize bsize = 0;
	void *b = metautils_message_get_BODY(reply, &bsize);
	if (b) {
		if (out != NULL)
			g_byte_array_append(out, b, bsize);
	}

	g_byte_array_append(out, (const guint8*)"", 1);
	g_byte_array_set_size(out, out->len - 1);
	return TRUE;
}

static gchar **
list_request(const addr_info_t *a, GError **err, GByteArray *req)
{
	gchar stra[STRLEN_ADDRINFO];
	struct gridd_client_s *client = NULL;
	GByteArray *gba;
	GError *e = NULL;

	EXTRA_ASSERT(a != NULL);
	EXTRA_ASSERT(req != NULL);

	gba = g_byte_array_new();
	grid_addrinfo_to_string(a, stra, sizeof(stra));
	client = gridd_client_create(stra, req, gba, on_reply);
	g_byte_array_unref (req);

	gridd_client_start(client);
	if (!(e = gridd_client_loop(client)))
		e = gridd_client_error(client);

	gridd_client_free(client);

	if (e) {
		if (err)
			*err = e;
		else
			g_clear_error(&e);
		g_byte_array_free(gba, TRUE);
		return NULL;
	}

	gchar **lines = metautils_decode_lines((gchar*)gba->data,
			(gchar*)(gba->data + gba->len));
	if (!lines && err)
		*err = NEWERROR(CODE_BAD_REQUEST, "Invalid buffer content");
	g_byte_array_free(gba, TRUE);
	return lines;
}

static gboolean
oneway_request (const addr_info_t *a, GError **err, GByteArray *req)
{
	gchar ** result = list_request (a, err, req);
	if (!result)
		return FALSE;
	g_strfreev(result);
	return TRUE;
}

/* ------------------------------------------------------------------------- */

gboolean 
meta1v2_remote_create_reference (const addr_info_t *meta1, GError **err,
		struct hc_url_s *url)
{
	EXTRA_ASSERT(meta1 != NULL);
	EXTRA_ASSERT(url != NULL);

	MESSAGE req = metautils_message_create_named(NAME_MSGNAME_M1V2_USERCREATE);
	metautils_message_add_url (req, url);
	return oneway_request(meta1, err, message_marshall_gba_and_clean(req));
}

gboolean
meta1v2_remote_has_reference (const addr_info_t *meta1, GError **err,
		struct hc_url_s *url, struct hc_url_s ***out)
{
	EXTRA_ASSERT(meta1 != NULL);
	EXTRA_ASSERT(url != NULL);

	MESSAGE req = metautils_message_create_named(NAME_MSGNAME_M1V2_USERINFO);
	metautils_message_add_url (req, url);
	if (!out)
		return oneway_request(meta1, err, message_marshall_gba_and_clean(req));

	GError *e = NULL;
	gchar **tab = list_request (meta1, &e, message_marshall_gba_and_clean(req));
	if (!tab) {
		*out = NULL;
		return FALSE;
	}

	gsize len = g_strv_length (tab);
	*out = g_malloc0 ((1 + len) * sizeof(void*));
	struct hc_url_s **p = *out;
	for (guint i=0; i<len ;i++) {
		*p = hc_url_init (tab[i]);
		if (*p) p++;
	}
	g_strfreev (tab);
	return TRUE;
}

gboolean 
meta1v2_remote_delete_reference (const addr_info_t *meta1, GError **err,
		struct hc_url_s *url, gboolean force)
{
	EXTRA_ASSERT(meta1 != NULL);
	EXTRA_ASSERT(url != NULL);

	MESSAGE req = metautils_message_create_named(NAME_MSGNAME_M1V2_USERDESTROY);
	metautils_message_add_url (req, url);
	if (force)
		metautils_message_add_field_str (req, NAME_MSGKEY_FORCE, "1");
	return oneway_request(meta1, err, message_marshall_gba_and_clean(req));
}

gchar** 
meta1v2_remote_link_service(const addr_info_t *meta1, GError **err,
		struct hc_url_s *url, const gchar *srvtype,
		gboolean dryrun, gboolean autocreate)
{
	EXTRA_ASSERT(meta1 != NULL);
	EXTRA_ASSERT(url != NULL);
	EXTRA_ASSERT(srvtype != NULL);

	MESSAGE req = metautils_message_create_named(NAME_MSGNAME_M1V2_SRVLINK);
	metautils_message_add_url (req, url);
	metautils_message_add_field_str (req, NAME_MSGKEY_TYPENAME, srvtype);
	if (dryrun)
		metautils_message_add_field_str (req, NAME_MSGKEY_DRYRUN, "1");
	if (autocreate)
		metautils_message_add_field_str (req, NAME_MSGKEY_AUTOCREATE, "1");
	return list_request(meta1, err, message_marshall_gba_and_clean(req));
}

gchar**
meta1v2_remote_list_reference_services(const addr_info_t *meta1, GError **err,
		struct hc_url_s *url, const gchar *srvtype)
{
	EXTRA_ASSERT(meta1 != NULL);
	EXTRA_ASSERT(url != NULL);

	MESSAGE req = metautils_message_create_named(NAME_MSGNAME_M1V2_SRVLIST);
	metautils_message_add_url (req, url);
	if (srvtype)
		metautils_message_add_field_str (req, NAME_MSGKEY_TYPENAME, srvtype);

	return list_request(meta1, err, message_marshall_gba_and_clean(req));
}

gboolean 
meta1v2_remote_unlink_service(const addr_info_t *meta1, GError **err,
		struct hc_url_s *url, const gchar *srvtype)
{
	EXTRA_ASSERT(meta1 != NULL);
	EXTRA_ASSERT(url != NULL);
	EXTRA_ASSERT(srvtype != NULL);

	MESSAGE req = metautils_message_create_named(NAME_MSGNAME_M1V2_SRVUNLINK);
	metautils_message_add_url (req, url);
	metautils_message_add_field_str (req, NAME_MSGKEY_TYPENAME, srvtype);
	return oneway_request(meta1, err, message_marshall_gba_and_clean(req));
}

gboolean
meta1v2_remote_unlink_one_service(const addr_info_t *meta1, GError **err,
		struct hc_url_s *url, const gchar *srvtype, gint64 seqid)
{
	EXTRA_ASSERT(meta1 != NULL);
	EXTRA_ASSERT(url != NULL);
	EXTRA_ASSERT(srvtype != NULL);

	MESSAGE req = metautils_message_create_named(NAME_MSGNAME_M1V2_SRVUNLINK);
	metautils_message_add_field_str (req, NAME_MSGKEY_TYPENAME, srvtype);
	if (seqid > 0) {
		gchar str[32];
		g_snprintf(str, sizeof(str), "%"G_GINT64_FORMAT"\n", seqid);
		metautils_message_add_body_unref (req, metautils_gba_from_string (str));
	}

	return oneway_request(meta1, err, message_marshall_gba_and_clean(req));
}

gchar **
meta1v2_remote_poll_reference_service(const addr_info_t *meta1, GError **err,
		struct hc_url_s *url, const gchar *srvtype,
		gboolean dryrun, gboolean autocreate)
{
	EXTRA_ASSERT(meta1 != NULL);
	EXTRA_ASSERT(url != NULL);
	EXTRA_ASSERT(srvtype != NULL);

	MESSAGE req = metautils_message_create_named(NAME_MSGNAME_M1V2_SRVPOLL);
	metautils_message_add_url (req, url);
	metautils_message_add_field_str (req, NAME_MSGKEY_TYPENAME, srvtype);
	if (dryrun)
		metautils_message_add_field_str (req, NAME_MSGKEY_DRYRUN, "1");
	if (autocreate)
		metautils_message_add_field_str (req, NAME_MSGKEY_AUTOCREATE, "1");

	return list_request(meta1, err, message_marshall_gba_and_clean(req));
}

gboolean
meta1v2_remote_force_reference_service(const addr_info_t *meta1, GError **err,
		struct hc_url_s *url, const gchar *m1url,
		gboolean autocreate, gboolean force)
{
	EXTRA_ASSERT(meta1 != NULL);
	EXTRA_ASSERT(url != NULL);
	EXTRA_ASSERT(m1url != NULL);

	MESSAGE req = metautils_message_create_named(NAME_MSGNAME_M1V2_SRVSET);
	metautils_message_add_url (req, url);
	metautils_message_add_body_unref (req, metautils_gba_from_string(m1url));
	if (autocreate)
		metautils_message_add_field_str (req, NAME_MSGKEY_AUTOCREATE, "1");
	if (force)
		metautils_message_add_field_str (req, NAME_MSGKEY_FORCE, "1");

	return oneway_request(meta1, err, message_marshall_gba_and_clean(req));
}

gboolean
meta1v2_remote_configure_reference_service(const addr_info_t *meta1, GError **err,
		struct hc_url_s *url, const gchar *m1url)
{
	EXTRA_ASSERT(meta1 != NULL);
	EXTRA_ASSERT(url != NULL);
	EXTRA_ASSERT(m1url != NULL);

	MESSAGE req = metautils_message_create_named(NAME_MSGNAME_M1V2_SRVCONFIG);
	metautils_message_add_url (req, url);
	metautils_message_add_body_unref (req, metautils_gba_from_string(m1url));

	return oneway_request(meta1, err, message_marshall_gba_and_clean(req));
}

gboolean
meta1v2_remote_reference_set_property(const addr_info_t *m1, GError **err,
		struct hc_url_s *url, gchar **pairs, gboolean flush)
{
	EXTRA_ASSERT(m1 != NULL);
	EXTRA_ASSERT(url != NULL);

	MESSAGE req = metautils_message_create_named(NAME_MSGNAME_M1V2_PROPSET);
	metautils_message_add_url (req, url);
	if (flush)
		metautils_message_add_field_str (req, NAME_MSGKEY_FLUSH, "1");
	metautils_message_add_body_unref (req, metautils_encode_lines(pairs));

	return oneway_request(m1, err, message_marshall_gba_and_clean(req));
}

gboolean
meta1v2_remote_reference_get_property(const addr_info_t *m1, GError **err,
		struct hc_url_s *url, gchar **keys, gchar ***result)
{
	EXTRA_ASSERT(m1 != NULL);
	EXTRA_ASSERT(url != NULL);
	EXTRA_ASSERT(result != NULL);

	MESSAGE req = metautils_message_create_named(NAME_MSGNAME_M1V2_PROPGET);
	metautils_message_add_url (req, url);
	metautils_message_add_body_unref (req, metautils_encode_lines(keys));

	*result = list_request(m1, err, message_marshall_gba_and_clean(req));
	return *result != NULL;
}

gboolean
meta1v2_remote_reference_del_property(const addr_info_t *m1, GError **err,
		struct hc_url_s *url, gchar **keys)
{
	EXTRA_ASSERT(m1 != NULL);
	EXTRA_ASSERT(url != NULL);

	MESSAGE req = metautils_message_create_named(NAME_MSGNAME_M1V2_PROPDEL);
	metautils_message_add_url (req, url);
	metautils_message_add_body_unref (req, metautils_encode_lines(keys));

	return oneway_request(m1, err, message_marshall_gba_and_clean(req));
}

gchar**
meta1v2_remote_list_services_by_prefix(const addr_info_t *m1, GError **err,
        struct hc_url_s *url)
{
    EXTRA_ASSERT(m1 != NULL);
    EXTRA_ASSERT(url != NULL);

    MESSAGE req = metautils_message_create_named(NAME_MSGNAME_M1V2_SRVALLONM1);
	metautils_message_add_url (req, url);
	metautils_message_add_cid (req, NAME_MSGKEY_PREFIX, hc_url_get_id(url));

    return list_request(m1, err, message_marshall_gba_and_clean(req));
}

gboolean
meta1v2_remote_get_prefixes(const addr_info_t *m1, GError **err, gchar *** result)
{
	EXTRA_ASSERT(m1 != NULL);
	MESSAGE req = metautils_message_create_named(NAME_MSGNAME_M1V2_GETPREFIX);
	*result = list_request(m1, err, message_marshall_gba_and_clean(req));
	return *result != NULL;
}

