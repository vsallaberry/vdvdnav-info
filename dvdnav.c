/*
 * Copyright (C) 2017-2020 Vincent Sallaberry
 * dvdnav-info <https://github.com/vsallaberry>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */
/*
 *  show dvd titles/chapter and subs with libdvdnav
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <dvdnav/dvdnav.h>

#ifdef HAVE_VERSION_H
# include "version.h"
#endif

#define ERR_OK      0
#define ERR_OPEN    1
#define ERR_OTHER   2

static struct { char short_opt; const char *desc; const char* arg; } s_opt_desc[] = {
	{ 'h', "show usage", NULL },
	{ 's', "show source", NULL },
	{ 0, NULL, NULL }
};
static struct { char short_opt; const char *long_opt; } s_opt_long[] = {
	{ 's', "source" },
	{ 'h', "help" },
	{ 0, NULL }
};
typedef struct {
    char *devpath;
} options_t;
static int usage(int exit_status, int argc, char **argv);

/* parse_options() : Main entry point for generic options parsing
 * Returns
 *  > 0 on SUCCESS with no exit required
 *  0 on SUCCESS, with exit required
 *  < 0 on ERROR
 */
static int parse_options(int argc, char **argv, void *options);
/* parse_option() : handler for customized option management
 *   opt: the char option to be treated or '-' if it is a simple program argument
 *   arg: the following argument or simple program argument if opt is '-'
 *   v_options: the custom options structure
 *   argc, argv, i_argv: the parser state
 * Returns
 *  > 0 on SUCCESS, no exit required
 *  0   on SUCCESS, exit required
 *  < 0 on ERROR
 */
static int parse_option(char opt, char *arg, void *v_options, int argc, char **argv, int *i_argv) {
    options_t *options = (options_t *) v_options;
    switch (opt) {
        case 'h':
            usage(0, argc, argv);
            fprintf(stdout, "Arguments:\n");
	        fprintf(stdout, "  [dvd_device]    : (optional) dvd device/path to scan, /dev/disk1 by default\n");
	    fprintf(stdout, "\nDescription:\n"
			    "  This programs scans a dvd and output:\n"
			    "    TITLE <n> DURATION <secs.ms> <hh:mm:ss.ms> CHAPTERS <chapters_secs.ms>\n"
			    "    SUB <n> <id> <name>\n\n");
	        return 0;
        case 's':
            //fprintf(stdout, "%s\n", get_program_source());
            //break ;
            dvdnav_get_source(stdout, NULL, 0, NULL);
            //vlib_get_source(stdout, NULL, 0, NULL);
	        return 0;
	case '-':
	    if (options->devpath != NULL) {
		fprintf(stderr, "error: device path can be given only once.\n");
		return -1;
	    }
	    options->devpath = arg;
	    return 1;
        default:
	   return -1;
    }
    return 1;
}
int main(int argc, char **argv) {
    options_t options = { .devpath = NULL };
    int result;

    if ((result = parse_options(argc, argv, &options)) <= 0) {
	return -result;
    }

    if (options.devpath == NULL) {
   	options.devpath = "/dev/disk1";
    }
    dvdnav_status_t status = DVDNAV_STATUS_ERR;
    dvdnav_t *      nav = NULL;

    fprintf(stderr, "searching titles on dvd %s...\n", options.devpath);
    if ((status = dvdnav_open(&nav, options.devpath)) != DVDNAV_STATUS_OK) {
        fprintf(stderr, "dvdnav_open: error openning %s.\n", options.devpath);
        return ERR_OPEN;
    }

    do {
        int32_t ntitles;
        FILE * const out = stdout;
        uint64_t max_duration = 0;
        uint32_t longest_title = 0;

        if ((status = dvdnav_set_readahead_flag(nav, 0)) != DVDNAV_STATUS_OK) {
            fprintf(stderr, "dvdnav_set_readahead_flag: error: %s\n", dvdnav_err_to_string(nav));
            break ;
        }

        if ((status = dvdnav_get_number_of_titles(nav, &ntitles)) != DVDNAV_STATUS_OK) {
            fprintf(stderr, "dvdnav_get_number_of_titles: error: %s\n", dvdnav_err_to_string(nav));
            break ;
        }

        for (int32_t title = 1; title <= ntitles; title++) {
            uint64_t *times = NULL;
            uint64_t duration = 0;
            uint32_t nchapters;

            nchapters = dvdnav_describe_title_chapters(nav, title, &times, &duration);
            if (nchapters > 0 && times != NULL) {
                duration = ((duration * 100) / 90) / 100;
                fprintf(out, "TITLE % 3d DURATION % 9lld.%03lld %02lld:%02lld:%02lld.%03lld CHAPTERS", title, duration / 1000, duration % 1000,
                        (duration / 1000) / 3600, ((duration / 1000) / 60) % 60, (duration / 1000) % 60, duration % 1000);
                for (uint32_t chapter = 0; chapter < nchapters; chapter++) {
                    duration = ((times[chapter] * 100) / 90) / 100;
                    fprintf(out, " %lld.%03lld", duration / 1000, duration % 1000);
                    if (duration > max_duration) {
                        max_duration = duration;
                        longest_title = title;
                    }
                }
                fprintf(out, "\n");
                free(times);
            } else {
                fprintf(stderr, "dvdnav_describe_title_chapters(title %d): error: %s\n", title, dvdnav_err_to_string(nav));
            }
        }

        dvdnav_title_play(nav, longest_title);
        for (uint8_t sub_idx = 0; sub_idx < 32; sub_idx++) {
            uint8_t sub_log = dvdnav_get_spu_logical_stream(nav, sub_idx);
            if (sub_log != 0xff) {
                uint16_t sub_lang = dvdnav_spu_stream_to_lang(nav, sub_log);
                if (sub_lang != 0xffff) {
                    fprintf(out, "SUB % 2d % 2d %c%c\n", longest_title, sub_log, sub_lang >> 8, sub_lang);
                }
            }
        }
        dvdnav_stop(nav);

    } while (0);

    if (dvdnav_close(nav) != DVDNAV_STATUS_OK) {
        fprintf(stderr, "dvdnav_close: error: %s\n", dvdnav_err_to_string(nav));
    }
    return status == DVDNAV_STATUS_OK ? ERR_OK : ERR_OTHER;
}

/** OPTIONS *********************************************************************************/
#ifndef BUILD_APPNAME
# define BUILD_APPNAME "dvdnav"
#endif
#ifndef APP_INCLUDE_SOURCE
# define APP_NO_SOURCE_STRING "\n/* #@@# FILE #@@# " BUILD_APPNAME "/* */\n" \
                                  BUILD_APPNAME " source not included in this build.\n"
int dvdnav_get_source(FILE * out, char * buffer, unsigned int buffer_size, void ** ctx) {
        return fprintf(out, APP_NO_SOURCE_STRING);
        //return vdecode_buffer(out, buffer, buffer_size, ctx,
        //                      APP_NO_SOURCE_STRING, sizeof(APP_NO_SOURCE_STRING) - 1);
}
#endif
#ifndef BUILD_SRCPATH
# define BUILD_SRCPATH "."
#endif
#ifndef APP_BUILD_NUMBER
# define APP_BUILD_NUMBER 1998
#endif
#ifndef APP_VERSION
# define APP_VERSION "0.1_beta-116"
#endif
static int usage(int exit_status, int argc, char **argv) {
    FILE * out = exit_status ? stderr : stdout;
    char *start_name = strrchr(*argv, '/');
    if (start_name == NULL) {
	start_name = *argv;
    } else {
	start_name++;
    }

    fprintf(out, "%s %s build #%d on %s, %s from %s/%s\n\n", start_name, APP_VERSION, APP_BUILD_NUMBER, __DATE__, __TIME__, BUILD_SRCPATH, __FILE__);
    fprintf(out, "Usage: %s [<options>] [<arguments>]\n", start_name);
    for (int i_opt = 0; s_opt_desc[i_opt].short_opt; i_opt++) {;
	int n_printed;
	n_printed = fprintf(out, "  -%c", s_opt_desc[i_opt].short_opt);
	for (int i_long = 0; s_opt_long[i_long].short_opt; i_long++) {
	    if (s_opt_long[i_long].short_opt == s_opt_desc[i_opt].short_opt) {
	        n_printed += fprintf(out, ", --%s", s_opt_long[i_long].long_opt);
		break ;
	    }
	};
	if (s_opt_desc[i_opt].arg) {
	    n_printed += fprintf(out, " %s", s_opt_desc[i_opt].arg);
	}
	while (n_printed++ < 30) {
	    fprintf(out, " ");
	}
	fprintf(out, ": %s\n", s_opt_desc[i_opt].desc);
    }
    fprintf(out, "\n");
    return exit_status;
}
static int parse_options(int argc, char **argv, void *options) {
    for(int i_argv = 1, stop_options = 0; i_argv < argc; i_argv++) {
	int result;
        if (*argv[i_argv] == '-' && !stop_options) {
	    char *short_args = argv[i_argv] + 1;
	    char short_arg_from_long[2] = { 0, 0 };
	    if (!*short_args) {
		fprintf(stderr, "error: missing option\n");
		return usage(-3, argc, argv);
	    }
	    if (*short_args == '-') {
		int i_long;
		if (!short_args[1]) {
		    stop_options = 1;
		    continue ;
		}
	        for (i_long = 0; s_opt_long[i_long].short_opt; i_long++) {
	    	    if (!strcmp(argv[i_argv] + 2, s_opt_long[i_long].long_opt)) {
	    		short_arg_from_long[0] = s_opt_long[i_long].short_opt;
    			short_args = short_arg_from_long;
			break ;
		    }
		}
		if (!s_opt_long[i_long].short_opt) {
		    fprintf(stderr, "error: unknown option '%s'\n", argv[i_argv]);
		    return usage(-2, argc, argv);
		}
	    }
            for (char *arg = short_args; *arg; arg++) {
		result = parse_option(*arg, i_argv + 1 < argc ? argv[i_argv+1]: NULL, options, argc, argv, &i_argv);
		if (result < 0) {
                    fprintf(stderr, "error: unknown/incorrect option '-%c'\n", *arg);
                    return usage(-1, argc, argv);
	        }
		if (result == 0) {
		    return 0;
		}
            }
        } else {
	    char *arg = argv[i_argv];
	    result = parse_option('-', arg, options, argc, argv, &i_argv);
	    if (result < 0) {
                fprintf(stderr, "error: incorrect argument %s\n", arg);
		return usage(-3, argc, argv);
	    }
	    if (result == 0) {
		return 0;
	    }
        }
    }
    return 1;
}
