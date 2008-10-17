/* -*- C++ -*- */
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

#if !defined(__HFPD_DBUS_H__)
#define __HFPD_DBUS_H__

#include <stdarg.h>
#include <dbus/dbus.h>
#include <libhfp/events.h>
#include <libhfp/list.h>


class DbusSession;
class DbusExportObject;
class DbusInterface;


struct DbusMethod {
	typedef bool (*handler_t)(DbusExportObject *, DBusMessage *msgp);

	const char		*meth_name;
	handler_t		meth_func;
	const char		*meth_sig;
	const char		*ret_sig;
};

#define DbusMethodEntryName(NAME, CLASS, METHOD, SIG, RET) \
	{ NAME, DbusMethodRawMember<CLASS, &CLASS::METHOD>, SIG, RET }
#define DbusMethodEntry(CLASS, METHOD, SIG, RET) \
	DbusMethodEntryName(#METHOD, CLASS, METHOD, SIG, RET)

#define DbusSignalEntry(METHOD, SIG) \
	{ #METHOD, 0, SIG, 0 }

struct DbusProperty {
	typedef bool (*fn_t)(DbusExportObject *objp, DBusMessage *msgp,
			     const DbusProperty *prop, DBusMessageIter &iter);
	const char		*prop_name;
	const char		*prop_sig;
	fn_t			prop_get;
	fn_t			prop_set;
};


#define DbusPropertyRaw(SIG, NAME, CLASS, GET, SET) {			\
	#NAME, 								\
	SIG,								\
	DbusPropGetRawMember<CLASS, &CLASS::GET>,			\
	DbusPropSetRawMember<CLASS, &CLASS::SET>			\
}

#define DbusPropertyRawImmutable(SIG, NAME, CLASS, GET) {		\
	#NAME, 								\
	SIG,								\
	DbusPropGetRawMember<CLASS, &CLASS::GET>,			\
	0								\
}

#define DbusPropertyMarshall(TYPE, NAME, CLASS, GET, SET) {		\
	#NAME, 								\
	DbusTypeTraits<TYPE>::Signature(),				\
	DbusPropGetMarshallMember<DbusTypeTraits<TYPE>,			\
				CLASS, &CLASS::GET>,			\
	DbusPropSetMarshallMember<DbusTypeTraits<TYPE>,			\
				CLASS, &CLASS::SET>			\
}

#define DbusPropertyMarshallImmutable(TYPE, NAME, CLASS, GET) {		\
	#NAME, 								\
	DbusTypeTraits<TYPE>::Signature(),				\
	DbusPropGetMarshallMember<DbusTypeTraits<TYPE>,			\
				CLASS, &CLASS::GET>,			\
	0								\
}

struct DbusInterface {
	const char		*if_name;
	const DbusMethod	*if_meths;
	const DbusMethod	*if_sigs;
	const DbusProperty	*if_props;
};

class DbusExportObject {
	friend class DbusSession;
	DbusSession				*m_session;
	const char				*m_path;
	const DbusInterface 			*m_ifaces;

	static const DbusInterface		s_ifaces_common[];
	static const DbusMethod			s_methods_introspect[];
	static const DbusMethod			s_methods_properties[];
	static const DBusObjectPathVTable	s_vtable;

	static DBusHandlerResult DispatchHelper(DBusConnection *connection,
						DBusMessage *message,
						void *user_data);
	static void UnregisterHelper(DBusConnection *conn, void *ptr);

	bool DbusRegister(DbusSession *sessp);
	void DbusUnregister(void);

protected:
	/* The main message dispatch method */
	virtual DBusHandlerResult DbusDispatch(DBusMessage *msgp);

	/* Metadata search helpers */
	static const DbusInterface *DbusFindInterface(const DbusInterface *ifs,
						      const char *name);
	static const DbusMethod *DbusFindMethod(const DbusMethod *meths,
						const char *name);
	static const DbusProperty *DbusFindProperty(const DbusProperty *props,
						    const char *name);

	/* Common interface method implementations */
	/* org.freedesktop.DBus.Introspectable */
	virtual bool DbusIntrospect(DBusMessage *msgp);

	/* org.freedesktop.DBus.Properties */
	const DbusProperty *DbusPropertyCommon(DBusMessage *msgp,
					       DBusMessageIter &mi, bool &ret);
	virtual bool DbusPropertyGet(DBusMessage *msgp);
	virtual bool DbusPropertySet(DBusMessage *msgp);
	virtual bool DbusPropertyGetAll(DBusMessage *msgp);

public:
	DbusExportObject(const char *name, const DbusInterface *iface_tbl = 0)
		: m_session(0), m_path(name), m_ifaces(iface_tbl) {}
	virtual ~DbusExportObject();

	DbusSession *GetDbusSession(void) const { return m_session; }
	bool IsDbusExported(void) const { return GetDbusSession() != 0; }
	const char *GetDbusPath(void) const { return m_path; }

	bool SendSignalArgs(const char *iface, const char *name,
			    int first_arg_type, ...);
	bool SendSignalArgsVa(const char *iface, const char *signame,
			      int first_arg_type, va_list ap);
	bool SendReplyArgs(DBusMessage *src, int first_arg_type, ...);
	bool SendReplyArgsVa(DBusMessage *src, int first_arg_type, va_list ap);
	bool SendReplyError(DBusMessage *src, const char *name,
			    const char *msg, ...);
	DBusMessage *NewMethodReturn(DBusMessage *srcp);
	bool SendMessage(DBusMessage *out);
};


/*
 * The match notifier diverts D-Bus messages matching a given
 * expression to a callback.  The expression is given when the
 * object is created.
 */

class DbusMatchNotifier :
	public libhfp::Callback<void, class DbusMatchNotifier*, DBusMessage*> {
public:
	virtual bool SetEnabled(bool enable) = 0;
	virtual ~DbusMatchNotifier() {}
};


/*
 * The completion object notifies when an asynchronous remote method call
 * has completed.  The subject DBusMessage is provided if it is available,
 * and the working reference is released when the callback returns.
 */

class DbusCompletion
	: public libhfp::Callback<void, class DbusCompletion *, DBusMessage*> {
	friend class DbusSession;
	static void CompletionHelper(DBusPendingCall *pc, void *ud);
	DBusPendingCall 	*m_pend;
public:
	virtual ~DbusCompletion();
};


/*
 * The DbusPeer object supports the DbusPeerDisconnectNotifier object
 * below.
 */

class DbusPeerDisconnectNotifier;

class DbusPeer {
protected:
	DbusPeer(void) {}
	virtual ~DbusPeer() {};

public:
	virtual const char *GetName(void) const = 0;
	virtual DbusPeerDisconnectNotifier *NewDisconnectNotifier(void) = 0;
	virtual void Get(void) = 0;
	virtual void Put(void) = 0;
};

/*
 * The peer disconnect notifier is useful for cleaning up objects
 * referenced by specific D-Bus peers when those peers disconnect.
 */

class DbusPeerDisconnectNotifier
	: public libhfp::Callback<void, DbusPeerDisconnectNotifier *> {
	friend class DbusPeerImpl;

	libhfp::ListItem	m_links;
	DbusPeer		*m_peer;

public:
	DbusPeer *GetPeer(void) const { return m_peer; }
	virtual ~DbusPeerDisconnectNotifier();
};


/*
 * "connection" is the right word for this, but I don't want the
 * distinction between this object and DBusConnection to rest on
 * the captalization of a 'b'.
 */

class DbusSession {
	friend class DbusObjectLocal;
	friend class DbusMatchNotifierImpl;

	libhfp::DispatchInterface	*m_di;
	DBusConnection			*m_conn;
	libhfp::TimerNotifier		*m_dodispatch;
	DbusExportObject		*m_local;
	bool				m_owner;
	libhfp::ListItem		m_match_exprs;
	libhfp::ListItem		m_peers;

	void Dispatch(libhfp::TimerNotifier *notp);
	static void SetDispatchStatus(DBusConnection *conn,
				      DBusDispatchStatus new_stat,
				      void *ptr);
	bool SetupDispatchOwner(void);
	bool SetupEventsOwner(void);
	bool SetupEventsCommon(void);
	void __Disconnect(bool notify = false);

	static DBusHandlerResult FilterHelper(DBusConnection *connection,
					      DBusMessage *message,
					      void *user_data);

	bool AddMatchNotifier(class DbusMatchNotifierImpl *matchp);
	void RemoveMatchNotifier(class DbusMatchNotifierImpl *matchp);

public:
	DbusSession(libhfp::DispatchInterface *di);
	~DbusSession();

	libhfp::Callback<void, DbusSession *> cb_NotifyDisconnect;

	libhfp::DispatchInterface *GetDi(void) const { return m_di; }
	DBusConnection *GetConn(void) const { return m_conn; }

	bool Connect(DBusBusType bustype = DBUS_BUS_SESSION);
	void Disconnect(void) { __Disconnect(); }
	bool IsConnected(void) const { return m_conn != 0; }

	bool AddUniqueName(const char *name);
	bool RemoveUniqueName(const char *name);

	bool ExportObject(DbusExportObject *objp)
		{ return objp->DbusRegister(this); }
	void UnexportObject(DbusExportObject *objp)
		{ objp->DbusUnregister(); }

	DbusMatchNotifier *NewMatchNotifier(const char *expression);

	DbusPeer *GetPeer(const char *nm);
	DbusPeer *GetPeer(DBusMessage *from_sender) {
		return GetPeer(dbus_message_get_sender(from_sender));
	}

	bool SendMessage(DBusMessage *msgp);
	DbusCompletion *SendWithCompletion(DBusMessage *msgp);
};

#include <dbus-marshall.h>

#endif /* !defined(__HFPD_DBUS_H__) */
