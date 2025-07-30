#ifndef __LIB_LOGGING
#define __LIB_LOGGING

#include <time.h>

typedef enum {
    LOG_INFO,
    LOG_WARNING,
    LOG_ERROR,
} LogType;

void Log_writeTimeStamp(Stream *s) {
    time_t t;
    struct tm tinfo;
    time(&t);
    gmtime_r(&t, &tinfo);

    usz year = tinfo.tm_year + 1900;
    usz month = tinfo.tm_mon + 1;
    usz day = tinfo.tm_mday;

    usz hour = tinfo.tm_hour;
    usz minute = tinfo.tm_min;
    usz second = tinfo.tm_sec;

    writeU64ToDecimal(s, year);

    stream_writeChar(s, '-');

    if(month < 10) {
        stream_writeChar(s, '0');
    }
    writeU64ToDecimal(s, month);

    stream_writeChar(s, '-');

    if(day < 10) {
        stream_writeChar(s, '0');
    }
    writeU64ToDecimal(s, day);

    stream_writeChar(s, ' ');
    stream_writeChar(s, '/');
    stream_writeChar(s, ' ');

    if(hour < 10) {
        stream_writeChar(s, '0');
    }
    writeU64ToDecimal(s, hour);

    stream_writeChar(s, ':');

    if(minute < 10) {
        stream_writeChar(s, '0');
    }
    writeU64ToDecimal(s, minute);

    stream_writeChar(s, ':');

    if(second < 10) {
        stream_writeChar(s, '0');
    }
    writeU64ToDecimal(s, second);
}

void Log_writeType(Stream *s, LogType type) {
    if(false) {}
    else if(type == LOG_INFO) {
        stream_write(s, mkString("\e[1;37mINFO\e[0m"));
    }
    else if(type == LOG_WARNING) {
        stream_write(s, mkString("\e[1;33mWARNING\e[0m"));
    }
    else if(type == LOG_ERROR) {
        stream_write(s, mkString("\e[1;31mERROR\e[0m"));
    }
}

void Log_messageAlways(LogType type, String message) {
    char buffer[1024] = {0};
    StringBuilder sb = mkStringBuilderMem(mkMem(buffer, 1024));
    Stream s = mkStreamSb(&sb);

    stream_writeChar(&s, '[');
    stream_writeChar(&s, ' ');
    Log_writeTimeStamp(&s);
    stream_writeChar(&s, ' ');
    stream_writeChar(&s, ']');

    stream_writeChar(&s, ' ');

    stream_writeChar(&s, '[');
    Log_writeType(&s, type);
    stream_writeChar(&s, ']');

    stream_writeChar(&s, ' ');
    stream_writeChar(&s, '|');
    stream_writeChar(&s, ' ');

    stream_write(&s, message);
    stream_writeChar(&s, '\n');

    String result = sb_build(sb);
    write(STDOUT_FILENO, result.s, result.len);
}

#define Log_message(type, msg) Log_messageAlways(type, msg)

#define Log_message1(type, msg) Log_messageAlways(type, msg);

#define Log_message2(type, msg) Log_messageAlways(type, msg);

#endif // __LIB_LOGGING
