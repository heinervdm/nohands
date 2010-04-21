/*
 * Software Bluetooth Hands-Free Implementation
 *
 * Copyright (C) 2008 Sam Revitch <samr7@cs.washington.edu>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <dbus/dbus.h>
#include "dbus.h"
#include "util.h"

using namespace libhfp;

/*
 * libdbus does not provide a way to programmatically access the installed
 * version number of the header files.  So, we use autoconf to read
 * the version number out of pkgconfig, and compare it ourselves.
 */
#define VERSION_CODE(A,B,C) (((A)<<16)|((B)<<8)|(C))
#define CURRENT_DBUS_VERSION 			\
	VERSION_CODE(DBUS_VERSION_MAJOR,	\
		     DBUS_VERSION_MINOR,	\
		     DBUS_VERSION_MICRO)

/*
 * DbusTimerBridge and DbusWatchBridge are private, local classes that
 * connect the D-Bus notification mechanism to libhfp::DispatchInterface.
 */

struct DbusTimerBridge {
	DBusTimeout		*m_dbt;
	libhfp::TimerNotifier	*m_not;

	void TimerNotify(libhfp::TimerNotifier *notp) {
		assert(notp == m_not);
		if (!dbus_timeout_handle(m_dbt))
			m_not->Set(0);
		else if (dbus_timeout_get_enabled(m_dbt))
			m_not->Set(dbus_timeout_get_interval(m_dbt));
	}

	DbusTimerBridge(DBusTimeout *dbt, libhfp::TimerNotifier *notp)
		: m_dbt(dbt), m_not(notp) {

		dbus_timeout_set_data(dbt, this, TimerFree);
	}

	~DbusTimerBridge() {
		assert(m_not);
		delete m_not;
	}

	static dbus_bool_t TimerAdd(DBusTimeout *dbt, void *ptr) {
		libhfp::TimerNotifier *notp;
		DbusTimerBridge *bridgep;
		libhfp::DispatchInterface *di =
			(libhfp::DispatchInterface *) ptr;

		notp = di->NewTimer();
		if (!notp)
			return FALSE;

		bridgep = new DbusTimerBridge(dbt, notp);
		if (!bridgep) {
			delete notp;
			return FALSE;
		}

		notp->Register(bridgep, &DbusTimerBridge::TimerNotify);

		if (dbus_timeout_get_enabled(dbt))
			bridgep->m_not->Set(dbus_timeout_get_interval(dbt));
		return TRUE;
	}

	static void TimerRemove(DBusTimeout *dbt, void *ptr) {
		DbusTimerBridge *bridgep;
		bridgep = (DbusTimerBridge *) dbus_timeout_get_data(dbt);
		assert(bridgep && (bridgep->m_dbt == dbt));
		bridgep->m_not->Cancel();
	}

	static void TimerToggle(DBusTimeout *dbt, void *ptr) {
		DbusTimerBridge *bridgep;
		bridgep = (DbusTimerBridge *) dbus_timeout_get_data(dbt);
		assert(bridgep && (bridgep->m_dbt == dbt));
		if (dbus_timeout_get_enabled(dbt))
			bridgep->m_not->Set(dbus_timeout_get_interval(dbt));
		else
			bridgep->m_not->Cancel();
	}

	static void TimerFree(void *ptr) {
		DbusTimerBridge *bridgep = (DbusTimerBridge *) ptr;
		delete bridgep;
	}

	static bool ConfigureConnection(DBusConnection *connp,
					libhfp::DispatchInterface *di) {
		if (!di)
			return dbus_connection_set_timeout_functions(0, 0, 0,
								     0, 0, 0);

		/*
		 * Technically this is bending the standard.
		 * I hope it doesn't come back and bite anybody.
		 */
		return dbus_connection_set_timeout_functions(connp,
							     TimerAdd,
							     TimerRemove,
							     TimerToggle,
							     di, 0);
	}
};

struct DbusWatchBridge {
	DBusWatch		*m_dbw;
	libhfp::SocketNotifier	*m_rnot;
	libhfp::SocketNotifier	*m_wnot;

	void SocketNotify(libhfp::SocketNotifier *notp, int fh) {
		int flags = (notp == m_rnot)
			? DBUS_WATCH_READABLE : DBUS_WATCH_WRITABLE;
		(void) dbus_watch_handle(m_dbw, flags);
	}

	DbusWatchBridge(DBusWatch *dbw, libhfp::SocketNotifier *rnotp,
			libhfp::SocketNotifier *wnotp)
		: m_dbw(dbw), m_rnot(rnotp), m_wnot(wnotp) {

		dbus_watch_set_data(dbw, this, WatchFree);
	}

	~DbusWatchBridge() {
		delete m_rnot;
		delete m_wnot;
	}

	static dbus_bool_t WatchAdd(DBusWatch *dbw, void *ptr) {
		bool enabled;
		libhfp::SocketNotifier *rnotp, *wnotp;
		DbusWatchBridge *bridgep;
		libhfp::DispatchInterface *di =
			(libhfp::DispatchInterface *) ptr;
		int fd;

#if CURRENT_DBUS_VERSION >= VERSION_CODE(1,1,1)
		fd = dbus_watch_get_unix_fd(dbw);
#else
		fd = dbus_watch_get_fd(dbw);
#endif

		rnotp = di->NewSocket(fd, false);
		if (!rnotp)
			return FALSE;
		wnotp = di->NewSocket(fd, true);
		if (!rnotp) {
			delete rnotp;
			return FALSE;
		}

		bridgep = new DbusWatchBridge(dbw, rnotp, wnotp);
		if (!bridgep) {
			delete rnotp;
			delete wnotp;
			return FALSE;
		}

		rnotp->Register(bridgep, &DbusWatchBridge::SocketNotify);
		wnotp->Register(bridgep, &DbusWatchBridge::SocketNotify);

		enabled = dbus_watch_get_enabled(dbw);
		rnotp->SetEnabled((enabled && (dbus_watch_get_flags(dbw) &
					       DBUS_WATCH_READABLE))
				  ? true : false);
		wnotp->SetEnabled((enabled && (dbus_watch_get_flags(dbw) &
					       DBUS_WATCH_WRITABLE))
				  ? true : false);
		return TRUE;
	}

	static void WatchRemove(DBusWatch *dbw, void *ptr) {
		DbusWatchBridge *bridgep;
		bridgep = (DbusWatchBridge *) dbus_watch_get_data(dbw);
		assert(bridgep && (bridgep->m_dbw == dbw));
		bridgep->m_rnot->SetEnabled(false);
		bridgep->m_wnot->SetEnabled(false);
	}

	static void WatchToggle(DBusWatch *dbw, void *ptr) {
		DbusWatchBridge *bridgep;
		bool enabled;
		bridgep = (DbusWatchBridge *) dbus_watch_get_data(dbw);
		assert(bridgep && (bridgep->m_dbw == dbw));
		enabled = dbus_watch_get_enabled(dbw);
		bridgep->m_rnot->SetEnabled((enabled &&
					     (dbus_watch_get_flags(dbw) &
					      DBUS_WATCH_READABLE))
					    ? true : false);
		bridgep->m_wnot->SetEnabled((enabled &&
					     (dbus_watch_get_flags(dbw) &
					      DBUS_WATCH_WRITABLE))
					    ? true : false);
	}

	static void WatchFree(void *ptr) {
		DbusWatchBridge *bridgep = (DbusWatchBridge *) ptr;
		delete bridgep;
	}

	static bool ConfigureConnection(DBusConnection *connp,
					libhfp::DispatchInterface *di) {
		if (!di)
			return dbus_connection_set_watch_functions(0, 0, 0, 0,
								   0, 0);

		return dbus_connection_set_watch_functions(connp,
							   WatchAdd,
							   WatchRemove,
							   WatchToggle,
							   di, 0);
	}
};


/*
 * DbusObjectLocal receives messages for /org/freedesktop/DBus/Local
 * Currently this is limited to the Disconnected message.
 */

class DbusObjectLocal : public DbusExportObject {
	static const DbusMethod meths[];
	static const DbusInterface ifaces[];

protected:
	virtual bool Disconnected(DBusMessage *msgp) {
		GetDbusSession()->__Disconnect(true);
		return true;
	}

public:
	DbusObjectLocal(void) : DbusExportObject(DBUS_PATH_LOCAL, ifaces) {}
};


const DbusMethod DbusObjectLocal::meths[] = {
	DbusMethodEntry(DbusObjectLocal, Disconnected, 0, 0),
	{ 0, }
};

const DbusInterface DbusObjectLocal::ifaces[] = {
	{ DBUS_INTERFACE_LOCAL, meths },
	{ 0, }
};


/*
 * Support code for DbusMatchNotifier
 *
 * This long, complicated mess parses the expressions accepted by
 * org.freedesktop.DBus.AddMatch and converts them to a form that can
 * be applied directly to DBusMessage objects.
 *
 * The main parse function, ParseMatchExpression(), builds a list of
 * name/value tuples of type DbusMatchParseNode.  These get rebuilt
 * into a DbusMatchExpr object by BuildMatchExpression().
 */

enum DbusFieldType {
	DBUS_MFIELD_INVALID = 0,
	DBUS_MFIELD_TYPE,
	DBUS_MFIELD_SENDER,
	DBUS_MFIELD_IFACE,
	DBUS_MFIELD_MEMBER,
	DBUS_MFIELD_PATH,
	DBUS_MFIELD_DEST,
	DBUS_MFIELD_ARG,
};

struct DbusFieldMap {
	const char *name;
	DbusFieldType type;
};
static const DbusFieldMap g_fields[] = {
	{ "type",		DBUS_MFIELD_TYPE },
	{ "sender",		DBUS_MFIELD_SENDER },
	{ "interface",		DBUS_MFIELD_IFACE },
	{ "member",		DBUS_MFIELD_MEMBER },
	{ "path",		DBUS_MFIELD_PATH },
	{ "destination",	DBUS_MFIELD_DEST },
	{ 0, }
};

static DbusFieldType
FieldTypeSymbol(const char *fieldname, int &argnum)
{
	int i;
	char *endp;
	long v;

	argnum = 0;
	for (i = 0; g_fields[i].name; i++) {
		if (!strcmp(g_fields[i].name, fieldname))
			return g_fields[i].type;
	}
	if (strncmp(fieldname, "arg", 3))
		return DBUS_MFIELD_INVALID;
	v = strtol(&fieldname[3], &endp, 10);
	if (*endp || (endp == &fieldname[3]) || (v > 63))
		return DBUS_MFIELD_INVALID;
	argnum = v;
	return DBUS_MFIELD_ARG;
}

struct DbusMessageTypeMap {
	const char *name;
	int value;
};
static const DbusMessageTypeMap g_messagetypes[] = {
	{ "signal",		DBUS_MESSAGE_TYPE_SIGNAL },
	{ "method_call",	DBUS_MESSAGE_TYPE_METHOD_CALL },
	{ "method_return",	DBUS_MESSAGE_TYPE_METHOD_RETURN },
	{ "error",		DBUS_MESSAGE_TYPE_ERROR },
	{ 0, }
};

static int
MessageTypeSymbol(const char *msgtype)
{
	int i;
	for (i = 0; g_messagetypes[i].name; i++) {
		if (!strcmp(g_messagetypes[i].name, msgtype))
			return g_messagetypes[i].value;
	}
	return -1;
}

struct DbusMatchParseNode {
	ListItem links;
	DbusFieldType field;
	int argnum;
	char *value;

	void *operator new(size_t nb, const char *value) {
		return malloc(nb + (value ? (strlen(value) + 1) : 0));
	}

	void operator delete(void *mem) {
		free(mem);
	}
};

struct DbusMatchParseNodeComp {
	typedef int param_t;
	static bool CompItems(ListItem *la, ListItem *lb, int) {
		DbusMatchParseNode *a =
			GetContainer(la, DbusMatchParseNode, links);
		DbusMatchParseNode *b =
			GetContainer(lb, DbusMatchParseNode, links);
		if (a->field < b->field)
			return true;
		if (a->field > b->field)
			return false;
		return (a->argnum < b->argnum);
	}
};

static DbusMatchParseNode *
AllocMatchParseNode(const char *value)
{
	DbusMatchParseNode *nodep;
	nodep = new (value) DbusMatchParseNode;
	if (nodep) {
		nodep->value = (char *) (nodep + 1);
		strcpy(nodep->value, value);
	}
	return nodep;
}

static bool
ParseMatchExpression(const char *filter_arg, ListItem &nodes,
		     libhfp::DispatchInterface *di)
{
	ListItem *listp;
	DbusMatchParseNode *nodep, *lastp;
	DbusFieldType ftype;
	char *filter, *rule, *next_rule, *part;
	int argnum, len;
	int nnodes = 0;

	if (!filter_arg)
		return true;
	filter = strdup(filter_arg);
	if (!filter)
		return false;

	rule = filter;
	while (1) {
		bool in_quote = false;
		int nquotes = 0;

		/* Strip preceding whitespace */
		while (*rule && (*rule == ' ')) rule++;
		if (!*rule)
			break;

		/* Find the comma not between quotes */
		for (next_rule = rule, in_quote = false;
		     *next_rule && (in_quote || (*next_rule != ','));
		     next_rule++) {
			if (*next_rule == '\'') {
				nquotes++;
				in_quote = !in_quote;
			}
		}
		if (nquotes != 2)
			goto malformed_filter;
		assert(!in_quote);
		if (*next_rule) {
			*next_rule = '\0';
			next_rule++;
		}

		/* Find the equal sign */
		for (part = rule; *part && (*part != '='); part++);
		if (!*part)
			goto malformed_filter;
		assert(part < next_rule);
		len = part - rule;
		*part = '\0';
		part++;

		/* Strip whitespace before the equal sign */
		while (len && (rule[len - 1] == ' ')) len--;
		if (!len)
			goto malformed_filter;
		rule[len] = '\0';

		/* Strip trailing whitespace */
		len = next_rule - part;
		if (!len)
			goto malformed_filter;
		if (*next_rule)	
			len--;
		while (len && (part[len] == ' ')) len--;
		if (!len)
			goto malformed_filter;

		/* Parse out the match string */
		while (*part && (*part == ' ')) part++;
		if (*part != '\'')
			goto malformed_filter;
		part++;
		len--;
		if (!len || (part[len - 1] != '\''))
			goto malformed_filter;
		len--;
		part[len] = '\0';

		/* Find the field symbol */
		ftype = FieldTypeSymbol(rule, argnum);
		if (ftype == DBUS_MFIELD_INVALID) {
			di->LogError("DbusMatch: Unknown field "
				     "\"%s\"", rule);
			goto no_mem;
		}

		/* Is this the type field? */
		if (ftype == DBUS_MFIELD_TYPE) {
			argnum = MessageTypeSymbol(part);
			if (argnum < 0) {
				di->LogError("DbusMatch: Invalid message "
					     "type \"%s\"", part);
				goto no_mem;
			}
		}

		/* Construct a new node */
		nodep = AllocMatchParseNode(part);
		if (!nodep)
			goto no_mem;

		nodep->field = ftype;
		nodep->argnum = argnum;

		nodes.AppendItem(nodep->links);
		nnodes++;

		rule = next_rule;
	}

	if (nnodes) {
		/* Sort the nodes */
		libhfp::ListMergeSort<DbusMatchParseNodeComp>::
			Sort(nodes, nnodes, 0);

		/* Search for dupes */
		lastp = 0;
		ListForEach(listp, &nodes) {
			nodep = GetContainer(listp, DbusMatchParseNode,
					     links);
			if (lastp &&
			    (lastp->field == nodep->field) &&
			    ((nodep->field != DBUS_MFIELD_ARG) ||
			     (lastp->argnum == nodep->argnum))) {
				di->LogError("DbusMatch: "
					     "Multiple rules for a "
					     "single field in expr "
					     "\"%s\"",
					     filter_arg);
				goto no_mem;
			}
		}
	}

	return true;

malformed_filter:
	di->LogError("DbusMatch: Malformed expression \"%s\"",
		     filter_arg);
no_mem:
	if (filter)
		free(filter);
	return false;
}


/*
 * The below section constructs the runtime representation of the
 * rule expression and includes code for testing matches.
 */

struct DbusMatchRule {
	DbusFieldType field;
	int argnum;
	char *value;
};

struct DbusMatchExpr {
	ListItem	m_links;
	ListItem	m_notifiers;
	int		m_nnotifiers;
	const char	*m_bus_expr;
	DbusCompletion	*m_pend;
	int		m_nrules;
	DbusMatchRule	m_rules[0];

	void *operator new(size_t nb, size_t extra) {
		return malloc(nb + extra);
	}
	void operator delete(void *mem) {
		free(mem);
	}
	bool Compare(const DbusMatchExpr *rhs) const {
		int i;
		if (rhs->m_nrules != m_nrules)
			return false;
		for (i = 0; i < m_nrules; i++) {
			if ((rhs->m_rules[i].field != m_rules[i].field) ||
			    (rhs->m_rules[i].argnum != m_rules[i].argnum) ||
			    strcmp(rhs->m_rules[i].value, m_rules[i].value))
				return false;
		}
		return true;
	}
	bool MessageMatches(DBusMessage *msgp) {
		int i;
		for (i = 0; i < m_nrules; i++) {
			DBusMessageIter mi;
			int iter_arg = -1;
			const char *compme;
			switch (m_rules[i].field) {
			case DBUS_MFIELD_TYPE:
				if (dbus_message_get_type(msgp) !=
				    m_rules[i].argnum)
					return false;
				continue;
			case DBUS_MFIELD_SENDER:
				compme = dbus_message_get_sender(msgp);
				break;
			case DBUS_MFIELD_IFACE:
				compme = dbus_message_get_interface(msgp);
				break;
			case DBUS_MFIELD_MEMBER:
				compme = dbus_message_get_member(msgp);
				break;
			case DBUS_MFIELD_PATH:
				compme = dbus_message_get_path(msgp);
				break;
			case DBUS_MFIELD_DEST:
				compme = dbus_message_get_destination(msgp);
				break;
			case DBUS_MFIELD_ARG:
				if (iter_arg < 0) {
					if (!dbus_message_iter_init(msgp, &mi))
						return false;
					iter_arg = 0;
				}
				assert(iter_arg <= m_rules[i].argnum);
				while (iter_arg < m_rules[i].argnum) {
					if (!dbus_message_iter_next(&mi))
						return false;
					iter_arg++;
				}
				if (dbus_message_iter_get_arg_type(&mi) !=
				    DBUS_TYPE_STRING)
					return false;

				dbus_message_iter_get_basic(&mi, &compme);
				break;
			default:
				abort();
			}

			if (!compme ||
			    strcmp(compme, m_rules[i].value))
				return false;
		}

		return true;
	}
};

static void
FreeParseNodes(ListItem &nodes)
{
	DbusMatchParseNode *nodep;
	while (!nodes.Empty()) {
		nodep = GetContainer(nodes.next, DbusMatchParseNode, links);
		nodep->links.UnlinkOnly();
		delete nodep;
	}
}

static DbusMatchExpr *
BuildMatchExpression(const char *exprstring, libhfp::DispatchInterface *di)
{
	ListItem parserules, *listp;
	DbusMatchExpr *expr = 0;
	DbusMatchParseNode *nodep;
	char *valptr;
	size_t extra;
	int rulenum;

	if (!ParseMatchExpression(exprstring, parserules, di))
		goto failed;

	extra = strlen(exprstring) + 1;
	rulenum = 0;
	ListForEach(listp, &parserules) {
		nodep = GetContainer(listp, DbusMatchParseNode, links);
		extra += sizeof(DbusMatchRule);
		extra += (strlen(nodep->value) + 1);
		rulenum++;
	}

	expr = new (extra) DbusMatchExpr;
	if (!expr) {
		FreeParseNodes(parserules);
		return false;
	}

	expr->m_nnotifiers = 0;
	expr->m_nrules = rulenum;
	valptr = (char *) &(expr->m_rules[expr->m_nrules]);

	strcpy(valptr, exprstring);
	expr->m_bus_expr = valptr;
	valptr += (strlen(valptr) + 1);

	rulenum = 0;
	ListForEach(listp, &parserules) {
		nodep = GetContainer(listp, DbusMatchParseNode, links);
		expr->m_rules[rulenum].field = nodep->field;
		expr->m_rules[rulenum].argnum = nodep->argnum;
		strcpy(valptr, nodep->value);
		expr->m_rules[rulenum].value = valptr;
		valptr += (strlen(valptr) + 1);
		rulenum++;
	}

	FreeParseNodes(parserules);
	return expr;

failed:
	if (expr)
		delete expr;
	FreeParseNodes(parserules);
	return 0;
}


class DbusMatchNotifierImpl : public DbusMatchNotifier {
public:
	DbusSession		*m_session;
	libhfp::ListItem	m_links;
	class DbusMatchExpr	*m_expr;

	char			*m_rule;

	DbusMatchNotifierImpl(DbusSession *sessp)
		: m_session(sessp), m_expr(0), m_rule(0) {}
	virtual ~DbusMatchNotifierImpl() {
		if (m_expr)
			m_session->RemoveMatchNotifier(this);
	}

	virtual bool SetEnabled(bool enable) {
		if (enable && !m_expr)
			return m_session->AddMatchNotifier(this);
		if (!enable && m_expr)
			m_session->RemoveMatchNotifier(this);
		return true;
	}

	void *operator new(size_t nb, size_t extra) {
		return malloc(nb + extra);
	}
	void operator delete(void *mem) {
		free(mem);
	}
};

bool DbusSession::
AddMatchNotifier(DbusMatchNotifierImpl *matchp)
{
	DbusMatchExpr *exprp;
	DbusMatchExpr *eexprp;
	ListItem *listp;

	assert(matchp->m_session == this);
	assert(!matchp->m_expr);

	exprp = BuildMatchExpression(matchp->m_rule, GetDi());
	if (!exprp)
		return false;

	ListForEach(listp, &m_match_exprs) {
		eexprp = GetContainer(listp, DbusMatchExpr, m_links);
		if (exprp->Compare(eexprp)) {
			delete exprp;
			exprp = eexprp;
			goto finish;
		}
	}

	/*
	 * Register the thing with the D-Bus daemon.
	 * Maybe some day we'll check for errors and abort the match
	 * expression if the add fails.
	 */
	if (m_conn)
		dbus_bus_add_match(m_conn, exprp->m_bus_expr, 0);
	m_match_exprs.AppendItem(exprp->m_links);

finish:
	exprp->m_notifiers.AppendItem(matchp->m_links);
	exprp->m_nnotifiers++;
	matchp->m_expr = exprp;
	return true;
}

void DbusSession::
RemoveMatchNotifier(DbusMatchNotifierImpl *matchp)
{
	DbusMatchExpr *exprp;

	assert(matchp->m_session == this);

	if (!matchp->m_expr) {
		assert(matchp->m_links.Empty());
		return;
	}

	exprp = matchp->m_expr;
	assert(exprp->m_nnotifiers);
	matchp->m_links.Unlink();
	matchp->m_expr = 0;

	if (!--exprp->m_nnotifiers) {
		/* Remove the match expression */
		assert(exprp->m_notifiers.Empty());
		if (m_conn)
			dbus_bus_remove_match(m_conn, exprp->m_bus_expr, 0);
		exprp->m_links.Unlink();
		delete exprp;
	}
}

DBusHandlerResult DbusSession::
FilterHelper(DBusConnection *connection, DBusMessage *message,
	     void *user_data)
{
	DbusSession *sessp = (DbusSession *) user_data;
	ListItem notifiers, *listp;
	DbusMatchExpr *exprp;
	DbusMatchNotifierImpl *matchp;

	/* Search the match list to see if any of them claim the message */
	ListForEach(listp, &sessp->m_match_exprs) {
		exprp = GetContainer(listp, DbusMatchExpr, m_links);
		assert(exprp->m_nnotifiers);
		assert(exprp->m_nnotifiers == exprp->m_notifiers.Length());
		if (!exprp->MessageMatches(message))
			continue;

		notifiers.AppendItemsFrom(exprp->m_notifiers);
	}

	while (!notifiers.Empty()) {
		matchp = GetContainer(notifiers.next, DbusMatchNotifierImpl,
				      m_links);
		matchp->m_links.UnlinkOnly();
		assert(matchp->m_expr);
		assert(matchp->m_expr->m_nnotifiers);
		matchp->m_expr->m_notifiers.AppendItem(matchp->m_links);
		(*matchp)(matchp, message);
	}

#if !defined(NDEBUG)
	/* Reverify the match list consistency */
	ListForEach(listp, &sessp->m_match_exprs) {
		exprp = GetContainer(listp, DbusMatchExpr, m_links);
		assert(exprp->m_nnotifiers);
		assert(exprp->m_nnotifiers == exprp->m_notifiers.Length());
	}
#endif

	return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}


DbusMatchNotifier *DbusSession::
NewMatchNotifier(const char *expression)
{
	DbusMatchNotifierImpl *matchp;
	char *strptr;

	matchp = new (strlen(expression) + 1) DbusMatchNotifierImpl(this);
	if (matchp) {
		strptr = (char *)(matchp + 1);
		strcpy(strptr, expression);
		matchp->m_rule = strptr;

		if (!AddMatchNotifier(matchp)) {
			delete matchp;
			matchp = 0;
		}
	}

	return matchp;
}


class DbusPeerImpl : public DbusPeer {
public:
	ListItem		m_links;
	ListItem		m_notifiers;
	int			m_refs;
	bool			m_disconn;
	DbusSession		*m_sess;
	char			*m_name;
	DbusMatchNotifier	*m_match;
	DbusCompletion		*m_pend;

	void Disconnected(void) {
		DbusPeerDisconnectNotifier *notp;
		assert(!m_disconn);
		m_disconn = true;
		Get();
		while (!m_notifiers.Empty()) {
			notp = GetContainer(m_notifiers.next,
					    DbusPeerDisconnectNotifier,
					    m_links);
			notp->m_links.Unlink();
			(*notp)(notp);
			assert(m_refs > 1);
			Put();
		}
		Put();
	}

	void MatchNotify(DbusMatchNotifier *notp, DBusMessage *msgp) {
		assert(notp == m_match);
		delete m_match;
		m_match = 0;
		if (m_pend) {
			delete m_pend;
			m_pend = 0;
		}

		Disconnected();
	}

	void CompletionNotify(DbusCompletion *cplp, DBusMessage *replyp) {
		DBusMessageIter mi;
		dbus_bool_t res;
		assert(cplp == m_pend);

		if ((dbus_message_get_type(replyp) ==
		     DBUS_MESSAGE_TYPE_METHOD_RETURN) &&
		    dbus_message_iter_init(replyp, &mi) &&
		    (dbus_message_iter_get_arg_type(&mi) ==
		     DBUS_TYPE_BOOLEAN)) {
			dbus_message_iter_get_basic(&mi, &res);
			if (res) {
				/* Success!  The client still exists. */
				return;
			}
		}

		if (m_match) {
			delete m_match;
			m_match = 0;
		}

		Disconnected();
	}

	bool Subscribe(void) {
		DBusMessage *msgp = 0;
		DbusCompletion *cplp = 0;
		DbusMatchNotifier *matchp = 0;
		const char *client;
		libhfp::StringBuffer sb;

		client = GetName();
		if (!sb.AppendFmt("type='signal',"
				  "sender='org.freedesktop.DBus',"
				  "member='NameOwnerChanged',"
				  "arg0='%s',arg2=''",
				  client))
			return false;

		matchp = m_sess->NewMatchNotifier(sb.Contents());
		if (!matchp)
			goto failed;

		/*
		 * To ensure that the peer exists when the match is added,
		 * we ask the bus daemon.
		 */
		msgp = dbus_message_new(DBUS_MESSAGE_TYPE_METHOD_CALL);
		if (!msgp ||
		    !dbus_message_set_destination(msgp, DBUS_SERVICE_DBUS) ||
		    !dbus_message_set_path(msgp, DBUS_PATH_DBUS) ||
		    !dbus_message_set_interface(msgp, DBUS_INTERFACE_DBUS) ||
		    !dbus_message_set_member(msgp, "NameHasOwner") ||
		    !dbus_message_append_args(msgp,
					      DBUS_TYPE_STRING, &client,
					      DBUS_TYPE_INVALID) ||
		    !(cplp = m_sess->SendWithCompletion(msgp)))
			goto failed;

		matchp->Register(this, &DbusPeerImpl::MatchNotify);
		cplp->Register(this, &DbusPeerImpl::CompletionNotify);

		m_match = matchp;
		m_pend = cplp;
		return true;

	failed:
		if (msgp)
			dbus_message_unref(msgp);
		if (matchp)
			delete matchp;
		if (cplp)
			delete cplp;
		return false;
	}

	void Unsubscribe(void) {
		if (m_match) {
			delete m_match;
			m_match = 0;
		}
		if (m_pend) {
			delete m_pend;
			m_pend = 0;
		}
	}

	void *operator new(size_t nb, size_t extra) {
		return malloc(nb + extra);
	}
	void operator delete(void *mem) {
		free(mem);
	}

	const char *GetName(void) const { return m_name; }

	void Get(void) {
		assert(m_refs);
		m_refs++;
	}

	void Put(void) {
		assert(m_refs);
		if (!--m_refs)
			delete this;
	}

	virtual DbusPeerDisconnectNotifier *NewDisconnectNotifier(void) {
		DbusPeerDisconnectNotifier *notp;
		if (m_disconn)
			return 0;
		notp = new DbusPeerDisconnectNotifier;
		if (!notp)
			return 0;

		if (m_notifiers.Empty() && !Subscribe()) {
			delete notp;
			return 0;
		}

		notp->m_peer = this;
		Get();
		m_notifiers.AppendItem(notp->m_links);
		return notp;
	}

	void RemoveDisconnectNotifier(DbusPeerDisconnectNotifier *notp) {
		assert(!notp->m_links.Empty());
		assert(notp->m_peer == this);
		notp->m_links.Unlink();
		notp->m_peer = 0;
		if (!m_disconn && m_notifiers.Empty())
			Unsubscribe();
		Put();
	}

	DbusPeerImpl(DbusSession *sessp, const char *name)
		: m_refs(1), m_disconn(false), m_sess(sessp),
		  m_name((char *) (this + 1)), m_match(0), m_pend(0) {
		strcpy(m_name, name);
	}

	virtual ~DbusPeerImpl() {
		assert(m_notifiers.Empty());
		Unsubscribe();
		m_links.Unlink();
	}

};


DbusPeerDisconnectNotifier::
~DbusPeerDisconnectNotifier()
{
	if (!m_links.Empty()) {
		assert(m_peer);
		((DbusPeerImpl *)m_peer)->RemoveDisconnectNotifier(this);
	}
}

DbusPeer *DbusSession::
GetPeer(const char *name)
{
	DbusPeerImpl *peerp;
	ListItem *listp;

	ListForEach(listp, &m_peers) {
		peerp = GetContainer(listp, DbusPeerImpl, m_links);
		if (!strcmp(peerp->GetName(), name)) {
			peerp->Get();
			return peerp;
		}
	}

	peerp = new (strlen(name)) DbusPeerImpl(this, name);
	if (peerp) {
		m_peers.AppendItem(peerp->m_links);
	}
	return peerp;
}


DbusSession::
DbusSession(libhfp::DispatchInterface *di)
	: m_di(di), m_conn(0), m_dodispatch(0), m_local(0), m_owner(false)
{
}

DbusSession::
~DbusSession()
{
	Disconnect();
	if (m_dodispatch) {
		delete m_dodispatch;
		m_dodispatch = 0;
	}
}

bool DbusSession::
SetupDispatchOwner(void)
{
	assert(!m_conn);

	if (!m_dodispatch) {
		m_dodispatch = GetDi()->NewTimer();
		if (!m_dodispatch) {
			GetDi()->LogWarn("D-Bus: Could not allocate "
					 "dispatch timer");
			return false;
		}
		m_dodispatch->Register(this, &DbusSession::Dispatch);
	}

	if (!m_local) {
		m_local = new DbusObjectLocal;
		if (!m_local) {
			GetDi()->LogWarn("D-Bus: Could not allocate "
					 "local message handler");
			return false;
		}
	}

	return true;
}

bool DbusSession::
SetupEventsOwner(void)
{
	assert(m_owner);
	assert(m_dodispatch);
	assert(m_local);

	dbus_connection_set_exit_on_disconnect(m_conn, FALSE);

	if (!ExportObject(m_local)) {
		GetDi()->LogWarn("D-Bus: Could not configure local "
				 "message handler");
		return false;
	}

	if (!DbusTimerBridge::ConfigureConnection(m_conn, GetDi()) ||
	    !DbusWatchBridge::ConfigureConnection(m_conn, GetDi())) {
		GetDi()->LogWarn("D-Bus: Could not configure events");
		return false;
	}

	dbus_connection_set_dispatch_status_function(m_conn, SetDispatchStatus,
						     this, 0);
	if (dbus_connection_get_dispatch_status(m_conn) !=
	    DBUS_DISPATCH_COMPLETE)
		m_dodispatch->Set(0);
	return true;
}

bool DbusSession::
SetupEventsCommon(void)
{
	DbusMatchExpr *exprp;
	ListItem *listp;

	assert(m_conn);

	ListForEach(listp, &m_match_exprs) {
		exprp = GetContainer(listp, DbusMatchExpr, m_links);
		dbus_bus_add_match(m_conn, exprp->m_bus_expr, 0);
	}

	return dbus_connection_add_filter(m_conn,
					  FilterHelper,
					  this, 0);
}

bool DbusSession::
Connect(DBusBusType bustype)
{
	DBusError err;
	DBusConnection *conn;

	assert(!m_conn);

	if (!SetupDispatchOwner())
		return false;

	dbus_error_init(&err);
	conn = dbus_bus_get_private(bustype, &err);
	if (!conn) {
		GetDi()->LogWarn("D-Bus: Could not create private "
				 "connection: %s", err.message);
		return false;
	}

	m_conn = conn;
	m_owner = true;

	GetDi()->LogDebug("D-Bus: connected");

	if (!SetupEventsOwner() || !SetupEventsCommon()) {
		__Disconnect(false);
		return false;
	}

	return true;
}

void DbusSession::
__Disconnect(bool notify)
{
	if (m_conn) {
		if (m_owner) {
			if (m_dodispatch)
				m_dodispatch->Cancel();

			dbus_connection_close(m_conn);
			dbus_connection_unref(m_conn);
			m_owner = false;
		}

		m_conn = 0;

		GetDi()->LogDebug("D-Bus: disconnected");

		if (notify && cb_NotifyDisconnect.Registered())
			cb_NotifyDisconnect(this);
	}
}

void DbusSession::
Dispatch(libhfp::TimerNotifier *notp)
{
	assert(m_conn);
	assert(m_owner);
	assert(notp == m_dodispatch);

	dbus_connection_dispatch(m_conn);

	/*
	 * Careful -- this is a high level dispatching path, and we
	 * could have been disconnected from beneath!
	 */
	if (m_conn && (dbus_connection_get_dispatch_status(m_conn) !=
		       DBUS_DISPATCH_COMPLETE))
		m_dodispatch->Set(0);
}

void DbusSession::
SetDispatchStatus(DBusConnection *conn, DBusDispatchStatus new_stat, void *ptr)
{
	DbusSession *sess;
	sess = (DbusSession *) ptr;
	assert(sess->m_owner);
	assert(sess->m_conn == conn);
	if (new_stat == DBUS_DISPATCH_DATA_REMAINS)
		sess->m_dodispatch->Set(0);
	else
		sess->m_dodispatch->Cancel();
}

bool DbusSession::
AddUniqueName(const char *name)
{
	DBusError err;
	int res;

	if (!m_conn)
		return false;

	dbus_error_init(&err);
	res = dbus_bus_request_name(m_conn, name,
				    DBUS_NAME_FLAG_DO_NOT_QUEUE,
				    &err);

	if (res < 0) {
		GetDi()->LogWarn("D-Bus: Could not request name \"%s\": %s",
				 name, err.message);
		return false;
	}

	if (res == DBUS_REQUEST_NAME_REPLY_EXISTS)
		return false;

	assert((res == DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) ||
	       (res == DBUS_REQUEST_NAME_REPLY_ALREADY_OWNER));
	return true;
}

bool DbusSession::
RemoveUniqueName(const char *name)
{
	DBusError err;
	int res;

	if (!m_conn)
		return true;	/* We don't own that name! */

	dbus_error_init(&err);
	res = dbus_bus_release_name(m_conn, name, &err);

	if (res < 0) {
		GetDi()->LogWarn("D-Bus: Could not release name \"%s\": %s",
				 name, err.message);
		return false;
	}

	if (res != DBUS_RELEASE_NAME_REPLY_RELEASED) {
		GetDi()->LogWarn("D-Bus: Attempted to release unacquired "
				 "name \"%s\"", name);
	}
	return true;
}

bool DbusSession::
SendMessage(DBusMessage *msgp)
{
	if (!m_conn)
		return false;
	return dbus_connection_send(m_conn, msgp, 0);
}


void DbusCompletion::
CompletionHelper(DBusPendingCall *pcp, void *ud)
{
	DBusMessage *replyp;
	DbusCompletion *cp;

	cp = (DbusCompletion *) ud;
	assert(cp->m_pend == pcp);

	replyp = dbus_pending_call_steal_reply(pcp);

	dbus_pending_call_unref(cp->m_pend);
	cp->m_pend = 0;

	(*cp)(cp, replyp);

	if (replyp)
		dbus_message_unref(replyp);
}

DbusCompletion::
~DbusCompletion()
{
	if (m_pend) {
		dbus_pending_call_cancel(m_pend);
		dbus_pending_call_unref(m_pend);
		m_pend = 0;
	}
}

DbusCompletion *DbusSession::
SendWithCompletion(DBusMessage *msgp)
{
	DbusCompletion *cplp;
	if (!m_conn)
		return 0;

	cplp = new DbusCompletion;
	if (!dbus_connection_send_with_reply(m_conn, msgp,
					     &cplp->m_pend, -1) ||
	    !cplp->m_pend) {
		delete cplp;
		return 0;
	}

	if (!dbus_pending_call_set_notify(cplp->m_pend,
					  DbusCompletion::CompletionHelper,
					  cplp, 0)) {
		/*
		 * This is sticky as it will result in both a message
		 * being sent, and a failure being reported to the
		 * caller.  Bad business.
		 */
		delete cplp;
		return 0;
	}

	return cplp;
}


const DbusMethod DbusExportObject::s_methods_introspect[] = {
	DbusMethodEntryName("Introspect", DbusExportObject, DbusIntrospect,
			    "", "s"),
	{ 0, }
};

const DbusMethod DbusExportObject::s_methods_properties[] = {
	DbusMethodEntryName("Get", DbusExportObject, DbusPropertyGet,
			    "ss", "v"),
	DbusMethodEntryName("Set", DbusExportObject, DbusPropertySet,
			    "ssv", ""),
	DbusMethodEntryName("GetAll", DbusExportObject, DbusPropertyGetAll,
			    "s", 0),
	{ 0, }
};

const DbusInterface DbusExportObject::s_ifaces_common[] = {
	{ DBUS_INTERFACE_INTROSPECTABLE,
	  s_methods_introspect,
	  0,
	  0 },
	{ DBUS_INTERFACE_PROPERTIES,
	  s_methods_properties,
	  0,
	  0 },
	{ 0, }
};

const DBusObjectPathVTable DbusExportObject::s_vtable = {
	DbusExportObject::UnregisterHelper,
	DbusExportObject::DispatchHelper,
};

DbusExportObject::
~DbusExportObject()
{
	DbusUnregister();
}

DBusHandlerResult DbusExportObject::
DispatchHelper(DBusConnection *conn, DBusMessage *msg, void *ptr)
{
	DbusExportObject *objp;
	objp = (DbusExportObject *) ptr;
	return objp->DbusDispatch(msg);
}

const DbusInterface *DbusExportObject::
DbusFindInterface(const DbusInterface *ifaces, const char *name)
{
	if (!ifaces)
		return 0;
	while (ifaces->if_name) {
		if (!strcmp(ifaces->if_name, name))
			return ifaces;
		ifaces++;
	}
	return 0;
}

const DbusMethod *DbusExportObject::
DbusFindMethod(const DbusMethod *meths, const char *name)
{
	if (!meths)
		return 0;
	while (meths->meth_name) {
		if (!strcmp(meths->meth_name, name))
			return meths;
		meths++;
	}
	return 0;
}

const DbusProperty *DbusExportObject::
DbusFindProperty(const DbusProperty *props, const char *name)
{
	if (!props)
		return 0;
	while (props->prop_name) {
		if (!strcmp(props->prop_name, name))
			return props;
		props++;
	}
	return 0;
}

DBusHandlerResult DbusExportObject::
DbusDispatch(DBusMessage *msgp)
{
	const char *ifname;
	const DbusInterface *ifp;
	const DbusMethod *methp;

	assert(!strcmp(dbus_message_get_path(msgp), m_path));

	ifname = dbus_message_get_interface(msgp);
	ifp = DbusFindInterface(m_ifaces, ifname);
	if (!ifp) {
		ifp = DbusFindInterface(s_ifaces_common, ifname);
		if (!ifp)
			return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
	}
	methp = DbusFindMethod(ifp->if_meths, dbus_message_get_member(msgp));
	if (!methp)
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

	if (methp->meth_sig &&
	    !dbus_message_has_signature(msgp, methp->meth_sig)) {
		SendReplyError(msgp,
			       DBUS_ERROR_INVALID_ARGS,
			       "Signature Mismatch");
		return DBUS_HANDLER_RESULT_HANDLED;
	}

	return (methp->meth_func)(this, msgp)
		? DBUS_HANDLER_RESULT_HANDLED
		: DBUS_HANDLER_RESULT_NEED_MEMORY;
}

static bool
IntrospectMethod(libhfp::StringBuffer &sb, const DbusMethod *methp,
		 bool is_signal)
{
	DBusSignatureIter si;
	char *argsig;
	int argnum = 1;
	bool res;

	if (!sb.AppendFmt("    <%s name=\"%s\">\n",
			  is_signal ? "signal" : "method",
			  methp->meth_name))
		return false;

	/*
	 * Argument names are not part of our interface
	 * metadata, so we call them arg1..n.
	 */
	if (methp->meth_sig && methp->meth_sig[0]) {
		dbus_signature_iter_init(&si, methp->meth_sig);

		do {
			argsig = dbus_signature_iter_get_signature(&si);
			if (!argsig)
				return false;

			res = sb.AppendFmt("      <arg name=\"arg%d\" "
					   "type=\"%s\"%s/>\n",
					   argnum, argsig,
					   is_signal ? "" :
					   " direction=\"in\"");
			free(argsig);
			if (!res)
				return false;
			argnum++;
		} while (dbus_signature_iter_next(&si));
	}

	if (methp->ret_sig && methp->ret_sig[0]) {
		assert(!is_signal);
		dbus_signature_iter_init(&si, methp->ret_sig);

		do {
			argsig = dbus_signature_iter_get_signature(&si);
			if (!argsig)
				return false;

			res = sb.AppendFmt("      <arg name=\"arg%d\" "
					   "type=\"%s\" direction=\"out\"/>\n",
					   argnum, argsig);
			free(argsig);
			if (!res)
				return false;
			argnum++;
		} while (dbus_signature_iter_next(&si));
	}


	if (!sb.AppendFmt("    </%s>\n", is_signal ? "signal" : "method"))
		return false;

	return true;
}

bool DbusExportObject::
DbusIntrospect(DBusMessage *msgp)
{
	const DbusInterface *ifp;
	const DbusMethod *methp;
	const DbusProperty *propp;
	libhfp::StringBuffer sb;
	const char *bufptr;
	char **childnames = 0;
	int i;
	bool did_common = false;

	if (!sb.AppendFmt(DBUS_INTROSPECT_1_0_XML_DOCTYPE_DECL_NODE) ||
	    !sb.AppendFmt("<node name=\"%s\">\n", m_path))
		goto nomem;

	ifp = m_ifaces;
restart:
	while (ifp && ifp->if_name) {
		if (!sb.AppendFmt("  <interface name=\"%s\">\n",
				  ifp->if_name))
			goto nomem;

		methp = ifp->if_meths;
		while (methp && methp->meth_name) {
			if (!IntrospectMethod(sb, methp, false))
				goto nomem;
			methp++;
		}

		methp = ifp->if_sigs;
		while (methp && methp->meth_name) {
			if (!IntrospectMethod(sb, methp, true))
				goto nomem;
			methp++;
		}

		propp = ifp->if_props;
		while (propp && propp->prop_name) {
			/* Show properties that are readable or writable */
			if ((propp->prop_get || propp->prop_set) &&
			    !sb.AppendFmt("    <property name=\"%s\" "
					  "type=\"%s\" access=\"%s\"/>\n",
					  propp->prop_name, propp->prop_sig,
					  propp->prop_set ?
					  (propp->prop_get ? "readwrite"
					   : "write") : "read"))
				goto nomem;

			propp++;
		}

		if (!sb.AppendFmt("  </interface>\n"))
			goto nomem;
		ifp++;
	}

	if (!did_common) {
		did_common = true;
		ifp = s_ifaces_common;
		goto restart;
	}

	/* Find all the child nodes */
	if (!dbus_connection_list_registered(m_session->GetConn(),
					     m_path, &childnames))
		goto nomem;

	if (childnames) {
		for (i = 0; childnames[i]; i++) {
			if (!sb.AppendFmt("  <node name=\"%s\"/>\n",
					  childnames[i]))
				goto nomem;
		}

		dbus_free_string_array(childnames);
		childnames = 0;
	}

	if (!sb.AppendFmt("</node>\n"))
		goto nomem;

	bufptr = sb.Contents();
	SendReplyArgs(msgp,
		      DBUS_TYPE_STRING, &bufptr,
		      DBUS_TYPE_INVALID);
	return true;

nomem:
	if (childnames)
		dbus_free_string_array(childnames);
	return false;
}

const DbusProperty *DbusExportObject::
DbusPropertyCommon(DBusMessage *msgp, DBusMessageIter &mi, bool &retval)
{
	const DbusInterface *ifp;
	const DbusProperty *propp;
	const char *ifname, *propname;

	if (!dbus_message_iter_init(msgp, &mi) ||
	    (dbus_message_iter_get_arg_type(&mi) != DBUS_TYPE_STRING))
		goto badmessage;
	dbus_message_iter_get_basic(&mi, &ifname);
	if (!dbus_message_iter_next(&mi) ||
	    (dbus_message_iter_get_arg_type(&mi) != DBUS_TYPE_STRING))
		goto badmessage;
	dbus_message_iter_get_basic(&mi, &propname);

	if (ifname && !ifname[0])
		ifname = 0;

	if (ifname) {
		if (!(ifp = DbusFindInterface(m_ifaces, ifname)) &&
		    !(ifp = DbusFindInterface(s_ifaces_common, ifname))) {
			retval = SendReplyError(msgp,
						DBUS_ERROR_INVALID_ARGS,
						"Interface not supported");
			return 0;
		}

		propp = DbusFindProperty(ifp->if_props, propname);
	}
	else {
		propp = 0;
		ifp = m_ifaces;
		while (ifp->if_name &&
		       !(propp = DbusFindProperty(ifp->if_props, propname))) {
			ifp++;
		}

		if (!propp) {
			ifp = s_ifaces_common;
			while (ifp->if_name &&
			       !(propp = DbusFindProperty(ifp->if_props,
							  propname))) {
				ifp++;
			}
		}
	}

	if (!propp) {
		retval = SendReplyError(msgp,
					DBUS_ERROR_INVALID_ARGS,
					"Unknown Property");
		return 0;
	}

	retval = true;
	return propp;

badmessage:
	retval = SendReplyError(msgp,
				DBUS_ERROR_INVALID_ARGS,
				"Invalid Message Signature");
	return 0;
}

bool DbusExportObject::
DbusPropertyGet(DBusMessage *srcp)
{
	DBusMessage *msgp;
	const DbusProperty *propp;
	DBusMessageIter mi, rmi, vmi;
	bool retval;

	propp = DbusPropertyCommon(srcp, mi, retval);
	if (!propp)
		return retval;

	msgp = NewMethodReturn(srcp);
	if (!msgp)
		return false;

	dbus_message_iter_init_append(msgp, &rmi);
	if (!dbus_message_iter_open_container(&rmi,
					      DBUS_TYPE_VARIANT,
					      propp->prop_sig,
					      &vmi)) {
		dbus_message_unref(msgp);
		return false;
	}

	if (!propp->prop_get) {
		dbus_message_unref(msgp);
		return SendReplyError(srcp,
				      DBUS_ERROR_INVALID_ARGS,
				      "Property cannot be read");
	}

	if (!(propp->prop_get)(this, srcp, propp, vmi)) {
		dbus_message_unref(msgp);
		return false;
	}

	if (!dbus_message_iter_close_container(&rmi, &vmi) ||
	    !SendMessage(msgp)) {
		dbus_message_unref(msgp);
		return false;
	}
	return true;
}

bool DbusExportObject::
DbusPropertySet(DBusMessage *msgp)
{
	const DbusProperty *propp;
	char *sig = 0;
	DBusMessageIter mi, smi;
	bool retval;

	propp = DbusPropertyCommon(msgp, mi, retval);
	if (!propp)
		return retval;

	if (!propp->prop_set)
		return SendReplyError(msgp,
				      DBUS_ERROR_INVALID_ARGS,
				      "Property is immutable");

	if (!dbus_message_iter_next(&mi) ||
	    (dbus_message_iter_get_arg_type(&mi) != DBUS_TYPE_VARIANT))
		goto mismatch;
	dbus_message_iter_recurse(&mi, &smi);
	if (!(sig = dbus_message_iter_get_signature(&smi)))
		return false;
	retval = !strcmp(sig, propp->prop_sig);
	dbus_free(sig);
	if (!retval)
		goto mismatch;

	return (propp->prop_set)(this, msgp, propp, smi);

mismatch:
	return SendReplyError(msgp,
			      DBUS_ERROR_INVALID_ARGS,
			      "Property Type Mismatch");
}

bool DbusExportObject::
DbusPropertyGetAll(DBusMessage *srcp)
{
	const DbusInterface *ifp;
	const DbusProperty *propp;
	const char *ifname;
	DBusMessage *msgp;
	DBusMessageIter mi, ami, dmi, vmi;
	bool do_all = false, did_common = false;

	if (!dbus_message_iter_init(srcp, &mi) ||
	    (dbus_message_iter_get_arg_type(&mi) != DBUS_TYPE_STRING)) {
		return SendReplyError(srcp,
				      DBUS_ERROR_INVALID_ARGS,
				      "Invalid Message Signature");
	}

	dbus_message_iter_get_basic(&mi, &ifname);
	if (ifname && !ifname[0])
		ifname = 0;

	ifp = 0;
	if (ifname &&
	    !(ifp = DbusFindInterface(m_ifaces, ifname)) &&
	    !(ifp = DbusFindInterface(s_ifaces_common, ifname))) {
		return SendReplyError(srcp,
				      DBUS_ERROR_INVALID_ARGS,
				      "Interface not supported");
	}

	/* Prepare our reply message */
	msgp = NewMethodReturn(srcp);
	if (!msgp)
		return false;

	dbus_message_iter_init_append(msgp, &mi);
	if (!dbus_message_iter_open_container(&mi,
					      DBUS_TYPE_ARRAY,
			      DBUS_DICT_ENTRY_BEGIN_CHAR_AS_STRING
			      DBUS_TYPE_STRING_AS_STRING
			      DBUS_TYPE_VARIANT_AS_STRING
			      DBUS_DICT_ENTRY_END_CHAR_AS_STRING,
					      &ami))
		goto nomem;

	if (!ifp) {
		do_all = true;
		ifp = m_ifaces;
	}

restart:
	propp = 0;
	if (ifp->if_name)
		propp = ifp->if_props;
	while (propp && propp->prop_name) {

		if (!propp->prop_get) {
			propp++;
			continue;
		}

		if (!dbus_message_iter_open_container(&ami,
						      DBUS_TYPE_DICT_ENTRY,
						      0,
						      &dmi) ||
		    !dbus_message_iter_append_basic(&dmi,
						    DBUS_TYPE_STRING,
						    &propp->prop_name) ||
		    !dbus_message_iter_open_container(&dmi,
						      DBUS_TYPE_VARIANT,
						      propp->prop_sig,
						      &vmi))
			goto nomem;

		if (!(propp->prop_get)(this, srcp, propp, vmi))
			goto nomem;

		if (!dbus_message_iter_close_container(&dmi, &vmi) ||
		    !dbus_message_iter_close_container(&ami, &dmi))
			goto nomem;

		propp++;
	}

	if (do_all && ifp) {
		ifp++;
		if (!ifp->if_name && !did_common) {
			did_common = true;
			ifp = s_ifaces_common;
		}
		if (ifp && ifp->if_name)
			goto restart;
	}

	if (!dbus_message_iter_close_container(&mi, &ami) ||
	    !SendMessage(msgp))
		goto nomem;

	return true;

nomem:
	dbus_message_unref(msgp);
	return false;
}

void DbusExportObject::
UnregisterHelper(DBusConnection *conn, void *ptr)
{
	DbusExportObject *objp;
	objp = (DbusExportObject *) ptr;
	assert(objp->m_session);
	objp->m_session = 0;
}

bool DbusExportObject::
DbusRegister(DbusSession *sessp)
{
#if !defined(NDEBUG)
	/* Validate programmer-supplied metadata */
	const DbusInterface *ifp;
	const DbusMethod *methp;
	const DbusProperty *propp;
	bool did_local = false;

do_local:
	ifp = m_ifaces;
	if (ifp) do {
		assert(ifp->if_name);

		methp = ifp->if_meths;
		if (methp && methp->meth_name) do {
			assert(methp->meth_func);
			assert(!methp->meth_sig ||
			       !methp->meth_sig[0] ||
			       dbus_signature_validate(methp->meth_sig, 0));
			assert(!methp->ret_sig ||
			       !methp->ret_sig[0] ||
			       dbus_signature_validate(methp->ret_sig, 0));
		} while ((++methp)->meth_name);

		methp = ifp->if_sigs;
		if (methp && methp->meth_name) do {
			assert(!methp->meth_func);
			assert(!methp->meth_sig ||
			       !methp->meth_sig[0] ||
			       dbus_signature_validate(methp->meth_sig, 0));
			assert(!methp->ret_sig);
		} while ((++methp)->meth_name);

		propp = ifp->if_props;
		if (propp && propp->prop_name) do {
			assert(dbus_signature_validate(propp->prop_sig, 0));
		} while ((++propp)->prop_name);

	} while ((++ifp)->if_name);

	/* Don't skimp on the common interfaces */
	if (!did_local) {
		did_local = true;
		goto do_local;
	}
#endif /* !defined(NDEBUG) */

	assert(!m_session);
	if (!dbus_connection_register_object_path(sessp->GetConn(),
						 m_path,
						 &s_vtable,
						 this))
		return false;

	m_session = sessp;
	m_session->GetDi()->LogDebug("D-Bus: Exported \"%s\"", m_path);
	return true;
}

void DbusExportObject::
DbusUnregister(void)
{
	libhfp::DispatchInterface *dip;
	if (!m_session)
		return;

	dip = m_session->GetDi();
	if (!dbus_connection_unregister_object_path(m_session->GetConn(),
						    m_path)) {
		m_session->GetDi()->LogWarn("D-Bus: Unregistration of "
					    "path \"%s\" failed", m_path);
		return;
	}
	assert(!m_session);

	dip->LogDebug("D-Bus: Unexported \"%s\"", m_path);
}

DBusMessage *DbusExportObject::
NewMethodReturn(DBusMessage *srcp)
{
	return dbus_message_new_method_return(srcp);
}

bool DbusExportObject::
SendMessage(DBusMessage *msgp)
{
	assert(dbus_message_get_type(msgp) != DBUS_MESSAGE_TYPE_METHOD_CALL);
	if (!m_session)
		return false;
	return m_session->SendMessage(msgp);
}

bool DbusExportObject::
SendSignalArgs(const char *iface, const char *signame, int first_arg_type, ...)
{
	va_list ap;
	bool res;
	va_start(ap, first_arg_type);
	res = SendSignalArgsVa(iface, signame, first_arg_type, ap);
	va_end(ap);
	return res;
}

bool DbusExportObject::
SendSignalArgsVa(const char *iface, const char *signame,
		 int first_arg_type, va_list ap)
{
	DBusMessage *msgp;

	msgp = dbus_message_new_signal(m_path, iface, signame);
	if (!msgp)
		return false;

	if (!dbus_message_append_args_valist(msgp, first_arg_type, ap) ||
	    !SendMessage(msgp)) {
		dbus_message_unref(msgp);
		return false;
	}

	return true;
}

bool DbusExportObject::
SendReplyArgs(DBusMessage *srcp, int first_arg_type, ...)
{
	va_list ap;
	bool res;
	va_start(ap, first_arg_type);
	res = SendReplyArgsVa(srcp, first_arg_type, ap);
	va_end(ap);
	return res;
}

bool DbusExportObject::
SendReplyArgsVa(DBusMessage *srcp, int first_arg_type, va_list ap)
{
	DBusMessage *msgp;

	msgp = NewMethodReturn(srcp);
	if (!msgp)
		return false;

	if (!dbus_message_append_args_valist(msgp, first_arg_type, ap) ||
	    !SendMessage(msgp)) {
		dbus_message_unref(msgp);
		return false;
	}

	return true;
}

bool DbusExportObject::
SendReplyError(DBusMessage *src, const char *name, const char *msg, ...)
{
	va_list ap;
	libhfp::StringBuffer sb;
	DBusMessage *repl;
	bool res;

	va_start(ap, msg);
	res = sb.AppendFmtVa(msg, ap);
	va_end(ap);
	if (!res)
		return false;

	repl = dbus_message_new_error(src, name, sb.Contents());
	if (!repl)
		return false;
	if (!SendMessage(repl)) {
		dbus_message_unref(repl);
		return false;
	}
	return true;
}
