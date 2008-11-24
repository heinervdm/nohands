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

#if !defined(__HFPD_PROTO_H__)
#define __HFPD_PROTO_H__

/*
 * Definitions useful for clients of hfpd
 */

#define HFPD_SERVICE_NAME "net.sf.nohands.hfpd"
#define HFPD_HANDSFREE_INTERFACE_NAME "net.sf.nohands.hfpd.HandsFree"
#define HFPD_SOUNDIO_INTERFACE_NAME "net.sf.nohands.hfpd.SoundIo"
#define HFPD_AUDIOGATEWAY_INTERFACE_NAME "net.sf.nohands.hfpd.AudioGateway"
#define HFPD_HANDSFREE_OBJECT "/net/sf/nohands/hfpd"
#define HFPD_SOUNDIO_OBJECT "/net/sf/nohands/hfpd/soundio"

#define HFPD_ERROR_FAILED		 				\
	"net.sf.nohands.hfpd.Error"
#define HFPD_ERROR_BT_NO_KERNEL_SUPPORT 				\
	"net.sf.nohands.hfpd.Error.BtNoKernelSupport"
#define HFPD_ERROR_BT_SERVICE_CONFLICT					\
	"net.sf.nohands.hfpd.Error.BtServiceConflict"
#define HFPD_ERROR_BT_BAD_SCO_CONFIG					\
	"net.sf.nohands.hfpd.Error.BtScoConfigError"
#define HFPD_ERROR_SOUNDIO_SOUNDCARD_FAILED				\
	"net.sf.nohands.hfpd.Error.SoundIoSoundCardFailed"

enum AudioGatewayState {
	HFPD_AG_INVALID = 0,
	HFPD_AG_DESTROYED,
	HFPD_AG_DISCONNECTED,
	HFPD_AG_CONNECTING,
	HFPD_AG_CONNECTED,
};

enum AudioGatewayCallState {
	HFPD_AG_CALL_INVALID = 0,
	HFPD_AG_CALL_IDLE,
	HFPD_AG_CALL_CONNECTING,
	HFPD_AG_CALL_ESTAB,
	HFPD_AG_CALL_WAITING,
	HFPD_AG_CALL_ESTAB_WAITING,
};

enum AudioGatewayAudioState {
	HFPD_AG_AUDIO_INVALID = 0,
	HFPD_AG_AUDIO_DISCONNECTED,
	HFPD_AG_AUDIO_CONNECTING,
	HFPD_AG_AUDIO_CONNECTED,
};

enum SoundIoState {
	HFPD_SIO_INVALID = 0,
	HFPD_SIO_DECONFIGURED,
	HFPD_SIO_STOPPED,
	HFPD_SIO_AUDIOGATEWAY_CONNECTING,
	HFPD_SIO_AUDIOGATEWAY,
	HFPD_SIO_LOOPBACK,
	HFPD_SIO_MEMBUF,
};

#endif /* !defined(__HFPD_PROTO_H__) */
