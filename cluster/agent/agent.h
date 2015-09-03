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

#ifndef OIO_SDS__cluster__agent__agent_h
# define OIO_SDS__cluster__agent__agent_h 1

# include <sys/types.h>
# include <unistd.h>

# include <metautils/lib/metatypes.h>
# include <cluster/agent/gridagent.h>

# ifndef GS_CONFIG_NSINFO_REFRESH
#  define GS_CONFIG_NSINFO_REFRESH "nsinfo_refresh"
# endif

/* ------------------------------------------------------------------------- */

# define GCLUSTER_AGENT_REQ_TIMEOUT 8
# define GCLUSTER_AGENT_ACT_TIMEOUT 4

# define KEY_USER             "user"
# define KEY_GROUP            "group"
# define KEY_UID              "uid"
# define KEY_GID              "gid"
# define KEY_MODE             "mode"
# define KEY_PATH             "path"
# define KEY_PORT             "port"
# define KEY_BACKLOG          "backlog"
# define KEY_TIMEOUT          "timeout"

# define SVC_CHECK_KEY        "service_check"
# define SVC_CHECK_FREQ_KEY   "service_check_freq"
# define STATS_PERIOD         "period_local_stats"
# define SVC_PUSH_BLANK_KEY   "service_push_blank_unknown"

# define DEFAULT_SVC_CHECK       TRUE
# define DEFAULT_SVC_CHECK_FREQ  1
# define DEFAULT_SVC_PUSH_BLANK  TRUE

// Default value for the next 5
# define DEFAULT_CS_UPDATE_FREQ           5
# define CS_DEFAULT_FREQ_KEY              "cluster_update_freq"

# define CS_GET_NS_PERIOD_KEY            "period_get_ns"
# define CS_GET_SRVLIST_PERIOD_KEY       "period_get_srv"
# define CS_GET_SRVTYPE_PERIOD_KEY       "period_get_srvtype"
# define CS_PUSH_SRVLIST_PERIOD_KEY      "period_push_srv"

# define SECTION_GENERAL "General"
# define SECTION_SERVER_INET  "server.inet"
# define SECTION_SERVER_UNIX  "server.unix"

# define UNIX_DEFAULT_PATH      "" /*not set*/
# define UNIX_DEFAULT_GID       -1
# define UNIX_DEFAULT_UID       -1
# define UNIX_DEFAULT_MODE      0660
# define UNIX_DEFAULT_BACKLOG   32768
# define UNIX_DEFAULT_TIMEOUT   10000

# define INET_DEFAULT_PORT      -1 /*not set*/
# define INET_DEFAULT_BACKLOG   32768
# define INET_DEFAULT_TIMEOUT   10000

/* GLOBALS ----------------------------------------------------------------- */

/* main config */
extern gchar str_opt_config[512];
extern enum process_type_e agent_type;
extern gboolean flag_check_services;
extern int period_check_services;
extern gboolean gridagent_blank_undefined_srvtags;

/* networking */
extern int inet_socket_backlog;
extern int inet_socket_timeout;
extern int inet_socket_port;

extern char unix_socket_path[512];
extern int unix_socket_timeout;
extern int unix_socket_backlog;
extern int unix_socket_mode;
extern int unix_socket_uid;
extern int unix_socket_gid;

/* refresh */
extern int period_get_ns;
extern int period_get_srvtype;
extern int period_get_srvlist;
extern int period_push_srvlist;

/* ------------------------------------------------------------------------- */

typedef struct namespace_data_s {
	char name[LIMIT_LENGTH_NSNAME];
	struct addr_info_s addr;

	namespace_info_t ns_info;
	gboolean configured;
	struct conscience_s *conscience;
	
	/*services locally registered*/
	GHashTable *local_services;/**< Maps (gchar*) to (struct service_info_s*)*/
	GHashTable *down_services;/**< Maps (gchar*) to (struct service_info_s*)*/
} namespace_data_t;

void free_agent_structures(void);

void parse_namespaces(void);

extern GHashTable *namespaces;

int main_reqagent(void);

#endif /*OIO_SDS__cluster__agent__agent_h*/
