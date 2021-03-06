/*
 * netsniff-ng - the packet sniffing beast
 * By Daniel Borkmann <daniel@netsniff-ng.org>
 * Copyright 2009, 2010 Daniel Borkmann.
 * Subject to the GPL, version 2.
 */

#ifndef TPRINTF_H
#define TPRINTF_H

#define DEFAULT_TTY_SIZE	80

extern void tprintf_init(void);
extern void tprintf(char *msg, ...);
extern void tprintf_flush(void);
extern void tprintf_cleanup(void);

#define __reset			"0"
#define __bold			"1"
#define __black			"30"
#define __red			"31"
#define __green			"32"
#define __yellow		"33"
#define __blue			"34"
#define __magenta		"35"
#define __cyan			"36"
#define __white			"37"
#define __on_black		"40"
#define __on_red		"41"
#define __on_green		"42"
#define __on_yellow		"43"
#define __on_blue		"44"
#define __on_magenta		"45"
#define __on_cyan		"46"
#define __on_white		"47"

#define colorize_start(fore)            "\033[" __##fore "m"
#define colorize_start_full(fore, back) "\033[" __##fore ";" __on_##back "m"
#define colorize_end()                  "\033[" __reset "m"
#define colorize_str(fore, text)                                     \
		colorize_start(fore) text colorize_end()
#define colorize_full_str(fore, back, text)                          \
		colorize_start_full(fore, back) text colorize_end()

#endif /* TPRINTF_H */
