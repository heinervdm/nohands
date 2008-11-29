/* -*- C++ -*- */
/*
 * Software Bluetooth Hands-Free Implementation
 *
 * Copyright (C) 2006-2008 Sam Revitch <samr7@cs.washington.edu>
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

#if !defined(__LIBHFP_EVENTS_H__)
#define __LIBHFP_EVENTS_H__

#include <string.h>
#include <stdarg.h>
#include <stdint.h>

/*
 * Support class for callbacks and interfacing with an OS event loop
 */

/**
 * @file libhfp/events.h
 */

namespace libhfp {

/**
 * @defgroup events Callback and Event Handling Interfaces
 *
 * This group contains four general facilities:
 * - A templatized stored callback mechanism, Callback.
 * - An event loop interface, DispatchInterface, plus interfaces
 * for receiving socket and timeout notifications.
 * - A string formatting class, StringBuffer.  Provides a printf()
 * style interface for constructing strings, with dynamic allocation.
 * - An error reporting class, ErrorInfo.  This class is used to pass
 * integer code values and string description values of an error
 * condition through multiple layers of method calls.
 */

extern bool SetNonBlock(int fh, bool nonblock);


/**
 * @brief Dynamic-sized formatted string buffer
 * @ingroup events
 *
 * The string buffer is a dynamic printf container for collecting
 * formatted string values.  It is much more primitive than std::string,
 * and does not support any of the expected syntactic sugar, such as the
 * operator overloads.  However, it does support printf() style
 * formatting.
 */
class StringBuffer {
	char	*buf;
	size_t	nalloc;
	size_t	nused;
	bool notmybuf;

	bool Enlarge(void);

public:
	StringBuffer(void) : buf(0), nalloc(0), nused(0), notmybuf(false) {}

	/*
	 * @brief Initialize to a preallocated initial buffer
	 *
	 * @param initbuf Buffer to be used, until exhausted.
	 * @param size Size of buffer
	 */
	StringBuffer(char *initbuf, size_t size)
		: buf(initbuf), nalloc(size), nused(0), notmybuf(true) {}
	~StringBuffer() { Clear(); }

	/**
	 * @brief Initialize to a permanent string constant
	 */
	StringBuffer(const char *init)
		: buf((char *)init), nalloc(0), nused(strlen(init)),
		  notmybuf(true) {}

	/**
	 * @brief Clear the contents of the string buffer
	 *
	 * This method has the effect of setting the length of the
	 * string buffer contents to zero, and freeing any buffer
	 * memory allocated to the string buffer.
	 */
	void Clear(void);

	/**
	 * @brief Query the contents of the string buffer
	 *
	 * This method returns the current contents of the string
	 * buffer.  The pointer will remain valid until the contents are
	 * appended to by AppendFmt(), or cleared by Clear().
	 *
	 * The buffer returned should not be directly modified.
	 */
	char *Contents(void) const { return buf; }

	/**
	 * @brief Append a formatted string to the buffer, using
	 * varargs
	 *
	 * @sa AppendFmt()
	 */
	bool AppendFmtVa(const char *fmt, va_list ap);

	/**
	 * @brief Append a formatted string to the string buffer
	 *
	 * This function appends the result of a printf() style formatting
	 * operation to the end of the string buffer.
	 *
	 * @retval true String formatted and appended
	 * @retval false String could not be formatted, either due to
	 * an invalid format string, or because there was not enough
	 * memory to enlarge the buffer to contain the result of the
	 * formatting procedure.
	 */
	bool AppendFmt(const char *fmt, ...)
		__attribute__((format(printf, 2, 3))) {
		va_list ap;
		bool res;
		va_start(ap, fmt);
		res = AppendFmtVa(fmt, ap);
		va_end(ap);
		return res;
	}
};


/**
 * @brief Failure description class
 * @ingroup events
 */

class ErrorInfo {
	friend class ErrorInfoGlobal;

	struct Container {
		uint16_t	m_subsys;
		uint16_t	m_code;
		StringBuffer	m_desc;

		Container(uint16_t ss, uint16_t c)
			: m_subsys(ss), m_code(c) {}
		Container(uint16_t ss, uint16_t c, const char *desc)
			: m_subsys(ss), m_code(c), m_desc(desc) {}
	};

	Container	*m_error;

	static Container *CopyContainer(const Container *src);

public:
	void SetVa(uint16_t subsys, uint16_t code,
		   const char *fmt, va_list ap);

	/**
	 * @brief Set the error description info following a failure
	 *
	 * This method fills out an ErrorInfo structure.  The structure
	 * must be clear at the time this method is invoked.  All three
	 * elements of the failure description are set via this method.
	 *
	 * @param[in] subsys Subsystem ID code of the fault
	 * @param[in] code Failure code of the fault
	 * @param[in] fmt Format string for the descriptive string of the
	 * fault
	 *
	 * This method may internally allocate memory.  If it fails, it
	 * will set the fault code to LIBHFP_ERROR_EVENTS_NO_MEMORY.
	 */
	void Set(uint16_t subsys, uint16_t code, const char *fmt, ...)
		__attribute__((format(printf, 4, 5))) {
		va_list alist;
		va_start(alist, fmt);
		SetVa(subsys, code, fmt, alist);
		va_end(alist);
	}

	/**
	 * @brief Set the failure reason as due to lack of memory
	 */
	void SetNoMem(void);

	/**
	 * @brief Query whether a failure description has been set
	 *
	 * @return true A fault description has been set
	 * @return false A fault description has not been set and the
	 * structure is clear.
	 */
	bool IsSet(void) const {
		return (m_error != 0);
	}

	/**
	 * @brief Clear any existing failure description
	 */
	void Clear(void);

	/**
	 * @brief Query the subsystem ID of the failure
	 */
	uint16_t Subsys(void) const { return m_error ? m_error->m_subsys : 0; }

	/**
	 * @brief Query the code of the failure
	 */
	uint16_t Code(void) const { return m_error ? m_error->m_code : 0; }

	/**
	 * @brief Query the string description of the failure
	 */
	const char *Desc(void) const { return m_error->m_desc.Contents(); }

	/**
	 * @brief Test whether the subsystem and failure ID of the
	 * failure match a set of values
	 */
	bool Matches(uint16_t subsys, uint16_t code)
		{ return (Subsys() == subsys) && (Code() == code); }

	operator bool() { return (m_error != 0); }
	ErrorInfo &operator=(const ErrorInfo &rhs);

	ErrorInfo(const ErrorInfo &src) : m_error(0) { *this = src; }
	ErrorInfo() : m_error(0) {}
	~ErrorInfo() { Clear(); }
};

#define LIBHFP_ERROR_SUBSYS_EVENTS 1

/**
 * @brief Error values for subsystem LIBHFP_ERROR_SUBSYS_EVENTS
 * @ingroup events
 */
enum {
	LIBHFP_ERROR_EVENTS_INVALID = 0,
	/** Memory allocation failure */
	LIBHFP_ERROR_EVENTS_NO_MEMORY,
	/** Parameter failed validation */
	LIBHFP_ERROR_EVENTS_BAD_PARAMETER,
	/** Input/Output error */
	LIBHFP_ERROR_EVENTS_IO_ERROR,
};


/*
 * The callback system below supports variable numbers of arguments and
 * stored argument mapping using fixed stored argument storage.
 *
 * It's primitive compared to Boost or libsigc++, but it works, and
 * __does not allocate memory__ for a registration call.  Instead,
 * oversized stored arguments cause a compile time error.
 *
 * We use default template parameters and partial specialization
 * to support the argument count.  Techniques are borrowed.
 */

class CallTarget {
public:
	virtual ~CallTarget() {}
};

static const struct Nil {} _Nil = {};
static const struct _Arg1 {} Arg1 = {};
static const struct _Arg2 {} Arg2 = {};
static const struct _Arg3 {} Arg3 = {};
static const struct _Arg4 {} Arg4 = {};
static const struct _Arg5 {} Arg5 = {};
static const struct _Arg6 {} Arg6 = {};
static const struct _Arg7 {} Arg7 = {};
static const struct _Arg8 {} Arg8 = {};

template <bool Fits>
class CompileAssert {
private:
	static void check(void) {}
};
template <>
class CompileAssert<true> {
public:
	static void check(void) {}
};

/*
 * ArgSet is used to contain stored arguments, and to map
 * stored arguments and provided arguments into passed arguments.
 * We use the empty ArgN types as locals, and use the [] operator
 * in the supplied argument set object to decide whether to pass
 * through a real stored argument, or replace a fake ArgN stored
 * argument with a supplied argument.
 *
 * The Execute() static method does the invocation.  As part of
 * Register(), we instantiate the necessary template signature of
 * Execute() and attach it as the m_marshall member of CallbackAdapter.
 *
 * The core idea is very clever, kudos to the boost::bind folks!
 */

template <typename TA1 = Nil, typename TA2 = Nil, typename TA3 = Nil,
	  typename TA4 = Nil, typename TA5 = Nil, typename TA6 = Nil>
class ArgSet {
	typedef ArgSet<TA1, TA2, TA3, TA4, TA5, TA6> ThisType;
public:
	TA1 m_a1;
	TA2 m_a2;
	TA3 m_a3;
	TA4 m_a4;
	TA5 m_a5;
	TA6 m_a6;

	ArgSet(TA1 a1 = _Nil, TA2 a2 = _Nil, TA3 a3 = _Nil,
	       TA4 a4 = _Nil, TA5 a5 = _Nil, TA6 a6 = _Nil)
		: m_a1(a1), m_a2(a2), m_a3(a3), m_a4(a4), m_a5(a5),
		  m_a6(a6) {}

	template <typename T> T &operator[](T &v) const
		{ return v; }
	template <typename T> T const &operator[](T const &v) const
		{ return v; }

	TA1 operator[](_Arg1) const { return m_a1; }
	TA2 operator[](_Arg2) const { return m_a2; }
	TA3 operator[](_Arg3) const { return m_a3; }
	TA4 operator[](_Arg4) const { return m_a4; }
	TA5 operator[](_Arg5) const { return m_a5; }
	TA6 operator[](_Arg6) const { return m_a6; }

	template <typename StoreT>
	void *operator new(size_t size, StoreT &place) {
		CompileAssert<sizeof(place) >= sizeof(ThisType)>::check();
		return &place;
	}

	template <typename TRet, typename TargT, typename ArgsetT>
	static TRet Execute(ArgSet<Nil, Nil, Nil, Nil, Nil, Nil> &save,
			    TargT *targp,
			    TRet (TargT::*mfp)(void),
			    ArgsetT &xargs) {
		return (targp->*mfp)();
	}
	template <typename TRet, typename TargT, typename ArgsetT,
		  typename TSA1>
	static TRet Execute(ArgSet<TA1, Nil, Nil, Nil, Nil, Nil> &save,
			    TargT *targp,
			    TRet (TargT::*mfp)(TSA1),
			    ArgsetT &xargs) {
		return (targp->*mfp)(xargs[save.m_a1]);
	}
	template <typename TRet, typename TargT, typename ArgsetT,
		  typename TSA1, typename TSA2>
	static TRet Execute(ArgSet<TA1, TA2, Nil, Nil, Nil, Nil> &save,
			    TargT *targp,
			    TRet (TargT::*mfp)(TSA1, TSA2),
			    ArgsetT &xargs) {
		return (targp->*mfp)(xargs[save.m_a1], xargs[save.m_a2]);
	}
	template <typename TRet, typename TargT, typename ArgsetT,
		  typename TSA1, typename TSA2, typename TSA3>
	static TRet Execute(ArgSet<TA1, TA2, TA3, Nil, Nil, Nil> &save,
			    TargT *targp,
			    TRet (TargT::*mfp)(TSA1, TSA2, TSA3),
			    ArgsetT &xargs) {
		return (targp->*mfp)(xargs[save.m_a1], xargs[save.m_a2],
				     xargs[save.m_a3]);
	}
	template <typename TRet, typename TargT, typename ArgsetT,
		  typename TSA1, typename TSA2, typename TSA3,
		  typename TSA4>
	static TRet Execute(ArgSet<TA1, TA2, TA3, TA4, Nil, Nil> &save,
			    TargT *targp,
			    TRet (TargT::*mfp)(TSA1, TSA2, TSA3, TSA4),
			    ArgsetT &xargs) {
		return (targp->*mfp)(xargs[save.m_a1], xargs[save.m_a2],
				     xargs[save.m_a3], xargs[save.m_a4]);
	}
	template <typename TRet, typename TargT, typename ArgsetT,
		  typename TSA1, typename TSA2, typename TSA3,
		  typename TSA4, typename TSA5>
	static TRet Execute(ArgSet<TA1, TA2, TA3, TA4, TA5, Nil> &save,
			    TargT *targp,
			    TRet (TargT::*mfp)(TSA1, TSA2, TSA3, TSA4, TSA5),
			    ArgsetT &xargs) {
		return (targp->*mfp)(xargs[save.m_a1], xargs[save.m_a2],
				     xargs[save.m_a3], xargs[save.m_a4],
				     xargs[save.m_a5]);
	}
	template <typename TRet, typename TargT, typename ArgsetT,
		  typename TSA1, typename TSA2, typename TSA3,
		  typename TSA4, typename TSA5, typename TSA6>
	static TRet Execute(ArgSet<TA1, TA2, TA3, TA4, TA5, TA6> &save,
			    TargT *targp,
			    TRet (TargT::*mfp)(TSA1, TSA2, TSA3, TSA4, TSA5,
					       TSA6),
			    ArgsetT &xargs) {
		return (targp->*mfp)(xargs[save.m_a1], xargs[save.m_a2],
				     xargs[save.m_a3], xargs[save.m_a4],
				     xargs[save.m_a5], xargs[save.m_a6]);
	}
};

/**
 * @brief Base class for Callback, handles parameter adaptation
 */
template <typename TRet, typename InArgset>
class CallbackAdapter {
protected:
	typedef void (CallTarget::*fake_mfp_t)(void);
	typedef TRet (*marshall_t)(InArgset&, CallTarget *, fake_mfp_t,
				   InArgset&);
	typedef CallbackAdapter<TRet, InArgset> ThisT;

	marshall_t     	m_marshall;
	CallTarget	*m_targ;
	fake_mfp_t	m_method;
	uint8_t		m_save[4 * sizeof(void*)];

	TRet InvokeRet(InArgset &ia) {
		return (*m_marshall)(*(InArgset*)m_save, m_targ, m_method, ia);
	}

	void Invoke(InArgset &ia) {
		(*m_marshall)(*(InArgset*)m_save, m_targ, m_method, ia);
	}

	template <typename TargT, typename MethpT>
	void RegisterDirect(TargT *targp, MethpT method) {
		m_marshall = 0;
		m_targ = reinterpret_cast<CallTarget*>(targp);
		m_method = reinterpret_cast<fake_mfp_t>(method);
	}

public:
	CallbackAdapter(void) : m_marshall(0), m_targ(0) {}

	void Copy(ThisT const &src) {
		m_marshall = src.m_marshall;
		m_targ = src.m_targ;
		m_method = src.m_method;
		memcpy(m_save, src.m_save, sizeof(m_save));
	}

	/**
	 * @brief Test whether an object and method are registered to be
	 * called.
	 */
	bool Registered(void) const { return m_targ != 0; }

	/**
	 * @brief Clear the currently registered object and method.
	 */
	void Unregister(void) { m_targ = 0; }

	/**
	 * @brief Register a method with a nonconforming signature to
	 * the callback object
	 *
	 * In the spirit of boost::bind, this module provides a generic
	 * argument remapping mechanism for target methods.  This makes it
	 * possible to bind methods to callbacks where the method's
	 * signature does not match that of the callback, as long as a
	 * useful argument mapping can be created.  This mechanism allows
	 * values to be stored at the time the method is bound to the
	 * callback, and passed to the target function each time the
	 * callback is invoked.
	 *
	 * @note To register methods with matching signatures and no
	 * desire to remap arguments, use Callback::Register() instead.
	 * It results in lower overhead callback invocations.
	 *
	 * @param objp Target object to receive the callback
	 * @param mfp Target method to receive the callback.  The
	 * signature of the method need not conform to the signature
	 * of the callback, but it must have the same return type.
	 *
	 * @em Parameter @em 3 and on are arguments to pass to @c objp->mfp,
	 * and must match the signature of @c mfp.  Arguments will be saved in
	 * the callback object and passed to the method when the callback is
	 * invoked.  Special keywords @c Arg1, @c Arg2, ... @c Arg6 can be
	 * used to identify parameters passed to the callback by its
	 * caller, as potential arguments to the nonconforming method.
	 *
	 * As an untypical example, suppose we want to log messages to
	 * stdout, and prefix them with either "error" or "warning"
	 * depending on which of two objects the message is submitted to.
	 * @code
	 * class Target {
	 *	void LogValue(const char *source, const char *value) {
	 *		printf("%s: %d\n", source, value);
	 *	}
	 * };
	 *
	 * Target tgt;
	 * Callback<void, const char *> error;
	 * Callback<void, const char *> warning;
	 *
	 * error.Bind(&tgt, &Target::LogValue, "Error", Arg1);
	 * warning.Bind(&tgt, &Target::LogValue, "Error", Arg1);
	 *
	 * error("This is an error\n");
	 * warning("This is a warning\n");
	 * @endcode
	 *
	 * The output would be:
	 * @code
	 * Error: This is an error
	 * Warning: This is a warning
	 * @endcode
	 *
	 * This method has one specific advantage over boost::bind.
	 * @b It @b does @b not @b allocate @b memory @b under @b any
	 * @b circumstances.  It will not unpredictably fail or throw an
	 * exception at runtime.  On the down side, it is quite primitive
	 * compared to boost::bind.  It does not support pointers to
	 * nonmember functions.  Also, its stored parameters reside
	 * inside the Callback object itself.  The amount of space
	 * reserved for stored parameters is larger than that set by
	 * boost::function, and Callback objects take up more space.
	 * Additionally, if a binding request is made that would exceed
	 * the space reserved for stored parameters, a compile time error
	 * will be generated.
	 */
	template <typename TargT>
	void Bind(TargT *objp, TRet (TargT::*mfp)(void)) {
		typedef TRet (TargT::*omfp_t)(void);
		typedef ArgSet<> OutArgset;
		m_targ = reinterpret_cast<CallTarget*>(objp);
		m_method = reinterpret_cast<fake_mfp_t>(mfp);
		(void) new (m_save) OutArgset();
		TRet (*execp)(OutArgset&, TargT*, omfp_t, InArgset&)
			= &OutArgset::Execute;
		m_marshall = reinterpret_cast<marshall_t>(execp);
	}
	template <typename TargT, typename TSA1, typename TXA1>
	void Bind(TargT *objp, TRet (TargT::*mfp)(TSA1), TXA1 xa1) {
		typedef TRet (TargT::*omfp_t)(TSA1);
		typedef ArgSet<TXA1> OutArgset;
		m_targ = reinterpret_cast<CallTarget*>(objp);
		m_method = reinterpret_cast<fake_mfp_t>(mfp);
		(void) new (m_save) OutArgset(xa1);
		TRet (*execp)(OutArgset&, TargT*, omfp_t, InArgset&)
			= &OutArgset::Execute;
		m_marshall = reinterpret_cast<marshall_t>(execp);
	}
	template <typename TargT, typename TSA1, typename TXA1,
		  typename TSA2, typename TXA2>
	void Bind(TargT *objp, TRet (TargT::*mfp)(TSA1, TSA2),
		  TXA1 xa1, TXA2 xa2) {
		typedef TRet (TargT::*omfp_t)(TSA1, TSA2);
		typedef ArgSet<TXA1, TXA2> OutArgset;
		m_targ = reinterpret_cast<CallTarget*>(objp);
		m_method = reinterpret_cast<fake_mfp_t>(mfp);
		(void) new (m_save) OutArgset(xa1, xa2);
		TRet (*execp)(OutArgset&, TargT*, omfp_t, InArgset&)
			= &OutArgset::Execute;
		m_marshall = reinterpret_cast<marshall_t>(execp);
	}
	template <typename TargT, typename TSA1, typename TXA1,
		  typename TSA2, typename TXA2,
		  typename TSA3, typename TXA3>
	void Bind(TargT *objp, TRet (TargT::*mfp)(TSA1, TSA2, TSA3),
		  TXA1 xa1, TXA2 xa2, TXA3 xa3) {
		typedef TRet (TargT::*omfp_t)(TSA1, TSA2, TSA3);
		typedef ArgSet<TXA1, TXA2, TXA3> OutArgset;
		m_targ = reinterpret_cast<CallTarget*>(objp);
		m_method = reinterpret_cast<fake_mfp_t>(mfp);
		(void) new (m_save) OutArgset(xa1, xa2, xa3);
		TRet (*execp)(OutArgset&, TargT*, omfp_t, InArgset&)
			= &OutArgset::Execute;
		m_marshall = reinterpret_cast<marshall_t>(execp);
	}
	template <typename TargT, typename TSA1, typename TXA1,
		  typename TSA2, typename TXA2,
		  typename TSA3, typename TXA3,
		  typename TSA4, typename TXA4>
	void Bind(TargT *objp, TRet (TargT::*mfp)(TSA1, TSA2, TSA3, TSA4),
		  TXA1 xa1, TXA2 xa2, TXA3 xa3, TXA4 xa4) {
		typedef TRet (TargT::*omfp_t)(TSA1, TSA2, TSA3, TSA4);
		typedef ArgSet<TXA1, TXA2, TXA3, TXA4> OutArgset;
		m_targ = reinterpret_cast<CallTarget*>(objp);
		m_method = reinterpret_cast<fake_mfp_t>(mfp);
		(void) new (m_save) OutArgset(xa1, xa2, xa3, xa4);
		TRet (*execp)(OutArgset&, TargT*, omfp_t, InArgset&)
			= &OutArgset::Execute;
		m_marshall = reinterpret_cast<marshall_t>(execp);
	}
	template <typename TargT, typename TSA1, typename TXA1,
		  typename TSA2, typename TXA2,
		  typename TSA3, typename TXA3,
		  typename TSA4, typename TXA4,
		  typename TSA5, typename TXA5>
	void Bind(TargT *objp, TRet (TargT::*mfp)(TSA1, TSA2, TSA3, TSA4,
						  TSA5),
		  TXA1 xa1, TXA2 xa2, TXA3 xa3, TXA4 xa4, TXA5 xa5) {
		typedef TRet (TargT::*omfp_t)(TSA1, TSA2, TSA3, TSA4, TSA5);
		typedef ArgSet<TXA1, TXA2, TXA3, TXA4, TXA5> OutArgset;
		m_targ = reinterpret_cast<CallTarget*>(objp);
		m_method = reinterpret_cast<fake_mfp_t>(mfp);
		(void) new (m_save) OutArgset(xa1, xa2, xa3, xa4, xa5);
		TRet (*execp)(OutArgset&, TargT*, omfp_t, InArgset&)
			= &OutArgset::Execute;
		m_marshall = reinterpret_cast<marshall_t>(execp);
	}
	template <typename TargT, typename TSA1, typename TXA1,
		  typename TSA2, typename TXA2,
		  typename TSA3, typename TXA3,
		  typename TSA4, typename TXA4,
		  typename TSA5, typename TXA5,
		  typename TSA6, typename TXA6>
	void Bind(TargT *objp, TRet (TargT::*mfp)(TSA1, TSA2, TSA3, TSA4,
						  TSA5, TSA6),
		      TXA1 xa1, TXA2 xa2, TXA3 xa3, TXA4 xa4, TXA5 xa5,
		      TXA6 xa6) {
		typedef TRet (TargT::*omfp_t)(TSA1, TSA2, TSA3, TSA4, TSA5,
					      TSA6);
		typedef ArgSet<TXA1, TXA2, TXA3, TXA4, TXA5, TXA6> OutArgset;
		m_targ = reinterpret_cast<CallTarget*>(objp);
		m_method = reinterpret_cast<fake_mfp_t>(mfp);
		(void) new (m_save) OutArgset(xa1, xa2, xa3, xa4, xa5, xa6);
		TRet (*execp)(OutArgset&, TargT*, omfp_t, InArgset&)
			= &OutArgset::Execute;
		m_marshall = reinterpret_cast<marshall_t>(execp);
	}
};

/**
 * @brief Stored method callback class with templatized parameters
 * and parameter remapping
 * @ingroup events
 *
 * This class allows a callback to be defined to match any desired
 * function signature up to six arguments, and allows any arbitrary
 * member function up to six arguments to be registered.  Stored
 * parameters and provided parameter remapping is supported.
 *
 * The expected arguments are configured by template parameters:
 * @param TRet Return type of the callback method
 * @param TA1 Type of the first argument, or @c Nil if no arguments
 * @param TA2 Type of the second argument, or @c Nil if less than
 * two arguments
 * @param TA3 Type of the third argument, or @c Nil if less than
 * three arguments
 *
 * To invoke the callback, use the parenthesis operator () on the
 * callback object and pass in the arguments.
 *
 * The object to be called and method may be set using the Register()
 * or Bind() methods.  The callback may be cleared with
 * the Unregister() function.  The presence of a callback may be tested
 * with the Registered() function.
 *
 * In most situations, Callback is used to define an abstract interface.
 * The provider will define the template parameters and invoke the
 * callback.  The client will define a method to be called, register
 * the method, and receive the call.
 *
 * An untypical usage:
 * @code
 * class Target {
 *	void Alert(int priority, const char *text) {
 *		printf("PRIORITY %d ALERT: %s\n", priority, text);
 *	}
 *	void GroceryList(int priority, const char *text) {
 *		printf("Groceries: %s (Priority %d)\n", text, priority);
 *	}
 * };
 *
 * Callback<void, int, const char *> format_func;
 * Target t;
 *
 * format_func.Register(t, &t::Alert);
 * format_func(1, "Russian Nukes Launched");
 * format_func.Register(t, &t::GroceryList);
 * format_func(1, "Milk and Eggs");
 * @endcode
 *
 * The output of this example would be:
 * @code
 * PRIORITY 1 ALERT: Russian Nukes Launched
 * Groceries: Milk and Eggs (Priority 1)
 * @endcode
 */
template <typename TRet, typename TA1 = Nil, typename TA2 = Nil,
	  typename TA3 = Nil, typename TA4 = Nil, typename TA5 = Nil,
	  typename TA6 = Nil>
class Callback : public CallbackAdapter<TRet,
				 ArgSet<TA1, TA2, TA3, TA4, TA5, TA6> > {
	typedef ArgSet<TA1, TA2, TA3, TA4, TA5, TA6> InArgset;
	typedef CallbackAdapter<TRet, InArgset> BaseT;
	typedef TRet (CallTarget::*mfp_t)(TA1, TA2, TA3, TA4, TA5, TA6);
public:
	/**
	 * @brief Register a simple object and method to be called.
	 *
	 * @param targp The object to receive the method call
	 * @param mfp Pointer to the method of the object to be invoked
	 *
	 * If the method to receive the callback invocation matches the
	 * signature of the callback, as specified by template parameters,
	 * this method may be used to register the target method.
	 *
	 * For example, if the callback has signature:
	 * @code
	 * Callback<void, int, int, bool, const char *>
	 * @endcode
	 *
	 * The method to be registered must have signature:
	 * @code
	 * void Method(int, int, bool, const char *).
	 * @endcode
	 *
	 * Nonconforming methods may also be registered as the target
	 * using Bind().  However, Register() results in lower overhead
	 * callback invocations, and its use is advised when possible.
	 */
	template <typename TargT>
	void Register(TargT *targp, TRet (TargT::*mfp)(TA1, TA2, TA3,
						       TA4, TA5, TA6)) {
		RegisterDirect(targp, mfp);
	}

	/**
	 * @brief Copy the state of an identical callback object
	 */
	void Register(BaseT const &src) { Copy(src); }

	/**
	 * @brief Invoke the callback method.
	 * @note If no method is registered, this will cause a
	 * segfault/access violation.
	 * @return The return value from the called method
	 */
	TRet operator()(TA1 a1, TA2 a2, TA3 a3, TA4 a4, TA5 a5, TA6 a6) {
		if (BaseT::m_marshall) {
			InArgset ia(a1, a2, a3, a4, a5, a6);
			return InvokeRet(ia);
		}
		return ((BaseT::m_targ)->*(mfp_t)BaseT::m_method)
			(a1, a2, a3, a4, a5, a6);
	}
};

template <>
class Callback<void> : public CallbackAdapter<void, ArgSet<> > {
	typedef ArgSet<> InArgset;
	typedef CallbackAdapter<void, InArgset> BaseT;
	typedef void (CallTarget::*mfp_t)();
public:
	template <typename TargT>
	void Register(TargT *targp, void (TargT::*mfp)(void)) {
		RegisterDirect(targp, mfp);
	}
	void Register(BaseT const &src) { Copy(src); }
	void operator()(void) {
		if (BaseT::m_marshall) {
			InArgset ia;
			Invoke(ia);
		} else {
			((BaseT::m_targ)->*(mfp_t)BaseT::m_method)();
		}
	}
};

template <typename TRet>
class Callback<TRet> : public CallbackAdapter<TRet, ArgSet<> > {
	typedef ArgSet<> InArgset;
	typedef CallbackAdapter<TRet, InArgset> BaseT;
	typedef TRet (CallTarget::*mfp_t)();
public:
	template <typename TargT>
	void Register(TargT *targp, TRet (TargT::*mfp)(void)) {
		RegisterDirect(targp, mfp);
	}
	void Register(BaseT const &src) { Copy(src); }
	TRet operator()(void) {
		if (BaseT::m_marshall) {
			InArgset ia;
			return BaseT::InvokeRet(ia);
		}
		return ((BaseT::m_targ)->*(mfp_t)BaseT::m_method)();
	}
};


template <typename TA1>
class Callback<void, TA1> : public CallbackAdapter<void, ArgSet<TA1> > {
	typedef ArgSet<TA1> InArgset;
	typedef CallbackAdapter<void, InArgset> BaseT;
	typedef void (CallTarget::*mfp_t)(TA1);
public:
	template <typename TargT>
	void Register(TargT *targp, void (TargT::*mfp)(TA1)) {
		RegisterDirect(targp, mfp);
	}
	void Register(BaseT const &src) { Copy(src); }
	void operator()(TA1 a1) {
		if (BaseT::m_marshall) {
			InArgset ia(a1);
			Invoke(ia);
		} else {
			((BaseT::m_targ)->*(mfp_t)BaseT::m_method)(a1);
		}
	}
};

template <typename TRet, typename TA1>
class Callback<TRet, TA1> : public CallbackAdapter<TRet, ArgSet<TA1> > {
	typedef ArgSet<TA1> InArgset;
	typedef CallbackAdapter<TRet, InArgset> BaseT;
	typedef TRet (CallTarget::*mfp_t)(TA1);
public:
	template <typename TargT>
	void Register(TargT *targp, TRet (TargT::*mfp)(TA1)) {
		RegisterDirect(targp, mfp);
	}
	void Register(BaseT const &src) { Copy(src); }
	TRet operator()(TA1 a1) {
		if (BaseT::m_marshall) {
			InArgset ia(a1);
			return BaseT::InvokeRet(ia);
		}
		return ((BaseT::m_targ)->*(mfp_t)BaseT::m_method)(a1);
	}
};


template <typename TA1, typename TA2>
class Callback<void, TA1, TA2> :
		public CallbackAdapter<void, ArgSet<TA1, TA2> > {
	typedef ArgSet<TA1, TA2> InArgset;
	typedef CallbackAdapter<void, InArgset> BaseT;
	typedef void (CallTarget::*mfp_t)(TA1, TA2);
public:
	template <typename TargT>
	void Register(TargT *targp, void (TargT::*mfp)(TA1, TA2)) {
		RegisterDirect(targp, mfp);
	}
	void Register(BaseT const &src) { Copy(src); }
	void operator()(TA1 a1, TA2 a2) {
		if (BaseT::m_marshall) {
			InArgset ia(a1, a2);
			Invoke(ia);
		} else {
			((BaseT::m_targ)->*(mfp_t)BaseT::m_method)(a1, a2);
		}
	}
};

template <typename TRet, typename TA1, typename TA2>
class Callback<TRet, TA1, TA2> :
		public CallbackAdapter<TRet, ArgSet<TA1, TA2> > {
	typedef ArgSet<TA1, TA2> InArgset;
	typedef CallbackAdapter<TRet, InArgset> BaseT;
	typedef TRet (CallTarget::*mfp_t)(TA1, TA2);
public:
	template <typename TargT>
	void Register(TargT *targp, TRet (TargT::*mfp)(TA1, TA2)) {
		RegisterDirect(targp, mfp);
	}
	void Register(BaseT const &src) { Copy(src); }
	TRet operator()(TA1 a1, TA2 a2) {
		if (BaseT::m_marshall) {
			InArgset ia(a1, a2);
			return BaseT::InvokeRet(ia);
		}
		return ((BaseT::m_targ)->*(mfp_t)BaseT::m_method)(a1, a2);
	}
};


template <typename TA1, typename TA2, typename TA3>
class Callback<void, TA1, TA2, TA3> :
		public CallbackAdapter<void, ArgSet<TA1, TA2, TA3> > {
	typedef ArgSet<TA1, TA2, TA3> InArgset;
	typedef CallbackAdapter<void, InArgset> BaseT;
	typedef void (CallTarget::*mfp_t)(TA1, TA2, TA3);
public:
	template <typename TargT>
	void Register(TargT *targp, void (TargT::*mfp)(TA1, TA2, TA3)) {
		RegisterDirect(targp, mfp);
	}
	void Register(BaseT const &src) { Copy(src); }
	void operator()(TA1 a1, TA2 a2, TA3 a3) {
		if (BaseT::m_marshall) {
			InArgset ia(a1, a2, a3);
			Invoke(ia);
		} else {
			((BaseT::m_targ)->*(mfp_t)BaseT::m_method)(a1, a2, a3);
		}
	}
};

template <typename TRet, typename TA1, typename TA2, typename TA3>
class Callback<TRet, TA1, TA2, TA3> :
		public CallbackAdapter<TRet, ArgSet<TA1, TA2, TA3> > {
	typedef ArgSet<TA1, TA2, TA3> InArgset;
	typedef CallbackAdapter<TRet, InArgset> BaseT;
	typedef TRet (CallTarget::*mfp_t)(TA1, TA2, TA3);
public:
	template <typename TargT>
	void Register(TargT *targp, TRet (TargT::*mfp)(TA1, TA2, TA3)) {
		RegisterDirect(targp, mfp);
	}
	void Register(BaseT const &src) { Copy(src); }
	TRet operator()(TA1 a1, TA2 a2, TA3 a3) {
		if (BaseT::m_marshall) {
			InArgset ia(a1, a2, a3);
			return BaseT::InvokeRet(ia);
		}
		return ((BaseT::m_targ)->*(mfp_t)BaseT::m_method)(a1, a2, a3);
	}
};


template <typename TA1, typename TA2, typename TA3, typename TA4>
class Callback<void, TA1, TA2, TA3, TA4> :
		public CallbackAdapter<void, ArgSet<TA1, TA2, TA3, TA4> > {
	typedef ArgSet<TA1, TA2, TA3, TA4> InArgset;
	typedef CallbackAdapter<void, InArgset> BaseT;
	typedef void (CallTarget::*mfp_t)(TA1, TA2, TA3, TA4);
public:
	template <typename TargT>
	void Register(TargT *targp, void (TargT::*mfp)(TA1, TA2, TA3, TA4)) {
		RegisterDirect(targp, mfp);
	}
	void Register(BaseT const &src) { Copy(src); }
	void operator()(TA1 a1, TA2 a2, TA3 a3, TA4 a4) {
		if (BaseT::m_marshall) {
			InArgset ia(a1, a2, a3, a4);
			Invoke(ia);
		} else {
			((BaseT::m_targ)->*(mfp_t)BaseT::m_method)(
				a1, a2, a3, a4);
		}
	}
};

template <typename TRet, typename TA1, typename TA2, typename TA3,
	  typename TA4>
class Callback<TRet, TA1, TA2, TA3, TA4> :
		public CallbackAdapter<TRet, ArgSet<TA1, TA2, TA3, TA4> > {
	typedef ArgSet<TA1, TA2, TA3, TA4> InArgset;
	typedef CallbackAdapter<TRet, InArgset> BaseT;
	typedef TRet (CallTarget::*mfp_t)(TA1, TA2, TA3, TA4);
public:
	template <typename TargT>
	void Register(TargT *targp, TRet (TargT::*mfp)(TA1, TA2, TA3, TA4)) {
		RegisterDirect(targp, mfp);
	}
	void Register(BaseT const &src) { Copy(src); }
	TRet operator()(TA1 a1, TA2 a2, TA3 a3, TA4 a4) {
		if (BaseT::m_marshall) {
			InArgset ia(a1, a2, a3, a4);
			return BaseT::InvokeRet(ia);
		}
		return ((BaseT::m_targ)->*(mfp_t)BaseT::m_method)(
			a1, a2, a3, a4);
	}
};


/*
 * SocketNotifier and TimerNotifier are interfaces to an abstract
 * event loop.  We allow the implementations to be hidden and with
 * arbitrarily many additional members.
 *
 * DispatchInterface is a simple factory with POSIX semantics for
 * SocketNotifier and TimerNotifier.
 *
 * The theory is that this is all that is required to interface
 * the guts of this package with any event loop (Qt, GTK, ...)
 */

/**
 * @brief Environment-Independent Socket Notification Interface
 * @ingroup events
 *
 * SocketNotifier provides an abstract mechanism to receive socket
 * activity notifications independent of any specific main event loop.
 * SocketNotifier is meant to be as simple as possible and is nothing more
 * than a specialized derivative of Callback.
 *
 * The first parameter passed to the callback points to the SocketNotifier
 * object initiating the event.  The second parameter identifies the file
 * handle on which the event occurred.
 *
 * SocketNotifier derived objects are instantiated by
 * DispatchInterface::NewSocket().  Clients will then use the
 * Callback::Register() method to set their object and function to be
 * invoked on socket activity.
 *
 * SocketNotifier is inherently single threaded and not meant for event
 * loops that support multithreading.
 */
class SocketNotifier : public Callback<void, SocketNotifier*, int> {
public:
	/**
	 * @brief Set state of event delivery
	 *
	 * This method allows readiness reporting from a
	 * SocketNotifier object to be temporarily disabled.
	 *
	 * @param enable Set to @c true to receive read or write
	 * readiness callbacks, @c false otherwise.
	 */
	virtual void SetEnabled(bool enable) = 0;
	virtual ~SocketNotifier() {}
};

/**
 * @brief Environment Independent Timer Notification Interface
 * @ingroup events
 *
 * TimerNotifier provides an abstract mechanism to receive timeout
 * events independent of any specific main event loop.  TimerNotifier
 * is derived from Callback, and has additional methods to support
 * configuring the timeout and canceling a pending timeout.
 *
 * The sole parameter passed to the callback points to the TimerNotifier
 * object initiating the event.
 *
 * TimerNotifier derived objects are instantiated by
 * DispatchInterface::NewTimer().  Clients will then use the
 * TimerNotifier::Set() method to set the timeout to trigger, and the
 * Callback::Register() method to set their object and function to be
 * invoked when the timeout occurs.
 *
 * TimerNotifier is inherently single threaded and not meant for event
 * loops that support multithreading.
 */
class TimerNotifier : public Callback<void, TimerNotifier*> {
public:
	/**
	 * @brief Set the timer
	 *
	 * On return, the timer will be in the pending state, and
	 * will trigger at or after the specified number of milliseconds
	 * have elapsed.  If the timer is already pending, it will be
	 * canceled and reconfigured for the new timeout value.
	 *
	 * @param msec Time to wait until trigger
	 */
	virtual void Set(int msec) = 0;

	/**
	 * @brief Cancels a pending timer
	 *
	 * For timers that are pending, this function cancels them and
	 * ensures that the callback will not be invoked.
	 */
	virtual void Cancel(void) = 0;
	virtual ~TimerNotifier() {}
};

/**
 * @brief Environment-Independent Event Dispatcher Interface
 * @ingroup events
 *
 * Most components of libhfp are event-driven state machines, and are designed
 * to operate under a polling event loop.  The DispatchInterface describes all
 * methods that they use to schedule event notifications.  Two types of
 * notifications are required: @b socket (file handle) activity and @b timeouts.
 * As an abstract interface, a DispatchInterface may be implemented for every
 * environment that libhfp is to run in, e.g. Qt, Gtk+.
 *
 * To adapt libhfp to a new environment, the DispatchInterface, SocketNotifier,
 * and TimerNotifier interfaces must be implemented.  An instance of the
 * environment-specific DispatchInterface must then be provided as part of
 * constructing most libhfp objects.
 *
 * DispatchInterface is inherently single threaded and not meant for event
 * dispatchers that support multithreading.
 *
 * @sa BtHub::BtHub(), SoundIoPump::SoundIoPump()
 */
class DispatchInterface {
public:
	/**
	 * @brief Factory interface for SocketNotifier objects
	 *
	 * Creates an event dispatcher dependent SocketNotifier object
	 * that can be used to receive events on a specific socket.
	 *
	 * @param fh File handle to be monitored for activity
	 * @param writable true to notify on writability, false
	 * to notify on readability.
	 */
	virtual SocketNotifier *NewSocket(int fh, bool writable) = 0;

	/**
	 * @brief Factory interface for TimerNotifier objects
	 *
	 * Creates an event dispatcher dependent TimerNotifier object
	 * that can be used to trigger time-based actions.
	 */
	virtual TimerNotifier *NewTimer(void) = 0;


	/**
	 * @brief Log Levels
	 *
	 * Log levels are used to express messages submitted by LogError(),
	 * LogWarn(), LogInfo(), and LogDebug() to LogVa().  With this,
	 * DispatchInterface implementations need only implement one virtual
	 * method to support logging.
	 */
	enum logtype_t {
		/// Error messages, highest priority
		EVLOG_ERROR = 1,
		/// Warning messages
		EVLOG_WARNING,
		/// Informational messages
		EVLOG_INFO,
		/// Debug messages of limited interest to end users
		EVLOG_DEBUG,
	};

	/**
	 * @brief Back-end Logging Function
	 *
	 * To support logging, a DispatchInterface derived class need only
	 * implement this method.  The LogError(), LogWarn(), LogInfo(),
	 * and LogDebug() methods exist only as convenient frontends.
	 */
	virtual void LogVa(logtype_t lt, const char *fmt, va_list ap) = 0;

	/**
	 * @brief Submit an error message to the application log
	 */
	void LogError(const char *fmt, ...)
		__attribute__((format(printf, 2, 3))) {
		/* Apparently the this pointer counts as argument #1 */
		va_list alist;
		va_start(alist, fmt);
		LogVa(EVLOG_ERROR, fmt, alist);
		va_end(alist);
	}

	void LogError(ErrorInfo *err, uint16_t subsys, uint16_t code,
		      const char *fmt, ...)
		__attribute__((format(printf, 5, 6))) {
		va_list alist, xlist;
		va_start(alist, fmt);
		if (err) {
			va_copy(xlist, alist);
			err->SetVa(subsys, code, fmt, xlist);
			va_end(xlist);
		}
		LogVa(EVLOG_ERROR, fmt, alist);
		va_end(alist);
	}

	/**
	 * @brief Submit a warning message to the application log
	 */
	void LogWarn(const char *fmt, ...)
		__attribute__((format(printf, 2, 3))) {
		va_list alist;
		va_start(alist, fmt);
		LogVa(EVLOG_WARNING, fmt, alist);
		va_end(alist);
	}

	void LogWarn(ErrorInfo *err, uint16_t subsys, uint16_t code,
		     const char *fmt, ...)
		__attribute__((format(printf, 5, 6))) {
		va_list alist, xlist;
		va_start(alist, fmt);
		if (err) {
			va_copy(xlist, alist);
			err->SetVa(subsys, code, fmt, xlist);
			va_end(xlist);
		}
		LogVa(EVLOG_WARNING, fmt, alist);
		va_end(alist);
	}

	/**
	 * @brief Submit a warning message to the application log
	 */
	void LogInfo(const char *fmt, ...)
		__attribute__((format(printf, 2, 3))) {
		va_list alist;
		va_start(alist, fmt);
		LogVa(EVLOG_INFO, fmt, alist);
		va_end(alist);
	}

	/**
	 * @brief Submit a low-priority debug message to the application log
	 */
	void LogDebug(const char *fmt, ...)
		__attribute__((format(printf, 2, 3))) {
#if !defined(NDEBUG)
		va_list alist;
		va_start(alist, fmt);
		LogVa(EVLOG_DEBUG, fmt, alist);
		va_end(alist);
#endif
	}

	void LogDebug(ErrorInfo *err, uint16_t subsys, uint16_t code,
		     const char *fmt, ...)
		__attribute__((format(printf, 5, 6))) {
		va_list alist, xlist;
		va_start(alist, fmt);
		if (err) {
			va_copy(xlist, alist);
			err->SetVa(subsys, code, fmt, xlist);
			va_end(xlist);
		}
#if !defined(NDEBUG)
		LogVa(EVLOG_DEBUG, fmt, alist);
#endif
		va_end(alist);
	}

	/**
	 * @brief Stub virtual destructor
	 */
	virtual ~DispatchInterface() {}
};

}  /* namespace libhfp */
#endif  /* !defined(__LIBHFP_EVENTS_H__) */
