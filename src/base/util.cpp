/*
 * util.cpp
 *
 *  Created on: 2016-3-14
 *      Author: ziteng
 */

#include "util.h"
#include <assert.h>
#include <sys/resource.h>

uint64_t get_tick_count()
{
	struct timeval tval;
	gettimeofday(&tval, NULL);
    
	uint64_t current_tick = tval.tv_sec * 1000L + tval.tv_usec / 1000L;
	return current_tick;
}

bool is_valid_ip(const char *ip)
{
    struct sockaddr_in sa;
    int result = inet_pton(AF_INET, ip, &(sa.sin_addr));
    return result != 0;
}

void write_pid()
{
	uint32_t current_pid = (uint32_t) getpid();
    FILE* f = fopen("server.pid", "w");
    assert(f);
    char szPid[32];
    snprintf(szPid, sizeof(szPid), "%d", current_pid);
    fwrite(szPid, strlen(szPid), 1, f);
    fclose(f);
}

int get_file_content(const char* filename, string& content)
{
    struct stat st_buf;
    if (stat(filename, &st_buf) != 0) {
        fprintf(stderr, "stat failed\n");
        return 1;
    }
    
    size_t file_size = (size_t)st_buf.st_size;
    char* file_buf = new char[file_size];
    if (!file_buf) {
        fprintf(stderr, "new buffer failed\n");
        return 1;
    }
    
    FILE* fp = fopen(filename, "rb");
    if (!fp) {
        fprintf(stderr, "open file failed: %s\n", filename);
        delete [] file_buf;
        return 1;
    }
    
    int ret = 1;
    if (fread(file_buf, 1, file_size, fp) == file_size) {
        content.append(file_buf, file_size);
        ret = 0;
    }
    
    delete [] file_buf;
    fclose(fp);
    return ret;
}

int run_as_daemon()
{
    pid_t pid = fork();
    if (pid == -1) {
        fprintf(stderr, "fork failed\n");
        return -1;
    } else if (pid > 0) {
        exit(0);
    }
    
    umask(0);
    setsid();
    
    // close all open file
    struct rlimit rl;
    getrlimit(RLIMIT_NOFILE, &rl);
    if (rl.rlim_max > 1024) {
        rl.rlim_max = 1024;
    }
    
    for (uint32_t i = 0; i < rl.rlim_max; i++) {
        close(i);
    }
    
    // attach file descriptor 0, 1, 2 to "dev/null"
    int fd = open("/dev/null", O_RDWR, 0666);
    if (fd == -1) {
        fprintf(stderr, "open failed\n");
        return -1;
    }
    
    dup2(fd, STDIN_FILENO);
    dup2(fd, STDOUT_FILENO);
    dup2(fd, STDERR_FILENO);
    
    return 0;
}

vector<string> split(const string& str, const string& sep)
{
    vector<string> str_vector;
    string::size_type start_pos = 0, end_pos;
    while ((end_pos = str.find(sep, start_pos)) != string::npos) {
        if (end_pos != start_pos) {
            str_vector.push_back(str.substr(start_pos, end_pos - start_pos));
        }
        
        start_pos = end_pos + sep.size();
    };
    
    if (start_pos < str.size()) {
        str_vector.push_back(str.substr(start_pos, string::npos));
    }
    
    return str_vector;
}


bool get_ip_port(const string& addr, string& ip, int& port)
{
    vector<string> ip_port_vec = split(addr, ":");
    if (ip_port_vec.size() != 2) {
        return false;
    }
    
    ip = ip_port_vec[0];
    port = atoi(ip_port_vec[1].c_str());
    if (!is_valid_ip(ip.c_str())) {
        return false;
    }
    
    if ((port < 1024) || (port > 0xFFFF)) {
        return false;
    }
    
    // remove some case like ip::port, :ip:port
    string merge_addr = ip + ":" + to_string(port);
    if (addr != merge_addr) {
        return false;
    }
    
    return true;
}

void create_path(string& path)
{
    size_t len = path.size();
    if (path.at(len - 1) != '/') {
        path.append(1, '/');
    }
    
    // create path if not exist
    string parent_path;
    struct stat stat_buf;
    for (size_t i = 0; i < path.size(); ++i) {
        char ch = path.at(i);
        parent_path.append(1, ch);
        
        if ((ch == '/') && (stat(parent_path.c_str(), &stat_buf) != 0) && (mkdir(parent_path.c_str(), 0755) != 0)) {
            printf("create path failed: %s\n", parent_path.c_str());
            assert(false);
        }
    }
}

// only useful for Linux OS, because the exist of virtual machine，we need to get the effective cores before binding
bool set_thread_affinity(int core_idx)
{
#ifdef __linux__
    cpu_set_t mask;
    CPU_ZERO(&mask);
    CPU_SET(core_idx, &mask);
    if (!pthread_setaffinity_np(pthread_self(), sizeof(mask), &mask)) {
        printf("set thread affinity success on core=%d\n", core_idx);
        return true;
    }
#endif
    return false;
}

vector<int> get_effective_cores()
{
    vector<int> effective_cores;
#ifdef __linux__
    int total_cores = (int)sysconf(_SC_NPROCESSORS_CONF);
    
    for (int i = total_cores - 1; i >= 0; i--) {
        if (set_thread_affinity(i)) {
            effective_cores.push_back(i);
        }
    }
    
#endif
    return effective_cores;
}

static void print_help(const char* program_name)
{
    printf("%s [-dhv] [-c config_file]\n", program_name);
    printf("\t -d  run as background daemon process\n");
    printf("\t -h  show this help message\n");
    printf("\t -v  show version\n");
    printf("\t -c config_file  specify configuration file\n");
}

void parse_command_args(int argc, char* argv[], const char* version, string& config_file)
{
    int ch;
    while ((ch = getopt(argc, argv, "dhvc:")) != -1) {
        switch (ch) {
            case 'd':
                run_as_daemon();
                break;
            case 'h':
                print_help(argv[0]);
                exit(0);
            case 'v':
                printf("Server Version: %s\n", version);
                printf("Server Build: %s %s\n", __DATE__, __TIME__);
                exit(0);
            case 'c':
                config_file = optarg;
                break;
            case '?':
            default:
                print_help(argv[0]);
                exit(1);
        }
    }
}
