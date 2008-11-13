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

/**
 * @mainpage Bluetooth Hands-Free Profile Server
 *
 * @section intro Introduction
 *
 * HFP for Linux is a Bluetooth Hands-Free Profile server, allowing 
 * your Linux system to act as a speakerphone for your mobile phone.  
 * It aims to be a compliant Bluetooth HFP 1.5 Hands Free 
 * implementation, supporting all required commands and notifications, 
 * as well as streaming audio.  The HFP for Linux package includes
 * three main components:
 *
 * - @b libhfp, a modular, toolkit-independent backend library with
 * rich APIs.  This documentation is primarily concerned with libhfp.
 * - @b hfpd, a D-Bus service daemon providing D-Bus APIs.  A link to
 * documentation for the D-Bus APIs can be found on the HFP for Linux
 * home page.
 * - @b hfconsole, a PyGTK console application for controlling the
 * D-Bus service daemon.
 *
 * The package also includes @b hfstandalone, a demonstration
 * monolithic speakerphone program built with Trolltech Qt.  hfstandalone
 * uses its own instance of libhfp and operates independently of hfpd.
 *
 * The developer of HFP for Linux believes that HFP is most useful in 
 * embedded devices, each with unique user interface requirements.  
 * Because of this, emphasis is placed on the internals and backend 
 * interfaces of libhfp, and the D-Bus interfaces of hfpd, so that it 
 * may be useful as a toolkit for building HFP applications and 
 * integrating HFP functionality into other applications.
 *
 * @section features Features
 *
 * - Supports device scanning, connection, disconnection, and automatic
 * reconnection
 * - Supports multiple concurrently connected audio gateway devices
 * - Resilient to loss of Bluetooth service
 * - Modular, event-driven backend
 * - All APIs are single-threaded, clients do not need to be concerned with
 * thread synchronization
 * - Audio handling components are completely asynchronous
 * - Supports the ALSA and OSS audio hardware interfaces
 * - Supports microphone input cleanup, including echo cancellation and
 * noise reduction.
 * - Supports simple recording and playback of stored audio files, e.g.
 * for recording calls and playing ring tones.
 *
 * @section reqs Software Requirements and Build Options
 *
 * - libbluetooth and BlueZ, the Linux Bluetooth stack
 * - libasound, the ALSA user-level audio library @em optional
 * - The OSS kernel-level audio interface @em optional
 * - libaudiofile, the SGI audiofile library @em optional
 * - libspeexdsp, the Speex signal processing library @em optional
 * - libdbus for hfpd
 * - PyGTK and dbus-python for the hfconsole app
 * - Trolltech Qt 3.3 for the monolithic demonstration app
 */
