/*
OpenIO SDS meta0v2
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

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <metautils/lib/metacomm.h>
#include <sqliterepo/sqliterepo.h>
#include <sqliterepo/version.h>

#include "./internals.h"
#include "./meta0_backend.h"
#include "./meta0_utils.h"

struct meta0_backend_s
{
	gchar *id;
	gchar *ns;
	GRWLock rwlock;
	GPtrArray *array_by_prefix;
	GPtrArray *array_meta1_ref;
	struct sqlx_repository_s *repository;
	gboolean reload_requested;
};

static GError* _open_and_lock(struct meta0_backend_s *m0,
		enum m0v2_open_type_e how, struct sqlx_sqlite3_s **handle);

static void _unlock_and_close(struct sqlx_sqlite3_s *sq3);

/* ------------------------------------------------------------------------- */

static int
m0_to_sqlx(enum m0v2_open_type_e t)
{
	switch (t & 0x03) {
		case M0V2_OPENBASE_LOCAL:
			return SQLX_OPEN_LOCAL;
		case M0V2_OPENBASE_MASTERONLY:
			return SQLX_OPEN_MASTERONLY;
		case M0V2_OPENBASE_MASTERSLAVE:
			return SQLX_OPEN_MASTERSLAVE;
		case M0V2_OPENBASE_SLAVEONLY:
			return SQLX_OPEN_SLAVEONLY;
	}
	g_assert_not_reached();
	return SQLX_OPEN_LOCAL;
}

static GError*
_array_check(GPtrArray *gpa)
{
	guint16 prefix;
	guint i;
	gchar **v;

	if (gpa->len != 65536)
		return NEWERROR(EINVAL, "Invalid cache size");

	for (i=0; i<gpa->len ;i++) {
		prefix = i;
		if (!(v = gpa->pdata[i])) {
			/* cache not loaded */
			continue;
		}
		if (!*v)
			return NEWERROR(EINVAL, "Prefix not managed (2) : %04X", prefix);
	}

	return NULL;
}

struct meta0_backend_s *
meta0_backend_init(const gchar *ns, const gchar *id,
		struct sqlx_repository_s *repo)
{
	struct meta0_backend_s *m0 = g_malloc0(sizeof(*m0));
	g_rw_lock_init(&(m0->rwlock));
	m0->id = g_strdup(id);
	m0->ns = g_strdup(ns);
	m0->array_by_prefix = NULL;
	m0->array_meta1_ref = NULL;
	m0->repository = repo;
	m0->reload_requested = FALSE;

	return m0;
}

void
meta0_backend_clean(struct meta0_backend_s *m0)
{
	if (!m0)
		return;
	g_free0(m0->id);
	g_free0(m0->ns);
	if (m0->array_by_prefix)
		meta0_utils_array_clean(m0->array_by_prefix);
	if (m0->array_meta1_ref)
		meta0_utils_array_meta1ref_clean(m0->array_meta1_ref);
	g_rw_lock_clear(&(m0->rwlock));
	g_free(m0);
}

GError*
meta0_backend_check(struct meta0_backend_s *m0)
{
	EXTRA_ASSERT(m0 != NULL);
	EXTRA_ASSERT(m0->array_by_prefix != NULL);

	return _array_check(m0->array_by_prefix);
}

struct sqlx_repository_s*
meta0_backend_get_repository(struct meta0_backend_s *m0)
{
	EXTRA_ASSERT(m0 != NULL);
	return m0->repository;
}

void
meta0_backend_reload_requested(struct meta0_backend_s *m0)
{
	EXTRA_ASSERT(m0 != NULL);
	m0->reload_requested = TRUE;
}

/* ------------------------------------------------------------------------- */

static GError*
_load_from_base(struct sqlx_sqlite3_s *sq3, GPtrArray **result)
{
	GError *err = NULL;
	sqlite3_stmt *stmt;
	int rc;
	guint count = 0;

	*result = NULL;
	sqlite3_prepare_debug(rc, sq3->db, "SELECT prefix,addr,ROWID FROM meta1",
			-1, &stmt, NULL);
	if (rc != SQLITE_OK && rc != SQLITE_DONE)
		return SQLITE_GERROR(sq3->db, rc);

	GPtrArray *array = meta0_utils_array_create();

	do {
		rc = sqlite3_step(stmt);
		if (rc == SQLITE_ROW) {
			gint64 rowid;
			const guint8 *prefix, *url;
			gsize prefix_len;

			prefix_len = sqlite3_column_bytes(stmt, 0);
			prefix = sqlite3_column_blob(stmt, 0);
			url = sqlite3_column_text(stmt, 1);
			rowid = sqlite3_column_int64(stmt, 2);

			if (prefix_len != 2)
				GRID_WARN("Invalid prefix for URL [%s] ROWID %"G_GINT64_FORMAT,
						url, rowid);
			else {
				meta0_utils_check_url_from_base((gchar**)&url);
				meta0_utils_array_add(array, prefix, (gchar*)url);
				count ++;
			}
		}
	} while (rc == SQLITE_ROW);

	if (!sqlx_code_good(rc))
		err = SQLITE_GERROR(sq3->db, rc);
	sqlite3_finalize_debug(rc, stmt);

	if (err) {
		meta0_utils_array_clean (array);
		return err;
	}

	GRID_INFO("Reloaded %u prefixes in %p (%u)", count, array, array->len);
	*result = array;
	return NULL;
}

static GError*
_load_meta1ref_from_base(struct sqlx_sqlite3_s *sq3, GPtrArray **result)
{
	GError *err = NULL;
	sqlite3_stmt *stmt;
	int rc;
	guint count = 0;

	*result = NULL;

	sqlite3_prepare_debug(rc, sq3->db, "SELECT addr,state,prefixes FROM meta1_ref",
			-1, &stmt, NULL);
	if (rc != SQLITE_OK && rc != SQLITE_DONE)
		return SQLITE_GERROR(sq3->db, rc);

	GPtrArray *array = g_ptr_array_new();
	do {
		rc = sqlite3_step(stmt);
		if (rc == SQLITE_ROW) {
			const unsigned char *url,*prefix_nb,*ref;
			url = sqlite3_column_text(stmt,0);
			ref = sqlite3_column_text(stmt,1);
			prefix_nb = sqlite3_column_text(stmt,2);

			g_ptr_array_add (array, meta0_utils_pack_meta1ref (
						(gchar *)url, (gchar *)ref, (gchar *)prefix_nb));
			count++;
		}
	} while (rc == SQLITE_ROW);

	if (!sqlx_code_good(rc))
		err = SQLITE_GERROR(sq3->db, rc);
	sqlite3_finalize_debug(rc, stmt);

	if (err) {
		meta0_utils_array_meta1ref_clean (array);
		return err;
	}

	GRID_INFO("Reloaded %u meta1 in %p (%u)", count, array, array->len);
	*result = array;
	return NULL;
}

static GError*
_load(struct meta0_backend_s *m0)
{
	GError *err = NULL;
	struct sqlx_sqlite3_s *sq3 = NULL;

	GRID_TRACE2("%s(%p)", __FUNCTION__, m0);

	err = _open_and_lock(m0,M0V2_OPENBASE_MASTERSLAVE, &sq3);
	if (err != NULL) {
		return err;
	}

	err = _load_from_base(sq3, &(m0->array_by_prefix));
	if (err != NULL)
		g_prefix_error(&err, "Query error: ");

	err = _load_meta1ref_from_base(sq3, &(m0->array_meta1_ref));
	if (err != NULL)
		g_prefix_error(&err, "Query error: ");

	_unlock_and_close(sq3);
	return err;
}

static GError*
__fill(sqlite3 *db, gchar **urls, guint max, guint shift)
{
	gint rc;
	guint idx;
	sqlite3_stmt *stmt = NULL;
	GError *err = NULL;

	sqlite3_prepare_debug(rc, db, "INSERT INTO meta1"
			" (prefix,addr) VALUES (?,?)", -1, &stmt, NULL);
	if (rc != SQLITE_OK && rc != SQLITE_DONE)
		return SQLITE_GERROR(db, rc);

	/* One partition for each url */
	for (idx=0; idx<65536 ;idx++) {

		gchar **purl = urls + ((idx+shift) % max);
		guint16 index16 = idx;

		sqlite3_reset(stmt);
		sqlite3_clear_bindings(stmt);
		sqlite3_bind_blob(stmt, 1, &index16, 2, NULL);
		sqlite3_bind_text(stmt, 2, *purl, -1, NULL);
		while (!err) {
			rc = sqlite3_step(stmt);
			if (rc == SQLITE_OK || rc == SQLITE_DONE)
				break;
			if (rc == SQLITE_BUSY)
				sleep(1);
			else
				err = SQLITE_GERROR(db, rc);
		}
	}
	sqlite3_finalize_debug(rc, stmt);

	return err;
}

static GError*
_fill(struct sqlx_sqlite3_s *sq3,guint replicas, gchar **m1urls)
{
	struct sqlx_repctx_s *repctx = NULL;
	GError *err = NULL;
	guint max;

	max = m1urls ? g_strv_length(m1urls) : 0;
	EXTRA_ASSERT(max > 0 && max < 65536);

	err = sqlx_transaction_begin(sq3, &repctx);
	if (NULL != err)
		return err;

	while (replicas--) {
		err = __fill(sq3->db, m1urls, max, replicas);
		if (err)
			 break;
	}
	return sqlx_transaction_end(repctx, err);
}

static GError *
_reload(struct meta0_backend_s *m0, gboolean lazy)
{
	GError *err = NULL;

	EXTRA_ASSERT(m0 != NULL);
	GRID_TRACE("%s(%p,lazy=%d)", __FUNCTION__, m0, lazy);

	g_rw_lock_writer_lock(&(m0->rwlock));

	if (!lazy || m0->reload_requested || !m0->array_by_prefix || !m0->array_meta1_ref) {
		if (m0->array_by_prefix) {
			meta0_utils_array_clean(m0->array_by_prefix);
			m0->array_by_prefix = NULL;
		}
		if (m0->array_meta1_ref) {
			meta0_utils_array_meta1ref_clean(m0->array_meta1_ref);
			m0->array_meta1_ref = NULL;
		}

		err = _load(m0);
		m0->reload_requested = FALSE;
		if (NULL != err)
			g_prefix_error(&err, "Loading error: ");
	}

	g_rw_lock_writer_unlock(&(m0->rwlock));
	return err;
}

static GError*
_open_and_lock(struct meta0_backend_s *m0, enum m0v2_open_type_e how,
		struct sqlx_sqlite3_s **handle)
{
	GError *err = NULL;

	EXTRA_ASSERT(m0 != NULL);
	EXTRA_ASSERT(handle != NULL);

	/* Now open/lock the base in a way suitable for our op */
	guint flag = m0_to_sqlx(how);
	struct sqlx_name_s n = {.base=m0->ns, .type=NAME_SRVTYPE_META0, .ns=m0->ns};
	err = sqlx_repository_open_and_lock(m0->repository, &n, flag, handle, NULL);

	if (err != NULL) {
		if (!CODE_IS_REDIRECT(err->code))
			g_prefix_error(&err, "Open/Lock error: ");
		return err;
	}

	EXTRA_ASSERT(*handle != NULL);
	GRID_TRACE("Opened and locked [%s/%s]", m0->id, NAME_SRVTYPE_META0);

	return NULL;
}

static void
_unlock_and_close(struct sqlx_sqlite3_s *sq3)
{
	EXTRA_ASSERT(sq3 != NULL);

	sqlx_repository_unlock_and_close_noerror(sq3);
}

static GError *
_assign_prefixes(sqlite3 *db, const GPtrArray *new_assign_prefixes,
		gboolean init)
{
	GError *err = NULL;
	gint rc;
	guint idx;
	sqlite3_stmt *stmt = NULL;

	if ( !init ) {
		sqlite3_prepare_debug(rc, db, "DELETE FROM meta1", -1, &stmt, NULL);
		if (rc != SQLITE_OK && rc != SQLITE_DONE)
			return SQLITE_GERROR(db, rc);
		while (!err) {
			rc = sqlite3_step(stmt);
			if (rc == SQLITE_OK || rc == SQLITE_DONE)
				break;
			if (rc == SQLITE_BUSY)
				sleep(1);
			else {
				return SQLITE_GERROR(db,rc);
			}
		}
		sqlite3_finalize_debug(rc, stmt);
	}

	sqlite3_prepare_debug(rc, db, "INSERT  INTO meta1"
			" (prefix,addr) VALUES (?,?)", -1, &stmt, NULL);

	if (rc != SQLITE_OK && rc != SQLITE_DONE) {
		return SQLITE_GERROR(db, rc);
	}

	for (idx=0; idx<65536 ;idx++) {

		gchar **url = new_assign_prefixes->pdata[idx];
		guint16 index16 = idx;

		if (!url || ! *url )
			continue;
		for (; *url ;url++) {
			sqlite3_reset(stmt);
			sqlite3_clear_bindings(stmt);
			sqlite3_bind_blob(stmt, 1, &index16, 2, NULL);
			sqlite3_bind_text(stmt, 2, *url, -1, NULL);
			while (!err) {
				rc = sqlite3_step(stmt);
				if (rc == SQLITE_OK || rc == SQLITE_DONE)
					break;
				if (rc == SQLITE_BUSY)
					sleep(1);
				else {
					err = SQLITE_GERROR(db, rc);
				}
			}
		}
	}
	sqlite3_finalize_debug(rc, stmt);

	return err;

}

static GError *
_record_meta1ref(sqlite3 *db, const GPtrArray *new_assign_meta1ref)
{
	GError *err = NULL;
	gint rc;
	guint idx;
	sqlite3_stmt *stmt = NULL;

	sqlite3_prepare_debug(rc, db, "REPLACE INTO meta1_ref"
			" (addr,state,prefixes) VALUES (?,?,?)", -1, &stmt, NULL);
	if (rc != SQLITE_OK && rc != SQLITE_DONE) {
		if ( rc == SQLITE_ERROR ) {
			GRID_DEBUG("Missing table meta1ref in DB");
			return NULL;
		}
		return SQLITE_GERROR(db, rc);
	}

	for (idx=0; idx < new_assign_meta1ref->len; idx++) {
		gchar *m1ref = new_assign_meta1ref->pdata[idx];
		gchar *addr, *ref, *nb;
		if ( ! meta0_utils_unpack_meta1ref(m1ref,&addr,&ref,&nb) )
			continue;
		sqlite3_reset(stmt);
		sqlite3_clear_bindings(stmt);
		sqlite3_bind_text(stmt, 1, addr, -1, NULL);
		sqlite3_bind_text(stmt, 2, ref, -1, NULL);
		sqlite3_bind_text(stmt, 3, nb, -1, NULL);

		while (!err) {
			rc = sqlite3_step(stmt);
			if (rc == SQLITE_OK || rc == SQLITE_DONE)
				break;
			if (rc == SQLITE_BUSY)
				sleep(1);
			else
				err = SQLITE_GERROR(db, rc);
		}

		if (addr)
			g_free(addr);
		if (ref)
			g_free(ref);
		if (nb)
			g_free(nb);
	}
	sqlite3_finalize_debug(rc, stmt);
	return err;
}

static GError *
_delete_meta1_ref(sqlite3 *db, gchar *meta1_ref)
{
	GError *err = NULL;
	gint rc;
	sqlite3_stmt *stmt = NULL;

	sqlite3_prepare_debug(rc, db, "DELETE FROM meta1_ref where addr=?",
			-1, &stmt, NULL);
	if (rc != SQLITE_OK && rc != SQLITE_DONE)
		return SQLITE_GERROR(db, rc);

	(void) sqlite3_bind_text(stmt,1,meta1_ref, strlen(meta1_ref), NULL);
	do { rc = sqlite3_step(stmt); } while (rc == SQLITE_ROW );
	if (rc != SQLITE_OK && rc != SQLITE_DONE)
		err = SQLITE_GERROR(db, rc);

	sqlite3_finalize_debug(rc, stmt);
	return err;
}

/* ------------------------------------------------------------------------- */

GError*
meta0_backend_fill(struct meta0_backend_s *m0, guint replicas, gchar **m1urls)
{
	struct sqlx_sqlite3_s *sq3 = NULL;
	GError *err;
	gchar **u = NULL;

	EXTRA_ASSERT(m0 != NULL);
	EXTRA_ASSERT(replicas < 65535);
	EXTRA_ASSERT(m1urls != 0);

	u = g_strdupv(m1urls);
	err = _open_and_lock(m0, M0V2_OPENBASE_MASTERONLY, &sq3);
	if (!err) {
		err = _fill(sq3,replicas,u);
		_unlock_and_close(sq3);
	}

	g_strfreev(u);
	return err;
}

GError *
meta0_backend_reload(struct meta0_backend_s *m0)
{
	EXTRA_ASSERT(m0 != NULL);
	return _reload(m0, FALSE);
}

GError*
meta0_backend_get_all(struct meta0_backend_s *m0, GPtrArray **result)
{
	GError *err = NULL;

	EXTRA_ASSERT(m0 != NULL);
	EXTRA_ASSERT(result != NULL);

	if (NULL != (err = _reload(m0, TRUE))) {
		g_prefix_error(&err, "Reload error: ");
		return err;
	}

	g_rw_lock_reader_lock(&(m0->rwlock));
	EXTRA_ASSERT(m0->array_by_prefix != NULL);
	*result = meta0_utils_array_dup(m0->array_by_prefix);
	g_rw_lock_reader_unlock(&(m0->rwlock));

	return NULL;
}

GError*
meta0_backend_get_one(struct meta0_backend_s *m0, const guint8 *prefix,
		gchar ***u)
{
	GError *err;

	EXTRA_ASSERT(m0 != NULL);
	EXTRA_ASSERT(u != NULL);

	GRID_TRACE("%s(%p,%02X%02X,%p)", __FUNCTION__,
			m0, prefix[0], prefix[1], u);

	if (NULL != (err = _reload(m0, TRUE))) {
		g_prefix_error(&err, "Reload error: ");
		return err;
	}

	g_rw_lock_reader_lock(&(m0->rwlock));
	EXTRA_ASSERT(m0->array_by_prefix != NULL);
	*u = meta0_utils_array_get_urlv(m0->array_by_prefix, prefix);
	g_rw_lock_reader_unlock(&(m0->rwlock));

	return *u ? NULL : NEWERROR(EINVAL, "META0 partially missing");
}

GError*
meta0_backend_assign(struct meta0_backend_s *m0,
		const GPtrArray *new_assign_prefixes,
		const GPtrArray *new_assign_meta1ref, const gboolean init)
{
	GError *err;
	struct sqlx_sqlite3_s *sq3 = NULL;
	struct sqlx_repctx_s *repctx = NULL;

	EXTRA_ASSERT(m0 != NULL);
	EXTRA_ASSERT(new_assign_prefixes != NULL);
	EXTRA_ASSERT(new_assign_meta1ref != NULL);

	err = _open_and_lock(m0, M0V2_OPENBASE_MASTERONLY, &sq3);
	if (NULL != err)
		return err;

	err = sqlx_transaction_begin(sq3, &repctx);
	if (NULL == err) {
		err = _assign_prefixes(sq3->db, new_assign_prefixes,init);
		if (!err)
			err = _record_meta1ref(sq3->db, new_assign_meta1ref);
		err = sqlx_transaction_end(repctx, err);
	}
	_unlock_and_close(sq3);
	return err;
}

GError*
meta0_backend_get_all_meta1_ref(struct meta0_backend_s *m0, GPtrArray **result)
{
	GError *err;

	EXTRA_ASSERT(m0 != NULL);
	EXTRA_ASSERT(result != NULL);

	if (NULL != (err = _reload(m0, TRUE))) {
		g_prefix_error(&err, "Reload error: ");
		return err;
	}

	g_rw_lock_reader_lock(&(m0->rwlock));
	EXTRA_ASSERT(m0->array_meta1_ref != NULL);
	*result = meta0_utils_array_meta1ref_dup(m0->array_meta1_ref);
	g_rw_lock_reader_unlock(&(m0->rwlock));

	return NULL;
}

GError*
meta0_backend_destroy_meta1_ref(struct meta0_backend_s *m0, gchar *meta1)
{
	GError *err = NULL;
	struct sqlx_sqlite3_s *sq3 = NULL;
	struct sqlx_repctx_s *repctx = NULL;
	GPtrArray *result;
	gchar *v, *addr, *ref, *nb;
	guint i, max, cmpaddr, cmpstate;

	EXTRA_ASSERT(m0 != NULL);
	EXTRA_ASSERT(meta1 != NULL);

	/* check if meta1 is disable */
	if (NULL != (err = _reload(m0, TRUE))) {
		g_prefix_error(&err, "Reload error: ");
		return err;
	}

	g_rw_lock_reader_lock(&(m0->rwlock));
	EXTRA_ASSERT(m0->array_meta1_ref != NULL);
	result = meta0_utils_array_meta1ref_dup(m0->array_meta1_ref);
	g_rw_lock_reader_unlock(&(m0->rwlock));

	for (i=0,max=result->len; i<max ;i++) {
		if (!(v = result->pdata[i]))
			continue;
		meta0_utils_unpack_meta1ref(v,&addr,&ref,&nb);
		cmpaddr = g_ascii_strcasecmp(addr,meta1);
		cmpstate = g_ascii_strcasecmp(ref,"0");
		g_free(addr);
		g_free(ref);
		g_free(nb);
		if ( cmpaddr == 0) {
			if (cmpstate != 0)
				return NEWERROR(EINVAL, "meta1 always available to prefix allocation");
			err = _open_and_lock(m0, M0V2_OPENBASE_MASTERONLY, &sq3);
			if (NULL != err)
				return err;

			err = sqlx_transaction_begin(sq3, &repctx);
			if (NULL == err) {
				err = _delete_meta1_ref(sq3->db, meta1);
				err = sqlx_transaction_end(repctx, err);
			}
			_unlock_and_close(sq3);
			return err;
		}
	}
	return NEWERROR(EINVAL, "UNKNOWN meta1");
}

