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
 * @mainpage Bluetooth Hands-Free Profile Service Library
 *
 * @section intro Introduction
 *
 * The HFP for Linux package provides a low-level library, libhfp, that
 * implements the smallest details of the hands-free side of
 * the Bluetooth Hands-Free Profile.  libhfp is written in C++, and is
 * intended to be modular and independent of any specific application
 * toolkit.  libhfp can be integrated with the main event loop of Qt,
 * glib, or any other toolkit with support for low-level events.
 *
 * The HFP for Linux package includes two applications that make use of
 * libhfp:
 * - @b hfpd, a D-Bus service daemon that uses libhfp and provides
 * D-Bus APIs.  Documentation for the D-Bus APIs is available in
 * <a href="../dbus/index.html">a separate document</a>.  hfpd is the
 * backend for hfconsole, a PyGTK console application for controlling the
 * D-Bus service daemon.
 * - @b hfstandalone, a demonstration monolithic speakerphone program built
 * with libhfp and Trolltech Qt.  hfstandalone uses its own instance of
 * libhfp and operates independently of hfpd.
 *
 * @section features libhfp Features
 *
 * - Supports device scanning, connection, disconnection, and automatic
 * reconnection
 * - Supports multiple concurrently connected audio gateway devices
 * - Resilient to loss of Bluetooth service
 * - Modular, event-driven backend
 * - All APIs are single-threaded, libhfp does not force its clients to be
 * concerned with threading and synchronization
 * - Audio handling components are completely asynchronous
 * - Supports the ALSA and OSS audio hardware interfaces
 * - Supports microphone input cleanup, including echo cancellation and
 * noise reduction
 * - Supports simple recording and playback of stored audio files, e.g.
 * for recording calls and playing ring tones
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
