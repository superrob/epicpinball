/*
 * ini.h - epicpinball.ini and controller.ini: [section], key = value, and
 * ; comments, read and written without losing what is around a key.
 */
#pragma once

/* key's value in [sec], else def; a ';' anywhere starts a comment. out is
 * returned. Names are matched ignoring case; the first match wins. */
const char *ini_get(const char *path, const char *sec, const char *key, const char *def, char *out, int n);

/* The same for files whose values hold ';' (VPinballX.ini): only a line
 * starting with ';' is a comment. */
const char *ini_get_raw(const char *path, const char *sec, const char *key, const char *def, char *out, int n);

/* Sets key in [sec] to value, keeping every other line, the file's comments
 * and its line ends. The line is rewritten in place (a comment after the
 * value stays); a missing key goes at the end of its section, a missing
 * section at the end of the file. value NULL removes the key. A value may not
 * hold ';' or a line end. 1 when written. */
int ini_set(const char *path, const char *sec, const char *key, const char *value);
