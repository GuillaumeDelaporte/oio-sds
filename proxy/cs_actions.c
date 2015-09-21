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

static GError *
_cs_check_tokens (struct req_args_s *args)
{
	// XXX All the handler use the NS, this should have been checked earlier.
	if (!validate_namespace(NS()))
		return NEWERROR(CODE_NAMESPACE_NOTMANAGED, "Invalid NS");

	if (TYPE()) {
		if (!validate_srvtype(TYPE()))
			return NEWERROR(CODE_NAMESPACE_NOTMANAGED, "Invalid srvtype");
    }
    return NULL;
}

static GString *
_cs_pack_and_free_srvinfo_list (GSList * svc)
{
    GString *gstr = g_string_new ("[");
    for (GSList * l = svc; l; l = l->next) {
        if (l != svc)
            g_string_append_c (gstr, ',');
        service_info_encode_json (gstr, l->data, FALSE);
    }
    g_string_append (gstr, "]");
    g_slist_free_full (svc, (GDestroyNotify) service_info_clean);
    return gstr;
}

enum reg_op_e {
    REGOP_PUSH,
    REGOP_LOCK,
    REGOP_UNLOCK,
};

static enum http_rc_e
_registration (struct req_args_s *args, enum reg_op_e op, struct json_object *jsrv)
{
    GError *err;

    if (!push_queue)
        return _reply_bad_gateway(args, NEWERROR(CODE_INTERNAL_ERROR, "Service upstream disabled"));

    if (NULL != (err = _cs_check_tokens(args)))
        return _reply_notfound_error (args, err);

    struct service_info_s *si = NULL;
    err = service_info_load_json_object (jsrv, &si, TRUE);

    if (err) {
        if (err->code == CODE_BAD_REQUEST)
            return _reply_format_error (args, err);
        else
            return _reply_system_error (args, err);
    }

    if (!validate_namespace (si->ns_name)) {
        service_info_clean (si);
        return _reply_system_error (args, NEWERROR (CODE_NAMESPACE_NOTMANAGED,
                    "Unexpected NS"));
    }

    si->score.timestamp = network_server_bogonow(args->rq->client->server);

    if (op == REGOP_PUSH)
        si->score.value = SCORE_UNSET;
    else if (op == REGOP_UNLOCK)
        si->score.value = SCORE_UNLOCK;
    else /* if (op == REGOP_LOCK) */
        si->score.value = CLAMP(si->score.value, SCORE_DOWN, SCORE_MAX);

	// TODO follow the DRY principle and factorize this!
	if (flag_cache_enabled) {
		GSList l = {.data = si, .next = NULL};
		if (NULL != (err = gcluster_push_services (csurl, &l))) {
			service_info_clean (si);
			return _reply_common_error (args, err);
		} else {
			GString *gstr = g_string_new ("");
			service_info_encode_json (gstr, si, TRUE);
			service_info_clean (si);
			return _reply_success_json (args, gstr);
		}
	} else {
		GString *gstr = g_string_new ("");
		service_info_encode_json (gstr, si, TRUE);
		PUSH_DO(lru_tree_insert(push_queue, service_info_key(si), si));
		return _reply_success_json (args, gstr);
	}
}

//------------------------------------------------------------------------------

enum http_rc_e
action_cs_nscheck (struct req_args_s *args)
{
    GError *err;
    if (NULL != (err = _cs_check_tokens(args)))
        return _reply_notfound_error (args, err);

    return _reply_success_json (args, NULL);
}

enum http_rc_e
action_cs_info (struct req_args_s *args)
{
	const char *v = OPT("what");
	if (v && !strcmp(v, "types"))
		return action_cs_srvtypes (args);

    GError *err;
    if (NULL != (err = _cs_check_tokens(args)))
        return _reply_notfound_error (args, err);

    struct namespace_info_s ni;
    memset (&ni, 0, sizeof (ni));
    NSINFO_DO(namespace_info_copy (&nsinfo, &ni, NULL));

    GString *gstr = g_string_new ("");
    namespace_info_encode_json (gstr, &ni);
    namespace_info_clear (&ni);
    return _reply_success_json (args, gstr);
}

enum http_rc_e
action_cs_put (struct req_args_s *args)
{
    struct json_tokener *parser;
    struct json_object *jbody;
	enum http_rc_e rc;

    parser = json_tokener_new ();
    jbody = json_tokener_parse_ex (parser, (char *) args->rq->body->data,
            args->rq->body->len);
    if (!json_object_is_type (jbody, json_type_object))
        rc = _reply_format_error (args, BADREQ ("Invalid srv"));
    else
        rc = _registration (args, REGOP_PUSH, jbody);
    json_object_put (jbody);
    json_tokener_free (parser);
    return rc;
}

enum http_rc_e
action_cs_get (struct req_args_s *args)
{
	const char *types = TYPE();
	gboolean full = _request_has_flag (args, PROXYD_HEADER_MODE, "full");

    GError *err;
    if (NULL != (err = _cs_check_tokens(args)))
        return _reply_notfound_error(args, err);

    GSList *sl = NULL;
	err = gcluster_get_services (csurl, types, full, FALSE, &sl);

	if (NULL != err) {
		g_slist_free_full (sl, (GDestroyNotify) service_info_clean);
		g_prefix_error (&err, "Agent error: ");
		return _reply_system_error (args, err);
	}

	args->rp->access_tail ("%s=%u", types, g_slist_length(sl));
	return _reply_success_json (args, _cs_pack_and_free_srvinfo_list (sl));
}

enum http_rc_e
action_cs_srvcheck (struct req_args_s *args)
{
	GError *err;
	if (NULL != (err = _cs_check_tokens(args)))
		return _reply_notfound_error (args, err);

	return _reply_success_json (args, NULL);
}

enum http_rc_e
action_cs_del (struct req_args_s *args)
{
	GError *err;
	if (NULL != (err = _cs_check_tokens(args)))
		return _reply_notfound_error (args, err);

	err = gcluster_remove_services (csurl, TYPE(), NULL);

	if (err) {
		g_prefix_error (&err, "Agent error: ");
		return _reply_system_error (args, err);
	}
	return _reply_success_json (args, _create_status (CODE_FINAL_OK, "OK"));
}

enum http_rc_e
action_cs_srv_lock (struct req_args_s *args, struct json_object *jargs)
{
	return _registration (args, REGOP_LOCK, jargs);
}

enum http_rc_e
action_cs_srv_unlock (struct req_args_s *args, struct json_object *jargs)
{
	return _registration (args, REGOP_UNLOCK, jargs);
}

enum http_rc_e
action_cs_action (struct req_args_s *args)
{
	struct sub_action_s actions[] = {
		{"Lock", action_cs_srv_lock},
		{"Unlock", action_cs_srv_unlock},
		{NULL,NULL}
	};
	return abstract_action ("conscience", args, actions);
}

enum http_rc_e
action_cs_srvtypes (struct req_args_s *args)
{
	GError *err;
	if (NULL != (err = _cs_check_tokens(args)))
		return _reply_notfound_error (args, err);

	GString *out = g_string_new("");
	g_string_append_c(out, '[');
	NSINFO_DO(if (srvtypes && *srvtypes) {
		g_string_append_c(out, '"');
		g_string_append(out, *srvtypes);
		g_string_append_c(out, '"');
		for (gchar **ps = srvtypes+1; *ps ;ps++) {
			g_string_append_c(out, ',');
			g_string_append_c(out, '"');
			g_string_append(out, *ps);
			g_string_append_c(out, '"');
		}
	});
	g_string_append_c(out, ']');
	return _reply_success_json (args, out);
}

enum http_rc_e
action_conscience_check (struct req_args_s *args)
{
    return action_cs_nscheck (args);
}

enum http_rc_e
action_conscience_info (struct req_args_s *args)
{
	return action_cs_info (args);
}

enum http_rc_e
action_conscience_list (struct req_args_s *args)
{
	return action_cs_get (args);
}

enum http_rc_e
action_conscience_register (struct req_args_s *args)
{
	return action_cs_put (args);
}

enum http_rc_e
action_conscience_deregister (struct req_args_s *args)
{
	return action_cs_del (args);
}

enum http_rc_e
action_conscience_lock (struct req_args_s *args)
{
	return rest_action (args, action_cs_srv_lock);
}

enum http_rc_e
action_conscience_unlock (struct req_args_s *args)
{
	return rest_action (args, action_cs_srv_unlock);
}
