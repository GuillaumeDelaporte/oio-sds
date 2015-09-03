/*
OpenIO SDS cluster
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

#ifndef OIO_SDS__cluster__conscience__conscience_srvtype_h
# define OIO_SDS__cluster__conscience__conscience_srvtype_h 1

# include <metautils/lib/metautils.h>
# include <cluster/conscience/conscience_srv.h>

struct conscience_srvtype_s
{
	GStaticRWLock rw_lock;
	struct conscience_s *conscience;
	gchar type_name[LIMIT_LENGTH_SRVTYPE];

	time_t alert_frequency_limit; /**<Time limit between two zero-scored service alerts*/
	time_t score_expiration; /**<Time interval used on the current time to know the oldest valid score time*/
	gint32 score_variation_bound; /**<absolute upper bound to a score increase.*/
	gchar *score_expr_str;	      /**<String form of the expression*/
	struct expr_s *score_expr;/**<Preparsed expression*/

	GHashTable *config_ht;	 /**<Maps (gchar*) to (GByteArray*)*/
	GByteArray *config_serialized;	/**<Preserialized configuration sent to the agents*/

	GHashTable *services_ht;	     /**<Maps (conscience_srvid_s*) to (conscience_srv_s*)*/
	struct conscience_srv_s services_ring;
};

typedef gboolean (service_callback_f) (struct conscience_srv_s * srv, gpointer udata);

/* ------------------------------------------------------------------------- */

/**
 * Allocates a new service-type holder, and sets its configuration to
 * acceptable default values.
 *
 * @param conscience the conscience structure this service type will
 * be bound to
 * @param type the name of the new service type
 * @return a valid service-type holder, or NULL in case of failure
 */
struct conscience_srvtype_s *conscience_srvtype_create(struct conscience_s *conscience, const char *type);

/**
 * Frees a given service-type holder and all its internal data.
 *
 * The given service-type holder may be NULL, then nothing is done.
 *
 * @param srvtype
 */
void conscience_srvtype_destroy(struct conscience_srvtype_s *srvtype);

/**
 * Save the given expression in the given service-type holder. The expression
 * is immedately parsed and immediately replaces the previous expression if
 * the parsing succeeded.
 *
 * See the metautils library documentation to learn more about the acceptable
 * syntax.
 *
 * @param srvtype a valid service-type holder
 * @param err a double pointer to a GError structure set on failure
 * @param expr_str a 
 * @return 
 */
gboolean conscience_srvtype_set_type_expression(struct conscience_srvtype_s
    *srvtype, GError ** error, const gchar * expr_str);

/**
 * Removes all the servics registered uner this service-type
 * @param srvtype a valid
 */
void conscience_srvtype_flush(struct conscience_srvtype_s *srvtype);

/**
 * In a given service-type holder, ensure a service with the given
 * identifier exists, and returns it to the caller.
 *
 * @param srvtype a valid service-type holder
 * @param err a double pointer to a GError structure set on failure
 * @param srvid a valid servide identifier
 * @return the service currently registered or NULL in case of
 * failure
 */
struct conscience_srv_s *conscience_srvtype_register_srv(struct
    conscience_srvtype_s *srvtype, GError ** err, const struct conscience_srvid_s *srvid);

gboolean conscience_srvtype_refresh(struct conscience_srvtype_s *srvtype,
    GError ** error, struct service_info_s *srvinfo, gboolean keep_score);

/**
 * Removes the service-type holder all the services which
 * expired.
 * 
 * @param srvtype a valid service-type holder
 * @param err a double pointer to a GError structure set on failure
 * @param callback if supplied, called on each service removed 
 * @param udata arbitrary caller data passed to each callback call,
 * if such a callback has been provided.
 * @return the number of service removed
 */
gint conscience_srvtype_remove_expired(struct conscience_srvtype_s *srvtype,
    GError ** err, service_callback_f * callback, gpointer udata);

/**
 * Executes the given callback on all the services registered in the
 * given service-type holder.
 *
 * The caller may want to include or exclude expired services.
 * 
 * @param srvtype a valid service-type holder
 * @param err a double pointer to a GError structure set on failure
 * @param flags 
 * @param callback a non-NULL function address, called on each service
 * matching the conditions
 * @param udata arbitrary caller data fed on each callback usage
 * @return TRUE on success, FALSE on failure
 */
gboolean conscience_srvtype_run_all(struct conscience_srvtype_s *srvtype,
    GError ** error, guint32 flags, service_callback_f * callback, gpointer udata);

/**
 * Returns a service entry for the given service ID in the given service
 * type holder.
 *
 * @param srvtype a valid service-type holder
 * @param srvid the identifier of the desired service
 * @return the stored service if found (not a copy!), or NULL
 *  if not found or in case of failure.
 */
struct conscience_srv_s *conscience_srvtype_get_srv(struct
    conscience_srvtype_s *srvtype, const struct conscience_srvid_s *srvid);

/**
 * Remove a registered service corresponding to the given service
 * identifier
 *
 * @param srvtype a valid service-type holder
 * @param srvid a valid service Identifier
 */
void conscience_srvtype_remove_srv(struct conscience_srvtype_s *srvtype, struct conscience_srvid_s *srvid);

/**
 * Counts the services registered
 *
 * @param srvtype a valid service-type holder
 * @param include_expired tells if the result also concerns expired services
 * @return the number of registered services matching the conditions
 */
guint conscience_srvtype_count_srv(struct conscience_srvtype_s *srvtype, gboolean include_expired);

/**
 * Sets acceptable default value in the configuration parameters
 * of the given service type holder.
 *
 * @param srvtype the service type holder to init
 */
void conscience_srvtype_init(struct conscience_srvtype_s *srvtype);

#endif /*OIO_SDS__cluster__conscience__conscience_srvtype_h*/
