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

#if !defined(__HFPD_DBUS_MARSHALL_H__)
#define __HFPD_DBUS_MARSHALL_H__

/*
 * Marshalling in this dbus binding is not automatic at all, but
 * can be configured through functions set in DbusMethod and
 * DbusProperty.
 *
 * At present, only property marshalling is supported.
 *
 * The sanctioned way of describing interfaces is by initializing arrays
 * of DbusMethod and DbusProperty structures, and assigning names,
 * D-Bus type signatures, and function pointers.  To help with adapting
 * the function pointer signature to more useful one, there are some
 * templatized methods defined below, some of which support argument
 * marshalling from D-Bus.
 */

/*
 * To support marshalling/demarshalling of user-defined types,
 * a DbusTypeTraits specialization must be defined for the type.
 *
 * The specialization must follow the form:
 * template <> struct DbusTypeTraits<UserType> {
 *	typedef UserType type_t;
 *	static const char *Sig(void);
 *	static bool Marshall(DBusMessageIter &iter, const T &arg);
 *	static bool Demarshall(DBusMessageIter &iter, T &arg);
 *	static void DemarshallDelete(T &arg);
 * }
 *
 * The Signature method must return a static-storage string of the
 * D-Bus type signature, e.g. "ss(ias)a{is}".  See the D-Bus
 * specification for more information on type signatures.
 *
 * The Marshall method must use its iter argument to append the
 * D-Bus message representation of the type to a DBusMessage.  It
 * returns true on success.  A return value of false is interpreted
 * as resource exhaustion, i.e. malloc() failure.
 *
 * The Demarshall method must use its iter argument to unpack or
 * instantiate the user-defined type from its D-Bus message
 * representation.  It returns true on success.  A return value of
 * false is interpreted as resource exhaustion, i.e. malloc() failure.
 *
 * The DemarshallDestroy method may optionally release memory allocated
 * by the Demarshall method.  This makes it possible to use simple
 * pointer types, i.e. Object*, to represent a demarshalled parameter
 * instead of defining a smart pointer container type with a destructor.
 */

struct DbusValue {
	union {
		void		*ptr;
		void		(DbusValue::*membfunc)(void);
#if defined(DBUS_HAVE_INT64)
		dbus_int64_t	i64;
#endif /* defined(DBUS_HAVE_INT64) */
	}			m_value;
	bool			m_simple;
};

/* DbusTypeTraitsBasic supports DBus basic types */
template <typename T, int DBT>
struct DbusTypeTraitsBasic {
	typedef T type_t;
	static const char *Signature(void)
		{ static const char sig[2] = { DBT, 0 }; return sig; }
	static bool Marshall(DBusMessageIter &iter, const T &arg)
		{ return dbus_message_iter_append_basic(&iter, DBT, &arg); }
	static bool Demarshall(DBusMessageIter &iter, T &arg) {
		assert(dbus_message_iter_get_arg_type(&iter) == DBT);
		dbus_message_iter_get_basic(&iter, &arg);
		return true;
	}
	static void DemarshallDestroy(T &arg) {}
};

/* DbusTypeTraits for most basic types, defined below */
template <typename T> struct DbusTypeTraits { };
template <> struct DbusTypeTraits<unsigned char>
	: public DbusTypeTraitsBasic<unsigned char, DBUS_TYPE_BYTE> {};
template <> struct DbusTypeTraits<dbus_int16_t>
	: public DbusTypeTraitsBasic<dbus_int16_t, DBUS_TYPE_INT16> {};
template <> struct DbusTypeTraits<dbus_uint16_t>
	: public DbusTypeTraitsBasic<dbus_uint16_t, DBUS_TYPE_UINT16> {};
template <> struct DbusTypeTraits<dbus_int32_t>
	: public DbusTypeTraitsBasic<dbus_int32_t, DBUS_TYPE_INT32> {};
template <> struct DbusTypeTraits<dbus_uint32_t>
	: public DbusTypeTraitsBasic<dbus_uint32_t, DBUS_TYPE_UINT32> {};
#if defined(DBUS_HAVE_INT64)
template <> struct DbusTypeTraits<dbus_int64_t>
	: public DbusTypeTraitsBasic<dbus_int64_t, DBUS_TYPE_INT64> {};
template <> struct DbusTypeTraits<dbus_uint64_t>
	: public DbusTypeTraitsBasic<dbus_uint64_t, DBUS_TYPE_UINT64> {};
#endif /* defined(DBUS_HAVE_INT64) */
/* Boolean help: bool works, except that dbus_bool_t is a 32-bit integer */
template <> struct DbusTypeTraits<bool>
	: public DbusTypeTraitsBasic<bool, DBUS_TYPE_BOOLEAN> {
	static bool Marshall(DBusMessageIter &iter, const bool &arg) {
		dbus_bool_t xarg = arg;
		return dbus_message_iter_append_basic(&iter, DBUS_TYPE_BOOLEAN,
						      &xarg);
	}
	static bool Demarshall(DBusMessageIter &iter, bool &arg) {
		dbus_bool_t xarg;
		assert(dbus_message_iter_get_arg_type(&iter) ==
		       DBUS_TYPE_BOOLEAN);
		dbus_message_iter_get_basic(&iter, &xarg);
		arg = xarg;
		return true;
	}
};
template <> struct DbusTypeTraits<const char *>
	: public DbusTypeTraitsBasic<const char *, DBUS_TYPE_STRING> {};
template <> struct DbusTypeTraits<double>
	: public DbusTypeTraitsBasic<double, DBUS_TYPE_DOUBLE> {};
template <> struct DbusTypeTraits<float>
	: public DbusTypeTraitsBasic<float, DBUS_TYPE_DOUBLE> {
	static bool Marshall(DBusMessageIter &iter, const float &arg) {
		double xarg = arg;
		return dbus_message_iter_append_basic(&iter, DBUS_TYPE_DOUBLE,
						      &xarg);
	}
	static bool Demarshall(DBusMessageIter &iter, float &arg) {
		double xarg;
		assert(dbus_message_iter_get_arg_type(&iter) ==
		       DBUS_TYPE_DOUBLE);
		dbus_message_iter_get_basic(&iter, &xarg);
		arg = xarg;
		return true;
	}
};

template <typename T>
const char *
DbusTypeSig(T &) {
	return DbusTypeTraits<T>::Signature();
}

/*
 * @brief Dispatch a D-Bus method call with a raw DBusMessage
 * argument to a member function
 */
template <typename T, bool (T::*Func)(DBusMessage *)>
bool
DbusMethodRawMember(DbusExportObject *objp, DBusMessage *msgp)
{
	return (((T*) objp)->*Func)(msgp);
}

/*
 * @brief Dispatch a D-Bus property get call with a raw
 * DBusMessage argument to a member function, and a DBusMessageIter
 * where the result should be stored.
 *
 * This interface does not expect the callee to send a reply, and supports
 * both the org.freedesktop.DBus.Properties.{Get,GetAll} interfaces.
 */
template <typename T, bool (T::*Func)(DBusMessage *, const DbusProperty *,
				      DBusMessageIter &)>
bool
DbusPropGetRawMember(DbusExportObject *objp, DBusMessage *msgp,
		     const DbusProperty *propp, DBusMessageIter &iter)
{
	return (((T*) objp)->*Func)(msgp, propp, iter);
}

template <typename T, bool (T::*Func)(DBusMessage *, const DbusProperty *,
				      DBusMessageIter &)>
bool
DbusPropSetRawMember(DbusExportObject *objp, DBusMessage *msgp,
		     const DbusProperty *propp, DBusMessageIter &iter)
{
	return (((T*) objp)->*Func)(msgp, propp, iter);
}

/*
 * @brief Dispatch a D-Bus property get call, along with a reference to
 * a value to be marshalled, to a member function.
 *
 * This interface does not expect the callee to send a reply, and supports
 * both the org.freedesktop.DBus.Properties.{Get,GetAll} interfaces.
 */
template <typename TraitsT, typename T,
	  bool (T::*Func)(DBusMessage *msgp, typename TraitsT::type_t &)>
bool
DbusPropGetMarshallMember(DbusExportObject *objp, DBusMessage *msgp,
			  const DbusProperty *propp, DBusMessageIter &mi)
{
	typename TraitsT::type_t value;

	if (!(((T*)objp)->*Func)(msgp, value))
		return false;

	return TraitsT::Marshall(mi, value);
}

template <typename TraitsT, typename T,
	  bool (T::*Func)(DBusMessage *msgp,
			  const typename TraitsT::type_t &, bool &accept)>
bool
DbusPropSetMarshallMember(DbusExportObject *objp, DBusMessage *msgp,
			  const DbusProperty *propp, DBusMessageIter &mi)
{
	typename TraitsT::type_t value;
	bool res, accept;

	if (!TraitsT::Demarshall(mi, value))
		return false;

	accept = true;
	res = (((T*)objp)->*Func)(msgp, value, accept);

	TraitsT::DemarshallDestroy(value);

	if (!res)
		return false;
	if (!accept)
		return true;

	if (!dbus_message_get_no_reply(msgp))
		return objp->SendReplyArgs(msgp, DBUS_TYPE_INVALID);

	return true;
}

#endif /* !defined(__HFPD_DBUS_MARSHALL_H__) */
