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
#include <sys/stat.h>

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <limits.h>
#include <errno.h>

#include <dvdnav/dvdnav.h>
#include <libbluray/bluray.h>

#ifdef HAVE_VERSION_H
# include "version.h"
#endif

#define ERR_OK      0
#define ERR_OPEN    1
#define ERR_OTHER   2

#if defined(__APPLE__)
# define DEFAULT_DEVICE  "/dev/rdisk1"
#else //elif defined(__linux__)
# define DEFAULT_DEVICE  "/dev/sr0"
#endif

static struct { char short_opt; const char *desc; const char* arg; } s_opt_desc[] = {
	{ 'h', "show usage", NULL },
	{ 'V', "show version", NULL },
	{ 's', "show source", NULL },
	{ 'v', "verbose", NULL },
	{ 'm', "minimum title duration in seconds", NULL },
	{ 0, NULL, NULL }
};
static struct { char short_opt; const char *long_opt; } s_opt_long[] = {
	{ 's', "source" },
	{ 'V', "version" },
	{ 'v', "verbose" },
	{ 'h', "help" },
	{ 'm', "minimum" },
	{ 0, NULL }
};
typedef struct {
    char *devpath;
    unsigned int min_title_secs;
    unsigned int loglevel;
} options_t;
static int usage(int exit_status, int argc, char **argv);
static int version(FILE *out, const char *name);

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
            fprintf(stdout, "  [<device_or_path>]    : (optional) dvd/bluray device/path to scan, %s by default\n", DEFAULT_DEVICE);
            fprintf(stdout, "\nDescription:\n"
                    "  This programs scans a dvd or bluray and outputs:\n"
                    "    ID <hex>\n"
                    "    NAME <name>\n"
                    "    TITLE <n> DURATION <secs.ms> <hh:mm:ss.ms> CHAPTERS <secs.ms1> <hh:mm:ss.ms1> ...\n"
                    "    SUB <n> <id> <name>\n"
                    "    AUDIO <n> <id> <name>\n\n");
            return 0;
        case 'V':
            version(stdout, BUILD_APPNAME);
            return 0;
        case 'v':
            ++options->loglevel; break ;
        case 'm': {
            char * end = NULL;
            if (arg == NULL) {
                fprintf(stderr, "error: argument required for option '-m'\n");
                return -1;
            }
            ++(*i_argv);
            errno = 0;
            options->min_title_secs = strtoul(arg, &end, 0);
            if (end == NULL || *end != 0 || errno != 0) {
                fprintf(stderr, "error: argument for option '-m' should be a number\n"); return -1;
            }
            break ;
        }
        case 's':
            vdvdnav_info_get_source(stdout, NULL, 0, NULL);
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
#if 0
void tab_set(void * value, size_t index, void ** array, size_t * arraysz) {
    if (array == NULL || arraysz == NULL)
        return ;
    if (index > *arraysz) {
        if ((*array = realloc(*array, (index * 2) * sizeof(**array))) == NULL)
            return ;
        memset(*array + index, (index*2) * sizeof(**array), -1);
    }
    (*array)[index] = value;
}
void * tab_get(size_t index, void * array, size_t arraysz) {
    if (index > arraysz)
        return (void*)-1;
    return array[index]
}
#define SET_INT(value, index, parray, parraysz) (tab_set((void*)((ptrdiff_t)value), index, parray, parraysz))
#define GET_INT(index, array, arraysz)          ((int)tab_get(index, array, arraysz))
#endif
int process_bluray(options_t * opts) {
    BLURAY *                    br;
    const BLURAY_DISC_INFO *    disc_info;
    BLURAY_TITLE_INFO *         title_info;
    unsigned int                ntitles;
    uint64_t                    duration;
    uint64_t                    max_duration = 0;
    uint32_t                    longest_title = 0;
    FILE * const                out = stdout;
    int                         sub = 1, audio = 1;
    void *                      subs = NULL, * audios = NULL;
    //size_t                      subs_sz = 0, audio_sz = 0;
    char                        buf[1024];

    if (opts->loglevel > 1) {
        setenv("BD_DEBUG_MASK", "2566", 1);
        setenv("AACS_DEBUG_MASK", "65535", 1);
    } else if (opts->loglevel > 0) {
        setenv("BD_DEBUG_MASK", "4", 1);
        setenv("AACS_DEBUG_MASK", "7", 1);
    } else {
        setenv("BD_DEBUG_MASK", "0", 1);
        setenv("AACS_DEBUG_MASK", "0", 1);
    }

    if ((br = bd_open(opts->devpath, NULL)) == NULL) {
        fprintf(stderr, "bluray_open: error openning %s.\n", opts->devpath);
        return ERR_OPEN;
    }

    if ((disc_info = bd_get_disc_info(br)) == NULL) {
        fprintf(stderr, "bluray_get_disc_info: error.\n");
        return ERR_OTHER;
    }
    //char disc_id[1 + 2 * sizeof(disc_info->disc_id)/sizeof(*disc_info->disc_id)]; disc_id[sizeof(disc_id)-1] = 0;
    size_t buf_len = 2 * sizeof(disc_info->disc_id)/sizeof(*disc_info->disc_id) + 1;
    buf[buf_len] = 0;
    for (size_t i = 0; i < sizeof(disc_info->disc_id)/sizeof(*disc_info->disc_id); ++i)
        snprintf(buf + i*2, buf_len - 1 - i*2, "%02x", disc_info->disc_id[i] & 0xff);
    fprintf(stdout, "%-7s %s\n", "ID", buf);
    fprintf(stdout, "%-7s %s\n", "NAME", disc_info->disc_name);

    /* uint32_t bd_get_titles(BLURAY *bd, uint8_t flags, uint32_t min_title_length); */
    if ((ntitles = bd_get_titles(br, TITLES_RELEVANT, 0)) == 0) {
        fprintf(stderr, "bluray_get_titles(): no title.\n");
        bd_close(br);
        return ERR_OTHER;
    }

    for (unsigned int i = 0; i < ntitles; ++i) {
        //BLURAY_TITLE_INFO* bd_get_title_info(BLURAY *bd, uint32_t title_idx, unsigned angle);
        if ((title_info = bd_get_title_info(br, i, 0)) == NULL) {
            fprintf(stderr, "bluray_get_title_info(%d): error.\n", i);
            continue ;
        }
        duration = title_info->duration;
        duration = ((duration * 100) / 90) / 100;

        if (opts->min_title_secs > 0 && duration / 1000 < opts->min_title_secs) {
            bd_free_title_info(title_info);
            continue ;
        }

        if (duration > max_duration) {
            max_duration = duration;
            longest_title = title_info->idx;
        }

        fprintf(out, "TITLE % 3d DURATION % 9lld.%03lld %02lld:%02lld:%02lld.%03lld CHAPTERS",
                title_info->idx + 1, duration / 1000, duration % 1000,
                (duration / 1000) / 3600, ((duration / 1000) / 60) % 60,
                (duration / 1000) % 60, duration % 1000);

        for (unsigned int c = 0; c < title_info->chapter_count; ++c) {
            duration = title_info->chapters[c].start;
            duration = ((duration * 100) / 90) / 100;

            fprintf(out, " %llu.%03llu %02llu:%02llu:%02llu.%03llu",
                    duration / 1000ULL, duration % 1000ULL,
                    (duration / 1000ULL) / 3600, ((duration / 1000ULL) / 60) % 60,
                    (duration / 1000ULL) % 60, duration % 1000ULL);
        }

        fprintf(out, "\n");

        if (sub || audio) {
            for (unsigned int c = 0; c < title_info->clip_count; ++c) {
                unsigned int s;
                //char *elt;
                for (s = 0; s < title_info->clips[c].pg_stream_count; ++s) {
                    //unsigned int lang = title_info->clips[c].pg_streams[s].pid;
                    unsigned char * lang = title_info->clips[c].pg_streams[s].lang;
                    snprintf(buf, sizeof(buf)/sizeof(*buf), "%-7s % 2d % 2d %s\n", "SUB", i, s, lang);
                    //if (elt
                }
                /* no title_info->clips[c].ig_streams */
                if (s)
                    sub = 0;
            }

            for (unsigned int c = 0; c < title_info->clip_count; ++c) {
                unsigned int s;
                //char * tab;
                for (s = 0; s < title_info->clips[c].audio_stream_count; ++s) {
                    unsigned char * lang = title_info->clips[c].audio_streams[s].lang;
                    fprintf(out, "%-7s % 2d % 2d %s\n", "AUDIO", i, s, lang);
                }
                /* no title_info->clips[c].ig_streams */
                if (s)
                    audio = 0;
            }
        }

        bd_free_title_info(title_info);
    }
    fprintf(out, "%-7s %d\n", "LONGEST", longest_title);

    bd_close(br);
    if (subs != NULL)
        free(subs);
    if (audios != NULL)
        free(audios);

    return ERR_OK;
}

#if defined(DVDNAV_VERSION) && DVDNAV_VERSION > 60000
static void log_dvdnav ( void * data, dvdnav_logger_level_t level, const char * fmt, va_list va) {
    options_t * opts = (options_t *) data;
    if (opts->loglevel > 0 || level < DVDNAV_LOGGER_LEVEL_INFO) {
        FILE * out = stderr;
        flockfile(out);
        fprintf(out, "[dvdnav] ");
        vfprintf(out, fmt, va);
        fputc('\n', out);
        funlockfile(out);
    }
}
#endif

int process_dvd(options_t * opts) {
    dvdnav_status_t status = DVDNAV_STATUS_ERR;
    dvdnav_t *      nav = NULL;
    const char * discname = NULL, * id = NULL, * discpath = NULL;
    char * path = NULL;

#if defined(DVDNAV_VERSION) && DVDNAV_VERSION > 60000
    dvdnav_logger_cb logger = { .pf_log = log_dvdnav };
    if ((status = dvdnav_open2(&nav, opts, &logger, opts->devpath)) != DVDNAV_STATUS_OK) {
#else
    if ((status = dvdnav_open(&nav, opts->devpath)) != DVDNAV_STATUS_OK) {
#endif
        fprintf(stderr, "dvdnav_open: error openning %s.\n", opts->devpath);
        return ERR_OPEN;
    }

    if (dvdnav_get_title_string(nav, &discname) != DVDNAV_STATUS_OK) {
        fprintf(stderr, "dvdnav_get_title_string: error: %s\n", dvdnav_err_to_string(nav));
    }
    if (dvdnav_get_serial_string(nav, &id) != DVDNAV_STATUS_OK) {
        fprintf(stderr, "dvdnav_get_serial_string: error: %s\n", dvdnav_err_to_string(nav));
    }
    if (dvdnav_path(nav, &discpath) != DVDNAV_STATUS_OK) {
        fprintf(stderr, "dvdnav_path: error: %s\n", dvdnav_err_to_string(nav));
    } else if (discpath != NULL && (path = strdup(discpath)) != NULL) {
        ssize_t i, len = strlen(path);
        for (i = len - 1; i > 0 && (path[i] == '/' || path[i] == '\\'); --i) ; /* nothing */
        path[i+1] = 0;
        for (; i > 0 && path[i] != '/' && path[i] != '\\'; --i) ; /* nothing */
        if (i > 0) {
            memmove(path, path + i + 1, len - i - 1);
            path[len - i - 1] = 0;
        }
    }
    if (id == NULL || *id == 0)
        id = path;
    if (discname == NULL || *discname == 0)
        discname = path;
    fprintf(stdout, "%-7s %s\n", "ID", id);
    fprintf(stdout, "%-7s %s\n", "NAME", discname);
    if (path != NULL)
        free(path);

    do {
        int32_t ntitles;
        FILE * const out = stdout;

        //char discname[64], id[64];
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

                if (opts->min_title_secs > 0 && duration / 1000 < opts->min_title_secs) {
                    free(times);
                    continue ;
                }

                if (duration > max_duration) {
                    max_duration = duration;
                    longest_title = title;
                }
                fprintf(out, "TITLE % 3d DURATION % 9lld.%03lld %02lld:%02lld:%02lld.%03lld CHAPTERS", title, duration / 1000, duration % 1000,
                        (duration / 1000) / 3600, ((duration / 1000) / 60) % 60, (duration / 1000) % 60, duration % 1000);
                for (uint32_t chapter = 0; chapter < nchapters; chapter++) {
                    duration = ((times[chapter] * 100) / 90) / 100;
                    fprintf(out, " %lld.%03lld %02lld:%02lld:%02lld.%03lld", duration / 1000, duration % 1000,
                            (duration / 1000) / 3600, ((duration / 1000) / 60) % 60, (duration / 1000) % 60, duration % 1000);
                }
                fprintf(out, "\n");
                free(times);
            } else {
                fprintf(stderr, "dvdnav_describe_title_chapters(title %d): error: %s\n", title, dvdnav_err_to_string(nav));
            }
        }
        fprintf(out, "%-7s %d\n", "LONGEST", longest_title);

        dvdnav_title_play(nav, longest_title);
        for (uint8_t sub_idx = 0; sub_idx < 32; sub_idx++) {
            uint8_t sub_log = dvdnav_get_spu_logical_stream(nav, sub_idx);
            uint8_t aud_log = dvdnav_get_audio_logical_stream(nav, sub_idx);

            if (sub_log != 0xff) {
                uint16_t sub_lang = dvdnav_spu_stream_to_lang(nav, sub_log);
                if (sub_lang != 0xffff) {
                    fprintf(out, "%-7s % 2d % 2d %c%c\n", "SUB", longest_title, sub_log, sub_lang >> 8, sub_lang);
                }
            }
            if (aud_log != 0xff) {
                uint16_t aud_lang = dvdnav_audio_stream_to_lang(nav, aud_log);
                if (aud_lang != 0xffff) {
                    fprintf(out, "%-7s % 2d % 2d %c%c\n", "AUDIO", longest_title, aud_log, aud_lang >> 8, aud_lang);
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

int main(int argc, char **argv) {
    options_t       options = { .devpath = NULL, .min_title_secs = 0, .loglevel = 0 };
    int             result;
    struct stat     stats;
    char            path[PATH_MAX] = {0, };

    if ((result = parse_options(argc, argv, &options)) <= 0) {
	    return -result;
    }

    version(stderr, BUILD_APPNAME);

    if (options.devpath == NULL) {
   	    options.devpath = DEFAULT_DEVICE;
    }

    fprintf(stderr, "searching titles on %s...\n", options.devpath);
    strcpy(path, options.devpath);
    strcat(path, "/");
    strcat(path, "BDMV/index.bdmv");

    if (stat(path, &stats) == 0) {
        return process_bluray(&options);
    } else {
        return process_dvd(&options);
    }

}

/** OPTIONS *********************************************************************************/
#ifndef BUILD_APPNAME
# define BUILD_APPNAME "dvdnav"
#endif
#ifndef APP_INCLUDE_SOURCE
# define APP_NO_SOURCE_STRING "\n/* #@@# FILE #@@# " BUILD_APPNAME "/* */\n" \
                                  BUILD_APPNAME " source not included in this build.\n"
int vdvdnav_info_get_source(FILE * out, char * buffer, unsigned int buffer_size, void ** ctx) {
        return fprintf(out, APP_NO_SOURCE_STRING);
        //return vdecode_buffer(out, buffer, buffer_size, ctx,
        //                      APP_NO_SOURCE_STRING, sizeof(APP_NO_SOURCE_STRING) - 1);
}
#endif
#ifndef BUILD_SRCPATH
# define BUILD_SRCPATH "."
#endif
#ifndef BUILD_NUMBER
# define BUILD_NUMBER 1998
#endif
#ifndef APP_VERSION
# define APP_VERSION "0.1_beta-116"
#endif
static int version(FILE *out, const char *name) {
    char dvdnav_ver[256];
#if defined(DVDNAV_VERSION) && DVDNAV_VERSION > 60000
    snprintf(dvdnav_ver, sizeof(dvdnav_ver)/sizeof(*dvdnav_ver), "%s", dvdnav_version());
#else
# ifndef DVDNAV_VERSION
#  define DVDNAV_VERSION 0
# endif
    snprintf(dvdnav_ver, sizeof(dvdnav_ver)/sizeof(*dvdnav_ver), "%d.%d.%d",
             DVDNAV_VERSION/10000, (DVDNAV_VERSION%10000)/100, (DVDNAV_VERSION%10000)%100);
#endif
    int bd_maj = 0, bd_min = 0, bd_rev = 0;
    bd_get_version(&bd_maj, &bd_min, &bd_rev);
    fprintf(out, "%s %s build #%d on %s, %s git:%s from %s/%s\n  with: libdvdnav %s, libbluray %d.%d.%d\n",
            name, APP_VERSION, BUILD_NUMBER, __DATE__, __TIME__, BUILD_GITREV, BUILD_SRCPATH, __FILE__,
            dvdnav_ver, bd_maj, bd_min, bd_rev);
    return 0;
}
static int usage(int exit_status, int argc, char **argv) {
    FILE * out = exit_status ? stderr : stdout;
    char *start_name = strrchr(*argv, '/');
    (void) argc;

    if (start_name == NULL) {
        start_name = *argv;
    } else {
        start_name++;
    }

    version(out, start_name);
    fprintf(out, "\nUsage: %s [<options>] [<arguments>]\n", start_name);
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
