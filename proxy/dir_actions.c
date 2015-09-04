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

#include "common.h"
#include "actions.h"

static GString *
_pack_m1url_list (GString *gstr, gchar ** urlv)
{
	if (!gstr)
		gstr = g_string_new ("");
	g_string_append_c (gstr, '[');
	for (gchar ** v = urlv; v && *v; v++) {
		struct meta1_service_url_s *m1 = meta1_unpack_url (*v);
		meta1_service_url_encode_json (gstr, m1);
		meta1_service_url_clean (m1);
		if (*(v + 1))
			g_string_append_c (gstr, ',');
	}
	g_string_append_c (gstr, ']');
	return gstr;
}

static GString *
_pack_and_freev_m1url_list (GString *gstr, gchar ** urlv)
{
	gstr = _pack_m1url_list (gstr, urlv);
	g_strfreev (urlv);
	return gstr;
}

static GString *
_pack_and_freev_pairs (gchar ** pairs)
{
	GString *out = g_string_new ("{");
	for (gchar ** pp = pairs; pp && *pp; ++pp) {
		if (pp != pairs)
			g_string_append_c (out, ',');
		gchar *k = *pp;
		gchar *sep = strchr (k, '=');
		gchar *v = sep + 1;
		g_string_append_printf (out, "\"%.*s\":\"%s\"", (int) (sep - k), k, v);
	}
	g_string_append_c (out, '}');
	g_strfreev (pairs);
	return out;
}

static GError *
_m1_action (struct req_args_s *args, gchar ** m1v,
	GError * (*hook) (const char * m1))
{
	for (gchar ** pm1 = m1v; *pm1; ++pm1) {
		struct meta1_service_url_s *m1 = meta1_unpack_url (*pm1);
		if (!m1)
			continue;
		if (0 != g_ascii_strcasecmp(m1->srvtype, NAME_SRVTYPE_META1)) {
			meta1_service_url_clean (m1);
			continue;
		}

		struct addr_info_s m1a;
		if (!grid_string_to_addrinfo (m1->host, &m1a)) {
			GRID_INFO ("Invalid META1 [%s] for [%s]",
				m1->host, hc_url_get (args->url, HCURL_WHOLE));
			meta1_service_url_clean (m1);
			continue;
		}

		GError *err = hook (m1->host);
		meta1_service_url_clean (m1);
		if (!err)
			return NULL;
		else if (err->code == CODE_REDIRECT)
			g_clear_error (&err);
		else {
			g_prefix_error (&err, "META1 error: ");
			return err;
		}
	}
	return NEWERROR (CODE_UNAVAILABLE, "No meta1 answered");
}

GError *
_m1_locate_and_action (struct req_args_s *args, GError * (*hook) ())
{
	gchar **m1v = NULL;
	GError *err = hc_resolve_reference_directory (resolver, args->url, &m1v);
	if (NULL != err) {
		g_prefix_error (&err, "No META1: ");
		return err;
	}
	EXTRA_ASSERT (m1v != NULL);
	err = _m1_action (args, m1v, hook);
	g_strfreev (m1v);
	return err;
}

static GError *
decode_json_string_array (gchar *** pkeys, struct json_object *j)
{
	gchar **keys = NULL;
	GError *err = NULL;

	if (json_object_is_type (j, json_type_null)) {
		*pkeys = g_malloc0(sizeof(void*));
		return NULL;
	}

	// Parse the keys
	if (!json_object_is_type (j, json_type_array))
		return BADREQ ("Invalid/Unexpected JSON");

	GPtrArray *v = g_ptr_array_new ();
	guint count = 0;
	for (gint i = json_object_array_length (j); i > 0; --i) {
		++count;
		struct json_object *item =
			json_object_array_get_idx (j, i - 1);
		if (!json_object_is_type (item, json_type_string)) {
			err = BADREQ ("Invalid string at [%u]", count);
			break;
		}
		g_ptr_array_add (v, g_strdup (json_object_get_string (item)));
	}
	if (!err) {
		g_ptr_array_add (v, NULL);
		keys = (gchar **) g_ptr_array_free (v, FALSE);
	} else {
		g_ptr_array_free (v, TRUE);
	}

	*pkeys = keys;
	return err;
}

//------------------------------------------------------------------------------

enum http_rc_e
action_dir_srv_list (struct req_args_s *args)
{
	const char *type = TYPE();
	if (!type)
		return _reply_format_error (args, NEWERROR( CODE_BAD_REQUEST, "No service type provided"));

	gchar **urlv = NULL;
	GError *err = hc_resolve_reference_service (resolver, args->url, type, &urlv);
	EXTRA_ASSERT ((err != NULL) ^ (urlv != NULL));

	if (!err) {

		if ((args->flags & FLAG_NOEMPTY) && !*urlv) {
			g_strfreev (urlv);
			urlv = NULL;
			return _reply_notfound_error (args, NEWERROR (CODE_NOT_FOUND, "No service linked"));
		}
		return _reply_success_json (args, _pack_and_freev_m1url_list (NULL, urlv));
	}

	return _reply_common_error (args, err);
}

enum http_rc_e
action_dir_srv_unlink (struct req_args_s *args)
{
	const char *type = TYPE();
	if (!type)
		return _reply_format_error (args, NEWERROR( CODE_BAD_REQUEST, "No service type provided"));

	GError *hook (const char * m1) {
		return meta1v2_remote_unlink_service (m1, args->url, type);
	}

	GError *err = _m1_locate_and_action (args, hook);

	if (!err || CODE_IS_NETWORK_ERROR(err->code)) {
		/* Also decache on timeout, a majority of request succeed,
         * and it will probably silently succeed  */
		hc_decache_reference_service (resolver, args->url, type);
	}

	if (!err)
		return _reply_success_json (args, NULL);
	return _reply_common_error (args, err);
}

enum http_rc_e
action_dir_srv_link (struct req_args_s *args, struct json_object *jargs)
{
	(void) jargs;
	const char *type = TYPE();
	if (!type)
		return _reply_format_error (args, NEWERROR(CODE_BAD_REQUEST, "No service type provided"));
	gboolean autocreate = _request_has_flag (args, PROXYD_HEADER_MODE, "autocreate");
	gboolean dryrun = _request_has_flag (args, PROXYD_HEADER_MODE, "dryrun");

	gchar **urlv = NULL;
	GError *hook (const char * m1) {
		return meta1v2_remote_link_service (m1, args->url, type, dryrun, autocreate, &urlv);
	}

	GError *err = _m1_locate_and_action (args, hook);
	if (!err || CODE_IS_NETWORK_ERROR(err->code)) {
		/* Also decache on timeout, a majority of request succeed,
         * and it will probably silently succeed  */
		hc_decache_reference_service (resolver, args->url, type);
	}

	if (err) {
		if (CODE_IS_NSIMPOSSIBLE(err->code))
			return _reply_forbidden_error (args, err);
		return _reply_common_error (args, err);
	}

	EXTRA_ASSERT (urlv != NULL);
	return _reply_success_json (args, _pack_and_freev_m1url_list (NULL, urlv));
}

enum http_rc_e
action_dir_srv_force (struct req_args_s *args, struct json_object *jargs)
{
	struct meta1_service_url_s *m1u = NULL;
	const char *type = TYPE();
	if (!type)
		return _reply_format_error (args, NEWERROR(CODE_BAD_REQUEST, "No service type provided"));

	gboolean force = _request_has_flag (args, PROXYD_HEADER_MODE, "replace");
	gboolean autocreate = _request_has_flag (args, PROXYD_HEADER_MODE, "autocreate");

	GError *hook (const char * m1) {
		gchar *packed = meta1_pack_url (m1u);
		GError *e = meta1v2_remote_force_reference_service (m1, args->url, packed, autocreate, force);
		g_free (packed);
		return e;
	}

	GError *err = meta1_service_url_load_json_object (jargs, &m1u);

	if (!err)
		err = _m1_locate_and_action (args, hook);
	if (m1u) {
		meta1_service_url_clean (m1u);
		m1u = NULL;
	}

	if (!err || CODE_IS_NETWORK_ERROR(err->code)) {
		/* Also decache on timeout, a majority of request succeed,
         * and it will probably silently succeed  */
		hc_decache_reference_service (resolver, args->url, type);
	}

	if (err) {
		if (CODE_IS_NSIMPOSSIBLE(err->code))
			return _reply_forbidden_error (args, err);
		return _reply_common_error (args, err);
	}
	return _reply_success_json (args, NULL);
}

enum http_rc_e
action_dir_srv_renew (struct req_args_s *args, struct json_object *jargs)
{
	(void) jargs;
	const char *type = TYPE();
	if (!type)
		return _reply_format_error (args, NEWERROR(CODE_BAD_REQUEST, "No service type provided"));
	gboolean autocreate = _request_has_flag (args, PROXYD_HEADER_MODE, "autocreate");
	gboolean dryrun = _request_has_flag (args, PROXYD_HEADER_MODE, "dryrun");

	gchar **urlv = NULL;
	GError *hook (const char * m1) {
		return meta1v2_remote_poll_reference_service (m1, args->url, type, dryrun, autocreate, &urlv);
	}

	GError *err = _m1_locate_and_action (args, hook);

	if (!err || CODE_IS_NETWORK_ERROR(err->code)) {
		/* Also decache on timeout, a majority of request succeed,
         * and it will probably silently succeed  */
		hc_decache_reference_service (resolver, args->url, type);
	}

	if (err) {
		if (CODE_IS_NSIMPOSSIBLE(err->code))
			return _reply_forbidden_error (args, err);
		return _reply_common_error (args, err);
	}
	EXTRA_ASSERT (urlv != NULL);
	return _reply_success_json (args, _pack_and_freev_m1url_list (NULL, urlv));
}

enum http_rc_e
action_dir_srv_action (struct req_args_s *args)
{
	struct sub_action_s actions[] = {
		{"Link", action_dir_srv_link},
		{"Renew", action_dir_srv_renew},
		{"Force", action_dir_srv_force},
		{NULL,NULL}
	};
	return abstract_action ("directory services", args, actions);
}

//------------------------------------------------------------------------------

enum http_rc_e
action_dir_ref_list (struct req_args_s *args)
{
	if (!validate_namespace(NS()))
		return _reply_forbidden_error(args, NEWERROR(
					CODE_NAMESPACE_NOTMANAGED, "Namespace not managed"));

	gchar **urlv = NULL;
	GError *hook (const char * m1) {
		return meta1v2_remote_list_reference_services (m1, args->url, NULL, &urlv);
	}
	GError *err = _m1_locate_and_action (args, hook);
	if (!err) {
		gchar **dirv = NULL;
		err = hc_resolve_reference_directory (resolver, args->url, &dirv);
		GString *out = g_string_new ("{");
		g_string_append (out, "\"dir\":");
		if (dirv)
			out = _pack_and_freev_m1url_list (out, dirv);
		else
			g_string_append (out, "null");
		g_string_append (out, ",\"srv\":");
		out = _pack_and_freev_m1url_list (out, urlv);
		g_string_append (out, "}");
		return _reply_success_json (args, out);
	}
	return _reply_common_error (args, err);
}

enum http_rc_e
action_dir_ref_has (struct req_args_s *args)
{
	if (!validate_namespace(NS()))
		return _reply_forbidden_error(args,
				NEWERROR(CODE_NAMESPACE_NOTMANAGED, "Namespace not managed"));

	GError *hook (const char * m1) {
		return meta1v2_remote_has_reference (m1, args->url, NULL);
	}
	GError *err = _m1_locate_and_action (args, hook);
	if (!err)
		return _reply_success_json (args, NULL);
	return _reply_common_error (args, err);
}

enum http_rc_e
action_dir_ref_create (struct req_args_s *args)
{
	GError *hook (const char * m1) {
		return meta1v2_remote_create_reference (m1, args->url);
	}
	GError *err = _m1_locate_and_action (args, hook);
	if (!err)
		return _reply_created (args);
	if (err->code == CODE_CONTAINER_EXISTS) {
		g_clear_error (&err);
		return _reply_accepted (args);
	}
	return _reply_common_error (args, err);
}

enum http_rc_e
action_dir_ref_destroy (struct req_args_s *args)
{
	gboolean force = _request_has_flag (args, PROXYD_HEADER_MODE, "force");

	GError *hook (const char * m1) {
		return meta1v2_remote_delete_reference (m1, args->url, force);
	}
	GError *err = _m1_locate_and_action (args, hook);
	if (!err || CODE_IS_NETWORK_ERROR(err->code)) {
		/* Also decache on timeout, a majority of request succeed,
         * and it will probably silently succeed  */
		NSINFO_DO(if (srvtypes) {
			for (gchar ** p = srvtypes; *p; ++p)
				hc_decache_reference_service (resolver, args->url, *p);
		});
		hc_decache_reference (resolver, args->url);
	}
	if (!err)
		return _reply_nocontent (args);
	if (err->code == CODE_USER_INUSE)
		return _reply_forbidden_error (args, err);
	return _reply_common_error (args, err);
}

//------------------------------------------------------------------------------

enum http_rc_e
action_dir_resolve (struct req_args_s *args)
{
	struct hc_url_s **urlv = NULL;
	GError *hook (const char * m1) {
		return meta1v2_remote_has_reference (m1, args->url, &urlv);
	}
	GError *err = _m1_locate_and_action (args, hook);
	if (!err) {
		GString *body = g_string_new ("");
		g_string_append_c (body, '[');
		for (struct hc_url_s **u=urlv; *u ;++u) {
			if (u != urlv)
				g_string_append_c (body, ',');
			g_string_append_c (body, '{');
			hc_url_to_json (body, *u);
			g_string_append_c (body, '}');
		}
		g_string_append_c (body, ']');
		return _reply_success_json (args, body);
	}
	return _reply_common_error (args, err);
}

//------------------------------------------------------------------------------

enum http_rc_e
action_dir_prop_get (struct req_args_s *args, struct json_object *jargs)
{
	gchar **keys = NULL;
	GError *err = decode_json_string_array (&keys, jargs);

	// Execute the request
	gchar **pairs = NULL;
	GError *hook (const char * m1) {
		return meta1v2_remote_reference_get_property (m1, args->url,
				(*keys ? keys : NULL), &pairs);
	}

	if (!err) {
		err = _m1_locate_and_action (args, hook);
		g_strfreev (keys);
		keys = NULL;
	}
	if (!err)
		return _reply_success_json (args, _pack_and_freev_pairs (pairs));
	return _reply_common_error (args, err);
}

enum http_rc_e
action_dir_prop_set (struct req_args_s *args, struct json_object *jargs)
{
	GError *err = NULL;
	gchar **pairs = NULL;
	gboolean flush = NULL != OPT("flush");

	// Parse the <string>:<string> mapping.
	GPtrArray *v = g_ptr_array_new ();
	guint count = 0;
	if (jargs) {
		if (!json_object_is_type (jargs, json_type_object))
			return _reply_format_error (args, BADREQ("Invalid pairs"));
		json_object_object_foreach (jargs, key, val) {
			++count;
			if (!json_object_is_type (val, json_type_string)) {
				err = BADREQ ("Invalid property doc['pairs']['%s']", key);
				break;
			}
			g_ptr_array_add (v, g_strdup_printf ("%s=%s", key,
						json_object_get_string (val)));
		}
	}

	if (!err) {
		g_ptr_array_add (v, NULL);
		pairs = (gchar **) g_ptr_array_free (v, FALSE);
	} else {
		g_ptr_array_free (v, TRUE);
	}

	GError *hook (const char * m1) {
		return meta1v2_remote_reference_set_property (m1, args->url, pairs, flush);
	}

	if (!err) {
		err = _m1_locate_and_action (args, hook);
		g_free (pairs);
	}
	if (!err)
		return _reply_success_json (args, NULL);
	return _reply_common_error (args, err);
}

enum http_rc_e
action_dir_prop_del (struct req_args_s *args, struct json_object *jargs)
{
	gchar **keys = NULL;
	GError *err = decode_json_string_array (&keys, jargs);

	GError *hook (const char *m1) {
		return meta1v2_remote_reference_del_property (m1, args->url, keys);
	}

	if (!err) {
		err = _m1_locate_and_action (args, hook);
		g_strfreev (keys);
		keys = NULL;
	}
	if (!err)
		return _reply_success_json (args, NULL);
	return _reply_common_error (args, err);
}

enum http_rc_e
action_dir_ref_action (struct req_args_s *args)
{
	struct sub_action_s actions[] = {
		{"GetProperties", action_dir_prop_get},
		{"SetProperties", action_dir_prop_set},
		{"DeleteProperties", action_dir_prop_del},
		{NULL,NULL}
	};
	return abstract_action ("directory references", args, actions);
}

enum http_rc_e
action_ref_create (struct req_args_s *args)
{
    return action_dir_ref_create (args);
}

enum http_rc_e
action_ref_show (struct req_args_s *args)
{
    return action_dir_ref_list (args);
}

enum http_rc_e
action_ref_destroy (struct req_args_s *args)
{
    return action_dir_ref_destroy (args);
}

enum http_rc_e
action_ref_link (struct req_args_s *args)
{
    return rest_action (args, action_dir_srv_link);
}

enum http_rc_e
action_ref_unlink (struct req_args_s *args)
{
    return action_dir_srv_unlink (args);
}

enum http_rc_e
action_ref_renew (struct req_args_s *args)
{
    return rest_action (args, action_dir_srv_renew);
}

enum http_rc_e
action_ref_force (struct req_args_s *args)
{
    return rest_action (args, action_dir_srv_force);
}

enum http_rc_e
action_ref_prop_get (struct req_args_s *args)
{
    return rest_action (args, action_dir_prop_get);
}

enum http_rc_e
action_ref_prop_set (struct req_args_s *args)
{
    return rest_action (args, action_dir_prop_set);
}

enum http_rc_e
action_ref_prop_del (struct req_args_s *args)
{
    return rest_action (args, action_dir_prop_del);
}
