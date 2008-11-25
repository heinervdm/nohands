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

/*
 * Using C# here is a hack.  C# provides a set of language-level
 * features that are relatively close to the way that D-Bus interfaces
 * are defined.
 */

/**
 * @mainpage D-Bus API for HFPD
 *
 * The HFP for Linux package provides a service daemon, @c hfpd, that
 * performs most of the low-level functions required to implement the
 * hands-free side of Bluetooth Hands-Free Profile.  The @c hfpd process
 * attaches to the D-Bus session bus, and is entirely controlled via
 * D-Bus messages.  All of its status can be retrieved via D-Bus, and
 * changes to its status are notified through D-Bus signals.
 *
 * The D-Bus APIs are ideally suited for constructing hands-free
 * applications using high-level languages, such as Python, C#, or even
 * Perl.  As a lower level and much more complicated alternative, one
 * may use the <a href="../doxy/index.html">libhfp C++ APIs</a>.
 *
 * The D-Bus APIs described in this document are relatively complete.
 * The hfconsole application is implemented entirely in Python, and uses
 * dbus-python to access D-Bus APIs described in this document.  It does
 * not directly depend on BlueZ, and does not use the BlueZ D-Bus APIs or
 * Python bindings.  The APIs described here, and PyGTK, are the only
 * dependencies of hfconsole in order for it to do its job.
 *
 * @section features HFPD Features
 *
 * - Supports device scanning, connection, disconnection, and automatic
 * reconnection
 * - Supports multiple concurrently connected audio gateway devices
 * - Resilient to loss of Bluetooth service
 * - Supports the ALSA and OSS audio hardware interfaces
 * - Supports audio system test modes
 * - Supports microphone input cleanup, including echo cancellation and
 * noise reduction.
 * - Supports simple recording and playback of stored audio files, e.g.
 * for recording calls and playing ring tones.
 *
 * @section access D-Bus Access
 *
 * A D-Bus client wishing to make use of HFPD must be able to connect to
 * D-Bus, and send and receive messages.  The simplest way to do this is to
 * choose an appropriate D-Bus binding for your language or toolkit.  An
 * <a href="http://www.freedesktop.org/wiki/Software/DBusBindings">
 * updated list of bindings</a> can be found connected to the
 * D-Bus home page.
 *
 * The HFPD process acquires the D-Bus unique name @b @c net.sf.nohands.hfpd .
 *
 * The install target of the HFP for Linux Makefiles will install a
 * D-Bus service description file for HFPD.  This allows dbus-daemon to
 * start HFPD as needed.  D-Bus clients of this API only need to send
 * messages to the HFPD unique name in order for HFPD to be started.
 * D-Bus clients do not need to be concerned with starting and stopping HFPD
 * themselves.
 *
 * Two known object paths are intended as the primary points of access to
 * D-Bus clients:
 * - @b @c /net/sf/nohands/hfpd , with interface net.sf.nohands.hfpd.HandsFree
 * - @b @c /net/sf/nohands/hfpd/soundio , with interface net.sf.nohands.hfpd.SoundIo
 *
 * For each known audio gateway device, an object with interface
 * net.sf.nohands.hfpd.AudioGateway will be instantiated.  A new object
 * of this type can be instantiated for an audio gateway device with a
 * specific Bluetooth address using net.sf.nohands.hfpd.HandsFree.AddDevice(),
 * and the paths of all such objects of this type are enumerated in
 * net.sf.nohands.hfpd.HandsFree.AudioGateways.
 *
 * All interfaces provided by HFPD are introspectable and should be
 * usable with most any language binding.
 *
 * @section property D-Bus Properties
 *
 * HFPD D-Bus objects make extensive use of the standard D-Bus properties
 * interface, @c org.freedesktop.DBus.Properties.  Ideally, one would
 * expect a D-Bus language binding for an object-oriented language to
 * expose properties as simple data members of the proxy objects.
 * For example, in Python, the following would make a lot of sense to
 * set the net.sf.nohands.hfpd.HandsFree.SecMode property on an HFPD
 * instance:
 *
 * @code
 * hfpd = dbus.Interface(
 *	dbus.get_object('net.sf.nohands.hfpd',
 *			'/net/sf/nohands/hfpd'),
 *			dbus_interface = 'net.sf.nohands.hfpd.HandsFree')
 * hfpd.SecMode = 2
 * @endcode
 *
 * Unfortunately, a number of D-Bus language bindings, including
 * dbus-python, skimp on properties and do not provide access to
 * properties in the most transparent possible way.  Depending on the
 * language and bindings package used, a D-Bus client application of HFPD
 * may need to take extra measures to access properties, often manually
 * invoking @c org.freedesktop.DBus.Properties.Get and
 * @c org.freedesktop.DBus.Properties.Set .  For example, the above must
 * instead be implemented as:
 *
 * @code
 * hfpd = dbus.Interface(
 *	dbus.get_object('net.sf.nohands.hfpd',
 *			'/net/sf/nohands/hfpd'),
 *			dbus_interface = 'net.sf.nohands.hfpd.HandsFree')
 * hfpdprop = dbus.Interface(
 *	dbus.get_object('net.sf.nohands.hfpd',
 *			'/net/sf/nohands/hfpd'),
 *			dbus_interface = 'org.freedesktop.DBus.Properties')
 * hfpdprop.Set('net.sf.nohands.hfpd.HandsFree', 'SecMode', 2)
 * @endcode
 *
 * For more information on the standard D-Bus properties interface,
 * consult the
 * <a href="http://dbus.freedesktop.org/doc/dbus-specification.html#standard-interfaces-properties">
 * D-Bus specification</a>.
 *
 * @section clients D-Bus Client Guide
 *
 * The purpose of an HFPD D-Bus client is to implement the very high-level
 * logic of managing audio gateway devices and audio connections, and of
 * course, presenting status information to the user.  It is possible to
 * create a complete D-Bus client for HFPD that is very simple.  Such a
 * program might implement the following:
 * - Keep a private configuration file, and use it to save the Bluetooth
 * address of at least one audio gateway device that it is bound to.
 * - Connect to HFPD using D-Bus, and claim the device(s) remembered
 * in its configuration file using net.sf.nohands.hfpd.HandsFree.AddDevice().
 * - For each claimed device:
 *	- Set the net.sf.nohands.hfpd.AudioGateway.AutoReconnect property
 *	  to @c true.
 *	- Register to receive the
 *	  net.sf.nohands.hfpd.AudioGateway.AudiotateChanged signal, and use
 *	  the signal handler to start streaming audio to and from the audio
 *	  gateway device, using
 *	  net.sf.nohands.hfpd.SoundIo.AudioGatewayStart(), or failing that,
 *	  close the audio connection with
 *	  net.sf.nohands.hfpd.AudioGateway.CloseAudio().
 *	- Register to receive the
 *	  net.sf.nohands.hfpd.AudioGateway.StateChanged and
 *	  net.sf.nohands.hfpd.AudioGateway.CallStateChanged signals, and
 *	  use them to display simple status for the audio gateway.
 *
 * It is also possible to create a D-Bus client that does not claim any
 * devices.  Such a client might maintain a status display, and either
 * depend on another client to claim and manage audio gateway devices, or
 * depend on HFPD's list of permanently known audio gateway devices to
 * permit incoming connections.
 */

namespace net.sf.nohands.hfpd {

	/**
	 * @brief Main access object for Hands-Free Profile functions
	 *
	 * The main access object provides a number of basic functions
	 * useful for implementing hands-free profile.
	 *
	 * The HFPD process will instantiate one object that implements
	 * this interface at path @b @c /net/sf/nohands/hfpd
	 */

	interface HandsFree {
		/**
		 * @brief Attempt to start the Bluetooth service
		 *
		 * This method will attempt to start the Bluetooth
		 * service within the HFPD process.  This includes
		 * binding to a local Bluetooth adapter, opening a socket
		 * to receive incoming HFP service-level connections, and
		 * registering an SDP record.  Unless the service is
		 * started, Bluetooth devices will not observe the local
		 * system as implementing the Hands-Free Profile and
		 * will not be able to use it.
		 *
		 * If the Bluetooth service is already started, this
		 * method will do nothing.
		 *
		 * @throw net.sf.nohands.hfpd.Error Thrown for
		 * uninteresting errors.
		 * @throw net.sf.nohands.hfpd.Error.BtNoKernelSupport
		 * Kernel lacks required Bluetooth support.
		 * @throw net.sf.nohands.hfpd.Error.BtServiceConflict
		 * Another service has claimed the SCO listening socket.
		 * @throw net.sf.nohands.hfpd.Error.BtScoConfigError
		 * The attached HCI has misconfigured SCO settings, which
		 * can only be resolved by a superuser.
		 */
		public Start();

		/**
		 * @brief Shut down the Bluetooth service
		 *
		 * If the Bluetooth service is started, this method will
		 * cause it to be stopped.  Its SDP record will be
		 * deregistered, and its listening Bluetooth sockets will
		 * be closed.
		 *
		 * If the Bluetooth service is already stopped, this
		 * method will do nothing.
		 */
		public Stop();

		/**
		 * @brief Start inquiry (scan for devices)
		 *
		 * Initiates a Bluetooth inquiry to enumerate discoverable
		 * devices.
		 *
		 * When an inquiry is started, it will generate an
		 * InquiryStateChanged() signal.  While it is in progress,
		 * nearby discoverable devices will be identified and
		 * reported to D-Bus clients through the InquiryResult()
		 * signal.  After approximately five seconds, the inquiry
		 * will be terminated, which will cause an
		 * InquiryStateChanged() signal to be generated.
		 *
		 * @throw net.sf.nohands.hfpd.Error Thrown if an
		 * inquiry could not be started, i.e. because one is
		 * already in progress or the Bluetooth service has not
		 * been started.
		 *
		 * @sa StopInquiry(), InquiryStateChanged(), InquiryResult()
		 */
		public StartInquiry();

		/**
		 * @brief Stop an inquiry in progress
		 *
		 * Aborts an in-progress inquiry.  If an inquiry was in
		 * progress when this method was invoked, and was
		 * successfully stopped, an InquiryStateChanged() signal
		 * will be generated.
		 *
		 * @sa StartInquiry(), InquiryStateChanged(), InquiryResult()
		 */
		public StopInquiry();

		/**
		 * @brief Read the Bluetooth name of a given device
		 *
		 * This method executes the read name function on a
		 * given Bluetooth device and returns the name as a
		 * string.
		 *
		 * @param[in] address Address of the Bluetooth device to
		 * read the name of, in colon-separated form, e.g.
		 * "01:23:45:67:89:AB"
		 * @param[out] name Name of the device as read, returned
		 * as a string, e.g. "Motorola Phone"
		 *
		 * @throw net.sf.nohands.hfpd.Error Thrown on any
		 * sort of error, unspecific of the reason of failure.
		 *
		 * @note This method may take an extended period of time
		 * to complete, and should be executed asynchronously.
		 */
		public GetName(in string address, out string name);

		/**
		 * @brief Add a device to HFPD's device list, and claim
		 * ownership of it.
		 *
		 * This method will create a new device record inside
		 * of HFPD for a device with @em address, and will mark
		 * it as owned by the caller's D-Bus client.
		 *
		 * Claimed devices are granted access to specific services
		 * provided by HFPD:
		 * - Claimed devices may open a service-level connection
		 * to HFPD at any time, even when HandsFree.AcceptUnknown
		 * is set to @c false.  The D-Bus client that claimed the
		 * device will be informed of this through the
		 * AudioGateway.StateChanged signal emitted by the object
		 * created to represent the device.
		 * - Claimed devices may open an audio connection at any
		 * time.  They will not be automatically attached to the
		 * audio pump and routed through the local sound card --
		 * this must be explicitly configured by the D-Bus client.
		 *
		 * When HFPD starts, no devices are claimed.  The D-Bus
		 * client must enumerate the list of devices it wishes to
		 * claim each time it connects to HFPD through D-Bus,
		 * using this method.
		 *
		 * Only one D-Bus client is allowed to claim each device.
		 * If a request is made to claim a device that has already
		 * been claimed by a different D-Bus client, the request
		 * will fail.
		 *
		 * HFPD monitors D-Bus disconnections.  When a D-Bus client
		 * that has claimed devices is disconnected, its claimed
		 * devices are automatically released and reverted to the
		 * unclaimed state.  The following set of actions will be
		 * taken on devices claimed by disconnected D-Bus clients:
		 * - If the device has an audio connection open, the audio
		 * connection will be closed, unless HandsFree.VoicePersist
		 * is set to @c true.
		 * - If the device is connecting or connected, and is not
		 * permanently known to HFPD, and does not have a persisting
		 * audio connection as above, it will be disconnected.
		 *
		 * @param[in] address Bluetooth address of the target device,
		 * in colon-separated form, e.g. "01:23:45:67:89:AB"
		 * @param[in] set_known Set to @c true to mark the device as
		 * permanently known to HFPD, so that it may connect
		 * to HFPD and open an audio channel when not claimed by a
		 * D-Bus client.  This mark will be saved to HFPD's
		 * configuration file and will be persistent.
		 * @param[out] object D-Bus path to the AudioGateway
		 * object created to represent the device.
		 *
		 * @throw net.sf.nohands.hfpd.Error Thrown on any
		 * sort of error, unspecific of the reason of failure.
		 *
		 * @sa RemoveDevice()
		 */
		public AddDevice(in string address, in bool set_known,
				 out objectpath object);

		/**
		 * @brief Release a claim on a device
		 *
		 * For devices that have been claimed by a given D-Bus
		 * client, this method releases the claim on the device
		 * and disconnects the device if it is connected.
		 *
		 * @param[in] address Bluetooth address of the device to
		 * have its claim released, in colon-separated form, e.g.
		 * "01:23:45:67:89:AB"
		 *
		 * @throw net.sf.nohands.hfpd.Error Thrown on any
		 * sort of error, unspecific of the reason of failure.
		 *
		 * This method will fail if it is provided the address
		 * of an unknown device, or a device claimed by a
		 * different D-Bus client.
		 *
		 * @sa AddDevice()
		 */
		public RemoveDevice(in string address);

		/**
		 * @brief Save persistent properties to the HFPD
		 * configuration file
		 *
		 * This method will cause the HFPD configuration file,
		 * located at the path in HandsFree.SaveFile,
		 * to be rewritten and filled with the current set of
		 * non-default values for configuration options.
		 *
		 * It is not necessary to call this method if the
		 * HandsFree.AutoSave property is set to @c true.
		 *
		 * @throw net.sf.nohands.hfpd.Error Thrown if
		 * the configuration file could not be created or
		 * overwritten.
		 */
		public SaveSettings();

		/**
		 * @brief Interface version provided by HFPD.
		 *
		 * This property can be accessed using the
		 * @ref property "standard D-Bus property interface".
		 *
		 * The D-Bus interface provided by HFPD is subject to
		 * changes and evolution.  This property provides a fast
		 * way for a D-Bus client to determine whether the HFPD
		 * it is attempting to use implements the D-Bus interfaces
		 * that it expects.  If a client reads this property and
		 * finds an unexpected value, it should fail and warn about
		 * a version mismatch.
		 *
		 * This document describes version 1 of the HFPD D-Bus
		 * interface.
		 *
		 * A version number value in this field covers all HFPD
		 * D-Bus interfaces, including HandsFree, SoundIo, and
		 * AudioGateway.
		 */
		const uint32 Version;

		/**
		 * @brief Current state of Bluetooth system inside of HFPD
		 *
		 * This property can be accessed using the
		 * @ref property "standard D-Bus property interface".
		 *
		 * If @c true, the Bluetooth system has been started and
		 * is usable.
		 *
		 * If @c false, the Bluetooth system is stopped.
		 *
		 * @sa SystemStateChanged()
		 */
		const bool SystemState;

		/**
		 * @brief Set of object paths for known AudioGateway objects
		 *
		 * This property can be accessed using the
		 * @ref property "standard D-Bus property interface".
		 *
		 * This property contains a list of the object paths of all
		 * AudioGateway objects known to HFPD.  It can be used to
		 * enumerate known audio gateway devices.
		 *
		 * Changes to this property are indicated by
		 * AudioGatewayAdded() and AudioGatewayRemoved() signals.
		 *
		 * The D-Bus signature of this property is @c "ao".
		 */
		const objectpath AudioGateways[];

		/**
		 * @brief Configuration file auto-save property
		 *
		 * This property can be accessed using the
		 * @ref property "standard D-Bus property interface".
		 *
		 * If @c true, changes to persistent configuration options
		 * will be automatically written to the local HFPD
		 * configuration file when they are modified.
		 *
		 * If @c false, changes to persistent configuration options
		 * must be explicitly written to the local HFPD configuration
		 * file in order to persist.  This is done using
		 * SaveSettings().  This is default.  It makes the most
		 * sense for frontends such as hfconsole that use
		 * configuration dialogs, and expect to apply new settings
		 * immediately but save them to the configuration file
		 * only when the user clicks "OK."
		 *
		 * @note AutoSave is a persistent option that is, itself,
		 * saved to the HFPD configuration file.
		 */
		bool AutoSave;

		/**
		 * @brief Path and name of the HFPD local configuration file
		 *
		 * This property can be accessed using the
		 * @ref property "standard D-Bus property interface".
		 *
		 * This value identifies the configuration file that
		 * HFPD uses to store local settings.  It defaults to
		 * @c "~/.hfpdrc"
		 *
		 * @sa SaveSettings()
		 */
		string SaveFile;

		/**
		 * @brief Property to control periodic restart attempts
		 * for the Bluetooth service
		 *
		 * This property can be accessed using the
		 * @ref property "standard D-Bus property interface".
		 *
		 * When the Bluetooth service cannot be started, e.g.
		 * because there is no dongle plugged in, this property
		 * enables periodic attempts to start the Bluetooth
		 * service.
		 *
		 * If set to @c true, periodic auto-restart is enabled.
		 * This is the default value.
		 *
		 * If set to @c false, periodic auto-restart is disabled.
		 *
		 * @note AutoRestart will be automatically set to @c false
		 * when certain severe errors are encountered.  These
		 * include:
		 * - Lack of sufficient kernel support for Bluetooth
		 * - Improper SCO MTU values or voice settings on the HCI
		 * @note AutoRestart is a persistent option that is
		 * saved to the HFPD configuration file.
		 */
		bool AutoRestart;

		/**
		 * @brief Security mode for HFP service-level connections
		 *
		 * This property can be accessed using the
		 * @ref property "standard D-Bus property interface".
		 *
		 * This property allows the D-Bus client to control
		 * the security requirements placed on RFCOMM
		 * service-level connections for HFP.  It may be set to
		 * one of three values:
		 *
		 * - 0 = No security requirements.
		 * - 1 = PIN authentication required.  Most audio gateway
		 * devices will impose this requirement anyway.
		 * - 2 = PIN authentication and encryption required.
		 *
		 * Changes to this value affect new connections only,
		 * existing connections are not affected.
		 *
		 * The Bluetooth Hands-Free Profile specification version 1.5
		 * does not place any specific requirements on the
		 * security of RFCOMM service-level connections for HFP.
		 * This value defaults to 1 (PIN authentication).
		 *
		 * @note SecMode is a persistent option that is saved to
		 * the HFPD configuration file.
		 */
		byte SecMode;

		/**
		 * @brief Property controlling whether unknown devices
		 * are allowed to initiate service-level connections
		 *
		 * This property can be accessed using the
		 * @ref property "standard D-Bus property interface".
		 *
		 * If @c true, devices not claimed by a D-Bus client and
		 * not marked permanently known are permitted to initiate
		 * service-level connections to hfpd.
		 *
		 * If @c false, incoming service-level connections from
		 * devices not claimed by a D-Bus client and not marked
		 * permanently known are refused.  This is the default.
		 *
		 * In order for an unknown device to initiate pairing and
		 * make itself known to a D-Bus client, this value must be
		 * set to @c true.  In this case, D-Bus clients will be
		 * informed of inbound connections from previously unknown
		 * devices by the AudioGatewayAdded() signal.  Otherwise,
		 * all new device pairing must be initiated by D-Bus
		 * clients, typically using StartInquiry() and AddDevice().
		 *
		 * @note AcceptUnknown is a persistent option that is saved
		 * to the HFPD configuration file.
		 */
		bool AcceptUnknown;

		/**
		 * @brief Property controlling handling of audio connections
		 * for unclaimed devices
		 *
		 * This property can be accessed using the
		 * @ref property "standard D-Bus property interface".
		 *
		 * When the D-Bus owner of a claimed device is disconnected
		 * from D-Bus, often because a defect in the D-Bus client
		 * caused it to crash, normally all of its claimed devices
		 * will be immediately disconnected.  If one of the devices
		 * has its audio connection configured and routed through
		 * the local sound card, it may be undesirable to disconnect
		 * it immediately.  This property controls special handling
		 * of devices in the aforementioned situation.
		 *
		 * If @c true, the device will remain connected until its
		 * audio connection is dropped, either due to the device
		 * itself dropping the connection, or a sound card failure.
		 * When the audio connection is dropped, the device will be
		 * disconnected immediately.
		 *
		 * If @c false, the device will be disconnected immediately.
		 * Audio will be rerouted through the device itself.
		 *
		 * @note VoicePersist is a persistent option that is
		 * saved to the HFPD configuration file.
		 */
		bool VoicePersist;

		/**
		 * @brief Property controlling handling of audio connections
		 * from unclaimed but permanently known devices
		 *
		 * This property can be accessed using the
		 * @ref property "standard D-Bus property interface".
		 *
		 * When a permanently known device connects to HFPD, it
		 * may do so outside the supervision of a D-Bus client.
		 * Normally, the D-Bus client is responsible for
		 * noticing when a device initiates an audio connection,
		 * and for either routing the audio connection to the
		 * local sound card or immeidately dropping the audio
		 * connection.  It may be desirable for HFPD to automatically
		 * try to route audio connections initiated by permanently
		 * known devices.  This property controls special handling
		 * of devices in the aforementioned situation.
		 *
		 * If @c true, HFPD will attempt to automatically route
		 * incoming audio connections from permanently known,
		 * unclaimed devices to the local sound card.  If routing
		 * fails, the audio connection to the device will be closed.
		 *
		 * If @c false, HFPD will refuse incoming audio connections
		 * from permanently known, unclaimed devices.  For security,
		 * this is the default value.
		 *
		 * @note VoiceAutoConnect is a persistent option that is
		 * saved to the HFPD configuration file.
		 */
		bool VoiceAutoConnect;

		/**
		 * @brief Hands-free capability bit field reported to
		 * audio gateway devices when handshaking
		 *
		 * This property can be accessed using the
		 * @ref property "standard D-Bus property interface".
		 *
		 * The Bluetooth Hands-Free Profile specification identifies
		 * a supported features bit field that is presented by the
		 * hands-free to the audio gateway device when initiating
		 * a service level connection.  This property allows
		 * D-Bus clients access to the reported supported features.
		 *
		 * This field can be used to inform devices not to
		 * expect the hands-free to support specific features,
		 * such as voice recognition activation and volume control.
		 *
		 * Most D-Bus clients should not need to change the
		 * value of this property.
		 */
		uint32 ReportCapabilities;

		/**
		 * @brief Service name string set in HFPD's SDP record.
		 *
		 * This property can be accessed using the
		 * @ref property "standard D-Bus property interface".
		 *
		 * The SDP record created by HFPD has a customizable service
		 * name string.  It is not generally end-user visible,
		 * but can be queried using @c "sdptool records local".
		 * The default value is @c "Handsfree".
		 */
		string ServiceName;

		/**
		 * @brief Service description string set in HFPD's
		 * SDP record.
		 *
		 * This property can be accessed using the
		 * @ref property "standard D-Bus property interface".
		 *
		 * The SDP record created by HFPD has a customizable service
		 * description string.  It is not generally end-user visible,
		 * but can be queried using @c "sdptool records local".
		 * By default, the description field is empty.
		 */
		string ServiceDesc;

		/**
		 * @brief Notification of Bluetooth system state change
		 *
		 * This signal is sent whenever the Bluetooth system state
		 * changes, e.g. when it transitions from started to
		 * stopped due to the unplugging of a Bluetooth dongle.
		 *
		 * @param[out] state The parameter is set to @c true if the
		 * Bluetooth system transitioned to the started state,
		 * @c false if it transitioned to the stopped state.
		 *
		 * @sa Start(), Stop().
		 */
		public signal SystemStateChanged(out bool state);

		/**
		 * @brief Notification of Bluetooth inquiry state change
		 *
		 * This signal is sent whenever a Bluetooth inquiry
		 * is started or completed.
		 *
		 * @param[out] state The parameter is set to @c true if a
		 * Bluetooth inquiry has been started, @c false if a
		 * running Bluetooth inquiry has completed or has been
		 * terminated by StopInquiry().
		 *
		 * @sa StartInquiry(), StopInquiry(), InquiryResult()
		 */
		public signal InquiryStateChanged(out bool state);

		/**
		 * @brief Notification of newly-discovered Bluetooth
		 * inquiry result
		 *
		 * This signal is sent whenever a previously started
		 * Bluetooth inquiry discovered a new, unique device.
		 *
		 * @param[out] address The Bluetooth address of the newly
		 * discovered device, formatted as a colon-separated
		 * string, e.g. "01:23:45:67:89:AB"
		 * @param[out] device_class The reported Bluetooth device
		 * class of the newly discovered device.  This can be
		 * used to filter devices, e.g. to display only those
		 * that implement audio gateway functionality.
		 *
		 * @sa StartInquiry(), StopInquiry(), InquiryStateChanged()
		 */
		public signal InquiryResult(out string address,
					    out uint32 device_class);

		/**
		 * @brief Notification of newly-created AudioGateway
		 * object
		 *
		 * When a D-Bus client claims a previously unknown device,
		 * and when a previously unknown device initiates an
		 * inbound service level connection, this signal is emitted.
		 *
		 * The new AudioGateway object is also added to
		 * HandsFree.AudioGateways.
		 *
		 * @param object D-Bus path to new AudioGateway object.
		 */
		public signal AudioGatewayAdded(out objectpath object);

		/**
		 * @brief Notification of destroyed AudioGateway
		 * object
		 *
		 * When no reasons remain to retain an AudioGateway object,
		 * it is removed from the list of known AudioGateway
		 * objects and its D-Bus object is destroyed.  This signal
		 * informs of such an event.
		 *
		 * An AudioGateway object can be removed when:
		 * - It is not claimed by any D-Bus client
		 * - It is not marked permanently known to HFPD
		 * - It does not have a persisting audio connection
		 *
		 * The removed AudioGateway object is also removed from
		 * HandsFree.AudioGateways.
		 *
		 * @param object D-Bus path to removed AudioGateway object.
		 */
		public signal AudioGatewayRemoved(out objectpath object);
	}

	/**
	 * @brief Interface for managing HFPD's audio data pump
	 *
	 * The SoundIo interface allows a D-Bus client to control
	 * settings related to the local sound card, routing of audio
	 * packets from AudioGateway devices, the software digital
	 * signal processor, and other audio-related functions provided
	 * by HFPD.
	 *
	 * The HFPD process will instantiate one object that implements
	 * this interface at path @b @c /net/sf/nohands/hfpd/soundio
	 */
	interface SoundIo {
		/**
		 * @brief Set the local sound card driver and options
		 * used by the SoundIo
		 *
		 * @param[in] drivername Name of the sound driver to use.
		 * Available drivers can be enumerated by reading the
		 * SoundIo.Drivers property.
		 * @param[in] driveropts Options string for the driver.
		 * Options strings are specific to each driver, but
		 * specifying an empty options string will cause the
		 * driver to use intelligent defaults.
		 *
		 * @note The sound card driver settings are persistent
		 * options that are saved to the HFPD configuration file.
		 * @note On success, @c drivername will be assigned to the
		 * SoundIo.DriverName property, and @c driveropts to the
		 * SoundIo.DriverOpts property.
		 */
		public SetDriver(in string drivername, in string driveropts);

		/**
		 * @brief Probe for local sound card devices available
		 * from a specific driver
		 *
		 * This method actively searches for available sound
		 * card device names, and returns the list.  For most
		 * drivers, one of the return values of this method can
		 * be supplied as the @c driveropts parameter to
		 * SetDriver().
		 *
		 * @param[in] drivername Name of audio driver to probe.
		 * Available drivers can be enumerated by reading the
		 * SoundIo.Drivers property.
		 * @param[out] found_devices Array of string pairs
		 * describing the result of the device probe.  This
		 * field has D-Bus signature @c "a(ss)".
		 * - The first subscript of each array element is
		 * the name of the device as specifiable to SetDriver(),
		 * e.g. @c "/dev/dsp"
		 * - The second subscript of each array element is a
		 * human-readable description of the device, if available,
		 * e.g. @c "Intel HD Audio"
		 *
		 * @throw net.sf.nohands.hfpd.Error Thrown on any
		 * sort of error, unspecific of the reason of failure.
		 */
		public ProbeDevices(in string drivername,
				    out string[2][] found_devices);

		/**
		 * @brief Stop any in-progress audio streaming through
		 * the local sound card.
		 *
		 * This method will cause an active audio stream to be
		 * halted, regardless of the configured endpoint.  If
		 * streaming is not in progress, this method succeeds
		 * and takes no action.
		 *
		 * If the configured endpoint is an AudioGateway, the
		 * audio connection to the device will be closed.
		 */
		public Stop();

		/**
		 * @brief Start an audio stream with an AudioGateway
		 * device.
		 *
		 * This method attempts to start a full duplex audio
		 * stream with an AudioGateway device.
		 *
		 * If an audio connection to the device is already open,
		 * streaming will start immediately.  If the audio
		 * connection is not open, and the @c initiate_connection
		 * parameter is @c true, an attempt will be made to
		 * initiate the connection, and if successful, streaming
		 * will be started.
		 *
		 * If the local sound card is already streaming, or
		 * preparing to stream with a different audio gateway
		 * device, the operation will be aborted similar to
		 * Stop().
		 *
		 * @param[in] audio_gateway Object path of audio gateway
		 * device on which to start streaming.
		 * @param[in] initiate_connection Set to @c true to
		 * allow an audio connection to the device to be
		 * initiated if needed, @c false to only use an existing
		 * audio connection to the device.
		 *
		 * @throw net.sf.nohands.hfpd.Error Thrown on any
		 * sort of error, unspecific of the reason of failure.
		 *
		 * Audio gateway devices will typically perform all
		 * of the audio connection initiation themselves, and
		 * D-Bus clients should not need to direct HFPD to initiate
		 * audio connections.  D-Bus clients can be informed of
		 * remotely-initiated audio connections by the
		 * AudioGateway.AudioStateChanged() signal, and should
		 * typically invoke this method (AudioGatewayStart) on
		 * reception of that signal.
		 */
		public AudioGatewayStart(in objectpath audio_gateway,
					 in bool initiate_connection);

		/**
		 * @brief Start a WAV file audio stream
		 *
		 * Opens a .WAV file and initiates unidirectional
		 * streaming either from the file to the primary sound
		 * card or from the primary sound card to the file.
		 *
		 * This mode of streaming is useful for playing a
		 * ring tone, or for recording a voice note.
		 *
		 * @param[in] file_path Path to WAV file to be created
		 * and streamed to, or existing WAV file to be played
		 * back.
		 * @param[in] write Set to @c true to cause the file to
		 * be streamed into and created if necessary, @c false
		 * to play back from the file.
		 *
		 * If the local sound card is already streaming, or
		 * preparing to stream with an audio gateway, the
		 * operation in progress will be aborted similar to
		 * Stop().
		 *
		 * @throw net.sf.nohands.hfpd.Error Thrown on any
		 * sort of error, unspecific of the reason of failure.
		 */
		public FileStart(in string file_path, in bool write);

		/**
		 * @brief Start a loopback audio stream
		 *
		 * Starts streaming the input from the local sound card
		 * to the output to the local sound card.
		 *
		 * This mode of streaming is useful for testing the local
		 * sound card configuration and to subjectively evaluate
		 * the latency resulting from the buffering settings.
		 *
		 * If the local sound card is already streaming, or
		 * preparing to stream with a different audio gateway
		 * device, the operation will be aborted similar to
		 * Stop().
		 *
		 * @throw net.sf.nohands.hfpd.Error Thrown on any
		 * sort of error, unspecific of the reason of failure.
		 */
		public LoopbackStart();

		/**
		 * @brief Start streaming to and/or from a memory buffer
		 *
		 * A memory buffer object can be used to store small
		 * durations of audio data captured from the local sound
		 * card, and to conveniently play it back.  Specifically,
		 * the memory buffer is useful for testing digital
		 * signal processing settings by simulating a two-way
		 * conversation without initiating a phone call.
		 *
		 * If the local sound card is already streaming, or
		 * preparing to stream with a different audio gateway
		 * device, the operation will be aborted similar to
		 * Stop().
		 *
		 * @param[in] capture Set to @c true to capture audio data
		 * from the local sound card into the memory buffer.
		 * If true, an existing capture buffer will be shifted to
		 * the playback buffer position.
		 * @param[in] playback Set to @c true to play back from the
		 * playback buffer.  If no playback buffer exists, nothing
		 * will be played back.
		 * @param[in] membuf_size Size of the memory buffer, in
		 * 8KHz samples.  For example, to get enough space to
		 * record up to 10 seconds, this value should be 80000.
		 * @param[in] report_interval Number of samples to wait
		 * between sending MonitorNotify() signals while streaming.
		 * Set to zero to disable MonitorNotify() reporting.
		 * Setting this value too small may overload the D-Bus
		 * daemon process.
		 *
		 * @throw net.sf.nohands.hfpd.Error Thrown on any
		 * sort of error, unspecific of the reason of failure.
		 */
		public MembufStart(in bool capture, in bool playback,
				   in uint32 membuf_size,
				   in uint32 report_interval);
		
		/**
		 * @brief Drop the contents of an existing memory buffer.
		 *
		 * HFPD retains the contents of its memory buffer.  This
		 * method can be used to reset the contents of the existing
		 * memory buffer in preparation for a new test, or out of
		 * general concern for resource allocations within HFPD.
		 */
		public MembufClear();

		/**
		 * @brief Configure a WAV file to receive audio pump
		 * traffic
		 *
		 * The HFPD audio handling components support snooping
		 * the stream traffic to a WAV file.  This can be used
		 * to create voice notes and recordings of telephony
		 * sessions.  Each time the stream is started, if a snoop
		 * file is configured, it is created, or truncated if it
		 * already exists, and filled with stream data as it is
		 * received.
		 *
		 * The snoop file can be configured to receive audio data
		 * from one or both directions of the stream.  If configured
		 * to receive both directions, the audio data will be
		 * mixed before being saved to the target file.
		 *
		 * @param[in] filename Path and name of snoop file.  If
		 * this value is zero-length, snooping is disabled.
		 * @param[in] capture If @c true, data captured from the
		 * local sound card will be snooped.
		 * @param[in] playback If @c true, data played through the
		 * local sound card will be snooped.
		 * 
		 * @note If the contents of the snoop file are to be
		 * preserved, the snoop file should be deconfigured after
		 * streaming has been stopped to avoid having the file
		 * overwritten the next time streaming is started.
		 * @note Check local laws before creating recordings of
		 * telephone calls.
		 *
		 * @sa SoundIo.SnoopFileName
		 */
		public SetSnoopFile(in string filename,
				    in bool capture, in bool playback);

		/**
		 * @brief State of the SoundIo object
		 *
		 * This property can be accessed using the
		 * @ref property "standard D-Bus property interface".
		 *
		 * This property describes the state of the SoundIo
		 * object, including whether the local sound card is
		 * available, and if so, what type of secondary endpoint
		 * is configured, if any.  Values include:
		 *
		 * - 1 = Local sound card deconfigured
		 * - 2 = Stopped, not streaming
		 * - 3 = Audio gateway configured but awaiting connection
		 * - 4 = Audio gateway connected and streaming
		 * - 5 = File endpoint streaming
		 * - 6 = Loopback endpoint streaming
		 * - 7 = Memory buffer endpoint streaming
		 *
		 * The state cannot be modified directly.  Instead use the
		 * methods Stop(), AudioGatewayStart(), FileStart(),
		 * LoopbackStart(), and MembufStart().
		 */
		const byte State;

		/**
		 * @brief Mute state of the SoundIo object
		 *
		 * This property can be accessed using the
		 * @ref property "standard D-Bus property interface".
		 *
		 * This property controls the software mute feature of
		 * the SoundIo object.  When set to @c true, audio data
		 * captured from the local sound card is replaced with
		 * silence.
		 *
		 * Changing this property will cause a notification to
		 * be sent to all interested D-Bus clients through the
		 * MuteChanged() signal.
		 */
		bool Mute;

		/**
		 * @brief Configured audio gateway endpoint
		 *
		 * This property can be accessed using the
		 * @ref property "standard D-Bus property interface".
		 *
		 * The D-Bus type of this property is a variant, and
		 * its real type depends on the state of the SoundIo
		 * object.
		 *
		 * If the SoundIo object has an audio gateway object
		 * configured as its endpoint, either actively streaming
		 * or awaiting a connection, this property can be used
		 * to retrieve the object path of the AudioGateway.  In
		 * this case, its real type is a D-Bus object path.
		 *
		 * If the SoundIo object is stopped, or streaming with
		 * another type of endpoint, this property will contain
		 * the boolean value @c false.
		 *
		 * This property cannot be modified directly.  Instead
		 * use AudioGatewayStart() or Stop().
		 */
		const variant AudioGateway;

		/**
		 * @brief Target file for stream snooper
		 *
		 * See SetSnoopFile().
		 */
		const string SnoopFileName;

		/**
		 * @brief List of available local sound card drivers
		 *
		 * This property can be accessed using the
		 * @ref property "standard D-Bus property interface".
		 *
		 * This property allows a D-Bus client to interrogate the
		 * sound card driver back-ends that are available to HFPD.
		 * Typical values include ALSA and OSS.
		 *
		 * - The first subscript of each array element is the
		 * name of the driver backend as can be specified as a
		 * parameter to SetDriver(), e.g. @c "ALSA"
		 * - The second subscript of each array element is a
		 * human-readable description of the backend, e.g.
		 * @c "Advanced Linux Sound Architecture back-end"
		 */
		const string Drivers[2][];

		/**
		 * @brief Name of active local sound card driver
		 *
		 * This property can be accessed using the
		 * @ref property "standard D-Bus property interface".
		 *
		 * This property contains the name of the active local
		 * sound card driver back-end.
		 *
		 * To set this value, use SetDriver().
		 */
		const string DriverName;

		/**
		 * @brief Options provided to active local sound card driver
		 *
		 * This property can be accessed using the
		 * @ref property "standard D-Bus property interface".
		 *
		 * This property contains the options provided to the
		 * active local sound card driver back-end.
		 *
		 * To set this value, use SetDriver().
		 */
		const string DriverOpts;

		/**
		 * @brief Active sample packet interval in use by the local
		 * sound card
		 *
		 * This property can be accessed using the
		 * @ref property "standard D-Bus property interface".
		 *
		 * When the local sound card is streaming, this property
		 * can be used to retrieve the packet interval used by the
		 * local sound card, in milliseconds.  The packet interval
		 * is the interval between hardware interrupts, or the
		 * closest equivalent depending on the resampling and mixing
		 * operations that are being carried out.
		 *
		 * D-Bus clients cannot force a specific packet interval,
		 * but can provide a hint through the
		 * SoundIo.PacketIntervalHint property.
		 *
		 * When the local sound card is not streaming, the value is
		 * meaningless and returned as zero.
		 */
		const uint32 PacketInterval;

		/**
		 * @brief Active minimum acceptable buffer fill level in
		 * use by the local sound card
		 *
		 * This property can be accessed using the
		 * @ref property "standard D-Bus property interface".
		 *
		 * When the local sound card is streaming, this property
		 * can be used to retrieve the chosen minimum buffer fill
		 * level of the local sound card, in milliseconds.
		 *
		 * D-Bus clients cannot force a specific minimum buffer
		 * fill level, but can provide a hint through the
		 * SoundIo.MinBufferFillHint property.
		 *
		 * When the local sound card is not streaming, the value is
		 * meaningless and returned as zero.
		 */
		const uint32 MinBufferFill;

		/**
		 * @brief Active jitter window in use by the local sound card
		 *
		 * This property can be accessed using the
		 * @ref property "standard D-Bus property interface".
		 *
		 * When the local sound card is streaming, this property
		 * can be used to retrieve the chosen jitter window of the
		 * local sound card, in milliseconds.
		 *
		 * D-Bus clients cannot force a specific jitter window,
		 * but can provide a hint through the
		 * SoundIo.JitterWindowHint property.
		 *
		 * When the local sound card is not streaming, the value is
		 * meaningless and returned as zero.
		 */
		const uint32 JitterWindow;

		/**
		 * @brief Hint on the packet interval to be use with the
		 * local sound card
		 *
		 * This property can be accessed using the
		 * @ref property "standard D-Bus property interface".
		 *
		 * This property provides the client some level of control
		 * over the packet size of the primary endpoint, i.e. the
		 * interrupt period length.  This value has two consequences:
		 * - If it is too small, the number of hardware interrupts
		 * serviced per second will become too large, and CPU time
		 * used in the overhead of context switches and interrupt
		 * handling will burden the system.
		 * - Larger values can increase end-to-end latency, and
		 * potentially decrease the perceived quality of a voice
		 * telephony session.
		 *
		 * The local sound card driver is the ultimate consumer of
		 * this value, and it is free to reject it or round it as
		 * required.  
		 *
		 * The value is specified in milliseconds.  Setting it to
		 * zero will cause the default packet interval selection
		 * logic to be used.
		 *
		 * @note The packet interval hint is a persistent option
		 * that is saved to the HFPD configuration file.
		 */
		uint32 PacketIntervalHint;

		/**
		 * @brief Hint on the minimum acceptable buffer fill level
		 * to be use with the local sound card
		 *
		 * This property can be accessed using the
		 * @ref property "standard D-Bus property interface".
		 *
		 * This property provides the client some level of control over
		 * the minimum acceptable output buffer fill level.  Choices of
		 * minimum output buffer fill level can have two consequences:
		 * - If it is too small, inconsistent scheduling can cause
		 * underruns and stream interruptions.  If the process running
		 * SoundIoPump is not allowed to run for longer than the
		 * time period represented by the input buffer fill level, an
		 * underrun will almost be guaranteed.
		 * - Larger values increase end-to-end latency and decrease the
		 * perceived quality of a bidirectional stream used for
		 * telephony.
		 *
		 * Some situations will cause a client-provided value to be
		 * ignored:
		 * - The value is less than twice the packet size of a given
		 * endpoint.  In this case it will be rounded up.
		 * - The value is greater than the endpoint buffer size minus
		 * one packet size.  In this case it will be rounded down.
		 *
		 * The value is specified in milliseconds.  Setting it to
		 * zero will cause default minimum buffer fill level logic
		 * to be used to determine its value.
		 *
		 * @note The minimum acceptable buffer fill level hint is a
		 * persistent option that is saved to the HFPD configuration
		 * file.
		 */
		uint32 MinBufferFillHint;

		/**
		 * @brief Hint on the minimum acceptable buffer fill level
		 * to be use with the local sound card
		 *
		 * This property can be accessed using the
		 * @ref property "standard D-Bus property interface".
		 *
		 * The property provides the client some level of control
		 * over the output window size, i.e. the limit of the fill
		 * level of output buffers above and beyond the minimum.
		 * This value has two consequences:
		 * - If it is too small, transient inconsistencies in the
		 * rates of production and consumption between the two
		 * endpoints ("jitter") can cause samples to be dropped or
		 * silence to be inserted.
		 * - Larger values can increase end-to-end latency, and
		 * potentially decrease the perceived quality of a voice
		 * telephony session.
		 *
		 * Some situations will cause a client-provided value to be
		 * ignored:
		 * - The value is less than the packet size of a given
		 * endpoint.  In this case it will be rounded up.
		 * - The value would cause the total buffering required to
		 * exceed the endpoint's output buffer size.  In this case it
		 * will be rounded down.
		 *
		 * The value is specified in milliseconds.  Setting it
		 * to zero will cause the default jitter window size
		 * selection logic to be used.
		 *
		 * @note The jitter window hint is a persistent option
		 * that is saved to the HFPD configuration file.
		 */
		uint32 JitterWindowHint;

		/**
		 * @brief Digital signal processor denoise setting
		 *
		 * This property can be accessed using the
		 * @ref property "standard D-Bus property interface".
		 *
		 * This option controls whether the denoise feature of the
		 * Speex software digital signal processor is used.  The
		 * denoise feature can eliminate background noise and
		 * improve the focus on the voice of the speaker.
		 *
		 * @note The denoise setting is a persistent option
		 * that is saved to the HFPD configuration file.
		 */
		bool Denoise;

		/**
		 * @brief Digital signal processor auto-gain setting
		 *
		 * This property can be accessed using the
		 * @ref property "standard D-Bus property interface".
		 *
		 * This option controls whether the auto-gain feature of
		 * the Speex software digital signal processor is used.
		 * The auto-gain feature causes the volume level of the
		 * captured audio to be analyzed, and if it is too low,
		 * to have a gain applied to it automatically.
		 *
		 * Reasonable values for this property might be:
		 * - 0 to disable auto-gain
		 * - 8000 to enable a useful level of auto-gain
		 *
		 * @note The auto-gain setting is a persistent option
		 * that is saved to the HFPD configuration file.
		 */
		uint32 AutoGain;

		/**
		 * @brief Digital signal processor echo cancel tail setting
		 *
		 * This property can be accessed using the
		 * @ref property "standard D-Bus property interface".
		 *
		 * This option controls whether the echo canceler of
		 * the Speex software digital signal processor is used.
		 * Specifically, it sets the length of the sample period,
		 * in milliseconds, to be evaluated for acoustic echo.
		 *
		 * Reasonable values for this property might be:
		 * - 0 to disable the echo canceler
		 * - 100 for a normal amount of sound card latency
		 * - 200 if your sound card latency is higher
		 *
		 * @note The echo cancel tail setting is a persistent
		 * option that is saved to the HFPD configuration file.
		 */
		uint32 EchoCancelTail;

		/**
		 * @brief Notification of change of state of the SoundIo
		 *
		 * This signal apprises D-Bus clients of the state of the
		 * SoundIo object.
		 *
		 * @param[out] state Value of the state, which can also be
		 * read from the SoundIo.State property.  See the documentation
		 * for that property for more information on the meaning of
		 * the state values.
		 *
		 * A state change to the Stopped state may also be
		 * indicated by the StreamAborted() signal, which also
		 * includes information about the failure that caused the
		 * asynchronous abort.
		 */
		public signal StateChanged(out byte state);

		/**
		 * @brief Notification of asynchronous stream abort of
		 * the SoundIo
		 *
		 * This signal apprises D-Bus clients that a SoundIo object
		 * that was streaming has transitioned to the Stopped state
		 * due to a failure that occurred asynchronously.
		 *
		 * Other changes to SoundIo.State, including transitions to
		 * the Stopped state not caused by asynchronous failures,
		 * are indicated by the StateChanged() signal.
		 *
		 * @param[out] error_name Name of the D-Bus exception
		 * equivalent to the cause of the asynchronous failure.
		 * @param[out] description Explanation of the failure.
		 */
		public signal StreamAborted(out string error_name,
					    out string description);

		/**
		 * @brief Notification of change of the mute state of the
		 * SoundIo
		 *
		 * This signal apprises D-Bus clients of the mute state of
		 * the SoundIo object.
		 *
		 * @param[out] mute Set to @c true if mute is enabled,
		 * @c false otherwise.  This value can also be queried
		 * and controlled using the SoundIo.Mute property.
		 */
		public signal MuteChanged(out bool mute);

		/**
		 * @brief Notification of an audio gateway being selected
		 *
		 * This signal apprises D-Bus clients of an audio gateway
		 * device that has been recently configured as a streaming
		 * endpoint.  The audio gateway can be considered to have
		 * been deconfigured from the SoundIo when the state of the
		 * SoundIo changes back to stopped or deconfigured.
		 *
		 * This signal will precede a StateChanged() signal, in
		 * cases when the state is changing to Audio Gateway
		 * configured or Audio Gateway connected.  For more
		 * information on states, see SoundIo.State.
		 *
		 * @param[out] audio_gateway D-Bus path to the AudioGateway
		 * object that has been configured as the streaming endpoint.
		 * This value can also be accessed through the
		 * SoundIo.AudioGateway property.
		 */
		public signal AudioGatewaySet(out objectpath audio_gateway);

		/**
		 * @brief Notification of audio clock skew
		 *
		 * When the SoundIo object is streaming, it is possible
		 * that the different directions of an endpoint, or that
		 * the two attached endpoints, have a slight difference
		 * of sample clocks.  This has a negative effect on
		 * audio quality, and it is useful to make this type of
		 * problem clearly apparent to the user.
		 *
		 * @param[out] skew_type Type of skew detected.  Values
		 * are described below.
		 * @param[out] skew_value Detail of the skew.
		 * - @em skew_type = 1: Number of buffer overrun/underrun
		 * events that occurred in the last second.
		 * - @em skew_type = 2: Value describes a sample rate
		 * difference between the playback and capture halves of
		 * the primary sound card.  If the value is positive, the
		 * capture clock is faster than the playback clock, and
		 * vice-versa if the value is negative.  The magnitude
		 * of the value is the percentage difference of the
		 * slower clock relative to the faster clock.
		 * - @em skew_type = 3: Value describes a sample rate
		 * difference between the playback and capture halves of
		 * the attached endpoint, e.g. the audio gateway.  The
		 * value has the same interpretation as with
		 * @em skew_type = 2.
		 * - @em skew_type = 4: Value describes an overall sample
		 * rate difference between the primary sound card and the
		 * attached endpoint.  If the value is positive, the
		 * clock of the attached endpoint is faster than the
		 * primary sound card, and vice-versa if negative.  The
		 * magnitude of the value is the percentage difference
		 * of the slower clock relative to the faster clock.
		 */
		public signal SkewNotify(out byte skew_type,
					 out double skew_value);

		/**
		 * @brief Notification of audio stream progress
		 *
		 * This signal is emitted when a memory buffer endpoint
		 * is configured, and the @c report_interval parameter
		 * is nonzero.  Each report_interval number of samples,
		 * this signal will be emitted.
		 *
		 * @param[out] position Number of samples processed since
		 * the stream was started.
		 * @param[out] max_amplitude Largest observed amplitude
		 * in the captured audio stream since the last monitor
		 * notification signal was emitted.  This value can be
		 * used to provide visual feedback for captured audio.
		 */
		public signal MonitorNotify(out uint32 position,
					    out uint16 max_amplitude);
	}

	/**
	 * @brief Interface for managing a specific Audio Gateway device
	 */
	interface AudioGateway {

		/**
		 * @brief Initiate a service-level connection to the device
		 *
		 * This method will start to transition the device to
		 * the connected state.
		 *
		 * If a conneciton to the device is already in progress,
		 * or the device is already connected, this method will
		 * succeed and do nothing.
		 *
		 * @throw net.sf.nohands.hfpd.Error Thrown on any
		 * sort of error, unspecific of the reason of failure.
		 *
		 * This method may cause a StateChanged() signal to be
		 * emitted.
		 */
		public Connect();

		/**
		 * @brief Close the service-level connection to the device
		 *
		 * This method will force the device to the disconnected
		 * state.
		 *
		 * If a service-level connection to the device is in
		 * progress, it will be aborted.  If a service-level
		 * connection to the device is fully established, it will
		 * be disconnected.  If an audio connection to the device
		 * is open or in progress, it will also be closed or aborted.
		 * If the device is already disconnected, this method
		 * will succeed and do nothing.
		 *
		 * This method may cause StateChanged() and possibly
		 * AudioStateChanged() and SoundIo.StateChanged() signals
		 * to be emitted.
		 */
		public Disconnect();

		/**
		 * @brief Close the service-level connection to the device
		 *
		 * This method will force the device to the audio
		 * disconnected state.
		 *
		 * If an audio connection to the device is in progress, it
		 * will be aborted.  If an audio connection to the device
		 * is fully established, it will be disconnected.  If the
		 * device is configured as the endpoint of the SoundIo
		 * object, audio streaming will be halted.  If the audio
		 * connection to the device is already disconnected, this
		 * method will succeed and do nothing.
		 *
		 * This method may cause AudioStateChanged() and 
		 * SoundIo.StateChanged() signals to be emitted.
		 */
		public CloseAudio();

		/**
		 * @brief Send a dial command to the audio gateway
		 *
		 * This method causes a dial command to be sent to the
		 * audio gateway for a given phone number.
		 *
		 * @param[in] phone_num Phone number to request the audio
		 * gateway to dial.
		 *
		 * @throw net.sf.nohands.hfpd.Error Thrown on any
		 * sort of error, unspecific of the reason of failure.
		 *
		 * @note This method will not return until the audio gateway
		 * has responded to the command.  An audio gateway that has
		 * moved out of radio range may not be identified as being
		 * inaccessible for several seconds, and a poorly designed
		 * audio gateway may fail to respond altogether.  D-Bus
		 * clients should not expect this method to complete
		 * quickly and should invoke it asynchronously.
		 */
		public Dial(in string phone_num);

		/**
		 * @brief Send a last number redial command to the audio
		 * gateway
		 *
		 * This method causes a last number redial command to be
		 * sent to the audio gateway.  The number to be dialed
		 * will be the last number dialed by the audio gateway,
		 * whether it was submitted by a Dial() command or dialed
		 * directly on the keypad of the audio gateway.
		 *
		 * @throw net.sf.nohands.hfpd.Error Thrown on any
		 * sort of error, unspecific of the reason of failure.
		 *
		 * @note This method will not return until the audio gateway
		 * has responded to the command.  An audio gateway that has
		 * moved out of radio range may not be identified as being
		 * inaccessible for several seconds, and a poorly designed
		 * audio gateway may fail to respond altogether.  D-Bus
		 * clients should not expect this method to complete
		 * quickly and should invoke it asynchronously.
		 */
		public Redial();

		/**
		 * @brief Send a hangup command to the audio gateway
		 *
		 * This method causes a hangup command to be sent to
		 * the audio gateway.  This has several uses:
		 * - If the audio gateway has an active, established call,
		 * the call will be terminated.
		 * - If the audio gateway has an outgoing, incomplete
		 * call, the call will be aborted.
		 * - If the audio gateway supports call rejection, and
		 * has an incoming, incomplete, ringing call, the call
		 * will be rejected.
		 *
		 * @throw net.sf.nohands.hfpd.Error Thrown on any
		 * sort of error, unspecific of the reason of failure.
		 *
		 * @note This method will not return until the audio gateway
		 * has responded to the command.  An audio gateway that has
		 * moved out of radio range may not be identified as being
		 * inaccessible for several seconds, and a poorly designed
		 * audio gateway may fail to respond altogether.  D-Bus
		 * clients should not expect this method to complete
		 * quickly and should invoke it asynchronously.
		 */
		public HangUp();

		/**
		 * @brief Send a DTMF tone generation command to the audio
		 * gateway
		 *
		 * This method causes a DTMF tone generation command to be
		 * sent to the audio gateway.  The audio gateway will
		 * generate the DTMF tone appropriate for the code and
		 * play it over the audio connection.  This is useful for
		 * navigating menu systems such as for voice mail.
		 *
		 * @param[in] code DTMF code to generate.  Must be a
		 * decimal number, a letter A-D, #, or *.
		 *
		 * @throw net.sf.nohands.hfpd.Error Thrown on any
		 * sort of error, unspecific of the reason of failure.
		 *
		 * @note This method will not return until the audio gateway
		 * has responded to the command.  An audio gateway that has
		 * moved out of radio range may not be identified as being
		 * inaccessible for several seconds, and a poorly designed
		 * audio gateway may fail to respond altogether.  D-Bus
		 * clients should not expect this method to complete
		 * quickly and should invoke it asynchronously.
		 */
		public SendDtmf(in byte code);

		/**
		 * @brief Send an answer call command to the audio gateway
		 *
		 * This method causes an answer call command to be sent to
		 * the audio gateway.  If the audio gateway has an
		 * incoming, incomplete, ringing call, the call will be
		 * answered and will become the active call.
		 *
		 * @throw net.sf.nohands.hfpd.Error Thrown on any
		 * sort of error, unspecific of the reason of failure.
		 *
		 * @note This method will not return until the audio gateway
		 * has responded to the command.  An audio gateway that has
		 * moved out of radio range may not be identified as being
		 * inaccessible for several seconds, and a poorly designed
		 * audio gateway may fail to respond altogether.  D-Bus
		 * clients should not expect this method to complete
		 * quickly and should invoke it asynchronously.
		 */
		public Answer();

		/**
		 * @brief Send a drop held / UDUB command to the audio gateway
		 *
		 * This method causes an oddly designed command to be
		 * sent to the audio gateway that has the following effect:
		 * - If the audio gateway has an incoming, incomplete call,
		 * it will be rejected as User Determined User Busy.
		 * Cellular carriers are meant to handle this by reporting
		 * the line as busy to the caller.
		 * - Otherwise, if there is a call that is on hold, the call
		 * will be terminated.
		 *
		 * Support for this command is optional, and is indicated
		 * by the @c "DropHeldUdub" feature in the
		 * AudioGateway.Features property being @c true.
		 *
		 * @throw net.sf.nohands.hfpd.Error Thrown on any
		 * sort of error, unspecific of the reason of failure.
		 *
		 * @note This method will not return until the audio gateway
		 * has responded to the command.  An audio gateway that has
		 * moved out of radio range may not be identified as being
		 * inaccessible for several seconds, and a poorly designed
		 * audio gateway may fail to respond altogether.  D-Bus
		 * clients should not expect this method to complete
		 * quickly and should invoke it asynchronously.
		 */
		public CallDropHeldUdub();

		/**
		 * @brief Send a drop active call command to the audio gateway
		 *
		 * This method causes a command to be sent to the audio
		 * gateway that has the following effect:
		 * - If the audio gateway has an active call, it will be
		 * terminated.
		 * - If the audio gateway has a call that is on hold, or an
		 * incoming, unanswered call, it will be connected and
		 * will become the active call.
		 *
		 * Support for this command is optional, and is indicated
		 * by the @c "SwapDropActive" feature in the
		 * AudioGateway.Features property being @c true.
		 *
		 * @throw net.sf.nohands.hfpd.Error Thrown on any
		 * sort of error, unspecific of the reason of failure.
		 *
		 * @note This method will not return until the audio gateway
		 * has responded to the command.  An audio gateway that has
		 * moved out of radio range may not be identified as being
		 * inaccessible for several seconds, and a poorly designed
		 * audio gateway may fail to respond altogether.  D-Bus
		 * clients should not expect this method to complete
		 * quickly and should invoke it asynchronously.
		 */
		public CallSwapDropActive();

		/**
		 * @brief Send a hold active call command to the audio gateway
		 *
		 * This method causes a command to be sent to the audio
		 * gateway that has the following effect:
		 * - If the audio gateway has an active call, it will be
		 * placed on hold.
		 * - If the audio gateway has a call that is on hold, or an
		 * incoming, unanswered call, it will be connected and
		 * will become the active call.
		 *
		 * Support for this command is optional, and is indicated
		 * by the @c "SwapHoldActive" feature in the
		 * AudioGateway.Features property being @c true.
		 *
		 * @throw net.sf.nohands.hfpd.Error Thrown on any
		 * sort of error, unspecific of the reason of failure.
		 *
		 * @note This method will not return until the audio gateway
		 * has responded to the command.  An audio gateway that has
		 * moved out of radio range may not be identified as being
		 * inaccessible for several seconds, and a poorly designed
		 * audio gateway may fail to respond altogether.  D-Bus
		 * clients should not expect this method to complete
		 * quickly and should invoke it asynchronously.
		 */
		public CallSwapHoldActive();

		/**
		 * @brief Send a link calls command to the audio gateway
		 *
		 * This method causes a link calls command to be sent to the
		 * audio gateway.  If the audio gateway has an active call,
		 * and a call placed on hold, the calls will be linked
		 * into a single three-way call.
		 *
		 * Support for this command is optional, and is indicated
		 * by the @c "Link" feature in the
		 * AudioGateway.Features property being @c true.
		 *
		 * @throw net.sf.nohands.hfpd.Error Thrown on any
		 * sort of error, unspecific of the reason of failure.
		 *
		 * @note This method will not return until the audio gateway
		 * has responded to the command.  An audio gateway that has
		 * moved out of radio range may not be identified as being
		 * inaccessible for several seconds, and a poorly designed
		 * audio gateway may fail to respond altogether.  D-Bus
		 * clients should not expect this method to complete
		 * quickly and should invoke it asynchronously.
		 */
		public CallLink();

		/**
		 * @brief Send a transfer call command to the audio gateway
		 *
		 * This method causes a transfer call command to be sent to
		 * the audio gateway.  If the audio gateway has an active
		 * call, and a call placed on hold, the calls will be linked
		 * to each other, and disconnected from the audio gateway.
		 *
		 * Support for this command is optional, and is indicated
		 * by the @c "Transfer" feature in the
		 * AudioGateway.Features property being @c true.
		 *
		 * @throw net.sf.nohands.hfpd.Error Thrown on any
		 * sort of error, unspecific of the reason of failure.
		 *
		 * @note This method will not return until the audio gateway
		 * has responded to the command.  An audio gateway that has
		 * moved out of radio range may not be identified as being
		 * inaccessible for several seconds, and a poorly designed
		 * audio gateway may fail to respond altogether.  D-Bus
		 * clients should not expect this method to complete
		 * quickly and should invoke it asynchronously.
		 */
		public CallTransfer();

		/**
		 * @brief Service-level connection state of the device
		 *
		 * This property can be accessed using the
		 * @ref property "standard D-Bus property interface".
		 *
		 * This property describes the state of the service-level
		 * connection to the device.  The service-level connection
		 * is used to control the device and retrieve its status.
		 * An audio connection cannot be attempted until the
		 * service-level connection is established.
		 *
		 * This property may have values:
		 * - 1 = Object has been destroyed and removed
		 * - 2 = No service-level connection to device
		 * - 3 = Sevice-level connection is in progress
		 * - 4 = Service-level connection is established
		 *
		 * Changes to this value are reported by the
		 * StateChanged() signal.
		 */
		const byte State;

		/**
		 * @brief Call state of the device
		 *
		 * This property can be accessed using the
		 * @ref property "standard D-Bus property interface".
		 *
		 * This property describes the state of the active
		 * and incomplete calls being handled by the device.
		 *
		 * This property may have values:
		 * - 1 = No calls are present
		 * - 2 = An outbound, unanswered call is in progress
		 * - 3 = An established active call is present
		 * - 4 = An inbound, unanswered waiting call is present
		 * - 5 = An established active call, and an inbound
		 * unanswered waiting call are present.
		 *
		 * Changes to this value are reported by the
		 * CallStateChanged() signal.
		 */
		const byte CallState;

		/**
		 * @brief Audio connection state of the device
		 *
		 * This property can be accessed using the
		 * @ref property "standard D-Bus property interface".
		 *
		 * This property describes the state of the audio
		 * connection to the device.  The audio connection
		 * allows the device to stream audio data with the
		 * local sound card.
		 *
		 * This property may have values:
		 * - 1 = No audio connection to device
		 * - 2 = Audio connection is in progress
		 * - 3 = Audio connection is established
		 *
		 * Changes to this value are reported by the
		 * AudioStateChanged() signal.
		 */
		const byte AudioState;

		/**
		 * @brief D-Bus ownership state of the device
		 *
		 * This property can be accessed using the
		 * @ref property "standard D-Bus property interface".
		 *
		 * This property describes whether a D-Bus client has
		 * claimed the device or not.  If it is @c true, then a
		 * D-Bus client has claimed the device using
		 * HandsFree.AddDevice().  If it is @c false, no D-Bus
		 * client currently owns the device.
		 *
		 * Claimed devices are granted access to specific services
		 * provided by HFPD:
		 * - Claimed devices may open a service-level connection
		 * to HFPD at any time, even when HandsFree.AcceptUnknown
		 * is set to @c false.  Any D-Bus client can be notified of
		 * this by receiving the StateChanged() signal.
		 * - Claimed devices may open an audio connection at any
		 * time.  They will not be automatically attached to the
		 * audio pump and routed through the local sound card --
		 * this must be explicitly configured by the D-Bus client.
		 *
		 * Changes to this value are reported by the
		 * ClaimStateChanged() signal.
		 */
		const bool Claimed;

		/**
		 * @brief Mode of previous service-level disconnection
		 * of the device
		 *
		 * This property can be accessed using the
		 * @ref property "standard D-Bus property interface".
		 *
		 * This property describes the nature of the reason of
		 * the most recent service-level disconnection of the
		 * device.
		 *
		 * Voluntary disconnections are the result of a user
		 * initiated process, e.g. selecting "disconnect bluetooth"
		 * on a mobile phone.  Involuntary disconnections are the
		 * result of other factors, such as a Bluetooth dongle
		 * being removed, or a mobile phone being moved out of
		 * radio range.  When a disconnection is voluntary,
		 * reconnection should not be attempted until explicitly
		 * requested by the user.
		 * 
		 * Relevant samples of this value are reported by the
		 * StateChanged() signal.
		 */
		const bool VoluntaryDisconnect;

		/**
		 * @brief Bluetooth address of the AudioGateway
		 *
		 * This property can be accessed using the
		 * @ref property "standard D-Bus property interface".
		 *
		 * This property contains the Bluetooth address of
		 * the device, in colon-separated form, e.g.
		 * "01:23:45:67:89:AB".
		 */
		const string Address;

		/**
		 * @brief Permanently known flag
		 *
		 * This property can be accessed using the
		 * @ref property "standard D-Bus property interface".
		 *
		 * This property describes whether the audio gateway device
		 * is permanently known by HFPD.  This setting allows
		 * specific devices to make use of functionality of HFPD
		 * without the supervision of a D-Bus client.  Permanently
		 * known devices are allowed to connect despite
		 * HandsFree.AcceptUnknown being set to @c false.  Such
		 * devices are also automatically configured for audio
		 * streaming when they initiate an audio connection, as
		 * long as HandsFree.VoiceAutoConnect is @c true.
		 * 
		 * @note The permanently known setting of a device is a
		 * persistent option that is saved to the HFPD
		 * configuration file.
		 */
		bool Known;

		/**
		 * @brief Automatic reconnection flag
		 *
		 * This property can be accessed using the
		 * @ref property "standard D-Bus property interface".
		 *
		 * This property describes whether the periodic
		 * auto-reconnect mechanism should be used with the device.
		 * For audio gateways that do not have a service-level
		 * connection, enabling periodic auto-reconnect will cause a
		 * connection attempt to the device to be initiated
		 * periodically.
		 * 
		 * @note For permanently known devices, the auto-reconnect
		 * setting is a persistent option that is saved to the HFPD
		 * configuration file.  For other devices, it is ephemeral.
		 */
		bool AutoReconnect;

		/**
		 * @brief Feature set of device
		 *
		 * This property can be accessed using the
		 * @ref property "standard D-Bus property interface".
		 *
		 * The Hands-Free Profile specification describes an
		 * optional set of features that audio gateway devices
		 * are allowed to support.  This dictionary array
		 * contains feature names as keys, each mapping to a
		 * boolean value describing whether the device supports
		 * the named feature or not.
		 *
		 * The values of the feature set are only meaningful
		 * when a service-level connection is established.
		 *
		 * The D-Bus signature of this property is @c "a{sb}"
		 *
		 * Known optional feature names include:
		 * - ThreeWayCalling
		 * - ECNR
		 * - VoiceRecognition
		 * - InBandRingTone
		 * - VoiceTag
		 * - RejectCall
		 * - EnhancedCallStatus
		 * - EnhancedCallControl
		 * - DropHeldUdub
		 * - SwapDropActive
		 * - DropActive
		 * - SwapHoldActive
		 * - PrivateConsult
		 * - Link
		 * - Transfer
		 * - CallSetupIndicator
		 * - SignalStrengthIndicator
		 * - RoamingIndicator
		 * - BatteryChargeIndicator
		 */
		const dict<string, bool> Features;

		/**
		 * @brief Feature set of the device, as reported by the
		 * device through handshaking
		 *
		 * This property can be accessed using the
		 * @ref property "standard D-Bus property interface".
		 *
		 * The Bluetooth Hands-Free Profile specification provides
		 * a means for the audio gateway to identify a set of
		 * optional features that it supports.  Support for these
		 * optional features is reported by the +BRSF status code
		 * sent by the device during negotiation.  For more
		 * information, see the Bluetooth Hands-Free Profile
		 * specification.
		 *
		 * This presentation of the feature set does not contain
		 * as much detail as the AudioGateway.Features property,
		 * e.g. it has no way of representing whether the device
		 * supports a battery charge level indicator.
		 */
		const uint32 RawFeatures;

		/**
		 * @brief Notification of a service-level connection state
		 * change
		 *
		 * This signal is emitted whenever the service-level
		 * connection state of the device changes.  This can be
		 * caused by calls to Connect() or Disconnect(), removal
		 * of a Bluetooth dongle, explicit connection or
		 * disconnection from the audio gateway, or a number of
		 * other events.
		 *
		 * @param[out] state New value of the service-level
		 * connection state.  See AudioGateway.State for more
		 * information.
		 * @param[out] voluntary If the state changed to
		 * disconnected, this parameter describes whether the
		 * disconnection was voluntary or involuntary.  See
		 * AudioGateway.VoluntaryDisconnect for more information..
		 */
		public signal StateChanged(out byte state,
					   out bool voluntary);

		/**
		 * @brief Notification of a call state change
		 *
		 * This signal is emitted whenever the call state of the
		 * device changes.  This can be caused by calls to the
		 * Dial(), Answer(), or Hangup() methods, by an incoming
		 * call, or a number of other events.
		 *
		 * @param[out] call_state New value of the audio gateway's
		 * call state.  See AudioGateway.CallState for more
		 * information.
		 */
		public signal CallStateChanged(out byte call_state);

		/**
		 * @brief Notification of an audio connection state
		 * change
		 *
		 * This signal is emitted whenever the audio connection
		 * state of the device changes.  This can be caused by
		 * calls to SoundIo.AudioGatewayStart() or CloseAudio(),
		 * a service-level disconnection, or a number of
		 * other events.
		 *
		 * @param[out] audio_state New value of the audio
		 * connection state.  See AudioGateway.AudioState for
		 * more information.
		 *
		 * An open audio connection to an audio gateway device
		 * does not imply that audio data is being streamed to
		 * and from the device.  The D-Bus client is ultimately
		 * responsible for configuring streaming.  Ideally, the
		 * D-Bus client will either:
		 * - Start streaming audio between the audio gateway
		 * and the local sound card.  See
		 * SoundIo.AudioGatewayStart().
		 * - Drop the audio connection.  See CloseAudio().
		 *
		 * It is @b highly @b undesirable to leave an audio gateway
		 * device sitting with an open audio connection, without
		 * active streaming.  D-Bus clients should take care to
		 * avoid this situation, and ensure that they close the
		 * audio connection if they are unable to start streaming.
		 *
		 * Overall, HFPD should not need to initiate audio
		 * connections to devices -- audio gateway devices are
		 * suited to do all of the initiation on their own.
		 * If HFPD does initiate an audio connection, it should
		 * only be at the explicit request of the user.
		 *
		 * When an audio gateway device initiates an audio
		 * connection, the device's D-Bus owner will be informed
		 * of it only by this signal.
		 */
		public signal AudioStateChanged(out byte audio_state);

		/**
		 * @brief Notification of the AudioGateway being claimed or
		 * unclaimed
		 *
		 * This signal is emitted whenever the ownership state of
		 * the AudioGateway changes.  This can be caused by a D-Bus
		 * client invoking HandsFree.AddDevice() or
		 * HandsFree.RemoveDevice(), or the owner being disconnected
		 * from D-Bus.
		 *
		 * @param[out] claim_state New value of the ownership
		 * state of the AudioGateway.  See AudioGateway.Claimed for
		 * more information.
		 */
		public signal ClaimStateChanged(out bool claim_state);

		/**
		 * @brief Ring notification
		 *
		 * This signal is emitted whenever the phone "rings"
		 * due to an incoming call.  The D-Bus owner of the
		 * AudioGateway should use this signal to start alerting
		 * the user and possibly start playing a ring tone.
		 *
		 * @param[out] caller_id The caller line identification
		 * reported by the audio gateway, typically a set of digits
		 * occasionally prefixed by a "+".  The D-Bus client may
		 * use this value as a key for a phone book search.
		 * @param[out] caller_name Some audio gateway devices
		 * have built-in phone books, which may have an entry for
		 * the phone number of the caller.  In such cases, this
		 * parameter contains the caller name reported by the
		 * audio gateway.
		 *
		 * The D-Bus client has a number of options for dealing
		 * with the incoming call, although some are optionally
		 * supported by the audio gateway:
		 * - Answer the call with Answer().
		 * - Reject the call with HangUp().
		 * - Report User Determined User Busy with CallDropHeldUdub().
		 */
		public signal Ring(out string caller_id,
				   out string caller_name);

		/**
		 * @brief Notification of miscellaneous audio gateway
		 * state change
		 *
		 * Some audio gateways are able to report optional state,
		 * such as signal strength ("bars"), battery charge level,
		 * and even whether voice mail is waiting.  This signal
		 * is emitted when the audio gateway reports such a state
		 * change.
		 *
		 * @param[out] indicator_name One of a set of
		 * semi-standardized indicator names described below.
		 * @param[out] value Value of indicator, as reported by
		 * the device.
		 *
		 * Known indicators include:
		 * - @c "service" 0 = no cellular service, 1 = service
		 * - @c "roam" 0 = not roaming, 1 = roaming
		 * - @c "signal" Signal strength level, values 0-5
		 * - @c "battchg" Battery charge level, values 0-5
		 * - @c "Voice Mail" 0 = no voice mail, 1 = voice mail,
		 * nonstandard and supported by some Motorola phones
		 */
		public signal IndicatorChanged(out string indicator_name,
					       out int32 value);

		/**
		 * @brief Bluetooth name resolution complete notification
		 *
		 * When a service-level connection to the audio gateway
		 * is initiated, as soon as the low-level Bluetooth
		 * connection is complete, a request is made to read the
		 * Bluetooth name of the device.  This signal apprises
		 * D-Bus clients of the result of the read name request.
		 *
		 * @param[out] name Bluetooth name read from the audio
		 * gateway device.
		 */
		public signal NameResolved(out string name);
	}
}
