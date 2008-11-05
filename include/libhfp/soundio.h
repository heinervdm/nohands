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

#if !defined(__LIBHFP_SOUNDIO_H__)
#define __LIBHFP_SOUNDIO_H__

#include <stdint.h>
#include "events.h"

/**
 * @file libhfp/soundio.h
 */

namespace libhfp {

/**
 * @defgroup soundio Audio I/O and Signal Processing
 *
 * This group contains facilities related to audio processing.
 */

/**
 * @brief Sample count value type
 * @ingroup soundio
 */
typedef unsigned int sio_sampnum_t;

/**
 * @brief Basic properties of a SoundIo object
 */
struct SoundIoProps {
	/// Is capable of generating event notifications
	bool		has_clock;
	/// Is capable of providing sample data
	bool		does_source;
	/// Is capable of consuming sample data
	bool		does_sink;
	/// Output is directly tied to input
	bool		does_loop;
	/// Should be removed when transfers start failing
	bool		remove_on_exhaust;
	/// Output buffer size, 0=no limit
	sio_sampnum_t	outbuf_size;
};

/**
 * @brief Known Sample Format Enumeration
 * @ingroup soundio
 *
 * This enumeration contains common known sample formats for use with
 * SoundIo derived classes, such as the ALSA and OSS driver frontends.
 */
enum sio_sampletype_t {
	SIO_INVALID,
	/// Unsigned 8-bit
	SIO_PCM_U8,
	/// Signed 16-bit little endian
	SIO_PCM_S16_LE,
	/// A-Law
	SIO_PCM_A_LAW,
	/// Mu-Law
	SIO_PCM_MU_LAW,
};

/**
 * @brief PCM Audio Data Format Descriptor
 *
 * This structure describes the format of a PCM audio data stream. It has
 * several defined use cases, including:
 * - Setting and retrieving the active PCM data format for a SoundIo
 * object: SoundIo::SndGetFormat(), SoundIo::SndSetFormat().
 * - Describing the PCM data format to a filter: SoundIoFilter::FltPrepare()
 */
struct SoundIoFormat {
	/** @brief Format of each sample */
	sio_sampletype_t	sampletype;
	/** @brief Number of sample records to play/record per second */
	unsigned int		samplerate;
	/**
	 * @brief Number of sample records per packet
	 *
	 * Most audio hardware devices will not cause a software interrupt
	 * for each sample that is recorded or played back.  Instead, an
	 * interrupion will occur after each group of samples, of a
	 * predetermined size, has been recorded or played back.  In this
	 * code base, such groups are referred to as packets.
	 *
	 * For telephony applications, this value should be minimized
	 * to a fraction of the desired latency, and the output buffer
	 * fill level should be maintained at the desired latency.
	 * @sa SoundIoPump::SetMinBufferFill().
	 */
	sio_sampnum_t		packet_samps;

	/** @brief Number of channels per sample record */
	uint8_t			nchannels;

	/** @brief Size of each sample record in bytes */
	uint8_t			bytes_per_record;
};

/**
 * @brief Queue State Descriptor of a SoundIo Object
 *
 * Each SoundIo object manages separate input and output sample queues,
 * and these queues have a collective state.  This structure describes the
 * state of the queues of one SoundIo object.
 */
struct SoundIoQueueState {
	/** @brief Number of sample records waiting in the input queue */
	sio_sampnum_t	in_queued;
	/** @brief Number of sample records waiting in the output queue */
	sio_sampnum_t	out_queued;
};

/**
 * @brief Audio Buffer Descriptor
 */
struct SoundIoBuffer {
	/** @brief Number of sample records in the buffer (NOT bytes) */
	sio_sampnum_t	m_size;		/* in # of samples */
	/** @brief Pointer to the start of the sample data buffer */
	uint8_t		*m_data;
};


/**
 * @brief Audio Source/Sink Interface
 * @ingroup soundio
 *
 * SoundIo abstracts a full-duplex PCM audio hardware device, including
 * input and output queues, and asynchronous notification.
 *
 * There are SoundIo derived classes for handling ALSA and OSS type
 * sound card interfaces on Linux.  See SoundIoCreateOss() and
 * SoundIoCreateAlsa().  SoundIo is implemented by HfpSession for
 * streaming voice audio over Bluetooth.
 */
class SoundIo {
public:
	virtual ~SoundIo() {}

	/**
	 * @brief Request that the underlying device be opened
	 *
	 * Attempts to open the underlying resource, if applicable.
	 * For sound card driver frontends, such as the ALSA and OSS
	 * modules, this method causes the sound card device to be
	 * opened and claimed.
	 *
	 * The details of the device to be opened are assumed to have
	 * been set when the SoundIo derived object was constructed.
	 * These details might include the name and basic configuration of
	 * the device, e.g. SoundIoCreateAlsa().  Such details are not
	 * provided to this method.
	 *
	 * @param sink Set to @c true to open the output side of the device
	 * @param source Set to @c true to open the input side of the device
	 *
	 * @retval true Open Succeeded, SoundIo is now usable
	 * @retval false Open Failed, possibly because:
	 * - Creation or opening of a necessary file (if applicable) failed.
	 * - The underlying hardware device (if applicable) is in use.
	 * - The underlying hardware device (if applicable) does not
	 * support the last PCM data stream format configured via
	 * SndSetFormat().
	 * - Both the @em play and @em capture parameters were set to false.
	 * - The device is not ready, e.g. HfpSession::SndOpen() will
	 * return false until its socket-level connection is complete.
	 *
	 * @sa SndClose()
	 */
	virtual bool SndOpen(bool sink, bool source) = 0;

	/**
	 * @brief Request that an opened underlying device be closed
	 *
	 * Closes an underlying device that was previously opened with
	 * SndOpen().  Resources should be released.  For sound card
	 * driver frontends, such as the ALSA and OSS modules, this method
	 * causes the underlying devices to be closed and released.
	 *
	 * @sa SndOpen()
	 */
	virtual void SndClose(void) = 0;

	/**
	 * @brief Get Basic Capabilities
	 *
	 * This method fills in a structure describing the basic
	 * capabilities of the interface.  Some modules, such as the
	 * ALSA and OSS frontends, support full-duplex, asynchronous
	 * operation.  Others, such as the file module, support only
	 * input-only or output-only synchronous operation.
	 *
	 * @param[out] props Properties structure to be filled out
	 *
	 * @post @c props.has_clock will be set to @c true if the
	 * object supports asynchronous notification through
	 * SoundIo::cb_NotifyPacket, @c false otherwise.
	 * @post @c props.does_source will be set to @c true if the
	 * object supports input, @c false otherwise.
	 * @post @c props.does_sink will be set to @c true if the
	 * object supports output, @c false otherwise.
	 * @post @c props.does_loop will be set to @c true if the object's
	 * input buffer grows immediately as data is submitted to its
	 * output buffer, @c false otherwise.
	 *
	 * The result of this method may differ depending on:
	 * - Whether the device (if applicable) is open.  Some objects will
	 * not return meaningful results unless they are open.
	 * See SndOpen().
	 * - The active configuration of the object.  See SndSetFormat().
	 * - Parameters supplied when the object was constructed.  For
	 * example, providing a NULL output device specifier to the ALSA
	 * frontend will cause it to set SoundIoProps::does_sink = false.
	 */
	virtual void SndGetProps(SoundIoProps &props) const = 0;

	/**
	 * @brief Get Current PCM Data Stream Format
	 *
	 * This method fills out a SoundIoFormat structure based on the
	 * object's currently configured PCM data stream format.  This
	 * method may be invoked on objects in the open or closed states,
	 * but in general, the result should only be considered meaningful
	 * for objects in the open state.
	 *
	 * @param[out] format Structure to be filled with the object's
	 * currently configured data stream format.
	 *
	 * The result of this method may be affected by:
	 * - Whether the device (if applicable) is open.  Some objects will
	 * not return meaningful results unless they are open.  An object
	 * will always return the most specific results when it is open.
	 * See SndOpen().
	 * - Parameters supplied when the object was constructed.
	 */
	virtual void SndGetFormat(SoundIoFormat &format) const = 0;

	/**
	 * @brief Set Current PCM Data Stream Format
	 *
	 * This method attempts to reconfigure the PCM data stream format
	 * of the object.  This method may be invoked on objects in the
	 * open or closed states.
	 *
	 * @param[in,out] format Structure containing the new format
	 * parameters to be applied to the object, which is filled with the
	 * resulting effective parameters on return.
	 *
	 * @retval true Parameters accepted and applied.  Note that in
	 * some cases, for objects in the closed state, this is a tentative
	 * approval of the stream format.
	 * @retval false One or more mandatory parameter values are not
	 * supported by the object.
	 *
	 * The @em Mandatory @em Parameters include:
	 * - SoundIoFormat::sampletype, which must be exactly supported.
	 * - SoundIoFormat::samplerate, which must be exactly supported.
	 * - SoundIoFormat::nchannels, which must be exactly supported.
	 *
	 * The other fields of the SoundIoFormat object, including
	 * SoundIoFormat::packet_samps, are guidelines.  The object may
	 * use any convenient value for these fields.  On success, the
	 * contents of the @c format parameter will be updated to reflect
	 * the effective values.
	 */
	virtual bool SndSetFormat(SoundIoFormat &format) = 0;

	/**
	 * @brief Request Input Buffer Access
	 *
	 * Clients of SoundIo objects access their input and output sample
	 * buffers directly.  This method requests access to the object's
	 * input (capture) buffer.
	 *
	 * @param[in,out] fillme The buffer descriptor containing the
	 * requested size, to be filled with the memory location and
	 * actual size.
	 *
	 * @pre The requested size of the buffer, in @em sample @em records,
	 * is passed in through the @c fillme.m_size field.  If this value
	 * is zero, the largest possible buffer will be requested.
	 *
	 * @post The resulting buffer is returned through the @c fillme.m_data
	 * field.  The valid size of the buffer, in @em sample @em records,
	 * is returned through the @c fillme.m_size field.  If
	 * @c fillme.m_size is set to zero on return, no input samples are
	 * available, and @c fillme.m_data should be considered invalid.
	 * If a specific buffer size was requested, the resulting buffer
	 * size should never be larger, but may be smaller.
	 *
	 * The returned buffer always contains the least recent input
	 * samples.  After a client has finished examining the input buffer,
	 * it should dequeue some number of input samples with
	 * SndDequeueIBuf().  After samples are dequeued, SndGetIBuf() will
	 * make more recent input samples available.
	 *
	 * Buffers returned through this method will remain valid until:
	 * - All input samples returned through this method are dequeued
	 * via SndDequeueIBuf().
	 * - Asynchronous handling is halted via SndAsyncStop(), or a
	 * SoundIo::cb_NotifyPacket callback is received with its second
	 * parameter set to NULL.
	 * - The object is closed via SndClose().
	 *
	 * This method will only return useful results when invoked on
	 * objects in the open state.
	 *
	 * @sa SndDequeueIBuf(), SndGetQueueState()
	 */
	virtual void SndGetIBuf(SoundIoBuffer &fillme) = 0;

	/**
	 * @brief Request Dequeueing of Input Samples
	 *
	 * When a SoundIo client has finished processing input samples
	 * accessed through SndGetIBuf(), or merely wishes to discard
	 * them, it may do so using this method.  Input samples need
	 * not have been accessed using SndGetIBuf() in order to be
	 * dequeued through this method.
	 *
	 * @param nsamples Number of sample records to remove from the
	 * start of the input queue
	 *
	 * @note If the entire range of an input buffer previously acquired
	 * by SndGetIBuf() is fully dequeued, the input buffer will become
	 * invalid, and attempts to access it may cause SIGSEGV.  Clients
	 * must take care to avoid reusing input buffers in this way.
	 *
	 * @warning Attempting to dequeue more input samples than are
	 * available is not permitted and should cause an assertion failure.
	 */
	virtual void SndDequeueIBuf(sio_sampnum_t nsamples) = 0;

	/**
	 * @brief Request Output Buffer Access
	 *
	 * Clients of SoundIo objects access their input and output sample
	 * buffers directly.  This method requests access to the object's
	 * input (capture) buffer.
	 *
	 * @param[in,out] fillme The buffer descriptor containing the
	 * requested size, to be filled with the memory location and
	 * actual size.
	 *
	 * @pre The requested size of the buffer, in @em sample @em records,
	 * is passed in through the @c fillme.m_size field.  If this value
	 * is zero, the largest possible buffer will be requested.
	 *
	 * @post The resulting buffer is returned through the @c fillme.m_data
	 * field.  The valid size of the buffer, in @em sample @em records,
	 * is returned through the @c fillme.m_size field.  If
	 * @c fillme.m_size is set to zero on return, no input samples are
	 * available, and @c fillme.m_data should be considered invalid.
	 * If a specific buffer size was requested, the resulting buffer
	 * size should never be larger, but may be smaller.
	 *
	 * To output audio data, a client would fill the returned buffer
	 * with its next batch of outbound sample records.  After part or
	 * all of the buffer has been filled, the client should call the
	 * SndQueueOBuf() method to propagate the sample records.
	 *
	 * Buffers returned through this method will remain valid until:
	 * - Any number of output samples are queued via SndQueueOBuf().
	 * - Asynchronous handling is halted via SndAsyncStop(), or a
	 * SoundIo::cb_NotifyPacket callback is received with its second
	 * parameter set to NULL.
	 * - The object is closed via SndClose().
	 *
	 * This method will only return useful results when invoked on
	 * objects in the open state.
	 *
	 * @sa SndQueueOBuf(), SndGetQueueState()
	 */
	virtual void SndGetOBuf(SoundIoBuffer &fillme) = 0;

	/**
	 * @brief Request Queueing of Output Samples
	 *
	 * When a SoundIo client has finished filling part or all of an
	 * output buffer accessed via SndGetOBuf(), it may request
	 * the sample records be propagated to their destination with
	 * this method.
	 *
	 * @param nsamples Number of sample records copied into the
	 * output buffer to be propagated
	 *
	 * @warning Each call to SndQueueOBuf() must have been preceded
	 * by a call to SndGetOBuf().
	 * @warning Attempting to queue more samples than were previously
	 * returned by SndGetOBuf() is not permitted and should cause
	 * an assertion failure.
	 */
	virtual void SndQueueOBuf(sio_sampnum_t nsamples) = 0;

	/**
	 * @brief Query the queue state of the device
	 *
	 * This method fills out a SoundIoQueueState structure with
	 * the fill levels of the sample buffers of the SoundIo object.  This
	 * information is required by the SoundIoPump class, which uses it to
	 * make transfer size decisions.
	 *
	 * For sound cards and other clocked sources, this information is
	 * assumed to represent the state of the local device buffers, which
	 * must fill and drain as the device processes samples.
	 *
	 * For unclocked sinks, this information is assumed to represent
	 * a minimum number of samples available.  SoundIoPump will not
	 * transfer more than @c qs.out_queued samples.
	 *
	 * @param[out] qs Queue state structure to be filled in
	 *
	 * @note This method may only be expected to return useful
	 * results when the device is opened.
	 * @warning Bugs in this method can cause SoundIoPump problems
	 * that are difficult to trace back to this method.
	 */
	virtual void SndGetQueueState(SoundIoQueueState &qs) = 0;

	/**
	 * @brief Notification of Audio Packet Data Availability
	 *
	 * The cb_NotifyPacket callback is invoked slightly after each
	 * packet period boundary, as measured from the hardware clock
	 * of the audio device.
	 *
	 * @param SoundIo* Pointer to the SoundIo-derived object
	 * that initiated the call.  This object may have new input
	 * sample records, or may have completed handling of part or all
	 * of its output queue.
	 *
	 * @param SoundIoQueueState* Structure describing the state of the
	 * queues for the device:
	 * - in_queued is the number of available, unprocessed input
	 * samples, -1 if there was an overrun, or 0 if input is
	 * disabled.
	 * - out_queued is the number of unplayed output samples
	 * sitting in the hardware buffer, 0 if there was an
	 * underrun, or -1 if output is disabled.
	 *
	 * If the second parameter is NULL, the notification indicates
	 * a SoundIo data stream with an error condition that forced
	 * asynchronous audio handling to be halted and disabled.  In
	 * this case, because asynchronous audio handling has been halted,
	 * no further invocations of the callback should be expected until
	 * it is restarted via SndAsyncStart().
	 *
	 * A registered target method can be used to maintain the buffers
	 * for data in both directions.
	 */
	Callback<void, SoundIo*, SoundIoQueueState*>	cb_NotifyPacket;

	/**
	 * @brief Request start of asynchronous audio handling
	 *
	 * For SoundIo objects that support asynchronous operation, this
	 * method can be used to initiate it.
	 *
	 * @param sink Set to @c true to start asynchronous output
	 * @param source Set to @c true to start asynchronous input
	 *
	 * @retval true Asynchronous audio handling mode has been started
	 * @retval false Error enabling asynchronous data mode
	 *
	 * When asynchronous audio handling mode is operating,
	 * periodic calls will be made to the SoundIo::cb_NotifyPacket
	 * callback.
	 *
	 * Once asynchronous audio handling is successfully started,
	 * it may be spontaneously aborted due to an unexpected
	 * condition, such as a sound card catching on fire.  In this case,
	 * the SoundIo object will automatically halt asynchronous audio
	 * handling, and will inform its client of this by making a
	 * single call to SoundIo::cb_NotifyPacket with the second
	 * parameter set to 0.
	 *
	 * @sa SoundIo::cb_NotifyPacket, SndAsyncStop()
	 */
	virtual bool SndAsyncStart(bool sink, bool source) = 0;

	/**
	 * @brief Request halting of asynchronous audio handling
	 *
	 * For SoundIo objects that are currently operating in asynchronous
	 * mode, this method can be used to halt processing and cease
	 * future calls to SoundIo::cb_NotifyPacket.
	 *
	 * @sa SoundIo::cb_NotifyPacket, SndAsyncStart()
	 */
	virtual void SndAsyncStop(void) = 0;

	/**
	 * @brief Query whether asynchronous audio handling has been started
	 *
	 * @retval true Asynchronous audio handling is started
	 * @retval false Asynchronous audio handling is not started
	 */
	virtual bool SndIsAsyncStarted(void) const = 0;
};


/*
 * SoundIoDeviceList is a utility class used to communicate a list of
 * detected sound card names and descriptions associated with each
 * driver.
 */

class SoundIoDeviceList {
	struct InfoNode {
		const char	*m_name;
		const char	*m_desc;
		struct InfoNode	*m_next;

	}		*m_first, *m_last, *m_cursor;

public:
	SoundIoDeviceList(void) : m_first(0), m_last(0), m_cursor(0) {}
	~SoundIoDeviceList();

	bool Add(const char *name, const char *desc);

	bool First(void) { m_cursor = m_first; return m_cursor != 0; }
	bool Next(void) { m_cursor = m_cursor->m_next; return m_cursor != 0; }
	const char *GetName(void) const { return m_cursor->m_name; }
	const char *GetDesc(void) const { return m_cursor->m_desc; }
};


/* Factories for various flavors of SoundIo */

/**
 * @brief Construct a SoundIo object backed by an OSS driver
 * @ingroup soundio
 */
extern SoundIo *SoundIoCreateOss(DispatchInterface *dip,
				 const char *driveropts);

extern SoundIoDeviceList *SoundIoGetDeviceListOss(void);


/**
 * @brief Construct a SoundIo object backed by an ALSA driver
 * @ingroup soundio
 *
 * This function constructs a SoundIo object that acts as a frontend
 * to an ALSA driver.  The device specifier must be specified at
 * construction time to this function.
 *
 * @param dip Dispatcher interface object adapted to the environment in
 * which the SoundIo object is to run.
 * @param driveropts Driver options string for the ALSA driver.  This
 * can be empty, in which case the default devices will be selected.
 * It can be a simple devspec, in which case that devspec will be used
 * for both input and output.  It can also be a concatenation of
 * @c name @c = @c value pairs of the following form:
 * @code
 * name1=value1[&name2=value2[&...]]
 * @endcode
 * Recognized parameters for the ALSA driver include:
 * - @c dev @c = @em devspec (sets both input and output devices)
 * - @c in @c = @em devspec (sets input device)
 * - @c out @c = @em devspec (sets output device)
 * - @c mmap @c = @em on|off (enables or disables mmap mode -- default off)
 *
 * @return A newly constructed SoundIo object interfacing with ALSA, or
 * NULL on failure.
 *
 * Some common ALSA device specifiers:
 * - @c "default" -- the default PCM device.
 * - @c "plughw:0" -- Card 0 with the rate autoconversion plugin attached.
 * - @c "hw:0" -- Card 0 with no rate conversion plugin, unlikely to work.
 *
 * An example @c driveropts string:
 * @code
 * out=default&in=plughw:0&mmap=on
 * @endcode
 */
extern SoundIo *SoundIoCreateAlsa(DispatchInterface *dip,
				  const char *driveropts);

extern SoundIoDeviceList *SoundIoGetDeviceListAlsa(void);


/**
 * @brief Construct a SoundIo object backed by a fixed-size memory buffer
 * @ingroup soundio
 *
 * This function constructs a SoundIo object that uses independent
 * memory-only buffers for sample storage.  The object uses separate
 * buffers for "playback" and "capture", depending on the mode passed
 * to SndOpen().  When requested to be opened for playback, it will
 * initialize a new empty buffer to receive the playback samples.  When
 * requested to be opened for capture, if a prior playback buffer exists,
 * it will become the capture buffer and samples will be read from it.
 * The object can also operate in full-duplex mode, in which case it
 * will present separate playback and capture buffers.
 *
 * @param fmt Initial format to set on the object.
 * @param nsamps Buffer size to use when creating new buffers.  To
 * convert from seconds, multiply the number of seconds by
 * SoundIoFormat::samplerate.
 *
 * @return A newly constructed SoundIo memory buffer handler object
 * associated with @em filename, or NULL on failure.
 *
 * @note This object was created to aid in system testing, to observe
 * the effects of filters and signal processing settings.
 */
SoundIo *SoundIoCreateMembuf(const SoundIoFormat *fmt, sio_sampnum_t nsamps);

/**
 * @brief Construct a SoundIo object backed by a disk file
 * @ingroup soundio
 *
 * This function constructs a SoundIo object that acts as a frontend
 * to an uncompressed PCM audio file using the SGI AudioFile library.
 *
 * The object can be directed to open the file for reading or writing
 * depending on the parameters passed to SoundIo::SndOpen().  For
 * writing, the sample format should be configured via
 * SoundIo::SndSetFormat() prior to the call to SoundIo::SndOpen().
 * For reading, SoundIo::SndGetFormat() will only report meaningful
 * results after the file has been opened.
 *
 * @param ei Pointer to dispatcher interface object for the environment.
 * This is used for logging errors.
 * @param filename Name of the audio file to associate the object with.
 * @param create Set to @em true to allow the file to be created if it
 * is opened for output and does not exist.
 *
 * @return A newly constructed SoundIo disk file handler object
 * associated with @em filename, or NULL on failure.
 */
SoundIo *SoundIoCreateFileHandler(DispatchInterface *ei,
				  const char *filename, bool create);

/**
 * @brief Audio Filtering and Signal Processing Interface
 * @ingroup soundio
 */
class SoundIoFilter {
	friend class SoundIoPump;
	SoundIoFilter	*m_up, *m_down;

public:
	/** @brief Standard constructor */
	SoundIoFilter() : m_up(0), m_down(0) {}

	/** @brief Standard destructor */
	virtual ~SoundIoFilter() {}

	/**
	 * @brief Request filter to prepare for stream processing
	 *
	 * As part of starting stream processing, SoundIoPump will invoke
	 * this method on all registered filters.
	 *
	 * @param fmt PCM audio format to be used in the stream
	 * @param up @c true if samples will move up through this filter,
	 * @c false otherwise.
	 * @param dn @c true if samples will move down through this filter,
	 * @c false otherwise.
	 *
	 * @retval true Filter is prepared and ready to process samples.
	 * @retval false Filter is not prepared, effectively vetoing
	 * stream setup.  This will cause SoundIoPump::Start() to fail.
	 *
	 * For full duplex streams, both @c up and @c down will be @c true.
	 * For single direction streams, either @c up or @c down will be
	 * @c true.
	 *
	 * If this method succeeds, future calls to FltProcess() may be made.
	 *
	 * @note Each successful call to FltPrepare() will have a
	 * corresponding call to FltCleanup().
	 */
	virtual bool FltPrepare(SoundIoFormat const &fmt,
				bool up, bool dn) = 0;

	/**
	 * @brief Release filter from stream processing
	 *
	 * As part of terminating stream processing, SoundIoPump will
	 * invoke this method on all registered filters.
	 *
	 * After this method returns, no further calls to FltProcess()
	 * will be made until the next successful call to FltPrepare().
	 *
	 * @note Each successful call to FltPrepare() will have a
	 * corresponding call to FltCleanup().
	 */
	virtual void FltCleanup(void) = 0;

	/**
	 * @brief Request processing of a sample buffer
	 *
	 * The SoundIoPump object handles transfer of samples between
	 * two SoundIo objects, referred to as its bottom and top
	 * endpoints.  Between the endpoints, filters may be stacked
	 * to intercept and process samples as they pass between the
	 * endpoints.
	 *
	 * Sample data in a bidirectional pump configuration is
	 * transferred first from top to bottom, then bottom to top.
	 * In such a configuration, for each packet of sample data
	 * transferred, each filter will receive a FltProcess() call
	 * with @c up = @c false, then another FltProcess() call with
	 * @c up = @c true.
	 *
	 * @param up @c true if the samples are moving up through the
	 * filter stack, @c false otherwise.
	 * @param src Buffer containing source samples for filter
	 * @param dest Buffer to contain result samples from filter,
	 * if modification is required.
	 */
	virtual SoundIoBuffer const *FltProcess(bool up,
						SoundIoBuffer const &src,
						SoundIoBuffer &dest) = 0;
};


/**
 * @brief Signal processing configuration for SoundIoFltSpeex
 */
struct SoundIoSpeexProps {
	/// Noise reduction toggle
	bool		noisereduce;
	/// Echo cancel tail length in milliseconds, 0=disable
	int		echocancel_ms;
	/// Automatic gain level
	int		agc_level;
	/// Dereverberation level
	float		dereverb_level;
	/// Dereverberation decay value
	float		dereverb_decay;
};

/**
 * @brief Speex signal processing filter
 */
class SoundIoFltSpeex : public SoundIoFilter {
public:
	/**
	 * @brief Set signal processing configuration
	 */
	virtual bool Configure(SoundIoSpeexProps const &props) = 0;
};

/**
 * @brief Instantiate a Speex signal processing filter object
 * @ingroup soundio
 *
 * Constructs a new signal processing filter object employing the
 * libspeexdsp library.  This filter provides a number of signal
 * processing features useful for telephony applications.  Used in
 * conjunction with SoundIoPump, this filter can clean up microphone
 * inputs and remove acoustic echo.
 *
 * @note This filter is not a data compressor and does not employ the
 * Speex voice codec.  It does employ the Speex signal processing accessory
 * functions from libspeexdsp.
 *
 * This filter operates on PCM data moving upward through the stack,
 * i.e. from the bottom endpoint to the top endpoint.  The acoustic
 * echo canceler may also examine data moving downward to the bottom
 * endpoint, in order to determine a signal to be removed from the input.
 * In order for this filter to be effective, the sound card device
 * must be the bottom endpoint.  In order for the echo canceler to be
 * effective, this filter should either be the bottom-most filter, or
 * no filters placed below it should alter the data stream.
 *
 * @param ei Pointer to dispatcher interface object for the environment.
 * This is used for logging errors.
 *
 * To use this filter:
 * -# Instantaite it with this function.
 * -# Configure it, SoundIoFltSpeex::Configure().
 * -# Insert it into the filter stack, SoundIoPump::AddBottom().  This
 * filter functions best as the bottom-most filter in a stack where the
 * sound card device is the bottom endpoint.
 * -# Start the data pump.  SoundIoPump::Start().
 *
 * For information on the options supported by the filter and how to
 * configure it, see SoundIoSpeexProps and SoundIoFltSpeex::Configure().
 *
 * @return A newly constructed, unconfigured SoundIoFltSpeex object,
 * or NULL on error.
 */
SoundIoFltSpeex *SoundIoFltCreateSpeex(DispatchInterface *ei);


/**
 * @brief Audio Data Pump
 * @ingroup soundio
 *
 * SoundIoPump handles exchanging sample data between two SoundIo derived
 * source/sink endpoints.  It can operate in unidirectional or
 * bidirectional mode depending on the configured endpoints.  SoundIoPump
 * has no synchronous mode, operating exclusively asynchronously, using event
 * notifications from its endpoint SoundIo objects to execute data
 * transfers.
 *
 * Specifically, SoundIoPump operates on buffer fill level constraints,
 * and attempts to transfer as many samples as possible per availability
 * event.  Transfers are always done symmetrically -- the number of samples
 * transferred between one source/sink pair is always the same as all
 * others -- and must satisfy the fill level constraints of all sources and
 * sinks in the system.  When fill level constraints cannot be satisfied,
 * silence-padding and sample dropping will occur.  Sophisticated methods
 * such as resampling are not employed.  Fill level configuration is a
 * closed process, but hints for fill level constraints can be provided via
 * SetMinBufferFill() and SetJitterWindow().
 *
 * Besides the fill level constraints and the packet sizes of each
 * endpoint, the pump also chooses a "filter packet size" which is the
 * basic transfer unit.  The filter packet size is used for filters
 * (described below) and all chosen transfer sizes are multiples of the
 * filter packet size.
 *
 * The two endpoints are referred to as the @em top and @em bottom
 * endpoints.  Both must be set in order to start the pump.  Either can be
 * changed at idle or while the pump is running.  Removing an endpoint
 * while the pump is running without specifying a replacement will stop
 * the pump.  If attempting to replace an endpoint while the pump is
 * running, the replacement endpoint's buffer and packet sizes must
 * support the filter packet size that was selected when the pump was
 * started.  If the replacement endpoint has a smaller packet size or
 * output buffer size, it may not satisfy constraints.
 *
 * Stackable signal processing filters can be configured into SoundIoPump.
 * Filter objects must implement the SoundIoFilter interface.  A list of
 * filters is formed between the two endpoints, and filters can be added
 * or removed from the ends using AddTop(), AddBottom(), RemoveTop(), and
 * RemoveBottom().  For each data transfer, all attached filters are
 * provided with symmetric fixed-size PCM data packets for inbound and
 * outbound sample data.
 *
 * When sample data is to be transferred, it is always done in
 * @b top-to-bottom, then @b bottom-to-top order.  In a bidirectional
 * configuration, filters will always observe a single downward packet
 * of the filter packet size, followed by a single upward packet of the
 * filter packet size.
 *
 * The primary goal of the filter interface is to support acoustic echo
 * cancellation.
 *
 * Operating in single-direction mode, a SoundIoPump instance can be used
 * to play audio files and monitor the position of their playback.  See
 * SoundIoCreateFileHandler().
 */
class SoundIoPump {
private:
	enum {
		c_sampsize = 4,
	};

	struct SoundIoWorkingState {
		SoundIo		*siop;
		uint8_t		bpr;
		SoundIoBuffer	in_buf;
		sio_sampnum_t	in_xfer;
		sio_sampnum_t	in_xfer_expect;
		sio_sampnum_t	in_silencepad;
		uint8_t		in_silence[c_sampsize];
		SoundIoBuffer	out_buf;
		sio_sampnum_t	out_buf_used;
		sio_sampnum_t	out_xfer;
		sio_sampnum_t	out_xfer_expect;
		sio_sampnum_t	out_drop;
		uint8_t		out_silence[c_sampsize];
	};

	struct SoundIoPumpConfig {
		SoundIoFormat		fmt;
		sio_sampnum_t		filter_packet_samps;
		sio_sampnum_t		bottom_out_min;
		sio_sampnum_t		bottom_out_max;
		sio_sampnum_t		top_out_min;
		sio_sampnum_t		top_out_max;
		sio_sampnum_t		in_max;
		bool			bottom_async, top_async;
		bool			bottom_loop, top_loop;
		bool			bottom_roe, top_roe;
		bool			pump_down, pump_up;
		bool			warn_loss;
		unsigned int		watchdog_to;
	};

	DispatchInterface	*m_ei;
	SoundIo			*m_bottom, *m_top;
	SoundIoQueueState	m_bottom_qs, m_top_qs;
	SoundIoPumpConfig	m_config;
	bool			m_running;

	SoundIoFilter		*m_bottom_flt, *m_top_flt;

	bool			m_bottom_async_started, m_top_async_started;
	char			m_bottom_strikes, m_top_strikes;
	bool			m_async_entered;

	uint8_t			m_bo_last[c_sampsize], m_bi_last[c_sampsize];
	uint8_t			m_to_last[c_sampsize], m_ti_last[c_sampsize];

	TimerNotifier		*m_watchdog;

	unsigned int		m_config_out_min_ms;
	unsigned int		m_config_out_window_ms;

	void AsyncProcess(SoundIo *subp, SoundIoQueueState *statep);
	void Watchdog(TimerNotifier *notp);
	void __Stop(bool notify = false, SoundIo *offender = 0);

	static void FillSilence(SoundIoFormat &fmt, uint8_t *dest);

	static sio_sampnum_t CopyIn(uint8_t *dest, SoundIoWorkingState *swsp,
				    sio_sampnum_t nsamps);
	static sio_sampnum_t CopyOut(SoundIoWorkingState *dwsp,
				     const uint8_t *src,
				     sio_sampnum_t nsamps);
	static sio_sampnum_t CopyCross(SoundIoWorkingState *dwsp,
				       SoundIoWorkingState *swsp,
				       sio_sampnum_t nsamps);
	static sio_sampnum_t OutputSilence(SoundIoWorkingState *dwsp,
					   sio_sampnum_t nsamps);

	void ProcessOneWay(SoundIoWorkingState *swsp,
			   SoundIoWorkingState *dwsp,
			   bool up, SoundIoBuffer &buf1,
			   SoundIoBuffer &buf2);

	void ProcessorLoop(SoundIoWorkingState &pws,
			   SoundIoWorkingState &sws,
			   unsigned int npackets);

	bool ConfigureEndpoints(SoundIo *bottom, SoundIo *top,
				SoundIoPumpConfig &cfg);

	static bool PrepareFilter(SoundIoFilter *fltp, SoundIoPumpConfig &cfg);

public:
	/** @brief Standard destructor */
	virtual ~SoundIoPump();

	/**
	 * @brief Standard constructor
	 *
	 * @param eip Loop interface object adapted to the environment
	 * in which SoundIoPump is to run.
	 * @param bottom SoundIo object to reside at the bottom of the
	 * handling stack.
	 * 
	 * @note SoundIoPump does not perform any life cycle management
	 * of its endpoints.  It is the responsibility of the client to
	 * ensure that the SoundIo objects set as endpoints to the pump
	 * remain valid as long as they are associated with the pump.
	 */
	SoundIoPump(DispatchInterface *eip, SoundIo *bottom);

	/**
	 * @brief Notification of halted asynchronous processing
	 *
	 * When asynchronous processing is halted for reasons other than
	 * the Stop() method being invoked -- usually due to a failure
	 * of one of the endpoints, this callback is invoked.
	 *
	 * @param SoundIoPump* Pointer to the SoundIoPump object 
	 * @param SoundIo* Pointer to the SoundIo object that caused
	 * the pump to stop, if known.
	 */
	Callback<void, SoundIoPump*, SoundIo*> cb_NotifyAsyncState;

	DispatchInterface *GetDi(void) const { return m_ei; }

	/**
	 * @brief Query the bottom endpoint
	 *
	 * @return A pointer to the bottom endpoint of the pump, or NULL
	 * if no bottom endpoint is set.
	 */
	SoundIo *GetBottom(void) const { return m_bottom; }

	/**
	 * @brief Set the bottom endpoint
	 *
	 * Configures a SoundIo object as the bottom endpoint of the pump.
	 *
	 * @param bottom SoundIo object to set as the bottom endpoint, or
	 * 0 to clear the bottom endpoint.  If the pump is started, and
	 * the bottom endpoint is cleared, the pump will be stopped.
	 *
	 * @retval true @em bottom was set as the bottom endpoint.
	 * @retval false @em bottom could not be set as the bottom endpoint.
	 * This will only happen if the pump was started when the call
	 * to SetBottom() was made, and the new endpoint could not be
	 * configured.  In this case, the pump state will be unaltered.
	 *
	 * @note SoundIoPump does not perform any life cycle management
	 * of its endpoints.  It is the responsibility of the client to
	 * ensure that the SoundIo objects set as endpoints to the pump
	 * remain valid as long as they are associated with the pump.
	 */
	bool SetBottom(SoundIo *bottom);

	/**
	 * @brief Query the top endpoint
	 *
	 * @return A pointer to the top endpoint of the pump, or NULL
	 * if no top endpoint is set.
	 */
	SoundIo *GetTop(void) const { return m_top; }

	/**
	 * @brief Set the top endpoint
	 *
	 * Configures a SoundIo object as the top endpoint of the pump.
	 *
	 * @param top SoundIo object to set as the top endpoint, or
	 * 0 to clear the top endpoint.  If the pump is started, and
	 * the top endpoint is cleared, the pump will be stopped.
	 *
	 * @retval true @em top was set as the top endpoint.
	 * @retval false @em top could not be set as the top endpoint.
	 * This will only happen if the pump was started when the call
	 * to SetTop() was made, and the new endpoint could not be
	 * configured.  In this case, the pump state will be unaltered.
	 *
	 * @note SoundIoPump does not perform any life cycle management
	 * of its endpoints.  It is the responsibility of the client to
	 * ensure that the SoundIo objects set as endpoints to the pump
	 * remain valid as long as they are associated with the pump.
	 */
	bool SetTop(SoundIo *top);

	/**
	 * @brief Request pump to start
	 *
	 * After the top endpoint has been configured via SetTop(), and
	 * the filters have been installed via AddTop() and AddBottom(),
	 * a pump can be started.
	 *
	 * Both the top and bottom endpoints must be opened and configured
	 * with compatible PCM data stream formats.  They must agree on at
	 * least one transfer direction, i.e. if the top endpoint is
	 * configured for input only, the bottom endpoint must at least be
	 * configured for output.
	 *
	 * @retval true Pump has been prepared and asynchronous operation
	 * enabled at the endpoints.
	 * @retval false Pump could not be started.  Possible reasons:
	 * - No top endpoint configured
	 * - Neither bottom nor top endpoint support asynchronous operation
	 * - One or both of the endpoints has not been opened, see
	 * SoundIo::SndOpen()
	 * - The endpoints do not agree on a transfer direction
	 * - The endpoints are not configured with compatible PCM sample
	 * data formats
	 * - One of the configured filters failed to prepare itself
	 */
	bool Start(void);

	/**
	 * @brief Request pump to stop
	 *
	 * If a pump is operating, this method will cause it to halt
	 * immediately and detach from its endpoints.  The client may
	 * then close the endpoints if desired.
	 *
	 * If a call to this method changes the pump from the operating
	 * state to halted, there will not be a corresponding callback
	 * to cb_NotifyAsyncState.
	 *
	 * @sa Start(), IsStarted()
	 */
	void Stop(void) { __Stop(); }

	/**
	 * @brief Query operation state of pump
	 *
	 * Test whether the pump is actively handling sample data.
	 *
	 * @retval true The pump is operating
	 * @retval false The pump is stopped
	 *
	 * @sa Start(), Stop()
	 */
	bool IsStarted(void) const { return m_running; }

	/**
	 * @brief Query the topmost filter installed in the stack
	 */
	SoundIoFilter *GetTopFilter(void) const
		{ return m_top_flt; }

	/**
	 * @brief Query the bottommost filter installed in the stack
	 */
	SoundIoFilter *GetBottomFilter(void) const
		{ return m_bottom_flt; }

	/**
	 * @brief Query the filter immediately below another installed
	 * in the stack
	 */
	SoundIoFilter *GetBelowFilter(SoundIoFilter *fltp) const
		{ return fltp->m_down; }

	/**
	 * @brief Query the filter immediately above another installed
	 * in the stack
	 */
	SoundIoFilter *GetAboveFilter(SoundIoFilter *fltp) const
		{ return fltp->m_up; }

	/**
	 * @brief Install a filter above an already installed filter
	 *
	 * @param fltp Filter to be added to the stack.
	 * @param targp Filter already in the stack to sit directly
	 * below the given filter, or @c 0 to install the filter at the
	 * bottom of the stack.
	 * @retval true Filter successfully added to the stack
	 * @retval false Filter could not be added.  This may only
	 * occur if the pump is currently active, and the filter
	 * rejects the active configuration of the pump.
	 */
	bool AddAbove(SoundIoFilter *fltp, SoundIoFilter *targp) {
		return AddBelow(fltp, targp ? targp->m_up : m_bottom_flt);
	}

	/**
	 * @brief Install a filter below an already installed filter
	 *
	 * @param fltp Filter to be added to the stack.
	 * @param targp Filter already in the stack to sit directly
	 * above the given filter, or @c 0 to install the filter at the
	 * top of the stack.
	 * @retval true Filter successfully added to the stack
	 * @retval false Filter could not be added.  This may only
	 * occur if the pump is currently active, and the filter
	 * rejects the active configuration of the pump.
	 */
	bool AddBelow(SoundIoFilter *fltp, SoundIoFilter *targp);

	/**
	 * @brief Remove a filter from the stack
	 *
	 * This function will cause a filter installed in the stack to
	 * be removed.  If the pump is active, the filter will be
	 * deconfigured.
	 */
	void RemoveFilter(SoundIoFilter *fltp);

	/**
	 * @brief Install a filter at the topmost position
	 */
	bool AddTop(SoundIoFilter *fltp)
		{ return AddBelow(fltp, 0); }

	/**
	 * @brief Install a filter at the bottommost position
	 */
	bool AddBottom(SoundIoFilter *fltp)
		{ return AddAbove(fltp, 0); }

	/**
	 * @brief Remove the topmost filter
	 */
	SoundIoFilter *RemoveTop(void) {
		SoundIoFilter *fltp = GetTopFilter();
		if (fltp)
			RemoveFilter(fltp);
		return fltp;
	}

	/**
	 * @brief Remove the bottommost filter
	 */
	SoundIoFilter *RemoveBottom(void) {
		SoundIoFilter *fltp = GetBottomFilter();
		if (fltp)
			RemoveFilter(fltp);
		return fltp;
	}

	/**
	 * @brief Query the active minimum output buffer fill level of the
	 * bottom or top endpoint
	 *
	 * @param top Set to @c true to retrieve information about the top
	 * endpoint, @c false for the bottom endpoint.
	 * @return The minimum acceptable output buffer fill level of the
	 * top or bottom endpoint, in milliseconds, depending on the
	 * @c top parameter.  If the pump is inactive, @c -1 will be returned.
	 *
	 * For information about minimum buffer fill levels, see
	 * SetMinBufferFillHint().
	 */
	unsigned int GetMinBufferFill(bool top);

	/**
	 * @brief Query the active output buffer jitter window of the
	 * bottom or top endpoint
	 *
	 * @param top Set to @c true to retrieve information about the top
	 * endpoint, @c false for the bottom endpoint.
	 * @return The acceptable window size of the output buffer fill
	 * level, in milliseconds, of the top or bottom endpoint,
	 * depending on the @c top parameter.  If the pump is inactive,
	 * @c -1 will be returned.
	 *
	 * For information about jitter windows, see SetJitterWindowHint().
	 */
	unsigned int GetJitterWindow(bool top);

	/**
	 * @brief Query the minimum buffer fill level hint value
	 */
	unsigned int GetMinBufferFillHint(void) const
		{ return m_config_out_min_ms; }

	/**
	 * @brief Set the desired minimum output buffer fill level
	 *
	 * This method provides the client some level of control over
	 * the minimum acceptable output buffer fill level.  Choices of
	 * minimum output buffer fill level can have two consequences:
	 * - If it is too small, inconsistent scheduling can cause
	 * underruns and stream interruptions.  If the process running
	 * SoundIoPump is not allowed to run for longer than the
	 * time period represented by the input buffer fill level, an
	 * underrun will almost be guaranteed.
	 * - Larger values increase end-to-end latency and decrease the
	 * perceived quality of a bidirectional stream used for telephony.
	 *
	 * Some situations will cause a client-provided value to be ignored:
	 * - The value is less than twice the packet size of a given endpoint.
	 * In this case it will be rounded up.
	 * - The value is greater than the endpoint buffer size minus one
	 * packet size.  In this case it will be rounded down.
	 *
	 * This value is only applied to clocked endpoints.  It does not
	 * apply to file writers.
	 *
	 * @param ms Length, in milliseconds, of the minimum buffer fill
	 * level to apply.  Setting this parameter to zero will cause the
	 * default logic to be used.
	 *
	 * @note The minimum buffer fill level will be applied the next time
	 * the pump is started with Start().
	 * @note It is highly recommended that the minimum buffer fill level
	 * be set to at least twice the packet size.
	 *
	 * @sa GetMinBufferFillHint(), GetMinBufferFill(),
	 * SetJitterWindowHint()
	 */
	void SetMinBufferFillHint(unsigned int ms)
		{ m_config_out_min_ms = ms; }

	/**
	 * @brief Query the jitter window size hint value
	 */
	unsigned int GetJitterWindowHint(void) const
		{ return m_config_out_window_ms; }

	/**
	 * @brief Set the desired window size on output buffers
	 *
	 * The method provides the client some level of control over the
	 * output window size, i.e. the limit of the fill level of output
	 * buffers above and beyond the minimum.  This value has two
	 * consequences:
	 * - If it is too small, transient inconsistencies in the rates of
	 * production and consumption between the two endpoints ("jitter")
	 * can cause samples to be dropped or silence to be inserted.
	 * - Larger values can increase end-to-end latency, and potentially
	 * decrease the perceived quality of a voice telephony session.
	 *
	 * Some situations will cause a client-provided value to be ignored:
	 * - The value is less than the packet size of a given endpoint.
	 * In this case it will be rounded up.
	 * - The value would cause the total buffering required to exceed the
	 * endpoint's output buffer size.
	 * In this case it will be rounded down.
	 *
	 * This value is only applied to clocked endpoints.  It does not
	 * apply to file writers.
	 *
	 * @param ms Length, in milliseconds, of the desired jitter window.
	 * Setting this parameter to zero will cause the default jitter
	 * window size selection logic to be used.
	 *
	 * @note The jitter window size will be applied the next time
	 * the pump is started with Start().
	 * @note It is highly recommended that the jitter window
	 * be set to larger than the packet size.
	 *
	 * @sa SetMinBufferFillHint()
	 */
	void SetJitterWindowHint(unsigned int ms)
		{ m_config_out_window_ms = ms; }
};


/**
 * @brief Streaming Audio Configuration Manager
 * @ingroup soundio
 *
 * This object supports a local sound card usage model for SoundIoPump.
 * It encapsulates SoundIoPump, which it uses to stream between its
 * @em primary and @em secondary endpoints.  Most configuration details
 * other than those specific to the secondary endpoint are handled
 * internally.
 *
 * The primary endpoint can be configured by driver name and option
 * strings.  A default driver with default options will be used if no
 * options are provided.  Clients need not have any knowledge of available
 * drivers.
 *
 * The primary endpoint is automatically opened and closed, and
 * has its format set to that of the secondary endpoint when the stream
 * is to be started.  Clients need not have any knowledge of sample
 * formats or have any part in configuring packet sizes, although
 * methods exist to set the packet size and buffer fill levels.
 *
 * If no secondary endpoint is provided, the stream is configured for
 * loopback mode.  Loopback mode is useful for qualitatively evaluating
 * latency.
 *
 * This object also supports a streaming mute mode.  This can be enabled at
 * any time using SetMute().  When enabled, a clocked secondary endpoint
 * will continue streaming while the primary endpoint is swapped out and
 * closed.
 */
class SoundIoManager {
private:
	SoundIoPump		m_pump;
	int			m_config_packet_ms;
	SoundIo			*m_primary;
	bool			m_mute_swap;
	bool			m_mute_soft_up, m_mute_soft_dn;
	SoundIoFilter		*m_mute_soft;
	bool			m_top_loop;
	bool			m_primary_open;
	SoundIoFilter		*m_dsp;
	bool			m_dsp_enabled;
	bool			m_dsp_installed;

	char			*m_driver_name;
	char			*m_driver_opts;

	bool			m_stream_up, m_stream_dn;

	void PumpStopped(SoundIoPump *pumpp, SoundIo *offender);
	bool OpenPrimary(bool sink, bool source);
	void ClosePrimary(void);
	SoundIo *CreatePrimary(const char *name, const char *opts);

public:
	/**
	 * @brief Standard constructor
	 */
	SoundIoManager(DispatchInterface *di);

	/**
	 * @brief Standard destructor
	 */
	~SoundIoManager();

	DispatchInterface *GetDi(void) const { return m_pump.GetDi(); }

	/**
	 * @brief Notification of stream halting
	 *
	 * When streaming halts for reasons other than the Stop() method
	 * being invoked -- usually due to a failure of one of the
	 * endpoints, this callback is invoked.
	 *
	 * @param SoundIoManager* Pointer to the SoundIoManager object 
	 */
	Callback<void, SoundIoManager*>		cb_NotifyAsyncState;

	/**
	 * @brief Get descriptive information about a configured audio
	 * driver
	 *
	 * @param index Driver index number to retrieve information
	 * about.
	 * @param name Address of pointer to receive the name of the
	 * driver, or NULL if the name is not desired.
	 * @param desc Address of pointer to receive descriptive text
	 * about the driver, or NULL if descriptive text is not desired.
	 * @param devlist Address of pointer to receive the detected
	 * device list associated with the driver, or NULL of the
	 * detected device list is not desired.
	 *
	 * @note If @em devlist is specified, and the returned
	 * pointer value is 0, this indicates a failed enumeration.
	 * A successful enumeration will return an empty
	 * SoundIoDeviceList object rather than a null pointer.
	 *
	 * @retval true Driver info retrieved.
	 * @retval false Driver index is invalid.
	 */
	static bool GetDriverInfo(int index, const char **name,
				  const char **desc,
				  SoundIoDeviceList **devlist);

	/**
	 * @brief Set the audio driver parameters
	 */
	bool SetDriver(const char *drivername, const char *driveropts);

	const char *GetDriverName(void) const { return m_driver_name; }
	const char *GetDriverOpts(void) const { return m_driver_opts; }

	/**
	 * @brief Query the primary endpoint
	 */
	SoundIo *GetPrimary(void) const { return m_primary; }

	/**
	 * @brief Test whether the primary endpoint can be opened
	 */
	bool TestOpen(bool up = false, bool down = false);

	/**
	 * @brief Query the secondary endpoint
	 */
	SoundIo *GetSecondary(void) const {
		return m_top_loop ? 0 : m_pump.GetTop();
	}

	/**
	 * @brief Set the secondary endpoint
	 */
	bool SetSecondary(SoundIo *secp);

	/**
	 * @brief Configure the secondary endpoint for loopback
	 */
	bool Loopback(void);

	/**
	 * @brief Query whether the primary endpoint is disabled
	 */
	bool GetHardMute(void) const { return m_mute_swap; }

	/**
	 * @brief Temporarily disable and close the primary endpoint
	 *
	 * Hard streaming mute mode allows the secondary endpoint to contine
	 * streaming while the primary is removed.  The primary endpoint
	 * may also be optionally closed.
	 *
	 * This is not recommended for most use cases.  Instead, use the
	 * soft mute feature, SetMute().
	 *
	 * @param state Set to @c true to enable streaming mute mode,
	 * @c false to turn it off.
	 * @param closepri Set to @c true when muting (@em state = @c true)
	 * to cause the primary endpoint to be closed, @c false otherwise.
	 * If the primary endpoint is closed, it will be reopened the next
	 * time the device is unmuted, or started after being stopped and
	 * unmuted.
	 *
	 * @retval true The streaming mute state has been changed.
	 * @retval false The streaming mute state could not be changed.
	 * If the primary endpoint was closed, this could indicate an
	 * error attempting to reopen it.
	 *
	 * @note Stopping and restarting the primary endpoint, and
	 * frequently opening and closing the primary endpoint can be
	 * high latency operations.  A simple ALSA configuration with the
	 * hda-intel driver underneath has been observed to block for
	 * 115ms in its open function, and as much as 40ms in its close
	 * function.
	 */
	bool SetHardMute(bool state, bool closepri = false);


	/**
	 * @brief Query soft mute feature state
	 */
	bool GetMute(bool up = true) const
		{ return up ? m_mute_soft_up : m_mute_soft_dn; }

	/**
	 * @brief Configure soft mute feature
	 *
	 * The soft mute feature replaces all audio packet data moving
	 * in one or both directions with silence made up of the first
	 * sample received.  Soft mute can be instantly enabled/disabled
	 * as it requires no potential hardware operations, and incurs
	 * only minor CPU overhead.
	 *
	 * @param up Set to @c true to enable mute in the upward
	 * direction, i.e. microphone input from the sound card.
	 * @param dn Set to @c true to enable mute in the downward
	 * direction, i.e. speaker output to the sound card.
	 *
	 * @retval true Soft mute successfully configured
	 * @retval false Soft mute not configured -- memory allocation failure
	 *
	 * @sa GetMute(), SetHardMute()
	 */
	bool SetMute(bool up, bool dn = false);

	/**
	 * @brief Set the signal processing filter object
	 */
	bool SetDsp(SoundIoFilter *dspp);

	/**
	 * @brief Enable/disable the DSP filter
	 */
	bool SetDspEnabled(bool enabled = true);

	/**
	 * @brief Query whether the DSP filter is enabled
	 */
	bool IsDspEnabled(void) { return m_dsp_enabled; }

	/**
	 * @brief Request stream to start
	 *
	 * Attempts to start the audio stream.  This function will perform
	 * a number of configuration steps prior to enabling the
	 * underlying SoundIoPump's streaming mechanism.
	 * - If no primary endpoint has been configured via SetDriver(),
	 * one will be configured using default settings.  If this step
	 * fails, the operation will fail.
	 * - If no secondary endpoint has been configured via
	 * SetSecondary(), the loopback endpoint will be configured.
	 * - The primary endpoint will be opened.  If this step fails,
	 * the operation will fail.
	 * - The format configured in the secondary endpoint will be
	 * configured on the primary endpoint.  If the primary
	 * endpoint rejects the format, the operation will fail.
	 *
	 * Then, SoundIoPump::Start() is called, which will:
	 * - Configure buffer fill levels based on configuration set by
	 * SetPacketInterval(), SetMinBufferFill(), and SetJitterWindow().
	 * - Prepare filters that have been configured.
	 * - Enable asynchronous streaming on the endpoints.
	 *
	 * @param up Set to @c true to enable streaming from primary to
	 * secondary, @c false to disable.
	 * @param down Set to @c true to enable streaming from secondary to
	 * primary, @c false to disable.
	 *
	 * @note If both @em up and @em down are set to @c false, the
	 * stream directions will be inferred from the capabilities of the
	 * secondary endpoint.
	 *
	 * @retval true Streaming has been started
	 * @retval false Streaming start failed.  This can be caused by:
	 * - A failure of any of the steps listed above
	 * - The secondary endpoint not being open or properly configured
	 * - Streaming mute mode enabled with an unclocked secondary endpoint
	 * - Streaming already having been started
	 */
	bool Start(bool up = false, bool down = false);

	/**
	 * @brief Request stream to stop
	 *
	 * If the audio stream is running, this method will cause it to
	 * halt immediately and detach from its endpoints.  The client may
	 * then close the endpoints if desired.
	 *
	 * If a call to this method changes the pump from the operating
	 * state to halted, there will not be a corresponding callback
	 * to cb_NotifyAsyncState.
	 *
	 * @sa Start(), IsStarted()
	 */
	void Stop(void);

	/*
	 * doxygen grumbling:
	 * copybrief is unknown?!
	 * copydoc only works before brief?!
	 */

	/**
	 * @copydoc SoundIoPump::IsStarted()
	 * @brief Query operation state of audio stream
	 */
	bool IsStarted(void) const { return m_pump.IsStarted(); }

	/**
	 * @brief Query the topmost filter installed in the stack
	 */
	SoundIoFilter *GetTopFilter(void) const;

	/**
	 * @brief Query the bottommost filter installed in the stack
	 */
	SoundIoFilter *GetBottomFilter(void) const;

	/**
	 * @copydoc SoundIoPump::AddAbove()
	 * @brief Install a filter above an already installed filter
	 */
	bool AddAbove(SoundIoFilter *fltp, SoundIoFilter *targp);

	/**
	 * @copydoc SoundIoPump::AddBelow()
	 * @brief Install a filter below an already installed filter
	 */
	bool AddBelow(SoundIoFilter *fltp, SoundIoFilter *targp);

	/**
	 * @copydoc SoundIoPump::RemoveFilter()
	 * @brief Remove a filter from the stack
	 */
	void RemoveFilter(SoundIoFilter *fltp)
		{ return m_pump.RemoveFilter(fltp); }

	/**
	 * @brief Install a filter at the topmost position
	 */
	bool AddTop(SoundIoFilter *fltp)
		{ return AddBelow(fltp, 0); }

	/**
	 * @brief Install a filter at the bottommost position
	 */
	bool AddBottom(SoundIoFilter *fltp)
		{ return AddAbove(fltp, 0); }

	SoundIoFilter *RemoveTop(void) {
		SoundIoFilter *fltp = GetTopFilter();
		if (fltp)
			RemoveFilter(fltp);
		return fltp;
	}

	SoundIoFilter *RemoveBottom(void) {
		SoundIoFilter *fltp = GetBottomFilter();
		if (fltp)
			RemoveFilter(fltp);
		return fltp;
	}

	/**
	 * @brief Query the configured packet interval of the primary
	 * endpoint
	 */
	unsigned int GetPacketInterval(void);

	/**
	 * @brief Query the active minimum output buffer fill
	 * level of the primary endpoint
	 *
	 * @return The minimum acceptable output buffer fill level of the
	 * primary endpoint.  If the pump is active, the value in use will
	 * be returned, otherwise @c -1 will be returned.
	 */
	unsigned int GetMinBufferFill(void)
		{ return m_pump.GetMinBufferFill(false); }

	/**
	 * @brief Query the active acceptable output buffer fill level
	 * window size of the primary endpoint
	 *
	 * @return The acceptable output buffer fill level window size of the
	 * primary endpoint.  If the pump is active, the value in use will
	 * be returned, otherwise @c -1 will be returned.
	 */
	unsigned int GetJitterWindow(void)
		{ return m_pump.GetJitterWindow(false); }

	/**
	 * @brief Query the desired packet size for the primary endpoint
	 */
	unsigned int GetPacketIntervalHint(void) const
		{ return m_config_packet_ms; }

	/**
	 * @brief Set the desired packet size for the primary endpoint
	 *
	 * The method provides the client some level of control over the
	 * packet size of the primary endpoint, i.e. the interrupt period
	 * length.  This value has two consequences:
	 * - If it is too small, the number of hardware interrupts
	 * serviced per second will become too large, and CPU time
	 * used in the overhead of context switches and interrupt handling
	 * will burden the system.
	 * - Larger values can increase end-to-end latency, and potentially
	 * decrease the perceived quality of a voice telephony session.
	 *
	 * The primary endpoint is the ultimate consumer of this value,
	 * and it is free to reject it or round it as required.  
	 *
	 * @param ms Length, in milliseconds, of the desired packet
	 * interval.  Setting this parameter to zero will cause the
	 * default packet interval selection logic to be used.
	 *
	 * @note The packet interval will be applied the next time
	 * the pump is started with Start().
	 *
	 * @sa GetPacketInterval(), SetMinBufferFillHint(),
	 * SetJitterWindowHint()
	 */
	void SetPacketIntervalHint(unsigned int ms)
		{ m_config_packet_ms = ms; }

	/**
	 * @copydoc SoundIoPump::GetMinBufferFillHint()
	 * @brief Query the desired minimum output buffer fill level
	 */
	unsigned int GetMinBufferFillHint(void) const
		{ return m_pump.GetMinBufferFillHint(); }

	/**
	 * @copydoc SoundIoPump::SetMinBufferFillHint()
	 * @brief Set the desired minimum output buffer fill level
	 */
	void SetMinBufferFillHint(unsigned int ms)
		{ m_pump.SetMinBufferFillHint(ms); }

	/**
	 * @copydoc SoundIoPump::GetJitterWindowHint()
	 * @brief Set the desired window size on output buffers
	 */
	unsigned int GetJitterWindowHint(void) const
		{ return m_pump.GetJitterWindowHint(); }

	/**
	 * @copydoc SoundIoPump::SetJitterWindowHint()
	 * @brief Set the desired window size on output buffers
	 */
	void SetJitterWindowHint(unsigned int ms)
		{ m_pump.SetJitterWindowHint(ms); }
};


/* TODO: clean up this junk! */

SoundIoFilter *SoundIoFltCreateDummy(void);


} /* namespace libhfp */
#endif  /* !defined(__SOUND_IO_H__) */
