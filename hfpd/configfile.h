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

#if !defined(__HFPD_CONFIGFILE_H__)
#define __HFPD_CONFIGFILE_H__

#include <libhfp/list.h>
#include <libhfp/events.h>

/*
 * Configuration file parser/writer for hfpd
 * _REALLY_ basic, not meant to store large information sets
 * The file format is essentially .INI
 */

class ConfigFile {
	struct Tuple {
		libhfp::ListItem	links;
		const char		*key;
		const char		*value;
		int			layer;
		int			lowest_layer;

		void *operator new(size_t, Tuple *&tupp) { return tupp; }
	};

	struct Section {
		libhfp::ListItem	m_links;
		const char		*m_name;
		libhfp::ListItem	m_tuples;

		void *operator new(size_t, Section *&secp) { return secp; }
	};

	libhfp::ListItem		m_sections;

	Section *CreateSection(const char *name, int len);
	Tuple *CreateTuple(Section *secp,
			   const char *name, const char *value);
	Section *FindSection(const char *name);
	Tuple *FindTuple(Section *secp, const char *name);
	void DeleteSection(Section *secp);
	void DeleteTuple(Tuple *tupp);
	void DeleteAll(void);

	struct Context {
		int		layer;
		int		fh;
		Section		*cursec;
		const char	*filename;
		int		lineno;
	};

	bool ReadLoop(Context *ctxp);
	int ExtractLineLoop(Context *ctxp, char *buf, int len, bool last);
	bool ParseLine(Context *ctxp, char *buf);

public:

	ConfigFile(void) {}
	~ConfigFile() { Clear(); }

	void Clear(void) { DeleteAll(); }
	bool Load(const char *path, int layer);
	bool Save(const char *path, int min_layer,
		  libhfp::ErrorInfo *error = 0);
	bool Create(const char *path);

	bool Get(const char *section, const char *key,
		 const char *&value, const char *defaultval);
	bool Get(const char *section, const char *key,
		 int &value, int defaultval);
	bool Get(const char *section, const char *key,
		 unsigned int &value, unsigned int defaultval);
	bool Get(const char *section, const char *key,
		 float &value, float defaultval);
	bool Get(const char *section, const char *key,
		 bool &value, bool defaultval);
	bool Set(const char *section, const char *key, const char *value,
		 libhfp::ErrorInfo *error = 0);
	bool Set(const char *section, const char *key, int value,
		 libhfp::ErrorInfo *error = 0);
	bool Set(const char *section, const char *key, unsigned int value,
		 libhfp::ErrorInfo *error = 0);
	bool Set(const char *section, const char *key, float value,
		 libhfp::ErrorInfo *error = 0);
	bool Set(const char *section, const char *key, bool value,
		 libhfp::ErrorInfo *error = 0);

	bool Delete(const char *section, const char *key,
		    libhfp::ErrorInfo *error = 0) {
		return Set(section, key, (const char *) 0, error);
	}

	class Iterator {
		friend class ConfigFile;
		ConfigFile::Section *sec;
		ConfigFile::Tuple *tup;
	public:
		Iterator() : sec(0), tup(0) {}
		const char *GetSection(void) const
			{ return sec ? sec->m_name : 0; }
		const char *GetKey(void) const
			{ return sec ? tup->key : 0; }
		const char *GetValue(void) const
			{ return sec ? tup->value : 0; }
		const bool GetValueBool(void) const;
	};

	bool First(ConfigFile::Iterator &it);
	bool FirstInSection(ConfigFile::Iterator &it, const char *secname);
	bool Next(ConfigFile::Iterator &it);
	bool Prev(ConfigFile::Iterator &it);
};

#endif /* !defined(__HFPD_CONFIGFILE_H__) */
