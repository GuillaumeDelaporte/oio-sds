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

#ifndef OIO_SDS__meta2v2__generic_h
# define OIO_SDS__meta2v2__generic_h 1

# include <glib.h>
# include <sqlite3.h>

# define BEAN_FLAG_DIRTY     0x01
# define BEAN_FLAG_TRANSIENT 0x02

#ifndef M2_SQLITE_GERROR
# define M2_SQLITE_GERROR(db, RC) g_error_new(GQ(), (RC), "(%d) %s", (RC), db?sqlite3_errmsg(db):"-")
#endif

#define HDR(B)            ((struct bean_header_s*)(B))
#define DESCR(B)          (HDR(B)->descr)
#define DESCR_FIELD(B,i)  (DESCR(B)->fields + i)
#define FIELD(B,i)        (((guint8*)(B)) + DESCR(B)->offset_fields + DESCR_FIELD(B,i)->offset)
#define GSTR(pf)          (*((GString**)pf))
#define GBA(pf)           (*((GByteArray**)pf))

struct field_descriptor_s;
struct bean_decriptor_s;
struct fk_descriptor_s;

typedef void (*on_bean_f) (gpointer u, gpointer bean);

/** PRIVATE, DON'T TOUCH UNLESS YOU KNOW WHAT YOU ARE DOING */
struct bean_header_s
{
	guint32 flags;
	guint64 fields; /*!< the bit at position i means that the fields at
					  position i is set (and thus not NULL) */
	const struct bean_descriptor_s *descr;
};

struct field_descriptor_s
{
	const gchar *name;
	const guint position;
	const long offset;
	const gboolean mandatory;
	const enum {FT_BOOL, FT_INT, FT_REAL, FT_TEXT, FT_BLOB} type;
	const gboolean pk;
};

struct bean_descriptor_s
{
	const gchar *name;
	const gchar *c_name;
	const gchar *sql_name;
	const gchar *sql_select;
	const gchar *sql_count;
	const gchar *sql_replace;
	const gchar *sql_update;
	const long offset_fields;
	const long struct_size;
	const guint count_fields;
	const struct field_descriptor_s *fields;
	const struct fk_descriptor_s *fk;
	gchar **fk_names;
	const gint order;
};

struct fk_field_s
{
	gint i;
	const gchar *name;
};

struct fk_descriptor_s
{
	/* the name making sense for the  */
	const gchar *logical_name;

	/* a unique name as registered in the DB */
	const gchar *name;

	const struct bean_descriptor_s *src;
	struct fk_field_s *src_fields;

	const struct bean_descriptor_s *dst;
	struct fk_field_s *dst_fields;
};

void _bean_clean(gpointer bean);
void _bean_cleanv(gpointer *beanv);
void _bean_cleanv2(GPtrArray *v);
void _bean_cleanl2(GSList *v);

GError* _db_save_bean(sqlite3 *db, gpointer bean);
GError* _db_save_beans_list(sqlite3 *db, GSList *list);
GError* _db_save_beans_array(sqlite3 *db, GPtrArray *array);

GError* _db_delete_bean(sqlite3 *db, gpointer bean);
GError* _db_delete(const struct bean_descriptor_s *descr, sqlite3 *db,
		const gchar *clause, GVariant **params);

/** Fills 'result' with beans described by 'descr', filtered with
 * the 'clause' and its parameters. */
GError* _db_get_bean(const struct bean_descriptor_s *descr,
		sqlite3 *db, const gchar *clause, GVariant **params,
		on_bean_f cb, gpointer u);

GError* _db_count_bean(const struct bean_descriptor_s *descr,
		sqlite3 *db, const gchar *clause, GVariant **params,
		gint64 *pcount);

/** Finds the FK descriptor, then calls _db_get_FK() */
GError* _db_get_FK_by_name(gpointer bean, const gchar *name,
		sqlite3 *db, on_bean_f cb, gpointer u);

GError* _db_del_FK_by_name(gpointer bean, const gchar *name, sqlite3 *db);

GError* _db_count_FK_by_name(gpointer bean, const gchar *name,
		sqlite3 *db, gint64 *pcount);

GError* _db_get_FK_by_name_buffered(gpointer bean, const gchar *name,
		sqlite3 *db, GPtrArray *result);

GString* _bean_debug(GString *gstr, gpointer bean);
void _bean_debugl2 (const char *tag, GSList *beans);

void _bean_randomize(gpointer bean, gboolean avoid_pk);

/** Returns a newly allocated blank bean of the goven type */
gpointer _bean_create(const struct bean_descriptor_s *descr);

const gchar * _bean_get_typename(gpointer bean);
gchar ** _bean_get_FK_names(gpointer bean);

gpointer _bean_create_child(gpointer bean, const gchar *fkname);

gpointer _bean_dup(gpointer bean);

void _bean_set_field_value(gpointer bean, guint pos, gpointer pv);

/** Appends the bean into 'gpa'.  */
void _bean_buffer_cb(gpointer gpa, gpointer bean);

/** Prepends the bean to the GSList pointed (2x) by plist, and
 * sets <plist> with the new list.  */
void _bean_list_cb(gpointer plist, gpointer bean);

#define _bean_has_field(bean,pos) (HDR(bean)->fields & (1<<(pos)))
#define _bean_set_field(bean,pos) (HDR(bean)->fields |= (1<<(pos)))
#define _bean_del_field(bean,pos) (HDR(bean)->fields &= ~(1<<(pos)))

gsize SHA256_randomized_buffer(guint8 *d, gsize dlen);

gsize SHA256_randomized_string(gchar *d, gsize dlen);

GVariant* _gba_to_gvariant(GByteArray *gba);

GVariant* _gb_to_gvariant(GBytes *gb);

/** Compare the beans by "order". The order is autogenerated and is different
 * for every kind of bean into a same API */
gint _bean_compare_kind (gconstpointer b0, gconstpointer b1);

#endif /*OIO_SDS__meta2v2__generic_h*/
