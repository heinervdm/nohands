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

#include <unistd.h>
#include <sys/types.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>

#include "configfile.h"

using namespace libhfp;

ConfigFile::Section *ConfigFile::
CreateSection(const char *name, int len)
{
	struct ConfigFile::Section *secp;
	char *buf;
	size_t asize;

	assert(name && strlen(name));
	asize = sizeof(*secp) + len + 1;
	secp = (Section *) malloc(asize);
	if (!secp)
		return 0;

	secp = new (secp) Section;
	buf = (char *) (secp + 1);
	strncpy(buf, name, len);
	buf[len] = '\0';
	secp->m_name = buf;

	m_sections.AppendItem(secp->m_links);
	return secp;
}

ConfigFile::Tuple *ConfigFile::
CreateTuple(struct ConfigFile::Section *secp,
	    const char *name, const char *value)
{
	Tuple *tupp;
	char *buf;
	size_t len;

	assert(secp);
	assert(name && strlen(name));
	if (!value)
		value = "";

	len = sizeof(*tupp) + strlen(name) + strlen(value) + 2;
	tupp = (Tuple *) malloc(len);
	if (!tupp)
		return 0;

	tupp = new (tupp) Tuple;
	buf = (char *) (tupp + 1);
	strcpy(buf, name);
	tupp->key = buf;
	buf += (strlen(name) + 1);
	strcpy(buf, value);
	tupp->value = buf;
	assert((size_t) ((buf + strlen(value) + 1) - ((char *) tupp)) == len);
	tupp->layer = -1;

	secp->m_tuples.AppendItem(tupp->links);
	return tupp;
}

ConfigFile::Section *ConfigFile::
FindSection(const char *name)
{
	Section *secp;
	ListItem *listp;

	ListForEach(listp, &m_sections) {
		secp = GetContainer(listp, Section, m_links);
		if (!strcmp(secp->m_name, name))
			return secp;
	}
	return 0;
}

ConfigFile::Tuple *ConfigFile::
FindTuple(ConfigFile::Section *secp, const char *name)
{
	Tuple *tupp;
	ListItem *listp;

	ListForEach(listp, &secp->m_tuples) {
		tupp = GetContainer(listp, Tuple, links);
		if (!strcmp(tupp->key, name))
			return tupp;
	}
	return 0;
}

void ConfigFile::
DeleteSection(ConfigFile::Section *secp)
{
	assert(!secp->m_links.Empty());
	while (!secp->m_tuples.Empty()) {
		Tuple *tupp = GetContainer(secp->m_tuples.next, Tuple, links);
		DeleteTuple(tupp);
	}
	secp->m_links.UnlinkOnly();
	free(secp);
}

void ConfigFile::
DeleteTuple(ConfigFile::Tuple *tupp)
{
	assert(!tupp->links.Empty());
	tupp->links.UnlinkOnly();
	free(tupp);
}

void ConfigFile::
DeleteAll(void)
{
	while (!m_sections.Empty()) {
		Section *secp = GetContainer(m_sections.next,
					     Section, m_links);
		DeleteSection(secp);
	}
}

bool ConfigFile::
ReadLoop(ConfigFile::Context *ctxp)
{
	char buf[1024];
	size_t start, len;
	int cons;
	ssize_t readres;

	start = len = 0;

	while (1) {

		readres = read(ctxp->fh, &buf[start + len],
			       sizeof(buf) - (start + len));
		if (readres < 0)
			return false;

		len += readres;

		if (!len)
			break;

		/* Try to consume the buffer */
		do {
			cons = ExtractLineLoop(ctxp, &buf[start], len, false);
			if (cons < 0)
				return false;

			if (!cons) {
				/* Don't tolerate lines that are too long */
				if ((start + len) == sizeof(buf)) {
					if (start == 0)
						return false;

					/* Compact the buffer */
					memmove(buf, &buf[start], len);
					start = 0;
				}
				break;
			}

			assert((size_t)cons <= len);

			if (cons == (ssize_t) len) {
				start = len = 0;
			} else {
				start += cons;
				len -= cons;
			}

		} while (len);

		if (len && !readres) {
			cons = ExtractLineLoop(ctxp, &buf[start], len, true);
			if (cons < 0)
				return false;

			break;
		}

	}

	return true;
}

static bool IsWS(char c) { return ((c == ' ') || (c == '\t')); }
static bool IsNL(char c) { return ((c == '\r') || (c == '\n')); }

int ConfigFile::
ExtractLineLoop(ConfigFile::Context *ctxp, char *buf, int len, bool last)
{
	int pos = 0;
	char c;

	/* Are we looking at white space?  Trim it! */
	c = buf[0];
	assert(c);
	if (IsWS(c) || IsNL(c)) {
		if (c == '\n') ctxp->lineno++;
		do {
			c = buf[++pos];
			if (c == '\n') ctxp->lineno++;
		} while ((pos < len) && (IsWS(c) || IsNL(c)));
		return pos;
	}

	/* Is there a newline anywhere in the buffer? */
	for (pos = 1; pos < len; pos++) {
		if (IsNL(buf[pos])) {
			c = buf[pos];
			buf[pos] = '\0';
			if (!ParseLine(ctxp, buf))
				return -1;
			if (c == '\n') ctxp->lineno++;
			return pos + 1;
		}
	}

	if (last) {
		/* No newline, but it's the end of the file */
		if (!ParseLine(ctxp, buf))
			return -1;
		return 0;
	}

	/* No newline, nothing to consume */
	return 0;
}

bool ConfigFile::
ParseLine(ConfigFile::Context *ctxp, char *line)
{
	char *part, *end;
	Tuple *tupp;
	int kl, dl;
	int lowest;

	if (line[0] == '#')
		/* Skip it! */
		return true;

	if (line[0] == '[') {
		/* Found a section header */
		part = &line[1];
		while (IsWS(*part)) { part++; }
		end = strchr(part, ']');
		if (!end || (end == part)) {
			fprintf(stderr, "%s:%d: Malformed section header\n",
				ctxp->filename, ctxp->lineno);
			return true;
		}

		ctxp->cursec = CreateSection(part, end - part);
		if (!ctxp->cursec)
			return false;

		return true;
	}

	if (!ctxp->cursec) {
		fprintf(stderr, "%s:%d: key/value entry with no preceding "
			"section header\n",
			ctxp->filename, ctxp->lineno);
		return true;
	}

	part = strchr(line, '=');
	if (!part || (part == line)) {
		fprintf(stderr, "%s:%d: Malformed key/value entry\n",
			ctxp->filename, ctxp->lineno);
		return true;
	}

	for (kl = part - line; kl && IsWS(line[kl - 1]); kl--);
	assert(kl);
	for (part++; *part && IsWS(*part); part++);
	for (dl = strlen(part); dl && IsWS(part[dl - 1]); dl--);

	line[kl] = '\0';
	part[dl] = '\0';

	tupp = FindTuple(ctxp->cursec, line);
	if (tupp && (tupp->layer == ctxp->layer)) {
		fprintf(stderr, "%s:%d: Key \"%s\" in section \"%s\" "
			"redefined\n",
			ctxp->filename, ctxp->lineno, line,
			ctxp->cursec->m_name);
		return true;
	}

	lowest = ctxp->layer;
	if (tupp) {
		/* Do not overwrite settings from a higher layer */
		if (tupp->layer > ctxp->layer)
			return true;

		/* Or settings that we aren't truly changing */
		if (!strcmp(tupp->value, part))
			return true;

		lowest = tupp->lowest_layer;
		DeleteTuple(tupp);
	}

	tupp = CreateTuple(ctxp->cursec, line, part);
	if (!tupp)
		return false;

	tupp->layer = ctxp->layer;
	tupp->lowest_layer = lowest;
	return true;
}


static char *
TildeExpand(const char *string)
{
	int len;
	char *home, *res;

	if ((string[0] != '~') ||
	    !(home = getenv("HOME")))
		return strdup(string);

	len = (strlen(string) - 1) + strlen(home);
	res = (char *) malloc(len + 1);
	if (res) {
		strcpy(res, home);
		strcat(res, &string[1]);
	}

	return res;
}

bool ConfigFile::
Load(const char *path, int layer)
{
	char *expanded;
	Context ctx;
	bool res;

	expanded = TildeExpand(path);
	if (!expanded)
		return false;

	ctx.layer = layer;
	ctx.filename = path;
	ctx.lineno = 1;
	ctx.fh = open(expanded, O_RDONLY);
	free(expanded);
	if (ctx.fh < 0)
		return false;

	res = ReadLoop(&ctx);

	close(ctx.fh);
	if (!res)
		Clear();
	return res;
}


bool ConfigFile::
Save(const char *path, int layer, ErrorInfo *error)
{
	char *expanded;
	FILE *fp;
	Section *secp;
	Tuple *tupp;
	ListItem *listp, *tlistp;
	bool wrote_sect;
	int res;

	expanded = TildeExpand(path);
	if (!expanded) {
		if (error)
			error->SetNoMem();
		return false;
	}

	fp = fopen(expanded, "w");
	free(expanded);
	if (!fp) {
		res = errno;
		if (error)
			error->Set(LIBHFP_ERROR_SUBSYS_EVENTS,
				   LIBHFP_ERROR_EVENTS_IO_ERROR,
				   "Could not open config file to write: %s",
				   strerror(res));
		return false;
	}

	if (fprintf(fp,
		    "# Local settings file for hfpd\n"
		    "# Automatically generated, comments will be lost\n") < 0)
		goto io_error;

	ListForEach(listp, &m_sections) {
		secp = GetContainer(listp, Section, m_links);
		wrote_sect = false;

		ListForEach(tlistp, &secp->m_tuples) {
			tupp = GetContainer(tlistp, Tuple, links);
			if ((tupp->layer >= layer) &&
			    (tupp->value[0] ||
			     (tupp->lowest_layer < layer))) {

				if (!wrote_sect) {
					if (fprintf(fp, "\n[%s]\n",
						    secp->m_name) < 0)
						goto io_error;
					wrote_sect = true;
				}

				if (fprintf(fp, "%s = %s\n",
					    tupp->key, tupp->value) < 0)
					goto io_error;

				/*
				 * Record the layer where the tuple now exists
				 */
				tupp->layer = layer;
			}
		}
	}

	if (fclose(fp) == 0)
		return true;

	fp = 0;

io_error:
	res = errno;
	if (fp)
		(void) fclose(fp);

	if (error)
		error->Set(LIBHFP_ERROR_SUBSYS_EVENTS,
			   LIBHFP_ERROR_EVENTS_IO_ERROR,
			   "Error writing to config file: %s",
			   strerror(res));
	return false;
}

bool ConfigFile::
Create(const char *path)
{
	char *expanded;
	int fh;
	bool res = false;

	expanded = TildeExpand(path);
	if (!expanded)
		return false;

	fh = open(expanded, O_RDWR|O_CREAT, 0644);
	if (fh >= 0) {
		close(fh);
		res = true;
	}

	free(expanded);
	return res;
}


bool ConfigFile::
Get(const char *section, const char *key, const char *&value,
    const char *defaultval)
{
	Section *secp;
	Tuple *tupp;

	secp = FindSection(section);
	if (!secp) {
		secp = CreateSection(section, strlen(section));
		if (!secp) {
			value = defaultval;
			return false;
		}
	}

	tupp = FindTuple(secp, key);
	if (!tupp) {
		/*
		 * TODO: feature: remember default values
		 */
		value = defaultval;
		return false;
	}

	value = tupp->value;
	if (value && !value[0])
		value = 0;
	if (!value) {
		value = defaultval;
		return false;
	}

	return true;
}

bool ConfigFile::
Get(const char *section, const char *key, int &value, int defaultval)
{
	const char *v;
	char *e;
	if (!Get(section, key, v, 0)) {
		value = defaultval;
		return false;
	}

	value = strtol(v, &e, 0);
	if (e == v) {
		value = defaultval;
		return false;
	}

	return true;
}

bool ConfigFile::
Get(const char *section, const char *key, unsigned int &value,
    unsigned int defaultval)
{
	const char *v;
	char *e;
	if (!Get(section, key, v, 0)) {
		value = defaultval;
		return false;
	}

	value = strtoul(v, &e, 0);
	if (e == v) {
		value = defaultval;
		return false;
	}

	return true;
}

bool ConfigFile::
Get(const char *section, const char *key, float &value, float defaultval)
{
	const char *v;
	char *e;
	if (!Get(section, key, v, 0)) {
		value = defaultval;
		return false;
	}

	value = strtof(v, &e);
	if (e == v) {
		value = defaultval;
		return false;
	}

	return true;
}

bool ConfigFile::
Get(const char *section, const char *key, bool &value, bool defaultval)
{
	const char *v;
	char *e;
	int intval;
	if (!Get(section, key, v, 0)) {
		value = defaultval;
		return false;
	}

	/* Preferred forms */
	if (!strcasecmp(v, "true") || !strcasecmp(v, "yes")) {
		value = true;
		return true;
	}
	if (!strcasecmp(v, "false") || !strcasecmp(v, "no")) {
		value = false;
		return true;
	}

	/* Also accept 0 and 1 */
	intval = strtol(v, &e, 0);
	if ((e == v) || ((intval != 0) && (intval != 1))) {
		value = defaultval;
		return false;
	}

	value = (intval == 1);
	return true;
}

bool ConfigFile::
Set(const char *section, const char *key, const char *value, ErrorInfo *error)
{
	Section *secp;
	Tuple *tupp;
	int lowest = INT_MAX;

	secp = FindSection(section);
	if (!secp) {
		secp = CreateSection(section, strlen(section));
		if (!secp) {
			if (error)
				error->SetNoMem();
			return false;
		}
	}

	tupp = FindTuple(secp, key);
	if (tupp) {
		lowest = tupp->lowest_layer;
		DeleteTuple(tupp);
	}

	if (!value) {
		/* Don't bother saving non-overlaid empty tuples */
		if (lowest == INT_MAX)
			return true;
		value = "";
	}

	tupp = CreateTuple(secp, key, value);
	if (!tupp) {
		if (error)
			error->SetNoMem();
		return false;
	}

	tupp->layer = INT_MAX;
	tupp->lowest_layer = lowest;
	return true;
}

bool ConfigFile::
Set(const char *section, const char *key, int value, ErrorInfo *error)
{
	char buf[32];
	int len;
	len = snprintf(buf, sizeof(buf), "%d", value);
	assert(len > 0);
	return Set(section, key, buf, error);
}

bool ConfigFile::
Set(const char *section, const char *key, unsigned int value, ErrorInfo *error)
{
	char buf[32];
	int len;
	len = snprintf(buf, sizeof(buf), "%u", value);
	assert(len > 0);
	return Set(section, key, buf, error);
}

bool ConfigFile::
Set(const char *section, const char *key, float value, ErrorInfo *error)
{
	char buf[32];
	int len;
	len = snprintf(buf, sizeof(buf), "%.8g", value);
	assert(len > 0);
	return Set(section, key, buf, error);
}

bool ConfigFile::
Set(const char *section, const char *key, bool value, ErrorInfo *error)
{
	return Set(section, key, value ? "true" : "false", error);
}


bool ConfigFile::
First(ConfigFile::Iterator &it)
{
	ListItem *listp;

	if (m_sections.Empty()) {
		it.sec = 0;
		it.tup = 0;
		return false;
	}

	do {
		it.sec = GetContainer(m_sections.next, Section, m_links);
		listp = it.sec->m_tuples.next;
		while (listp == &it.sec->m_tuples) {
			if (it.sec->m_links.next == &m_sections) {
				it.sec = 0;
				it.tup = 0;
				return false;
			}
			it.sec = GetContainer(it.sec->m_links.next,
					      Section, m_links);
			listp = it.sec->m_tuples.next;
		}
		it.tup = GetContainer(listp, Tuple, links);

	} while (!it.tup->value[0]);

	return true;
}

bool ConfigFile::
FirstInSection(ConfigFile::Iterator &it, const char *secname)
{
	Section *secp;
	it.sec = FindSection(secname);
	if (!it.sec || it.sec->m_tuples.Empty()) {
		it.sec = 0;
		it.tup = 0;
		return false;
	}

	secp = it.sec;
	it.tup = GetContainer(it.sec->m_tuples.next, Tuple, links);
	if (!it.tup->value[0]) {
		Next(it);
		if (it.sec != secp) {
			it.sec = 0;
			it.tup = 0;
			return false;
		}
	}
	return true;
}

bool ConfigFile::
Next(ConfigFile::Iterator &it)
{
	ListItem *listp;

	if (!it.tup)
		return false;

	do {
		listp = it.tup->links.next;
		while (listp == &it.sec->m_tuples) {
			if (it.sec->m_links.next == &m_sections) {
				it.sec = 0;
				it.tup = 0;
				return false;
			}
			it.sec = GetContainer(it.sec->m_links.next,
					      Section, m_links);
			listp = it.sec->m_tuples.next;
		}
		it.tup = GetContainer(listp, Tuple, links);

	} while (!it.tup->value[0]);

	return true;
}

bool ConfigFile::
Prev(ConfigFile::Iterator &it)
{
	ListItem *listp;

	if (!it.tup)
		return false;

	do {
		listp = it.tup->links.prev;
		while (listp == &it.sec->m_tuples) {
			if (it.sec->m_links.prev == &m_sections) {
				it.sec = 0;
				it.tup = 0;
				return false;
			}
			it.sec = GetContainer(it.sec->m_links.prev,
					      Section, m_links);
			listp = it.sec->m_tuples.prev;
		}
		it.tup = GetContainer(listp, Tuple, links);

	} while (!it.tup->value[0]);

	return true;
}

const bool ConfigFile::Iterator::
GetValueBool(void) const
{
	int intval;
	char *e;
	const char *v = GetValue();

	/* Preferred forms */
	if (!strcasecmp(v, "true") || !strcasecmp(v, "yes")) {
		return true;
	}
	if (!strcasecmp(v, "false") || !strcasecmp(v, "no")) {
		return false;
	}

	/* Also accept 0 and 1 */
	intval = strtol(v, &e, 0);
	if ((e == v) || ((intval != 0) && (intval != 1)))
		return false;

	return (intval == 1);
}
