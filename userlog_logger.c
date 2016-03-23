/* -*- Mode: C; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 * arcus-memcached - Arcus memory cache server
 * Copyright 2016 JaM2in Co., Ltd.
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
#include <pthread.h>
#include "protocol_extension.h"

static EXTENSION_LOG_LEVEL current_log_level = EXTENSION_LOG_WARNING;
SERVER_HANDLE_V1 *sapi;

static const char *get_name(void) {
    return "userlog";
}

/* Instead of syslog which needs su authority, make and use user's log files : 2016.02
 * make & use 5 log files, rotate the files, append the created date to the file name
 * make a directory containing log files, named "/ARCUSlog" under the current directory */
/* definitions and data */
#define NUM_LOGFILE 5
#define MAX_LOGFILE_SIZE (1024*1024*10)  // for now, 10M
#define DEFAULT_LOGFILE_NAME "arcus"
#define LOGDIRECTORY         "./ARCUSlog"

static char logfile_name[NUM_LOGFILE][40];
static int  current_file;             // currently which file is used among NUM_LOGFILE
static FILE *current_fp;
static int  current_flength;          // current file length
static char prev_log[2048];           // reserve the previously printed log
static char prev_time[200];           // previous log time
static int  samelog_cnt;              // number of the same log
pthread_mutex_t log_lock;             // userlog thread lock

static void do_make_logname(int filenum, char *ret_name)
{
    /* log file name : DIR-NAME + "/" + DEFAULT-NAME + sequence no(0~4) + date + ".log" */
    if (ret_name == NULL || filenum < 0 || filenum > NUM_LOGFILE)  return;
    char buf[20];
    sprintf(ret_name, "%s/%s", LOGDIRECTORY, DEFAULT_LOGFILE_NAME);
    sprintf(buf, "%d_", filenum);
    strcat(ret_name, buf);
    struct tm *day;
    time_t clock = time(0);
    day = localtime(&clock);
    sprintf(buf, "%d_%d_%d.log", day->tm_year+1900,day->tm_mon+1,day->tm_mday);
    strcat(ret_name, buf);
}

static void do_make_prefix(char *ret_string)
{
    /* prefix : syslog type : time(month(Eng) day hr:min:sec) + hostname + pid */
    struct tm *now;
    time_t clock;
    char buf[50], hname[30];
    clock = time(0);
    now = localtime(&clock);
    (void) strftime(ret_string, 30, "%h %e %T ", now);
    if ( gethostname(hname, sizeof(hname)-1) != 0 )   hname[0] = 0;
    sprintf(buf, "%s memcached[%d]: ", hname, getpid());
    strcat(ret_string, buf);
}


static void logger_log(EXTENSION_LOG_LEVEL severity,
                       const void* client_cookie,
                       const char *fmt, ...)
{
    (void)client_cookie;
    if (severity >= current_log_level) {

        char full_buf[2048], body_buf[2048];
        va_list ap;
        va_start(ap, fmt);
        int len = vsnprintf(body_buf, sizeof(body_buf), fmt, ap);
        va_end(ap);

        /* userlog codes */
	pthread_mutex_lock(&log_lock);
        if ( (len!=strlen(prev_log)) || strcmp(body_buf, prev_log)!=0 ) {
            /* Two log messages are different. Print the count and last time,
             * then restart the count */
            if ( samelog_cnt > 1 ) {
                sprintf(full_buf, "%slast message repeated %d times\n",
                         prev_time, samelog_cnt);
                int tmplen = strlen(full_buf);
                fprintf(current_fp, "%s", full_buf);
                fflush(current_fp);
                current_flength += tmplen;
            }
            do_make_prefix(full_buf);
            strcpy(prev_time, full_buf);
            len += strlen(full_buf);
            strcat(full_buf, body_buf);
            if (len != -1) {
                fprintf(current_fp, "%s", full_buf);
                fflush(current_fp);
                current_flength += len;
            }
            samelog_cnt = 1;
            strcpy(prev_log, body_buf);
        }
        else {  /* The current log 'body_buf and the previous log are same */
            samelog_cnt++;  // Just increase the count and save the time
            do_make_prefix(prev_time);
        }

        if (current_flength >= MAX_LOGFILE_SIZE) {
            fclose(current_fp);
            current_file++;
            if ( current_file == NUM_LOGFILE)  current_file = 0;
            remove(logfile_name[current_file]);    // To maintain the total # of log files
            do_make_logname(current_file, logfile_name[current_file]);
            current_fp = fopen(logfile_name[current_file], "w");
            current_flength = 0;
            if (current_fp == NULL) {
                fprintf(stderr, "\n FATAL error : can't open user log file: %s\n", logfile_name[current_file]);
	        pthread_mutex_unlock(&log_lock);
                return ;
            }
        }
	pthread_mutex_unlock(&log_lock);
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
            fprintf(stderr, "\n FATAL error : can't make log Directory: %s\n", LOGDIRECTORY);
            return EXTENSION_FATAL;
        }
    }
    do_make_logname(0, logfile_name[0]);
    if ( (current_fp = fopen(logfile_name[0], "w")) == NULL ) {
        fprintf(stderr, "\n FATAL error : can't make log file: %s\n", logfile_name[0]);
        return EXTENSION_FATAL;
    }
    current_file = 0;
    current_flength = 0;
    prev_log[0] = 0;
    prev_time[0] = 0;
    samelog_cnt = 1;
    pthread_mutex_init(&log_lock, NULL);
    /* end of userlog codes */

    if (!sapi->extension->register_extension(EXTENSION_LOGGER, &descriptor)) {
        return EXTENSION_FATAL;
    }

    sapi->callback->register_callback(NULL, ON_LOG_LEVEL, on_log_level, NULL);
    return EXTENSION_SUCCESS;
}
