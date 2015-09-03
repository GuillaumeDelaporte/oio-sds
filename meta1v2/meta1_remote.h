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

#ifndef OIO_SDS__meta1v2__meta1_remote_h
# define OIO_SDS__meta1v2__meta1_remote_h 1

# include <stdlib.h>
# include <errno.h>
# include <string.h>
# include <unistd.h>

# include <metautils/lib/metacomm.h>

/**
 * @addtogroup meta1v2_remote 
 * @{
 */

gboolean meta1v2_remote_create_reference (const addr_info_t *meta1,
		GError **err, struct hc_url_s *url);

gboolean meta1v2_remote_delete_reference(const addr_info_t *meta1,
		GError **err, struct hc_url_s *url, gboolean force);

gboolean meta1v2_remote_has_reference(const addr_info_t *meta1,
		GError **err, struct hc_url_s *url, struct hc_url_s ***out);


gchar ** meta1v2_remote_list_reference_services(const addr_info_t *meta1,
		GError **err, struct hc_url_s *url, const gchar *srvtype);

gchar ** meta1v2_remote_link_service(const addr_info_t *meta1,
		GError **err, struct hc_url_s *url, const gchar *srvtype,
		gboolean dryrun, gboolean autocreate);

gboolean meta1v2_remote_unlink_service(const addr_info_t *meta1,
		GError **err, struct hc_url_s *url, const gchar *srvtype);

gboolean meta1v2_remote_unlink_one_service(const addr_info_t *meta1,
		GError **err, struct hc_url_s *url, const gchar *srvtype, gint64 seqid);

gchar** meta1v2_remote_poll_reference_service(const addr_info_t *meta1,
		GError **err, struct hc_url_s *url, const gchar *srvtype,
		gboolean dryrun, gboolean autocreate);

gboolean meta1v2_remote_force_reference_service(const addr_info_t *meta1,
		GError **err, struct hc_url_s *url, const gchar *m1url,
		gboolean autocreate, gboolean force);

gboolean meta1v2_remote_configure_reference_service(const addr_info_t *meta1,
		GError **err, struct hc_url_s *url, const gchar *m1url);


gboolean meta1v2_remote_reference_get_property(const addr_info_t *m1,
		GError **err, struct hc_url_s *url, gchar **keys, gchar ***result);

gboolean meta1v2_remote_reference_set_property(const addr_info_t *m1,
		GError **err, struct hc_url_s *url, gchar **pairs, gboolean flush);

gboolean meta1v2_remote_reference_del_property(const addr_info_t *m1,
		GError **err, struct hc_url_s *url,
		gchar **keys);

gchar** meta1v2_remote_list_services_by_prefix(const addr_info_t *m1, GError **err,
        struct hc_url_s *url);

gboolean meta1v2_remote_get_prefixes(const addr_info_t *m1,
		GError **err, gchar ***result);

/** @} */

#endif /*OIO_SDS__meta1v2__meta1_remote_h*/
