/*
 * util.h
 *
 *  Created on: 2016-3-14
 *      Author: ziteng
 */

#ifndef __BASE_UTIL_H__
#define __BASE_UTIL_H__

#include "ostype.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <assert.h>
#include <string>
#include <list>
#include <vector>
#include <unordered_map>
#include <map>
#include <set>
#include <mutex>
#include <atomic>
using namespace std;

#define NOTUSED_ARG(v) ((void)v) // used this to remove warning C4100, unreferenced parameter

enum {
    ERROR_CODE_WRONG_PKT_LEN        = 1,
    ERROR_CODE_PARSE_FAILED         = 2,
    ERROR_CODE_UNKNOWN_PKT_ID       = 3,
};

class PktException {
public:
	PktException(uint32_t error_code, const char* error_msg) {
		error_code_ = error_code;
		error_msg_ = error_msg;
	}
	virtual ~PktException() {}
    
	uint32_t GetErrorCode() { return error_code_; }
	char* GetErrorMsg() { return (char*)error_msg_.c_str(); }
private:
	uint32_t	error_code_;
	string		error_msg_;
};

class RefCount
{
public:
	RefCount() : ref_count_(1), mutex_(NULL) {}
	virtual ~RefCount() {}
    
	void SetMutex(mutex* mtx) { mutex_ = mtx; }

	void AddRef() {
        if (mutex_) {
            mutex_->lock();
            ++ref_count_;
            mutex_->unlock();
        } else {
            ++ref_count_;
        }
    }
    
	void ReleaseRef() {
        if (mutex_) {
            lock_guard<mutex> mg(*mutex_);
            --ref_count_;
            if (ref_count_ == 0) {
                delete this;
            }
        } else {
            --ref_count_;
            if (ref_count_ == 0)
                delete this;
        }
    }
private:
	int		ref_count_;
	mutex*	mutex_;
};

uint64_t get_tick_count();

bool is_valid_ip(const char *ip);

void write_pid();

// read small config file content
int get_file_content(const char* filename, string& file_content);

// system daemon() function will report deprecated warning under MacOS X, so just rewrite the function
int run_as_daemon();

vector<string> split(const string& str, const string& sep);

bool get_ip_port(const string& addr, string& ip, int& port);

// create a path if not exist
// side effect: if path is not end with '/'， it will add '/' at the end
void create_path(string& path);

bool set_thread_affinity(int core_idx);

vector<int> get_effective_cores();

void parse_command_args(int argc, char* argv[], const char* version, string& config_file);

#endif
