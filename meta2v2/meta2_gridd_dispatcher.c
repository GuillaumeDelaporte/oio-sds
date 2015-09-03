/*
OpenIO SDS meta2v2
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

#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <metautils/lib/metautils.h>

#include <server/grid_daemon.h>
#include <server/transport_gridd.h>
#include <server/gridd_dispatcher_filters.h>

#include <meta2v2/meta2_macros.h>
#include <meta2v2/meta2_backend.h>
#include <meta2v2/meta2_gridd_dispatcher.h>
#include <meta2v2/meta2_filters.h>
#include <meta2v2/meta2_filter_context.h>

#define PTR(p) ((gpointer)(p))
#define POS(F) (int)(PTR(F) - PTR(hdata))

static gboolean
meta2_dispatch_all(struct gridd_reply_ctx_s *reply,
		gpointer gdata, gpointer hdata)
{
	gridd_filter *fl;

	if (!(fl = (gridd_filter*)hdata)) {
		GRID_INFO("No filter defined for this request, consider not yet implemented");
		reply->send_reply(CODE_NOT_IMPLEMENTED, "NOT IMPLEMENTED");
		return TRUE;
	}

	if (!meta2_backend_initiated(gdata)) {
		reply->send_reply(CODE_UNAVAILABLE, "backend not yet ready");
		return TRUE;
	}

	struct gridd_filter_ctx_s *ctx = meta2_filter_ctx_new();
	meta2_filter_ctx_set_backend(ctx, (struct meta2_backend_s *) gdata);

	GError *err = NULL;
	for (guint loop=1; loop && *fl; fl++) {
		switch ((*fl)(ctx, reply)) {
			case FILTER_KO:
				if (!(err = meta2_filter_ctx_get_error(ctx)))
					err = NEWERROR(CODE_INTERNAL_ERROR, "BUG: no error set by the filter");
			case FILTER_DONE:
				loop = 0;
			case FILTER_OK:
				break;
			default:
				g_assert_not_reached();
				break;
		}
	}
	if (err)
		reply->send_error(0, err);

	meta2_filter_ctx_clean(ctx);
	return TRUE;
}

/* ------------------------------------------------------------------------- */

static gridd_filter M2V2_CREATE_FILTERS[] =
{
	meta2_filter_extract_header_url,
	meta2_filter_fill_subject,
	meta2_filter_check_url_cid,
	meta2_filter_check_ns_name,
	meta2_filter_extract_header_storage_policy,
	meta2_filter_extract_header_version_policy,
	meta2_filter_extract_header_localflag,
	meta2_filter_action_create_container,
	meta2_filter_success_reply,
	NULL
};

static gridd_filter M2V2_DESTROY_FILTERS[] =
{
	meta2_filter_extract_header_url,
	meta2_filter_extract_header_forceflag,
	meta2_filter_extract_header_purgeflag,
	meta2_filter_extract_header_flushflag,
	meta2_filter_extract_header_localflag,
	meta2_filter_fill_subject,
	meta2_filter_check_url_cid,
	meta2_filter_check_ns_name,
	meta2_filter_action_delete_container,
	meta2_filter_success_reply,
	NULL
};

static gridd_filter M2V2_HAS_FILTERS[] =
{
	meta2_filter_extract_header_url,
	meta2_filter_extract_header_localflag,
	meta2_filter_fill_subject,
	meta2_filter_check_url_cid,
	meta2_filter_check_ns_name,
	meta2_filter_action_has_container,
	meta2_filter_success_reply,
	NULL
};

static gridd_filter M2V2_PURGE_FILTERS[] =
{
	meta2_filter_extract_header_url,
	meta2_filter_extract_header_flags32,
	meta2_filter_fill_subject,
	meta2_filter_check_url_cid,
	meta2_filter_check_ns_name,
	meta2_filter_action_purge_container,
	NULL
};

static gridd_filter M2V2_DEDUP_FILTERS[] =
{
	meta2_filter_extract_header_url,
	meta2_filter_extract_header_flags32,
	meta2_filter_fill_subject,
	meta2_filter_check_url_cid,
	meta2_filter_check_ns_name,
	meta2_filter_action_deduplicate_container,
	meta2_filter_success_reply,
	NULL
};

static gridd_filter M2V2_LIST_FILTERS[] =
{
	meta2_filter_extract_header_url,
	meta2_filter_extract_header_flags32,
	meta2_filter_extract_list_params,
	meta2_filter_extract_header_localflag,
	meta2_filter_fill_subject,
	meta2_filter_check_url_cid,
	meta2_filter_check_ns_name,
	meta2_filter_action_list_contents,
	NULL
};

static gridd_filter M2V2_LCHUNK_FILTERS[] =
{
	meta2_filter_extract_header_url,
	meta2_filter_extract_header_flags32,
	meta2_filter_extract_list_params,
	meta2_filter_fill_subject,
	meta2_filter_check_url_cid,
	meta2_filter_check_ns_name,
	meta2_filter_action_list_by_chunk_id,
	NULL
};

static gridd_filter M2V2_LHEADER_FILTERS[] =
{
	meta2_filter_extract_header_url,
	meta2_filter_extract_header_flags32,
	meta2_filter_extract_list_params,
	meta2_filter_fill_subject,
	meta2_filter_check_url_cid,
	meta2_filter_check_ns_name,
	meta2_filter_action_list_by_header_hash,
	NULL
};

static gridd_filter M2V2_BEANS_FILTER[] =
{
	meta2_filter_extract_header_url,
	meta2_filter_extract_header_append,
	meta2_filter_extract_header_spare,
	meta2_filter_extract_header_storage_policy,
	meta2_filter_extract_header_string_size,
	meta2_filter_extract_header_localflag,
	meta2_filter_fill_subject,
	meta2_filter_check_url_cid,
	meta2_filter_check_ns_name,
	meta2_filter_extract_body_beans,
	meta2_filter_action_generate_beans,
	NULL
};

static gridd_filter M2V2_PUT_FILTERS[] =
{
	meta2_filter_extract_header_url,
	meta2_filter_extract_header_copy,
	meta2_filter_extract_header_optional_overwrite,
	meta2_filter_extract_header_localflag,
	meta2_filter_fill_subject,
	meta2_filter_check_url_cid,
	meta2_filter_check_ns_name,
	meta2_filter_extract_body_beans,
	meta2_filter_action_put_content,
	NULL
};

static gridd_filter M2V2_APPEND_FILTERS[] =
{
	meta2_filter_extract_header_url,
	meta2_filter_extract_header_localflag,
	meta2_filter_fill_subject,
	meta2_filter_check_url_cid,
	meta2_filter_check_ns_name,
	meta2_filter_extract_body_beans,
	meta2_filter_extract_header_storage_policy,
	meta2_filter_action_append_content,
	NULL
};

static gridd_filter M2V2_GET_FILTERS[] =
{
	meta2_filter_extract_header_url,
	meta2_filter_extract_header_localflag,
	meta2_filter_extract_header_flags32,
	meta2_filter_fill_subject,
	meta2_filter_check_url_cid,
	meta2_filter_check_ns_name,
	meta2_filter_action_get_content,
	NULL
};

static gridd_filter M2V2_DELETE_FILTERS[] =
{
	meta2_filter_extract_header_url,
	meta2_filter_extract_header_localflag,
	meta2_filter_extract_header_flags32,
	meta2_filter_fill_subject,
	meta2_filter_check_url_cid,
	meta2_filter_check_ns_name,
	meta2_filter_action_delete_content,
	NULL
};

static gridd_filter M2V2_PROPSET_FILTERS[] =
{
	meta2_filter_extract_header_url,
	meta2_filter_fill_subject,
	meta2_filter_check_url_cid,
	meta2_filter_check_ns_name,
	meta2_filter_extract_header_flags32,
	meta2_filter_extract_body_beans,
	meta2_filter_action_set_content_properties,
	NULL
};

static gridd_filter M2V2_PROPGET_FILTERS[] =
{
	meta2_filter_extract_header_url,
	meta2_filter_fill_subject,
	meta2_filter_check_url_cid,
	meta2_filter_check_ns_name,
	meta2_filter_extract_header_flags32,
	meta2_filter_action_get_content_properties,
	NULL
};

static gridd_filter M2V2_PROPDEL_FILTERS[] =
{
	meta2_filter_extract_header_url,
	meta2_filter_fill_subject,
	meta2_filter_check_url_cid,
	meta2_filter_check_ns_name,
	meta2_filter_extract_body_strings,
	meta2_filter_action_del_content_properties,
	meta2_filter_success_reply,
	NULL
};

static gridd_filter M2V2_STGPOL_FILTERS[] =
{
	meta2_filter_extract_header_url,
	meta2_filter_extract_header_storage_policy,
	meta2_filter_fill_subject,
	meta2_filter_check_url_cid,
	meta2_filter_check_ns_name,
	meta2_filter_action_update_storage_policy,
	meta2_filter_success_reply,
	NULL
};

static gridd_filter M2V2_EXITELECTION_FILTERS[] =
{
	meta2_filter_extract_header_url,
	meta2_filter_action_exit_election,
	meta2_filter_success_reply,
	NULL
};

static gridd_filter M2V2_RAW_DEL_filters[] =
{
	meta2_filter_extract_header_url,
	meta2_filter_check_url_cid,
	meta2_filter_fill_subject,
	meta2_filter_check_ns_name,
	meta2_filter_extract_body_beans,
	meta2_filter_action_delete_beans,
	meta2_filter_success_reply,
	NULL
};

static gridd_filter M2V2_RAW_ADD_filters[] =
{
	meta2_filter_extract_header_url,
	meta2_filter_fill_subject,
	meta2_filter_check_url_cid,
	meta2_filter_check_ns_name,
	meta2_filter_extract_body_beans,
	meta2_filter_action_insert_beans,
	meta2_filter_success_reply,
	NULL
};

static gridd_filter M2V2_RAW_SUBST_filters[] =
{
	meta2_filter_extract_header_url,
	meta2_filter_fill_subject,
	meta2_filter_check_url_cid,
	meta2_filter_check_ns_name,
	meta2_filter_extract_header_chunk_beans,
	meta2_filter_action_update_beans,
	meta2_filter_success_reply,
	NULL
};

static gridd_filter M2V2_FILTERS_touch_content_v1[] =
{
	meta2_filter_extract_header_url,
	meta2_filter_check_url_cid,
	meta2_filter_check_optional_ns_name,
	meta2_filter_action_touch_content_v1, /* XXX TODO FIXME NOOP in facts */
	meta2_filter_success_reply,
	NULL
};

static gridd_filter M2V2_FILTERS_touch_container_v1[] =
{
	meta2_filter_extract_header_url,
	meta2_filter_check_url_cid,
	meta2_filter_check_optional_ns_name,
    meta2_filter_extract_header_flags32,
	meta2_filter_action_touch_container_v1, /* XXX TODO FIXME NOOP in facts */
	meta2_filter_success_reply,
	NULL
};

/* ------------------------------------------------------------------------- */

typedef gboolean (*hook) (struct gridd_reply_ctx_s *, gpointer, gpointer);

const struct gridd_request_descr_s *
meta2_gridd_get_v2_requests(void)
{
	/* one-shot features */
	static struct gridd_request_descr_s descriptions[] = {
		/* containers */
		{NAME_MSGNAME_M2V2_CREATE,  (hook) meta2_dispatch_all, M2V2_CREATE_FILTERS},
		{NAME_MSGNAME_M2V2_DESTROY, (hook) meta2_dispatch_all, M2V2_DESTROY_FILTERS},
		{NAME_MSGNAME_M2V2_HAS,	    (hook) meta2_dispatch_all, M2V2_HAS_FILTERS},
		{NAME_MSGNAME_M2V2_PURGE,   (hook) meta2_dispatch_all, M2V2_PURGE_FILTERS},
		{NAME_MSGNAME_M2V2_DEDUP,   (hook) meta2_dispatch_all, M2V2_DEDUP_FILTERS},
		/* contents */
		{NAME_MSGNAME_M2V2_PUT,     (hook) meta2_dispatch_all, M2V2_PUT_FILTERS},
		{NAME_MSGNAME_M2V2_BEANS,   (hook) meta2_dispatch_all, M2V2_BEANS_FILTER},
		{NAME_MSGNAME_M2V2_APPEND,  (hook) meta2_dispatch_all, M2V2_APPEND_FILTERS},
		{NAME_MSGNAME_M2V2_GET,     (hook) meta2_dispatch_all, M2V2_GET_FILTERS},
		{NAME_MSGNAME_M2V2_DEL,     (hook) meta2_dispatch_all, M2V2_DELETE_FILTERS},

		{NAME_MSGNAME_M2V2_LIST,    (hook) meta2_dispatch_all, M2V2_LIST_FILTERS},
		{NAME_MSGNAME_M2V2_LCHUNK,  (hook) meta2_dispatch_all, M2V2_LCHUNK_FILTERS},
		{NAME_MSGNAME_M2V2_LHEADER, (hook) meta2_dispatch_all, M2V2_LHEADER_FILTERS},

		/* content properties (container properties now managed through
		 * sqlx queries) */
		{NAME_MSGNAME_M2V2_PROP_SET, (hook) meta2_dispatch_all, M2V2_PROPSET_FILTERS},
		{NAME_MSGNAME_M2V2_PROP_GET, (hook) meta2_dispatch_all, M2V2_PROPGET_FILTERS},
		{NAME_MSGNAME_M2V2_PROP_DEL, (hook) meta2_dispatch_all, M2V2_PROPDEL_FILTERS},

		/* raw beans */
		{NAME_MSGNAME_M2V2_RAW_DEL,   (hook) meta2_dispatch_all, M2V2_RAW_DEL_filters},
		{NAME_MSGNAME_M2V2_RAW_ADD,   (hook) meta2_dispatch_all, M2V2_RAW_ADD_filters},
		{NAME_MSGNAME_M2V2_RAW_SUBST, (hook) meta2_dispatch_all, M2V2_RAW_SUBST_filters},

		{NAME_MSGNAME_M2V2_EXITELECTION, (hook) meta2_dispatch_all,  M2V2_EXITELECTION_FILTERS},
		/* url */
		{NAME_MSGNAME_M2V2_STGPOL,    (hook) meta2_dispatch_all, M2V2_STGPOL_FILTERS},

		{NAME_MSGNAME_M2V1_TOUCH_CONTAINER, (hook) meta2_dispatch_all, M2V2_FILTERS_touch_container_v1},
		{NAME_MSGNAME_M2V1_TOUCH_CONTENT,   (hook) meta2_dispatch_all, M2V2_FILTERS_touch_content_v1},

		{NULL, NULL, NULL}
	};

	return descriptions;
}

