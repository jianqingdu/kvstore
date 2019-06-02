/*
 * simple_log.cpp
 *
 *  Created on: 2016-3-14
 *      Author: ziteng
 */

#include "simple_log.h"
#include "util.h"
#include "thread_pool.h"
#include <sys/stat.h>
#include <sys/time.h>
#include <string.h>
#include <assert.h>
#include <dirent.h>

class CleanLogThread : public Thread {
public:
    CleanLogThread() {}
    virtual ~CleanLogThread() {}
    
    void AddLogPath(const string& path) {
        string log_path = path;
        size_t len = log_path.size();
        if (log_path.at(len - 1) != '/') {
            log_path.append(1, '/');
        }
        log_paths_.insert(log_path);
    }
    
    void OnThreadRun() override {
        while (m_running) {
            sleep(3600);
            
            time_t cur_time = time(NULL);
            for (const string& path : log_paths_) {
                // iterate all log file in path, and delete all log files that was created 30 days ago
                DIR* dirp = opendir(path.c_str());
                if (!dirp) {
                    log_message(kLogLevelError, "opendir log path failed: errno=%d\n", errno);
                    continue;
                }
                
                dirent* dp = NULL;
                while ((dp = readdir(dirp)) != NULL) {
                    if (dp->d_type != DT_REG) {
                        continue;
                    }
                    
                    string log_file = path + dp->d_name;
                    struct stat stat_buf;
                    if (!stat(log_file.c_str(), &stat_buf) && (stat_buf.st_mtime + 3600 * 24 * 30 < cur_time)) {
                        log_message(kLogLevelInfo, "delete old log file: %s\n", log_file.c_str());
                        remove(log_file.c_str());
                    }
                }
            }
        }
    }
private:
    set<string> log_paths_;
};

CleanLogThread g_clean_log_thread;

SimpleLog::SimpleLog()
{
    log_file_ = NULL;
    last_log_time_ = 0;
    log_level_ = kLogLevelDebug;
}

SimpleLog::~SimpleLog()
{
    if (log_file_) {
        fclose(log_file_);
        log_file_ = NULL;
    }
}

void SimpleLog::Init(LogLevel level, const string& path)
{
    // use last_log_time_ to determine if initialized
    if (last_log_time_) {
        return;
    }
    
    log_level_ = level;
    log_path_ = path;
    create_path(log_path_);
    
    last_log_time_ = time(NULL);
    OpenFile();
    
    if (!g_clean_log_thread.IsRunning()) {
        g_clean_log_thread.StartThread();
    }
    g_clean_log_thread.AddLogPath(path);
}

void SimpleLog::LogMessage(LogLevel level, const char* fmt, ...)
{
    if (level > log_level_) {
        return;
    }
    
    struct timeval tval;
	struct tm tm_now;
	time_t curr_time;
	time(&curr_time);
	localtime_r(&curr_time, &tm_now);
	gettimeofday(&tval, NULL);
    
    lock_guard<mutex> mg(mutex_);
	int prefix_len = snprintf(log_buf_, sizeof(log_buf_), "%02d:%02d:%02d.%03d(tid=%d) %s ",
                              tm_now.tm_hour, tm_now.tm_min, tm_now.tm_sec, (int)tval.tv_usec / 1000,
                              (uint16_t)(long)pthread_self(), kLogLevelName[level].c_str());
    
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(log_buf_ + prefix_len, kMaxLogLineSize - prefix_len, fmt, ap);
    va_end(ap);
    
    if (IsNewDay(curr_time)) {
        if (log_file_) {
            fclose(log_file_);
            log_file_ = NULL;
        }
        
        OpenFile();
    }
    
    if (log_file_) {
        fwrite(log_buf_, strlen(log_buf_), 1, log_file_);
        fflush(log_file_);
    }
    
    last_log_time_ = curr_time;
}

void SimpleLog::OpenFile()
{
    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);
    char date_buf[64];
    snprintf(date_buf, sizeof(date_buf), "%04d-%02d-%02d.log",
             tm_now.tm_year + 1900, tm_now.tm_mon + 1, tm_now.tm_mday);
    string file = log_path_ + date_buf;
    log_file_ = fopen(file.c_str(), "a+");
    assert(log_file_ != NULL);
}

bool SimpleLog::IsNewDay(time_t current_time)
{
    struct tm tm_last, tm_now;
    localtime_r(&last_log_time_, &tm_last);
    localtime_r(&current_time, &tm_now);
    
    if ((tm_last.tm_year == tm_now.tm_year) &&
        (tm_last.tm_mon == tm_now.tm_mon) &&
        (tm_last.tm_mday == tm_now.tm_mday)) {
        return false;
    } else {
        return true;
    }
}


SimpleLog  g_simple_log;

void init_simple_log(LogLevel level, const string& path)
{
    g_simple_log.Init(level, path);
}

