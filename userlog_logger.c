
/* -*- Mode: C; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 * arcus-memcached - Arcus memory cache server
 * Copyright 2010-2014 NAVER Corp.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "config.h"
//#include <syslog.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <memcached/extension.h>
#include <memcached/engine.h>
#include <time.h>
#include <errno.h>
#include <sys/time.h>
#include "protocol_extension.h"

static EXTENSION_LOG_LEVEL current_log_level = EXTENSION_LOG_WARNING;
SERVER_HANDLE_V1 *sapi;

static const char *get_name(void) {
    return "userlog";
}

/* Instead of syslog which needs su authority, make and use user's log files : 2016.02 */
   /* make & use 5 log files, rotate the files, append the created date to the file name */
   /* make a directory containing log files, named "/ARCUSlog" under the current directory */
   /* definitions and data */
#define NUM_LOGFILE 5
#define MAX_LOGFILE_SIZE (1024*1024*10)  // for now, 10M
#define DEFAULT_LOGFILE_NAME "arcuslog"
#define LOGDIRECTORY         "./ARCUSlog"

static char logfile_name[NUM_LOGFILE][40];
static int current_file;		// currently which file is used among NUM_LOGFILE
static FILE *current_fp;
static int current_flength;		// current file length

static void do_make_logname(int filenum, char *ret_name)
{
	/* log file name : DIR-NAME + "/" + DEFAULT-NAME + sequence no(0~4) + date */
    if (ret_name == NULL || filenum < 0 || filenum > NUM_LOGFILE)  return;
    char buf[20];
    sprintf(ret_name, "%s/%s", LOGDIRECTORY, DEFAULT_LOGFILE_NAME);
    sprintf(buf, "%d_", filenum);      strcat(ret_name, buf);
    struct tm *day;
    time_t clock = time(0);
    day = localtime(&clock);
    sprintf(buf, "%d_%d_%d", day->tm_year+1900,day->tm_mon+1,day->tm_mday);
    strcat(ret_name, buf);
}


static void logger_log(EXTENSION_LOG_LEVEL severity,
                       const void* client_cookie,
                       const char *fmt, ...)
{
    (void)client_cookie;
    if (severity >= current_log_level) {
             /* userlog codes */
        if (current_flength >= MAX_LOGFILE_SIZE) {
	    fclose(current_fp);
	    current_file++;
	    if ( current_file == NUM_LOGFILE)  current_file = 0;
	    remove(logfile_name[current_file]);    // To maintain the total # of log files
	    do_make_logname(current_file, logfile_name[current_file]);
	    current_fp = fopen(logfile_name[current_file], "w");
	    current_flength = 0;
	}
	if (current_fp == NULL) {
	    printf("\n FATAL error : can't open user log file: %s\n", logfile_name[current_file]);
            return ;
	}    /* end of userlog codes */

        char buffer[2048];
        va_list ap;
        va_start(ap, fmt);
        int len = vsnprintf(buffer, sizeof(buffer), fmt, ap);
        va_end(ap);

        if (len != -1) {
            fprintf(current_fp, "%s", buffer);
	    fflush(current_fp);
	    current_flength += len;
        }
    }
}

static EXTENSION_LOGGER_DESCRIPTOR descriptor = {
    .get_name = get_name,
    .log = logger_log
};

static void on_log_level(const void *cookie, ENGINE_EVENT_TYPE type,
                         const void *event_data, const void *cb_data) {
    if (sapi != NULL) {
        current_log_level = sapi->log->get_level();
    }
}

MEMCACHED_PUBLIC_API
EXTENSION_ERROR_CODE memcached_extensions_initialize(const char *config,
                                                     GET_SERVER_API get_server_api) {

    sapi = get_server_api();
    if (sapi == NULL) {
        return EXTENSION_FATAL;
    }
    current_log_level = sapi->log->get_level();

         /* userlog codes */
    if ( mkdir(LOGDIRECTORY, 0744) == -1 ) {
        if (errno != EEXIST) {
	    printf("\n FATAL error : can't make log Directory: %s\n", LOGDIRECTORY);
            return EXTENSION_FATAL;
	}
    }
    do_make_logname(0, logfile_name[0]);
    if ( (current_fp = fopen(logfile_name[0], "w")) == NULL ) {
	    printf("\n FATAL error : can't make log file: %s\n", logfile_name[0]);
        return EXTENSION_FATAL;
    }
    current_file = 0;
    current_flength = 0;
        /* end of userlog codes */

    if (!sapi->extension->register_extension(EXTENSION_LOGGER, &descriptor)) {
        return EXTENSION_FATAL;
    }

    sapi->callback->register_callback(NULL, ON_LOG_LEVEL, on_log_level, NULL);

    return EXTENSION_SUCCESS;
}
