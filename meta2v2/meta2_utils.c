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

#include <string.h>
#include <metautils/metautils.h>
#include <core/oiolog.h>

#include <meta2v2/meta2_backend_internals.h>

#include <meta2v2/meta2_utils.h>
#include <meta2v2/meta2_macros.h>
#include <meta2v2/meta2v2_remote.h>
#include <meta2v2/meta2_dedup_utils.h>
#include <meta2v2/generic.h>
#include <meta2v2/autogen.h>

static GError*
_get_container_policy(struct sqlx_sqlite3_s *sq3, struct namespace_info_s *nsinfo,
		struct storage_policy_s **result);

static gint64
_m2db_count_alias_versions(struct sqlx_sqlite3_s *sq3, struct hc_url_s *url)
{
	int rc;
	gint64 v;
	sqlite3_stmt *stmt = NULL;

	EXTRA_ASSERT(sq3 != NULL);
	EXTRA_ASSERT(url != NULL);

	v = 0;
	sqlite3_prepare_debug(rc, sq3->db,
			"SELECT COUNT(version) FROM alias_v2 WHERE alias = ?", -1, &stmt, NULL);
	if (rc == SQLITE_OK) {
		sqlite3_bind_text(stmt, 1, hc_url_get(url, HCURL_PATH), -1, NULL);
		while (SQLITE_ROW == (rc = sqlite3_step(stmt))) {
			v = sqlite3_column_int64(stmt, 0);
		}
		rc = sqlite3_finalize(stmt);
	}

	return v;
}

#define FORMAT_ERROR(v,s,e) (!(v) && errno == EINVAL)
#define RANGE_ERROR(v) ((v) == G_MININT64 || (v) == G_MAXINT64)
#define STRTOLL_ERROR(v,s,e) (FORMAT_ERROR(v,s,e) || RANGE_ERROR(v))

gboolean
m2v2_parse_chunk_position(const gchar *s, gint *pos, gboolean *par, gint *sub)
{
	gchar *end = NULL;
	gboolean parity = FALSE;
	gint64 p64, s64;

	EXTRA_ASSERT(pos != NULL);
	EXTRA_ASSERT(par != NULL);
	EXTRA_ASSERT(sub != NULL);
	if (!s)
		return FALSE;

	p64 = g_ascii_strtoll(s, &end, 10);
	if (STRTOLL_ERROR(p64, s, end))
		return FALSE;
	if (!*end) { // No sub-position found
		*pos = p64;
		*par = FALSE;
		*sub = -1;
		return TRUE;
	}

	if (*end != '.')
		return FALSE;
	s = end + 1;
	if (!*s) // Trailing dot not accepted
		return FALSE;

	if (*s == 'p') {
		parity = 1;
		++ s;
	}

	end = NULL;
	s64 = g_ascii_strtoll(s, &end, 10);
	if (STRTOLL_ERROR(s64, s, end))
		return FALSE;
	if (*end) // Trailing extra chars not accepted
		return FALSE;

	*pos = p64;
	*par = parity;
	*sub = s64;
	return TRUE;
}

guint64
m2db_get_container_size(sqlite3 *db, gboolean check_alias)
{
	guint64 size = 0;
	gchar tmp[512];
	g_snprintf(tmp, sizeof(tmp), "%s%s", "SELECT SUM(size) FROM content_header_v2",
			!check_alias ? "" :
			" WHERE EXISTS (SELECT content_id FROM alias_v2 WHERE content_id = id)");
	const gchar *sql = tmp;
	int rc, grc = SQLITE_OK;
	const gchar *next;
	sqlite3_stmt *stmt = NULL;

	while ((grc == SQLITE_OK) && sql && *sql) {
		next = NULL;
		sqlite3_prepare_debug(rc, db, sql, -1, &stmt, &next);
		sql = next;
		if (rc != SQLITE_OK && rc != SQLITE_DONE)
			grc = rc;
		else if (stmt) {
			while (SQLITE_ROW == (rc = sqlite3_step(stmt))) {
				size = sqlite3_column_int64(stmt, 0);
			}
			if (rc != SQLITE_OK && rc != SQLITE_DONE) {
				grc = rc;
			}
			rc = sqlite3_finalize(stmt);
		}

		stmt = NULL;
	}
	return size;
}

/**
 * @return The namespace name defined in the admin table. Must be freed.
 */
gchar *
m2db_get_namespace(struct sqlx_sqlite3_s *sq3, const gchar *def)
{
	gchar *ns = sqlx_admin_get_str(sq3, SQLX_ADMIN_NAMESPACE);
	if (!ns)
		return def? g_strdup(def) : NULL;
	return ns;
}

gint64
m2db_get_max_versions(struct sqlx_sqlite3_s *sq3, gint64 def)
{
	return sqlx_admin_get_i64(sq3, M2V2_ADMIN_VERSIONING_POLICY, def);
}

void
m2db_set_max_versions(struct sqlx_sqlite3_s *sq3, gint64 max)
{
	sqlx_admin_set_i64(sq3, M2V2_ADMIN_VERSIONING_POLICY, max);
}

gint64
m2db_get_ctime(struct sqlx_sqlite3_s *sq3)
{
	return sqlx_admin_get_i64(sq3, M2V2_ADMIN_CTIME, 0);
}

void
m2db_set_ctime(struct sqlx_sqlite3_s *sq3, gint64 now)
{
	sqlx_admin_set_i64(sq3, M2V2_ADMIN_CTIME, now);
}

gint64
m2db_get_keep_deleted_delay(struct sqlx_sqlite3_s *sq3, gint64 def)
{
	return sqlx_admin_get_i64(sq3, M2V2_ADMIN_KEEP_DELETED_DELAY, def);
}

void
m2db_set_keep_deleted_delay(struct sqlx_sqlite3_s *sq3, gint64 delay)
{
	sqlx_admin_set_i64(sq3, M2V2_ADMIN_KEEP_DELETED_DELAY, delay);
}

gint64
m2db_get_version(struct sqlx_sqlite3_s *sq3)
{
	return sqlx_admin_get_i64(sq3, M2V2_ADMIN_VERSION, 1);
}

void
m2db_increment_version(struct sqlx_sqlite3_s *sq3)
{
	sqlx_admin_inc_i64(sq3, M2V2_ADMIN_VERSION, 1);
}

gint64
m2db_get_size(struct sqlx_sqlite3_s *sq3)
{
	return sqlx_admin_get_i64(sq3, M2V2_ADMIN_SIZE, 0);
}

void
m2db_set_size(struct sqlx_sqlite3_s *sq3, gint64 size)
{
	sqlx_admin_set_i64(sq3, M2V2_ADMIN_SIZE, size);
}

gint64
m2db_get_quota(struct sqlx_sqlite3_s *sq3, gint64 def)
{
	return sqlx_admin_get_i64(sq3, M2V2_ADMIN_QUOTA, def);
}

void
m2db_set_quota(struct sqlx_sqlite3_s *sq3, gint64 quota)
{
	sqlx_admin_set_i64(sq3, M2V2_ADMIN_QUOTA, quota);
}

/* GET ---------------------------------------------------------------------- */

static GError*
_manage_content(sqlite3 *db, struct bean_CONTENT_s *bean,
		m2_onbean_cb cb, gpointer u0)
{
	GPtrArray *tmp = g_ptr_array_new();
	GError *err = _db_get_FK_by_name_buffered(bean, "chunks", db, tmp);
	if (!err) {
		while (tmp->len > 0) {
			struct bean_CHUNK_s *chunk = tmp->pdata[0];
			g_ptr_array_remove_index_fast(tmp, 0);
			if (chunk)
				cb(u0,chunk);
		}
	}
	_bean_cleanv2(tmp); /* cleans unset beans */

	if (!err)
		cb(u0, bean);
	return err;
}

static GError*
_manage_alias(sqlite3 *db, struct bean_ALIAS_s *bean,
		gboolean deeper, m2_onbean_cb cb, gpointer u0)
{
	GPtrArray *tmp = g_ptr_array_new();
	GError *err = _db_get_FK_by_name_buffered(bean, "content", db, tmp);
	if (!err) {
		while (tmp->len > 0) {
			struct bean_CONTENT_s *content = tmp->pdata[0];
			g_ptr_array_remove_index_fast(tmp, 0);
			if (content) {
				if (deeper)
					_manage_content(db, content, cb, u0);
				else
					cb(u0, content);
			}
		}
	}

	_bean_cleanv2(tmp); /* cleans unset beans */
	return err;
}

GError*
m2db_get_alias(struct sqlx_sqlite3_s *sq3, struct hc_url_s *u,
		guint32 flags, m2_onbean_cb cb, gpointer u0)
{
	/* sanity checks */
	if (!hc_url_has(u, HCURL_PATH))
		return NEWERROR(CODE_BAD_REQUEST, "Missing path");

	GRID_TRACE("GET(%s)", hc_url_get(u, HCURL_WHOLE));

	/* query, and nevermind the snapshot */
	const gchar *sql = NULL;
	GVariant *params[3] = {NULL, NULL, NULL};
	params[0] = g_variant_new_string(hc_url_get(u, HCURL_PATH));
	if (flags & M2V2_FLAG_LATEST) {
		sql = "alias = ? ORDER BY version DESC LIMIT 1";
	} else if (flags & M2V2_FLAG_ALLVERSION) {
		sql = "alias = ? ORDER BY version DESC";
	} else {
		if (hc_url_has(u, HCURL_VERSION)) {
			sql = "alias = ? AND version = ? LIMIT 1";
			params[1] = g_variant_new_int64(atoi(hc_url_get(u, HCURL_VERSION)));
		} else {
			sql = "alias = ? ORDER BY version DESC LIMIT 1";
		}
	}

	GPtrArray *tmp = g_ptr_array_new();
	GError *err = ALIAS_load_buffered(sq3->db, sql, params, tmp);
	metautils_gvariant_unrefv(params);

	if (!err) {
		if (tmp->len <= 0) {
			err = NEWERROR(CODE_CONTENT_NOTFOUND, "Alias not found");
		} else if (tmp->len == 1 && ALIAS_get_deleted(tmp->pdata[0]) &&
				(flags & M2V2_FLAG_NODELETED)) {
			err = NEWERROR(CODE_CONTENT_NOTFOUND, "Alias deleted");
		}
	}

	/* recurse on headers if allowed */
	if (!err && cb && ((flags & M2V2_FLAG_HEADERS) || !(flags & M2V2_FLAG_NORECURSION))) {
		for (guint i=0; !err && i<tmp->len ;i++) {
			struct bean_ALIAS_s *alias = tmp->pdata[i];
			if (!alias)
				continue;
			if ((flags & M2V2_FLAG_NODELETED) && ALIAS_get_deleted(alias))
				continue;
			_manage_alias(sq3->db, alias, !(flags & M2V2_FLAG_NORECURSION), cb, u0);
		}
	}

	/* recurse on properties if allowed */
	if (!err && cb && !(flags & M2V2_FLAG_NOPROPS)) {
		for (guint i=0; !err && i<tmp->len ;i++) {
			struct bean_ALIAS_s *alias = tmp->pdata[i];
			if (!alias)
				continue;
			GPtrArray *props = g_ptr_array_new();
			err = _db_get_FK_by_name_buffered(alias, "properties", sq3->db, props);
			if (!err) {
				for (guint j=0; j<props->len ;++j) {
					cb(u0, props->pdata[j]);
					props->pdata[j] = NULL;
				}
			}
			_bean_cleanv2 (props);
		}
	}

	/* eventually manage the aliases */
	if (!err && cb) {
		for (guint i=0; i<tmp->len ;i++) {
			struct bean_ALIAS_s *alias = tmp->pdata[i];
			if (!alias)
				continue;
			if ((flags & M2V2_FLAG_NODELETED) && ALIAS_get_deleted(alias))
				_bean_clean(alias);
			else
				cb(u0, alias);
			tmp->pdata[i] = NULL;
		}
	}

	_bean_cleanv2(tmp);
	return err;
}

GError*
m2db_get_alias1(struct sqlx_sqlite3_s *sq3, struct hc_url_s *url,
		guint32 flags, struct bean_ALIAS_s **out)
{
	EXTRA_ASSERT (out != NULL);

	flags &= ~(M2V2_FLAG_HEADERS|M2V2_FLAG_NOFORMATCHECK); // meaningless
	flags |=  (M2V2_FLAG_NOPROPS|M2V2_FLAG_NORECURSION);   // only alias

	GPtrArray *tmp = g_ptr_array_new ();
	GError *err = m2db_get_alias(sq3, url, flags, _bean_buffer_cb, tmp);
	if (!err) {
		*out = tmp->pdata[0];
		tmp->pdata[0] = NULL;
	}
	_bean_cleanv2 (tmp);
	return err;
}

GError*
m2db_get_alias_version(struct sqlx_sqlite3_s *sq3, struct hc_url_s *url,
		gint64 *out)
{
	GError *err = NULL;
	struct bean_ALIAS_s *latest = NULL;

	if (NULL != (err = m2db_latest_alias(sq3, url, &latest))) {
		g_prefix_error(&err, "Latest error: ");
		return err;
	}

	if (!latest)
		return NEWERROR(CODE_CONTENT_NOTFOUND, "Alias not found");

	*out = ALIAS_get_version(latest);
	_bean_clean(latest);
	return NULL;
}

GError*
m2db_latest_alias(struct sqlx_sqlite3_s *sq3,  struct hc_url_s *url,
		struct bean_ALIAS_s **alias)
{
	return m2db_get_alias1(sq3, url, M2V2_FLAG_LATEST, alias);
}

GError*
m2db_get_versioned_alias(struct sqlx_sqlite3_s *sq3, struct hc_url_s *url,
		struct bean_ALIAS_s **alias)
{
	return m2db_get_alias1 (sq3, url, 0, alias);
}

/* LIST --------------------------------------------------------------------- */

static GVariant **
_list_params_to_sql_clause(struct list_params_s *lp, GString *clause,
		GSList *headers)
{
	void lazy_and () {
		if (clause->len > 0) g_string_append(clause, " AND");
	}
	GPtrArray *params = g_ptr_array_new ();

	if (lp->marker_start) {
		lazy_and();
		g_string_append (clause, " alias > ?");
		g_ptr_array_add (params, g_variant_new_string (lp->marker_start));
	} else if (lp->prefix) {
		lazy_and();
		g_string_append (clause, " alias >= ?");
		g_ptr_array_add (params, g_variant_new_string (lp->prefix));
	}

	if (lp->marker_end) {
		lazy_and();
		g_string_append (clause, " alias < ?");
		g_ptr_array_add (params, g_variant_new_string (lp->marker_end));
	}

	if (headers) {
		lazy_and();
		if (headers->next) {
			g_string_append (clause, " content IN (");
			for (GSList *l=headers; l ;l=l->next) {
				if (l != headers)
					g_string_append_c (clause, ',');
				g_string_append_c (clause, '?');
				GByteArray *gba = CONTENT_get_id (l->data);
				g_ptr_array_add (params, _gba_to_gvariant (gba));
			}
			g_string_append (clause, ")");
		} else {
			g_string_append (clause, " content = ?");
			GByteArray *gba = CONTENT_get_id (headers->data);
			g_ptr_array_add (params, _gba_to_gvariant (gba));
		}
	}

	if (clause->len == 0)
		clause = g_string_append(clause, " 1");

	if (!lp->flag_allversion || lp->maxkeys>0 || lp->marker_start || lp->marker_end)
		g_string_append(clause, " ORDER BY alias ASC, version ASC");

	if (lp->maxkeys > 0)
		g_string_append_printf(clause, " LIMIT %"G_GINT64_FORMAT, lp->maxkeys);

	g_ptr_array_add (params, NULL);
	return (GVariant**) g_ptr_array_free (params, FALSE);
}

GError*
m2db_list_aliases(struct sqlx_sqlite3_s *sq3, struct list_params_s *lp0,
		GSList *headers, m2_onbean_cb cb, gpointer u)
{
	GError *err = NULL;
	GSList *aliases = NULL;
	guint count_aliases = 0;
	struct list_params_s lp = *lp0;
	gboolean done = FALSE;

	while (!done && (lp.maxkeys <= 0 || count_aliases < lp.maxkeys)) {
		GPtrArray *tmp = g_ptr_array_new();
		void cleanup (void) {
			g_ptr_array_set_free_func (tmp, _bean_clean);
			g_ptr_array_free (tmp, TRUE);
			tmp = NULL;
		}

		if (aliases)
			lp.marker_start = ALIAS_get_alias(aliases->data)->str;
		if (lp.maxkeys > 0)
			lp.maxkeys -= count_aliases;

		// List the next items
		GString *clause = g_string_new("");
		GVariant **params = _list_params_to_sql_clause (&lp, clause, headers);
		err = ALIAS_load(sq3->db, clause->str, params, _bean_buffer_cb, tmp);
		metautils_gvariant_unrefv (params);
		g_free (params), params = NULL;
		g_string_free (clause, TRUE);
		if (err) { cleanup (); goto label_error; }
		if (!tmp->len) { cleanup (); goto label_ok; }

		metautils_gpa_reverse (tmp);

		for (guint i=tmp->len; i>0 ;i--) {
			struct bean_ALIAS_s *alias = tmp->pdata[i-1];
			const gchar *name = ALIAS_get_alias(alias)->str;

			if ((lp.prefix && !g_str_has_prefix(name, lp.prefix)) ||
					(lp.maxkeys > 0 && count_aliases > lp.maxkeys)) {
				cleanup (); goto label_ok;
			}

			g_ptr_array_remove_index_fast (tmp, i-1);

			if (!aliases || lp.flag_allversion) {
				aliases = g_slist_prepend(aliases, alias);
				++ count_aliases;
			} else {
				const gchar *last_name = ALIAS_get_alias(aliases->data)->str;
				if (!strcmp(last_name, name)) {
					_bean_clean (aliases->data);
					aliases->data = alias;
				} else {
					aliases = g_slist_prepend(aliases, alias);
					++ count_aliases;
				}
			}
		}

		done = (lp.maxkeys <= 0) || (lp.maxkeys > tmp->len);
		cleanup();
	}

label_ok:
	aliases = g_slist_reverse (aliases);
	for (GSList *l=aliases; l ;l=l->next) {
		struct bean_ALIAS_s *alias = l->data;
		if (lp.flag_headers) {
			GPtrArray *t0 = g_ptr_array_new();
			GError *e = _db_get_FK_by_name_buffered(alias, "content", sq3->db, t0);
			if (e) {
				GRID_DEBUG("No content for [%s] : (%d) %s",
						ALIAS_get_alias(alias)->str,
						err->code, err->message);
				g_clear_error (&e);
			}
			for (guint i=0; i<t0->len ;i++)
				cb(u, t0->pdata[i]);
			g_ptr_array_free (t0, TRUE);
		}
		cb(u, alias);
		l->data = NULL;
	}

label_error:
	g_slist_free_full (aliases, _bean_clean);
	return err;
}

/* PROPERTY --------------------------------------------------------------- */

GError*
m2db_get_properties(struct sqlx_sqlite3_s *sq3, struct hc_url_s *url,
		m2_onbean_cb cb, gpointer u0)
{
	GPtrArray *tmp = g_ptr_array_new ();

	GError *err = m2db_get_alias(sq3, url, M2V2_FLAG_HEADERS|M2V2_FLAG_NORECURSION, _bean_buffer_cb, tmp);
	if (err)
		_bean_cleanv2(tmp);
	else {
		for (guint i=0; i<tmp->len; ++i)
			cb(u0, tmp->pdata[i]);
		g_ptr_array_free(tmp, TRUE);
	}
	return err;
}

GError*
m2db_set_properties(struct sqlx_sqlite3_s *sq3, struct hc_url_s *url,
		gboolean flush, GSList *beans, m2_onbean_cb cb, gpointer u0)
{
	struct bean_ALIAS_s *alias = NULL;
	GError *err = m2db_get_alias1(sq3, url, M2V2_FLAG_NOPROPS
			|M2V2_FLAG_NORECURSION, &alias);
	if (err)
		return err;
	if (!alias)
		return NEWERROR(CODE_CONTENT_NOTFOUND, "Alias not found");

	const char *name = ALIAS_get_alias(alias)->str;
	gint64 version = ALIAS_get_version(alias);

	if (flush) {
		const char *sql = "alias = ? AND version = ?";
		GVariant *params[3] = {NULL,NULL,NULL};
		params[0] = g_variant_new_string (name);
		params[1] = g_variant_new_int64 (ALIAS_get_version(alias));
		err = _db_delete (&descr_struct_PROPERTY, sq3->db, sql, params);
		metautils_gvariant_unrefv (params);
	}

	for (; !err && beans ;beans=beans->next) {
		struct bean_PROPERTY_s *prop = beans->data;
		if (DESCR(prop) != &descr_struct_PROPERTY)
			continue;
		PROPERTY_set2_alias(prop, name);
		PROPERTY_set_version(prop, version);
		GByteArray *v = PROPERTY_get_value(prop);
		if (!v || !v->len || !v->data) {
			err = _db_delete_bean (sq3->db, prop);
		} else {
			err = _db_save_bean (sq3->db, prop);
			if (!err && cb)
				cb(u0, _bean_dup(prop));
		}
	}

	_bean_clean(alias);
	return err;
}

GError*
m2db_del_properties(struct sqlx_sqlite3_s *sq3, struct hc_url_s *url, gchar **namev)
{
	GError *err;
	GPtrArray *tmp;

	EXTRA_ASSERT(sq3 != NULL);
	EXTRA_ASSERT(url != NULL);
	EXTRA_ASSERT(namev != NULL);

	tmp = g_ptr_array_new();
	err = m2db_get_properties(sq3, url, _bean_buffer_cb, tmp);
	if (!err) {
		for (guint i=0; !err && i<tmp->len ;++i) {
			struct bean_PROPERTY_s *bean = tmp->pdata[i];
			if (DESCR(bean) != &descr_struct_PROPERTY)
				continue;
			if (namev && *namev) {
				/* explicit properties to be deleted */
				for (gchar **p=namev; *p ;++p) {
					if (!strcmp(*p, PROPERTY_get_key(bean)->str))
						_db_delete_bean (sq3->db, bean);
				}
			} else {
				/* all properties to be deleted */
				_db_delete_bean (sq3->db, bean);
			}
		}
	}

	_bean_cleanv2(tmp);
	return err;
}

/* DELETE ------------------------------------------------------------------- */

static GError *
m2db_purge_alias_being_deleted(struct sqlx_sqlite3_s *sq3, GSList *beans,
		GSList **pdeleted)
{
	GError *err = NULL;

	gboolean check_FK(gpointer bean, const gchar *fk) {
		gint64 count = 0;
		err = _db_count_FK_by_name(bean, fk, sq3->db, &count);
		return (NULL == err) && (1 >= count);
	}

	GSList *deleted = NULL;

	// First, mark the ALIAS for deletion, and check its CONTENT
	gboolean content_deleted = FALSE;
	for (GSList *l = beans; !err && l ;l=l->next) {
		gpointer bean = l->data;
		if (&descr_struct_ALIAS == DESCR(bean))
			deleted = g_slist_prepend(deleted, bean);
		else if (&descr_struct_CONTENT == DESCR(bean)) {
			if (check_FK(bean, "aliases")) {
				content_deleted = TRUE;
				deleted = g_slist_prepend(deleted, bean);
			}
		}
	}

	// If the content has been deleted, we can remove its CHUNKs
	if (!err && content_deleted) {
		for (GSList *l = beans; l ;l=l->next) {
			gpointer bean = l->data;
			if (bean && &descr_struct_CHUNK == DESCR(bean))
				deleted = g_slist_prepend(deleted, bean);
		}
	}

	if (err || !pdeleted)
		g_slist_free(deleted);
	else
		*pdeleted = metautils_gslist_precat (*pdeleted, deleted);
	return err;
}

static GError*
_real_delete(struct sqlx_sqlite3_s *sq3, GSList *beans, GSList **deleted_beans)
{
	// call the purge to know which beans must be really deleted
	GSList *deleted = NULL;
	GError *err = m2db_purge_alias_being_deleted(sq3, beans, &deleted);
	if (err) {
		g_slist_free(deleted);
		g_prefix_error(&err, "Purge error: ");
		return err;
	}

	_bean_debugl2 ("PURGE", deleted);

	// Now really delete the beans, and notify them.
	// But do not notify ALIAS already marked deleted (they have already been notified)
	for (GSList *l = deleted; l ;l=l->next) {
		if (DESCR(l->data) != &descr_struct_ALIAS || !ALIAS_get_deleted(l->data))
			*deleted_beans = g_slist_prepend (*deleted_beans, l->data);
		GError *e = _db_delete_bean(sq3->db, l->data);
		if (e != NULL) {
			GRID_WARN("Bean delete failed: (%d) %s", e->code, e->message);
			g_clear_error(&e);
		}
	}

	// recompute container size
	for (GSList *l = deleted; l ;l=l->next) {
		if (&descr_struct_CONTENT == DESCR(l->data)) {
			gint64 decrement = CONTENT_get_size(l->data);
			gint64 size = m2db_get_size(sq3) - decrement;
			m2db_set_size(sq3, size);
			GRID_DEBUG("CONTAINER size = %"G_GINT64_FORMAT
					" (lost %"G_GINT64_FORMAT")", size, decrement);
		}
	}

	g_slist_free(deleted);
	deleted = NULL;
	return NULL;
}

GError*
m2db_delete_alias(struct sqlx_sqlite3_s *sq3, gint64 max_versions,
		struct hc_url_s *url, m2_onbean_cb cb, gpointer u0)
{
	GError *err;
	struct bean_ALIAS_s *alias = NULL;
	GSList *beans = NULL;

	void _search_alias_and_size(gpointer ignored, gpointer bean) {
		(void) ignored;
		if (DESCR(bean) == &descr_struct_ALIAS)
			alias = _bean_dup(bean);
		beans = g_slist_prepend(beans, bean);
	}

	if (VERSIONS_DISABLED(max_versions) && hc_url_has(url, HCURL_VERSION)
			&& 0!=g_ascii_strtoll(hc_url_get(url, HCURL_VERSION), NULL, 10))
		return NEWERROR(CODE_BAD_REQUEST,
				"Versioning not supported and version specified");

	err = m2db_get_alias(sq3, url, M2V2_FLAG_NOPROPS|M2V2_FLAG_HEADERS,
			_search_alias_and_size, NULL);

	if (NULL != err) {
		_bean_cleanl2(beans);
		return err;
	}
	if (!alias || !beans) {
		_bean_cleanl2(beans);
		return NEWERROR(CODE_CONTENT_NOTFOUND, "No content to delete");
	}

	GRID_TRACE("CONTENT [%s] uses [%u] beans",
			hc_url_get(url, HCURL_WHOLE), g_slist_length(beans));

	if (VERSIONS_DISABLED(max_versions) || VERSIONS_SUSPENDED(max_versions) ||
			(hc_url_has(url, HCURL_VERSION) || ALIAS_get_deleted(alias))) {

		GSList *deleted_beans = NULL;
		err = _real_delete(sq3, beans, &deleted_beans);
		/* Client asked to remove no-more referenced beans, we tell him which */
		for (GSList *bean = deleted_beans; bean; bean = bean->next) {
			if (cb)
				cb(u0, _bean_dup(bean->data));
		}
		// XXX the list's content are the direct pointers to the original beans.
		g_slist_free(deleted_beans);

	} else if (hc_url_has(url, HCURL_VERSION)) {
		/* Alias is in a snapshot but user explicitly asked for its deletion */
		err = NEWERROR(CODE_NOT_ALLOWED,
				"Cannot delete a content belonging to a snapshot");
	} else {
		/* Create a new version marked as deleted */
		struct bean_ALIAS_s *new_alias = _bean_dup(alias);
		ALIAS_set_deleted(new_alias, TRUE);
		ALIAS_set_version(new_alias, 1 + ALIAS_get_version(alias));
		ALIAS_set_ctime(new_alias, (gint64)time(0));
		err = _db_save_bean(sq3->db, new_alias);
		if (cb)
			cb(u0, new_alias);
		else
			_bean_clean(new_alias);
		new_alias = NULL;
	}

	_bean_clean(alias);
	_bean_cleanl2(beans);
	return err;
}

/* PUT commons -------------------------------------------------------------- */

struct put_args_s
{
	struct m2db_put_args_s *put_args;
	guint8 *uid;
	gsize uid_size;

	gint64 version;
	gint64 count_version;

	m2_onbean_cb cb;
	gpointer cb_data;

	GSList *beans;
	gboolean merge_only;
};

static GError*
m2db_real_put_alias(struct sqlx_sqlite3_s *sq3, struct put_args_s *args)
{
	GError *err = NULL;
	GSList *l;

	guint64 now = g_get_real_time () / 1000000;

	for (l=args->beans; !err && l ;l=l->next) {
		gpointer bean = l->data;

		if (!l->data)
			continue;

		if (DESCR(bean) == &descr_struct_ALIAS) {
			if (args->merge_only)
				continue;
			ALIAS_set_version(bean, args->version+1);
			ALIAS_set2_content(bean, args->uid, args->uid_size);
			ALIAS_set_deleted(bean, FALSE);
			ALIAS_set_ctime(bean, now);
		}
		else if (DESCR(bean) == &descr_struct_CONTENT) {
			CONTENT_set2_id(bean, args->uid, args->uid_size);
			CONTENT_set_ctime(bean, now);
		}
		else if (DESCR(bean) == &descr_struct_CHUNK) {
			CHUNK_set_ctime(bean, now);
			CHUNK_set2_content(bean, args->uid, args->uid_size);
		}

		err = _db_save_bean(sq3->db, bean);
	}

	if (!err && args->cb) {
		for (l=args->beans; l ;l=l->next)
			args->cb(args->cb_data, _bean_dup(l->data));
	}

	return err;
}

static GError*
m2db_merge_alias(struct m2db_put_args_s *m2db_args, struct bean_ALIAS_s *latest,
		struct put_args_s *args)
{
	void cb(gpointer u0, gpointer bean) {
		GByteArray **pgba = u0;
		if (DESCR(bean) == &descr_struct_CONTENT) {
			GByteArray *gba = CONTENT_get_id(bean);
			if (!*pgba)
				*pgba = g_byte_array_new();
			else
				g_byte_array_set_size(*pgba, 0);
			g_byte_array_append(*pgba, gba->data, gba->len);
		}
		_bean_clean(bean);
	}

	GError *err = NULL;;
	GByteArray *gba = NULL;

	/* Extract the CONTENT_HEADER id in place */
	EXTRA_ASSERT(latest != NULL);
	if (NULL != (err = _db_get_FK_by_name(latest, "content", m2db_args->sq3->db, cb, &gba)))
		g_prefix_error(&err, "DB error: ");
	else {
		if (gba == NULL)
			err = NEWERROR(CODE_INTERNAL_ERROR, "CONTENT not found");
		else {
			args->merge_only = TRUE;
			args->uid = gba->data;
			args->uid_size = gba->len;
			err = m2db_real_put_alias(m2db_args->sq3, args);
		}
	}

	if (gba)
		g_byte_array_free(gba, TRUE);
	return err;
}

/* PUT ---------------------------------------------------------------------- */

static gint64
m2db_patch_alias_beans_list(struct m2db_put_args_s *args,
		GSList *beans)
{
	gchar policy[256];
	GSList *l;
	gint64 size = 0;

	EXTRA_ASSERT(args != NULL);

	/* ensure a storage policy and store the result */
	for (l=beans; l ;l=l->next) {
		gpointer bean;
		if (!(bean = l->data))
			continue;
		if (DESCR(bean) == &descr_struct_CONTENT) {
			size = CONTENT_get_size(bean);
			GString *spol = CONTENT_get_policy(bean);
			if (!spol || spol->len <= 0) {
				struct storage_policy_s *pol = NULL;
				GError *err;

				/* force the policy to the default policy of the container
				 * or namespace */
				err = _get_container_policy(args->sq3, &(args->nsinfo), &pol);
				CONTENT_set2_policy(bean, (!err && pol) ? storage_policy_get_name(pol) : "none");
				if (pol)
					storage_policy_clean(pol);
				if (err)
					g_clear_error(&err);
			}

			g_strlcpy(policy, CONTENT_get_policy(bean)->str, sizeof(policy));
		}
	}

	return size;
}

GError*
m2db_force_alias(struct m2db_put_args_s *args, GSList *beans,
		GSList **out_deleted, GSList **out_added)
{
	guint8 uid[33];
	struct put_args_s args2;
	struct bean_ALIAS_s *latest = NULL;
	GError *err = NULL;

	EXTRA_ASSERT(args != NULL);
	EXTRA_ASSERT(args->sq3 != NULL);
	EXTRA_ASSERT(args->url != NULL);
	if (!hc_url_has(args->url, HCURL_PATH))
		return NEWERROR(CODE_BAD_REQUEST, "Missing path");

	memset(&args2, 0, sizeof(args2));
	args2.beans = beans;

	gint64 size = m2db_patch_alias_beans_list(args, beans);

	if (hc_url_has(args->url, HCURL_VERSION)) {
		const char *tmp = hc_url_get(args->url, HCURL_VERSION);
		args2.version = g_ascii_strtoll(tmp, NULL, 10);
		err = m2db_get_versioned_alias(args->sq3, args->url, &latest);
	} else {
		err = m2db_latest_alias(args->sq3, args->url, &latest);
	}

	if (NULL != err) {
		if (err->code != CODE_CONTENT_NOTFOUND)
			g_prefix_error(&err, "Version error: ");
		else {
			g_clear_error(&err);
			SHA256_randomized_buffer(uid, sizeof(uid));
			args2.uid = uid;
			args2.uid_size = sizeof(uid);
			err = m2db_real_put_alias(args->sq3, &args2);
		}
	}
	else {
		if (latest)
			err = m2db_merge_alias(args, latest, &args2);
		else {
			SHA256_randomized_buffer(uid, sizeof(uid));
			args2.uid = uid;
			args2.uid_size = sizeof(uid);
			err = m2db_real_put_alias(args->sq3, &args2);
		}
	}

	if(!err)
       m2db_set_size(args->sq3, m2db_get_size(args->sq3) + size);

	if (latest)
		_bean_clean(latest);

	return err;
}

GError*
m2db_put_alias(struct m2db_put_args_s *args, GSList *beans,
		GSList **out_deleted, GSList **out_added)
{
	struct bean_ALIAS_s *latest = NULL;
	GError *err = NULL;
	gboolean purge_latest = FALSE;

	EXTRA_ASSERT(args != NULL);
	EXTRA_ASSERT(args->sq3 != NULL);
	EXTRA_ASSERT(args->url != NULL);
	if (!hc_url_has(args->url, HCURL_PATH))
		return NEWERROR(CODE_BAD_REQUEST, "Missing path");

	gint64 size = m2db_patch_alias_beans_list(args, beans);

	struct { guint32 r; guint32 now; guint16 pid; guint16 th; } uid;
	uid.now = time(0);
	uid.r = g_random_int();
	uid.pid = getpid();
	uid.th = oio_log_current_thread_id();

	struct put_args_s args2;
	memset(&args2, 0, sizeof(args2));
	args2.put_args = args;
	args2.uid = (guint8*) &uid;
	args2.uid_size = sizeof(uid);
	args2.cb = out_added ? _bean_list_cb : NULL;
	args2.cb_data = out_added;
	args2.beans = beans;
	args2.version = -1;

	if (NULL != (err = m2db_latest_alias(args->sq3, args->url, &latest))) {
		if (err->code == CODE_CONTENT_NOTFOUND) {
			GRID_TRACE("Alias not yet present (1)");
			g_clear_error(&err);
		} else {
			g_prefix_error(&err, "Version error: ");
		}
	}
	else if (!latest) {
		GRID_TRACE("Alias not yet present (2)");
	}
	else {
		/* create a new version, and do not override */
		args2.version = ALIAS_get_version(latest);

		if (VERSIONS_DISABLED(args->max_versions)) {
			if (ALIAS_get_deleted(latest) || ALIAS_get_version(latest) > 0) {
				GRID_DEBUG("Versioning DISABLED but clues of SUSPENDED");
				goto suspended;
			} else {
				err = NEWERROR(CODE_CONTENT_EXISTS, "versioning disabled + content present");
			}
		}
		else if (VERSIONS_SUSPENDED(args->max_versions)) {
suspended:
			// JFS: do not alter the size to manage the alias being removed,
			// this will be done by the real purge of the latest.
			purge_latest = TRUE;
		}
		else {
			purge_latest = FALSE;
		}
	}

	/** Perform the insertion now and patch the URL with the version */
	if (!err) {
		err = m2db_real_put_alias(args->sq3, &args2);
		if (!err) {
			gchar buf[16];
			g_snprintf(buf, sizeof(buf), "%"G_GINT64_FORMAT, args2.version+1);
			hc_url_set(args->url, HCURL_VERSION, buf);
			m2db_set_size(args->sq3, m2db_get_size(args->sq3) + size);
		}
	}

	/** Purge the latest alias if the condition was met */
	if (!err && purge_latest && latest) {
		GRID_TRACE("Need to purge the previous LATEST");
		GSList *inplace = g_slist_prepend (NULL, _bean_dup(latest));
		err = _manage_alias (args->sq3->db, latest, TRUE, _bean_list_cb, &inplace);
		if (!err) {
			GSList *deleted = NULL;
			err = _real_delete (args->sq3, inplace, &deleted);
			if (out_deleted) {
				*out_deleted = metautils_gslist_precat (*out_deleted, deleted);
				deleted = NULL;
			} else {
				_bean_cleanl2 (deleted);
			}
		}
		g_slist_free (inplace);
	}

	/** Purge the exceeding aliases */
	if (VERSIONS_LIMITED(args->max_versions)) {
		gint64 count_versions = _m2db_count_alias_versions(args->sq3, args->url);
		if (args->max_versions <= count_versions) {
			/** XXX purge the oldest alias */
			GRID_WARN("GLOBAL PURGE necessary");
		}
	}

	if (latest)
		_bean_clean(latest);
	return err;
}

GError*
m2db_copy_alias(struct m2db_put_args_s *args, const char *source)
{
	struct bean_ALIAS_s *latest = NULL;
	struct bean_ALIAS_s *dst_latest = NULL;
	struct hc_url_s *orig = NULL;
	GError *err = NULL;

	GRID_TRACE("M2 COPY(%s FROM %s)", hc_url_get(args->url, HCURL_WHOLE), source);
	EXTRA_ASSERT(args != NULL);
	EXTRA_ASSERT(args->sq3 != NULL);
	EXTRA_ASSERT(args->url != NULL);
	EXTRA_ASSERT(source != NULL);

	if (!hc_url_has(args->url, HCURL_PATH))
		return NEWERROR(CODE_BAD_REQUEST, "Missing path");

	// Try to use source as an URL
	orig = hc_url_oldinit(source);
	if (!orig || !hc_url_has(orig, HCURL_PATH)) {
		// Source is just the name of the content to copy
		orig = hc_url_oldinit(hc_url_get(args->url, HCURL_WHOLE));
		hc_url_set(orig, HCURL_PATH, source);
	}

	if (hc_url_has(orig, HCURL_VERSION)) {
		err = m2db_get_versioned_alias(args->sq3, orig, &latest);
	} else {
		err = m2db_latest_alias(args->sq3, orig, &latest);
	}

	if (!err) {
		if (latest == NULL) {
			/* source ko */
			err = NEWERROR(CODE_CONTENT_NOTFOUND,
					"Cannot copy content, source doesn't exist");
		} else if (ALIAS_get_deleted(latest)) {
			err = NEWERROR(CODE_CONTENT_NOTFOUND,
					"Cannot copy content, source is deleted");
		} else {
			ALIAS_set2_alias(latest, hc_url_get(args->url, HCURL_PATH));
			if (VERSIONS_DISABLED(args->max_versions) ||
					VERSIONS_SUSPENDED(args->max_versions)) {
				ALIAS_set_version(latest, 0);
			} else {
				// Will be overwritten a few lines below if content already exists
				ALIAS_set_version(latest, 1);
			}
			/* source ok */
			if (!(err = m2db_latest_alias(args->sq3, args->url, &dst_latest))) {
				if (dst_latest) {
					if (VERSIONS_DISABLED(args->max_versions)) {
						err = NEWERROR(CODE_CONTENT_EXISTS, "Cannot copy content,"
								"destination already exists (and versioning disabled)");
					} else if (VERSIONS_ENABLED(args->max_versions)) {
						ALIAS_set_version(latest, ALIAS_get_version(dst_latest) + 1);
					} // else -> VERSIONS_SUSPENDED -> version always 0
					_bean_clean(dst_latest);
				}
			} else {
				g_clear_error(&err);
			}
			if (!err) {
				GString *tmp = _bean_debug(NULL, latest);
				GRID_INFO("Saving new ALIAS %s", tmp->str);
				g_string_free(tmp, TRUE);
				err = _db_save_bean(args->sq3->db, latest);
			}
			_bean_clean(latest);
		}
	}

	hc_url_clean(orig);

	return err;
}

/* APPEND ------------------------------------------------------------------- */

struct append_context_s
{
	GPtrArray *tmp;
	guint8 uid[32];
	GByteArray *old_uid;
	GString *policy;
	gint64 old_version;
	gint64 old_count;
	gint64 old_size;
	gboolean fresh;
	gboolean versioning;
	gboolean append_on_deleted;
};

static void
_increment_position(GString *encoded_position, gint64 inc)
{
	gchar tail[128];
	gchar *end = NULL;
	gint64 pos;

	memset(tail, 0, sizeof(tail));
	pos = g_ascii_strtoll(encoded_position->str, &end, 10);
	g_strlcpy(tail, end, sizeof(tail));

	g_string_printf(encoded_position, "%"G_GINT64_FORMAT"%s", pos+inc, tail);
}

static void
_keep_old_bean(gpointer u, gpointer bean)
{
	struct append_context_s *ctx = u;
	GString *gs;
	gint64 i64;

	if (DESCR(bean) == &descr_struct_CHUNK) {
		CHUNK_set2_content(bean, ctx->uid, sizeof(ctx->uid));
		if (NULL != (gs = CHUNK_get_position(bean))) {
			i64 = g_ascii_strtoll(gs->str, NULL, 10);
			if (ctx->old_count < i64)
				ctx->old_count = i64;
		}
		/* We need them for notification purposes */
		g_ptr_array_add(ctx->tmp, bean);
	}
	else {
		if (DESCR(bean) == &descr_struct_ALIAS) {
			/* get the most up-to-date version */
			i64 = ALIAS_get_version(bean);
			if (ctx->old_version < i64) {
				ctx->old_version = i64;
				/* if it's deleted, overwrite (do a PUT instead of append) */
				if (ALIAS_get_deleted(bean)) {
					ctx->old_version -= 1;
					ctx->fresh = TRUE;
				}
			}
		}
		else if (DESCR(bean) == &descr_struct_CONTENT) {
			ctx->old_size = CONTENT_get_size(bean);
			if (NULL != (gs = CONTENT_get_policy(bean))) {
				g_string_assign(ctx->policy, gs->str);
			}
			// Save old content id so we can find old content beans
			ctx->old_uid = metautils_gba_dup(CONTENT_get_id(bean));
		}
		_bean_clean(bean);
	}
}

static gboolean
_mangle_new_bean(struct append_context_s *ctx, gpointer bean)
{
	if (DESCR(bean) == &descr_struct_ALIAS) {
		bean = _bean_dup(bean);
		ALIAS_set_version(bean, ((ctx->versioning) ? ctx->old_version + 1 : ctx->old_version));
		ALIAS_set2_content(bean, ctx->uid, sizeof(ctx->uid));
		g_ptr_array_add(ctx->tmp, bean);
		return FALSE;
	}

	if (DESCR(bean) == &descr_struct_CONTENT) {
		bean = _bean_dup(bean);
		CONTENT_set_size(bean, CONTENT_get_size(bean) + ctx->old_size);
		CONTENT_set2_id(bean, ctx->uid, sizeof(ctx->uid));
		CONTENT_set2_policy(bean, ctx->policy->str);
		CONTENT_nullify_hash(bean);
		g_ptr_array_add(ctx->tmp, bean);
		return TRUE;
	}

	if (DESCR(bean) == &descr_struct_CHUNK) {
		bean = _bean_dup(bean);
		_increment_position(CHUNK_get_position(bean), ctx->old_count);
		CHUNK_set2_content(bean, ctx->uid, sizeof(ctx->uid));
		g_ptr_array_add(ctx->tmp, bean);
		return FALSE;
	}

	return FALSE;
}

static void _delete_contents_by_id(struct sqlx_sqlite3_s *sq3, GByteArray *cid,
		GError **err)
{
	const gchar *clause = " content_id = ? ";
	GVariant *params[] = {NULL, NULL};
	params[0] = _gba_to_gvariant(cid);
	*err = CONTENT_delete(sq3->db, clause, params);
	g_variant_unref(params[0]);
}

GError*
m2db_append_to_alias(struct sqlx_sqlite3_s *sq3, namespace_info_t *ni,
		gint64 max_versions, struct hc_url_s *url, GSList *beans,
		m2_onbean_cb cb, gpointer u0)
{
	struct append_context_s ctx;
	GError *err = NULL;

	// Sanity checks
	GRID_TRACE("M2 APPEND(%s)", hc_url_get(url, HCURL_WHOLE));
	EXTRA_ASSERT(sq3 != NULL);
	EXTRA_ASSERT(url != NULL);
	if (!hc_url_has(url, HCURL_PATH))
		return NEWERROR(CODE_BAD_REQUEST, "Missing path");

	memset(&ctx, 0, sizeof(ctx));
	ctx.tmp = g_ptr_array_new();
	ctx.policy = g_string_new("");
	ctx.old_count = G_MININT64;
	ctx.old_version = G_MININT64;
	ctx.old_size = G_MININT64;
	ctx.versioning = VERSIONS_ENABLED(max_versions);
	SHA256_randomized_buffer(ctx.uid, sizeof(ctx.uid));

	// Merge the previous versions of the beans with the new part
	err = m2db_get_alias(sq3, url, M2V2_FLAG_NOPROPS, _keep_old_bean, &ctx);
	/* Content does not exist or is deleted */
	if (err && err->code == CODE_CONTENT_NOTFOUND &&
			!VERSIONS_DISABLED(max_versions)) {
		ctx.fresh = TRUE; // Do a PUT instead of append
		g_clear_error(&err);
	}
	if (ctx.fresh) {
		/* renew the buffer */
		_bean_cleanv2(ctx.tmp);
		ctx.tmp = g_ptr_array_new();
	}

	/* Append the old beans that will be kept */
	if (!err) {
		if (ctx.fresh) {
			struct m2db_put_args_s args;
			memset(&args, 0, sizeof(args));
			args.sq3 = sq3;
			args.url = url;
			args.max_versions = max_versions;
			args.nsinfo = *ni;
			err = m2db_put_alias(&args, beans, NULL, NULL); /** XXX TODO FIXME */
		}
		else {
			GRID_TRACE("M2 already %u beans, version=%"G_GINT64_FORMAT
					" position=%"G_GINT64_FORMAT" size=%"G_GINT64_FORMAT,
					ctx.tmp->len, ctx.old_version, ctx.old_count,
					ctx.old_size);
			gint64 append_size = 0;

			ctx.old_count = ctx.tmp->len ? ctx.old_count + 1 : 0;
			if (ctx.old_version == G_MININT64) {
				ctx.old_version = 0;
			}

			/* append and mangle the new beans */
			GRID_TRACE("MANGLE NEW BEAN => v + 1");
			for (; beans ;beans=beans->next) {
				if( _mangle_new_bean(&ctx, beans->data)) {
					append_size = CONTENT_get_size(beans->data);
				}
			}

			/* Now save the whole */
			if (!(err = _db_save_beans_array(sq3->db, ctx.tmp)) && cb) {
				guint i;
				for (i=0; i<ctx.tmp->len; i++) {
					cb(u0, _bean_dup(ctx.tmp->pdata[i]));
				}
			}
			if (VERSIONS_ENABLED(max_versions)) {
				// Container size is cumulative. Even if some chunks are
				// referenced by another alias version, we count them.
				m2db_set_size(sq3,
						m2db_get_size(sq3) + append_size + ctx.old_size);
			} else {
				m2db_set_size(sq3, m2db_get_size(sq3) + append_size);
			}
			if (!err && VERSIONS_DISABLED(max_versions)) {
				// We must not let old contents in the base or
				// synchronous deletion won't delete all chunks.
				_delete_contents_by_id(sq3, ctx.old_uid, &err);
				if (err) {
					GRID_DEBUG("%s", err->message);
					GRID_WARN("Some chunks may be referenced twice, purge recommended");
					g_clear_error(&err);
				}
			}
		}
	}

	g_string_free(ctx.policy, TRUE);
	_bean_cleanv2(ctx.tmp);
	metautils_gba_unref(ctx.old_uid);
	return err;
}

/* GENERATOR ---------------------------------------------------------------- */

struct gen_ctx_s
{
	struct hc_url_s *url;
	struct storage_policy_s *pol;
	struct grid_lb_iterator_s *iter;
	guint8 h[16];
	gint64 size;
	gint64 chunk_size;
	m2_onbean_cb cb;
	gpointer cb_data;
	guint8 uid[1];
};

static guint
_policy_parameter(struct storage_policy_s *pol, const gchar *key, guint def)
{
	const char *s;
	if (!pol || !key)
		return def;
	s = data_security_get_param(storage_policy_get_data_security(pol), key);
	if (!s)
		return def;
	return (guint) atoi(s);
}

static void
_m2_generate_alias_header(struct gen_ctx_s *ctx)
{
	const gchar *p;
	p = ctx->pol ? storage_policy_get_name(ctx->pol) : "none";

	GRID_TRACE2("%s(%s)", __FUNCTION__, hc_url_get(ctx->url, HCURL_WHOLE));

	struct bean_ALIAS_s *alias = _bean_create(&descr_struct_ALIAS);
	ALIAS_set2_alias(alias, hc_url_get(ctx->url, HCURL_PATH));
	ALIAS_set_version(alias, 0);
	ALIAS_set_ctime(alias, 0);
	ALIAS_set_deleted(alias, FALSE);
	ALIAS_set2_content(alias, ctx->uid, sizeof(ctx->uid));
	ctx->cb(ctx->cb_data, alias);

	struct bean_CONTENT_s *content = _bean_create(&descr_struct_CONTENT);
	CONTENT_set_size(content, ctx->size);
	CONTENT_set2_id(content, ctx->uid, sizeof(ctx->uid));
	CONTENT_set2_policy(content, p);
	CONTENT_set2_chunk_method(content, OIO_DEFAULT_CHUNK_METHOD);
	CONTENT_set2_mime_type(content, OIO_DEFAULT_MIME_TYPE);
	CONTENT_nullify_hash(content);
	ctx->cb(ctx->cb_data, content);
}

static void
_m2_generate_chunk(struct gen_ctx_s *ctx, struct service_info_s *si,
		guint pos, gint64 cs, gint subpos, gboolean parity)
{
	gchar *chunkid, strpos[64];
	gchar straddr[STRLEN_ADDRINFO], strid[STRLEN_CHUNKID];

	GRID_TRACE2("%s(%s)", __FUNCTION__, hc_url_get(ctx->url, HCURL_WHOLE));

	grid_addrinfo_to_string(&(si->addr), straddr, sizeof(straddr));
	SHA256_randomized_string(strid, sizeof(strid));

	if (subpos >= 0)
		g_snprintf(strpos, sizeof(strpos), (parity ? "%u.p%d" : "%u.%d"), pos, subpos);
	else
		g_snprintf(strpos, sizeof(strpos), "%u", pos);

	chunkid = g_strdup_printf("http://%s/%s", straddr, strid);

	struct bean_CHUNK_s *chunk = _bean_create(&descr_struct_CHUNK);
	CHUNK_set2_id(chunk, chunkid);
	CHUNK_set2_position(chunk, strpos);
	CHUNK_set_ctime(chunk, 0);
	CHUNK_set2_hash(chunk, ctx->h, sizeof(ctx->h));
	CHUNK_set_size(chunk, cs);
	CHUNK_set2_content(chunk, ctx->uid, sizeof(ctx->uid));
	ctx->cb(ctx->cb_data, chunk);

	g_free(chunkid);
}

static GError*
_m2_generate_RAIN(struct gen_ctx_s *ctx)
{
	GError *err = NULL;
	guint pos;
	gint64 s;
	const struct storage_class_s *stgclass;
	gint distance, k, m;

	GRID_TRACE2("%s(%s)", __FUNCTION__, hc_url_get(ctx->url, HCURL_WHOLE));
	distance = _policy_parameter(ctx->pol, DS_KEY_DISTANCE, 1);
	k = _policy_parameter(ctx->pol, DS_KEY_K, 3);
	m = _policy_parameter(ctx->pol, DS_KEY_M, 2);
	stgclass = storage_policy_get_storage_class(ctx->pol);
	_m2_generate_alias_header(ctx);

	(void) distance;

	for (pos=0,s=0; s < MAX(ctx->size,1) ;) {
		struct service_info_s **siv = NULL;

		struct lb_next_opt_s opt;
		memset(&opt, 0, sizeof(opt));
		opt.req.duplicates = (distance <= 0);
		opt.req.max = ((ctx->size>0)?(k + m):1);
		opt.req.distance = distance;
		opt.req.stgclass = stgclass;
		opt.req.strict_stgclass = FALSE; // Accept ersatzes

		if (!grid_lb_iterator_next_set(ctx->iter, &siv, &opt)) {
			if ( pos == 0 )
				err = NEWERROR(CODE_PLATFORM_ERROR,"No Rawx available");
			else
				err = NEWERROR(CODE_POLICY_NOT_SATISFIABLE, "Not enough RAWX");
			break;
		}

		for (gint i=0; siv[i] ;++i) {
			gboolean parity = (i >= k);
			_m2_generate_chunk(ctx, siv[i], pos, ctx->chunk_size,
					(parity ? i-k : i), parity);
		}

		service_info_cleanv(siv, FALSE);

		++ pos;
		s += ctx->chunk_size;
	}

	return err;
}

static GError*
_m2_generate_DUPLI(struct gen_ctx_s *ctx)
{
	GError *err = NULL;
	guint pos;
	gint64 s;
	const struct storage_class_s *stgclass;
	gint distance, copies;

	GRID_TRACE2("%s(%s)", __FUNCTION__, hc_url_get(ctx->url, HCURL_WHOLE));
	distance = _policy_parameter(ctx->pol, DS_KEY_DISTANCE, 1);
	copies = _policy_parameter(ctx->pol, DS_KEY_COPY_COUNT, 1);
	stgclass = storage_policy_get_storage_class(ctx->pol);
	_m2_generate_alias_header(ctx);

	(void) distance;

	for (pos=0,s=0; s < MAX(ctx->size,1) ;) {
		struct service_info_s **psi, **siv = NULL;

		struct lb_next_opt_s opt;
		memset(&opt, 0, sizeof(opt));
		// suppose that duplicates have distance=0 between them
		opt.req.duplicates = (distance <= 0);
		opt.req.max = copies;
		opt.req.distance = distance;
		opt.req.stgclass = stgclass;
		opt.req.strict_stgclass = FALSE; // Accept ersatzes

		if (!grid_lb_iterator_next_set(ctx->iter, &siv, &opt)) {
			if ( pos == 0 )
				err = NEWERROR(CODE_PLATFORM_ERROR,"No Rawx available");
			else
				err = NEWERROR(CODE_POLICY_NOT_SATISFIABLE, "Not enough RAWX");
			break;
		}

		for (psi=siv; *psi ;++psi)
			_m2_generate_chunk(ctx, *psi, pos, ctx->chunk_size, -1, FALSE);

		service_info_cleanv(siv, FALSE);

		++ pos;
		s += ctx->chunk_size;
	}

	return err;
}

GError*
m2_generate_beans(struct hc_url_s *url, gint64 size, gint64 chunk_size,
		struct storage_policy_s *pol, struct grid_lb_iterator_s *iter,
		m2_onbean_cb cb, gpointer cb_data)
{
	struct gen_ctx_s ctx;

	GRID_TRACE2("%s(%s)", __FUNCTION__, hc_url_get(url, HCURL_WHOLE));
	EXTRA_ASSERT(url != NULL);
	EXTRA_ASSERT(iter != NULL);

	if (!hc_url_has(url, HCURL_PATH))
		return NEWERROR(CODE_BAD_REQUEST, "Missing path");
	if (size < 0)
		return NEWERROR(CODE_BAD_REQUEST, "Invalid size");
	if (chunk_size <= 0)
		return NEWERROR(CODE_BAD_REQUEST, "Invalid chunk size");

	memset(ctx.h, 0, sizeof(ctx.h));
	memset(ctx.uid, 0, sizeof(ctx.uid));
	ctx.iter = iter;
	ctx.pol = pol;
	ctx.url = url;
	ctx.size = size;
	ctx.chunk_size = chunk_size;
	ctx.cb = cb;
	ctx.cb_data = cb_data;

	if (!pol/* || size == 0*/)
		return _m2_generate_DUPLI(&ctx);

	switch (data_security_get_type(storage_policy_get_data_security(pol))) {
		case RAIN:
			return _m2_generate_RAIN(&ctx);
		case DS_NONE:
		case DUPLI:
			return _m2_generate_DUPLI(&ctx);
		default:
			return NEWERROR(CODE_POLICY_NOT_SUPPORTED, "Invalid policy type");
	}
}

/* Storage Policy ----------------------------------------------------------- */

static GError*
_get_content_policy(struct sqlx_sqlite3_s *sq3, struct hc_url_s *url,
		struct namespace_info_s *nsinfo, struct storage_policy_s **result)
{
	GError *err = NULL;
	GPtrArray *tmp = NULL;
	struct bean_ALIAS_s *latest = NULL;
	struct storage_policy_s *policy = NULL;

	tmp = g_ptr_array_new();

	if (!(err = m2db_latest_alias(sq3, url, &latest))) {
		if (latest != NULL) {
			err = _db_get_FK_by_name_buffered(latest, "content", sq3->db, tmp);
			if (!err && tmp->len > 0) {
				struct bean_CONTENT_s *header = tmp->pdata[0];
				GString *polname = CONTENT_get_policy(header);
				if (polname && polname->str && polname->len) {
					policy = storage_policy_init(nsinfo, polname->str);
				}
			}
		}
	}

	_bean_cleanv2(tmp);
	if (!err)
		*result = policy;
	return err;
}

static GError*
_get_container_policy(struct sqlx_sqlite3_s *sq3, struct namespace_info_s *nsinfo,
		struct storage_policy_s **result)
{
	gchar *pname;
	EXTRA_ASSERT(result != NULL);

	*result = NULL;
	pname = sqlx_admin_get_str(sq3, M2V2_ADMIN_STORAGE_POLICY);
	if (pname) {
		*result = storage_policy_init(nsinfo, pname);
		g_free(pname);
	}

	return NULL;
}

GError*
m2db_get_storage_policy(struct sqlx_sqlite3_s *sq3, struct hc_url_s *url,
		struct namespace_info_s *nsinfo, gboolean from_previous,
		struct storage_policy_s **result)
{
	GError *err = NULL;
	struct storage_policy_s *policy = NULL;

	GRID_TRACE2("%s(%s)", __FUNCTION__, hc_url_get(url, HCURL_WHOLE));
	EXTRA_ASSERT(sq3 != NULL);
	EXTRA_ASSERT(url != NULL);
	EXTRA_ASSERT(nsinfo != NULL);
	EXTRA_ASSERT(result != NULL);

	if (from_previous)
		err = _get_content_policy(sq3, url, nsinfo, &policy);

	if (!err && !policy)
		err = _get_container_policy(sq3, nsinfo, &policy);

	if (!err)
		*result = policy;
	return err;
}

GError*
m2db_set_storage_policy(struct sqlx_sqlite3_s *sq3, const gchar *polname, int replace)
{
	const gchar *k = M2V2_ADMIN_STORAGE_POLICY;

	if (!replace) {
		gchar *s = sqlx_admin_get_str(sq3, k);
		if (s) {
			g_free(s);
			return NULL;
		}
	}

	sqlx_admin_set_str(sq3, k, polname);
	return NULL;
}

void
m2db_set_container_name(struct sqlx_sqlite3_s *sq3, struct hc_url_s *url)
{
	sqlx_admin_init_str(sq3, M2V2_ADMIN_VERSION, "0");

	struct map_s { const char *f; int k; } map[] = {
		{SQLX_ADMIN_NAMESPACE, HCURL_NS},
		{SQLX_ADMIN_ACCOUNT, HCURL_ACCOUNT},
		{SQLX_ADMIN_USERNAME, HCURL_USER},
		{SQLX_ADMIN_USERTYPE, HCURL_TYPE},
		{NULL,0},
	};
	for (struct map_s *p = map; p->f ; ++p) {
		const gchar *v = hc_url_get(url, p->k);
		if (v != NULL)
			sqlx_admin_init_str(sq3, p->f, v);
	}
}

/* ------------------------------------------------------------------------- */

static void
_m2v2_dup_alias(struct dup_alias_params_s *params, gpointer bean)
{
	GError *local_err = NULL;
	if (DESCR(bean) == &descr_struct_ALIAS) {
		struct bean_ALIAS_s *new_alias = _bean_dup(bean);
		gint64 latest_version = ALIAS_get_version(bean);
		/* If source container version specified, we may not be duplicating
		 * latest version of each alias, so we must find it. */
		if (params->src_c_version >= 1) {
			struct hc_url_s *url = hc_url_empty();
			hc_url_set(url, HCURL_PATH, ALIAS_get_alias(new_alias)->str);
			local_err = m2db_get_alias_version(params->sq3, url, &latest_version);
			if (local_err != NULL) {
				GRID_WARN("Failed to get latest alias version for '%s'",
						ALIAS_get_alias(new_alias)->str);
				g_clear_error(&local_err);
				_bean_clean(new_alias);
				new_alias = NULL;
			}
			hc_url_clean(url);
		}
		if (local_err == NULL) {
			ALIAS_set_version(new_alias,
					(!params->overwrite_latest) + latest_version);
			if (params->set_deleted) {
				ALIAS_set_deleted(new_alias, TRUE);
			}
			local_err = ALIAS_save(params->sq3->db, new_alias);
		}
		_bean_clean(new_alias);
	}
	_bean_clean(bean); // TODO: add parameter to disable cleaning
	if (local_err != NULL) {
		params->errors = g_slist_prepend(params->errors, local_err);
	}
}

static GError*
_purge_exceeding_aliases(struct sqlx_sqlite3_s *sq3, gint64 max_versions,
		m2_onbean_cb cb, gpointer u0)
{
	struct elt_s {
		gchar *alias;
		gint64 count;
	};

	GRID_TRACE("%s, max_versions = %"G_GINT64_FORMAT, __FUNCTION__, max_versions);

	const gchar *sql_lookup = "SELECT alias, count(*)"
		"FROM alias_v2 "
		"WHERE NOT deleted " // Do not count last extra deleted version
		"GROUP BY alias "
		"HAVING COUNT(*) > ?";
	const gchar *sql_delete = " rowid IN "
		"(SELECT rowid FROM alias_v2 WHERE alias = ? "
		" ORDER BY version ASC LIMIT ? ) ";

	int rc = SQLITE_OK;
	GError *err = NULL;
	sqlite3_stmt *stmt = NULL;
	GSList *to_be_deleted = NULL;

	if (VERSIONS_UNLIMITED(max_versions))
		return NULL;
	if (!VERSIONS_ENABLED(max_versions))
		max_versions = 1;

	sqlite3_prepare_debug(rc, sq3->db, sql_lookup, -1, &stmt, NULL);
	sqlite3_clear_bindings(stmt);
	sqlite3_reset(stmt);
	sqlite3_bind_int64(stmt, 1, max_versions);
	while (SQLITE_ROW == (rc = sqlite3_step(stmt))) {
		struct elt_s *elt = g_malloc0(sizeof(*elt));
		elt->alias = g_strdup((gchar*)sqlite3_column_text(stmt, 0));
		elt->count = sqlite3_column_int64(stmt, 1);
		to_be_deleted = g_slist_prepend(to_be_deleted, elt);
	}
	(void) sqlite3_finalize(stmt);

	GRID_DEBUG("Nb alias to drop: %d", g_slist_length(to_be_deleted));

	// Delete the alias bean and send it to callback
	void _delete_cb(gpointer udata, struct bean_ALIAS_s *alias)
	{
		GError *local_err = _db_delete_bean(sq3->db, alias);
		if (!local_err) {
			cb(udata, alias); // alias is cleaned by callback
		} else {
			GRID_WARN("Failed to drop %s (v%"G_GINT64_FORMAT"): %s",
					ALIAS_get_alias(alias)->str,
					ALIAS_get_version(alias),
					local_err->message);
			_bean_clean(alias);
			g_clear_error(&local_err);
		}
	}

	for (GSList *l=to_be_deleted; l ; l=l->next) {
		GError *err2 = NULL;
		GVariant *params[] = {NULL, NULL, NULL};
		struct elt_s *elt = l->data;
		params[0] = g_variant_new_string(elt->alias);
		params[1] = g_variant_new_int64(elt->count - max_versions);
		err2 = ALIAS_load(sq3->db, sql_delete, params,
				(m2_onbean_cb)_delete_cb, u0);
		if (err2) {
			GRID_WARN("Failed to drop exceeding copies of %s: %s",
					elt->alias, err2->message);
			if (!err)
				err = err2;
			else
				g_clear_error(&err2);
		}
		metautils_gvariant_unrefv(params);
	}

	for (GSList *l=to_be_deleted; l ;l=l->next) {
		struct elt_s *elt = l->data;
		g_free(elt->alias);
		g_free(elt);
		l->data = NULL;
	}
	g_slist_free(to_be_deleted);
	to_be_deleted = NULL;
	return err;
}

static GError*
_purge_deleted_aliases(struct sqlx_sqlite3_s *sq3, gint64 delay,
		m2_onbean_cb cb, gpointer u0)
{
	GError *err = NULL;
	gchar *sql, *sql2;
	GSList *old_deleted = NULL;
	GVariant *params[] = {NULL, NULL};
	gint64 now = (gint64) time(0);
	gint64 time_limit = 0;
	struct dup_alias_params_s dup_params;

	// All aliases which have one version deleted (the last) older than time_limit
	sql = (" alias IN "
			"(SELECT alias FROM "
			"  (SELECT alias,ctime,deleted FROM alias_v2 GROUP BY alias) "
			" WHERE deleted AND ctime < ?) ");

	// Last snapshoted aliases part of deleted contents.
	// (take some paracetamol)
	sql2 = (" rowid IN (SELECT a1.rowid FROM alias_v2 AS a1"
			" INNER JOIN "
			"(SELECT alias,max(deleted) as mdel FROM alias_v2 GROUP BY alias) AS a2 ON a1.alias = a2.alias "
			"WHERE mdel GROUP BY a1.alias HAVING max(version)) ");

	if (now < 0) {
		err = g_error_new(GQ(), CODE_INTERNAL_ERROR,
				"Cannot get current time: %s", g_strerror(errno));
		return err;
	}

	if (delay >= 0 && delay < now) {
		time_limit = now - delay;
	}

	// We need to copy/delete snapshoted aliases that used to appear deleted
	// thanks to a more recent alias which will be erased soon.
	// Build the list.
	void _load_old_deleted(gpointer u, gpointer bean) {
		(void) u;
		old_deleted = g_slist_prepend(old_deleted, bean);
	}
	ALIAS_load(sq3->db, sql2, params, _load_old_deleted, NULL);

	// Delete the alias bean and send it to callback
	void _delete_cb(gpointer udata, struct bean_ALIAS_s *alias)
	{
		GError *local_err = _db_delete_bean(sq3->db, alias);
		if (!local_err) {
			cb(udata, alias); // alias is cleaned by callback
		} else {
			GRID_WARN("Failed to drop %s (v%"G_GINT64_FORMAT"): %s",
					ALIAS_get_alias(alias)->str,
					ALIAS_get_version(alias),
					local_err->message);
			_bean_clean(alias);
			g_clear_error(&local_err);
		}
	}

	// Do the purge.
	GRID_DEBUG("Purging deleted aliases older than %ld seconds (timestamp < %ld)",
			delay, time_limit);
	params[0] = g_variant_new_int64(time_limit);
	err = ALIAS_load(sq3->db, sql, params, (m2_onbean_cb)_delete_cb, u0);
	metautils_gvariant_unrefv(params);

	// Re-delete what needs to.
	memset(&dup_params, 0, sizeof(struct dup_alias_params_s));
	dup_params.sq3 = sq3;
	dup_params.set_deleted = TRUE;
	dup_params.c_version = m2db_get_version(sq3);
	for (GSList *l = old_deleted; l != NULL; l = l->next) {
		if (!ALIAS_get_deleted(l->data)) {
			if (GRID_TRACE_ENABLED()) {
				GRID_TRACE("Copy/delete %s version %ld",
					ALIAS_get_alias(l->data)->str,
					ALIAS_get_version(l->data));
			}
			_m2v2_dup_alias(&dup_params, l->data); // also cleans l->data
		} else {
			_bean_clean(l->data);
		}
	}
	g_slist_free(old_deleted);
	if (dup_params.errors != NULL) {
		if (err == NULL) {
			err = NEWERROR(0,
				"Got at least one error while purging aliases, see meta2 logs");
		}
		for (GSList *l = dup_params.errors; l; l = l->next) {
			GRID_WARN("Alias purge error: %s", ((GError*)l->data)->message);
			g_clear_error((GError**)&l->data);
		}
		g_slist_free(dup_params.errors);
	}

	return err;
}

static gint64
_get_chunks_to_drop(sqlite3 *db, m2_onbean_cb cb, gpointer u0)
{
	gint64 count = 0;
	GVariant *params[] = {NULL};

	void cb_counter(gpointer udata, gpointer bean) {
		cb(udata, bean);
		count++;
	}
	CHUNK_load(db,
			" NOT EXISTS (SELECT chunk_id FROM content_v2 WHERE chunk_id = id)",
			params, cb_counter, u0);
	return count;
}

GError*
m2db_purge(struct sqlx_sqlite3_s *sq3, gint64 max_versions, gint64 retention_delay,
	guint32 flags, m2_onbean_cb cb, gpointer u0)
{
	GError *err;
	gboolean dry_run = flags & M2V2_MODE_DRYRUN;

	if (!dry_run) {
		if ((err = _purge_exceeding_aliases(sq3, max_versions, cb, u0))) {
			GRID_WARN("Failed to purge ALIAS: (code=%d) %s",
					err->code, err->message);
			g_clear_error(&err);
		}

		if (retention_delay >= 0) {
			if ((err = _purge_deleted_aliases(sq3, retention_delay, cb, u0))) {
				GRID_WARN("Failed to purge deleted ALIAS: (code=%d) %s",
						err->code, err->message);
				g_clear_error(&err);
			}
		}

		/* purge unreferenced properties */
		sqlx_exec(sq3->db, "DELETE FROM properties_v2 WHERE NOT EXISTS "
				"(SELECT alias FROM alias_v2 "
				" WHERE alias_v2.alias = properties_v2.alias)");

		/* purge unreferenced content_headers, cascading to contents */
		sqlx_exec(sq3->db, "DELETE FROM content_header_v2 WHERE NOT EXISTS "
				"(SELECT content_id FROM alias_v2 WHERE content_id = id)");

		sqlx_exec(sq3->db, "DELETE FROM content_v2 WHERE NOT EXISTS "
				"(SELECT content_id FROM alias_v2 "
				" WHERE alias_v2.content_id = content_v2.content_id)");
	}

	/* delete chunks if asked */
	if (NULL != cb) {
		gint64 count = _get_chunks_to_drop(sq3->db, cb, u0);

		GRID_DEBUG("Nb chunks found to delete %ld", count);

		if (!dry_run) {
			/* purge unreferenced chunks */
			sqlx_exec(sq3->db, "DELETE FROM chunk_v2 WHERE NOT EXISTS "
					"(SELECT chunk_id FROM content_v2 WHERE chunk_id = id)");
		}
	}

	if (!dry_run) {
		guint64 size = m2db_get_container_size(sq3->db, FALSE);
		m2db_set_size(sq3, (gint64)size);
	}

	return NULL;
}

GError*
m2db_deduplicate_contents(struct sqlx_sqlite3_s *sq3, struct hc_url_s *url,
		guint32 flags, GString **status_message)
{
	GError *err = NULL;
	GSList *impacted_aliases = NULL;
	gboolean dry_run = flags & M2V2_MODE_DRYRUN;
	guint64 size_before = m2db_get_container_size(sq3->db, TRUE);
	guint64 saved_space = dedup_aliases(sq3->db, url, dry_run, &impacted_aliases, &err);
	guint64 size_after = m2db_get_container_size(sq3->db, TRUE);
	if (status_message != NULL) {
		if (*status_message == NULL) {
			*status_message = g_string_new(NULL);
		}
		g_string_printf(*status_message,
				"%"G_GUINT64_FORMAT" bytes saved "
				"by deduplication of %u contents "
				"(%"G_GUINT64_FORMAT" -> %"G_GUINT64_FORMAT" bytes)",
				saved_space, g_slist_length(impacted_aliases),
				size_before, size_after);
	}
	g_slist_free_full(impacted_aliases, g_free);
	return err;
}

GError*
m2db_flush_container(sqlite3 *db)
{
	int rc = sqlx_exec(db, "DELETE FROM alias_v2");
	if (SQLITE_OK == rc) return NULL;
	return SQLITE_GERROR(db, rc);
}

