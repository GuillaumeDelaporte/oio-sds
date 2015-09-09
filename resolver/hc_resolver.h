/*
OpenIO SDS resolver
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

#ifndef OIO_SDS__resolver__hc_resolver_h
# define OIO_SDS__resolver__hc_resolver_h 1

# include <glib.h>

enum hc_resolver_flags_e
{
	HC_RESOLVER_NOCACHE = 0x01,
	HC_RESOLVER_NOATIME = 0x02,
	HC_RESOLVER_NOMAX =   0x04,
};

/** timeout (in seconds) for requests to meta0 services.
 * Ignored if negative or zero. Negative (unset) by default. */
extern gdouble rc_resolver_timeout_m0;

/** timeout (in seconds) for requests to meta1 services.
 * Ignored if negative or zero. Negative (unset) by default. */
extern gdouble rc_resolver_timeout_m1;

/* forward declarations */
struct meta1_service_url_s;
struct oio_url_s;

/* Hidden type */
struct hc_resolver_s;

/* Simple constructor */
struct hc_resolver_s* hc_resolver_create1(time_t now);

/* Calls hc_resolver_create1() with the current EPOCH time */
struct hc_resolver_s* hc_resolver_create(void);

void hc_resolver_configure (struct hc_resolver_s *r, enum hc_resolver_flags_e f);

/* Cleanup all the internal structures. */
void hc_resolver_destroy(struct hc_resolver_s *r);

/* @param d Timeout for services from meta1 */
void hc_resolver_set_max_services(struct hc_resolver_s *r, guint d);

/* @param d max cached entries from meta1 */
void hc_resolver_set_ttl_services(struct hc_resolver_s *r, time_t d);

/* @param d Timeout for services from conscience and meta0 */
void hc_resolver_set_ttl_csm0(struct hc_resolver_s *r, time_t d);

/* @param d max cached services from conscience and meta0 */
void hc_resolver_set_max_csm0(struct hc_resolver_s *r, guint d);

/* Set the internal clock of the resolver. This has to be done in order
 * to manage expirations. */
void hc_resolver_set_now(struct hc_resolver_s *r, time_t now);

/* Applies time-based cache policies. */
guint hc_resolver_expire(struct hc_resolver_s *r);

/* Applies cardinality-based cache policies. */
guint hc_resolver_purge(struct hc_resolver_s *r);

void hc_resolver_flush_csm0(struct hc_resolver_s *r);

void hc_resolver_flush_services(struct hc_resolver_s *r);

/* Fills 'result' with a NULL-terminated array on meta1 urls, those referenced
 * in the meta0/1 directory for the given service and the given URL.
 * Please note that calling this function with srvtype=meta1 will give the the
 * meta1 associated with the reference, and not the meta1 that should have been
 * returned by hc_resolve_reference_directory(). */
GError* hc_resolve_reference_service(struct hc_resolver_s *r,
		struct oio_url_s *url, const gchar *srvtype, gchar ***result);

/* Fills 'result' with a NULL-terminated array of IP:port couples, those
 * responsible for the given URL. */
GError* hc_resolve_reference_directory(struct hc_resolver_s *r,
		struct oio_url_s *url, gchar ***result);

/* Removes from the cache the services associated to the given references.
 * It doesn't touch the directory services belonging to the reference. */
void hc_decache_reference_service(struct hc_resolver_s *r,
		struct oio_url_s *url, const gchar *srvtype);

/* Removes from the cache the directory services for the given references.
 * It doesn't touche the cache entries for the directory content. */
void hc_decache_reference(struct hc_resolver_s *r, struct oio_url_s *url);

struct hc_resolver_stats_s
{
	time_t clock;

	struct {
		gint64 count;
		guint max;
		time_t ttl;
	} csm0;

	struct {
		gint64 count;
		guint max;
		time_t ttl;
	} services;
};

void hc_resolver_info(struct hc_resolver_s *r, struct hc_resolver_stats_s *s);

#endif /*OIO_SDS__resolver__hc_resolver_h*/
