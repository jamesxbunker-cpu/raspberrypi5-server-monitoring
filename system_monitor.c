#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/sysinfo.h>
#include <sys/stat.h>
#include <dirent.h>
#include <ctype.h>
#include <sys/statvfs.h>

#define DATA_DIR "/var/www/geosmin/data"
#define DATA_FILE "/var/www/geosmin/data/system_metrics.json"

struct dirent *entry;
struct stat statbuf;

// Structure to hold all system metrics
typedef struct
{
    // CPU
    double cpu_usage;
    double cpu_temp;
    double cpu_freq_mhz;
    int cpu_cores;

    // Memory
    unsigned long total_mem_mb;
    unsigned long free_mem_mb;
    unsigned long used_mem_mb;
    double mem_usage_percent;

    // Storage
    unsigned long total_disk_gb;
    unsigned long used_disk_gb;
    unsigned long free_disk_gb;
    double disk_usage_percent;

    // Fan (Armor Lite V5)
    int fan_speed_rpm;
    int fan_pwm_duty; // PWM duty cycle percentage if available

    // System
    double uptime_hours;
    time_t timestamp;
    char hostname[256];
    char os_version[128];

    // Processes
    int total_processes;
    int running_processes;

    // Network
    unsigned long rx_bytes;
    unsigned long tx_bytes;
    double rx_mbps;
    double tx_mbps;

    // GPU
    double gpu_temp;
    int gpu_mem_mb;
} SystemMetrics;

// Helper: Read a file and return first line as string
char *read_file_string(const char *path)
{
    FILE *fp = fopen(path, "r");
    if (!fp)
        return NULL;

    char *buffer = malloc(256);
    if (!buffer)
    {
        fclose(fp);
        return NULL;
    }

    if (fgets(buffer, 256, fp) == NULL)
    {
        free(buffer);
        fclose(fp);
        return NULL;
    }

    // Remove newline
    char *newline = strchr(buffer, '\n');
    if (newline)
        *newline = '\0';

    fclose(fp);
    return buffer;
}

// Helper: Read a file and return as integer
int read_file_int(const char *path)
{
    FILE *fp = fopen(path, "r");
    if (!fp)
        return -1;

    int value;
    if (fscanf(fp, "%d", &value) != 1)
    {
        fclose(fp);
        return -1;
    }

    fclose(fp);
    return value;
}

// Helper: Read a file and return as double
double read_file_double(const char *path)
{
    FILE *fp = fopen(path, "r");
    if (!fp)
        return -1.0;

    double value;
    if (fscanf(fp, "%lf", &value) != 1)
    {
        fclose(fp);
        return -1.0;
    }

    fclose(fp);
    return value;
}

// Get CPU temperature (Raspberry Pi)
double get_cpu_temp()
{
    double temp = read_file_double("/sys/class/thermal/thermal_zone0/temp");
    if (temp > 0)
        return temp / 1000.0; // Convert millidegrees to Celsius
    return -1.0;
}

// Get CPU frequency
double get_cpu_freq()
{
    double freq = read_file_double("/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq");
    if (freq > 0)
        return freq / 1000.0; // Convert kHz to MHz
    return -1.0;
}

// Get CPU usage percentage
double get_cpu_usage()
{
    static unsigned long prev_idle = 0, prev_total = 0;
    unsigned long user, nice, system, idle, iowait, irq, softirq, steal, guest, guest_nice;
    unsigned long total;

    FILE *fp = fopen("/proc/stat", "r");
    if (!fp)
        return -1.0;

    if (fscanf(fp, "cpu  %lu  %lu  %lu  %lu  %lu  %lu  %lu  %lu  %lu  %lu",
               &user, &nice, &system, &idle, &iowait, &irq, &softirq, &steal, &guest, &guest_nice) != 10)
    {
        fclose(fp);
        return -1.0;
    }
    fclose(fp);

    total = user + nice + system + idle + iowait + irq + softirq + steal + guest + guest_nice;

    if (prev_total == 0)
    {
        prev_idle = idle;
        prev_total = total;
        return 0.0;
    }

    unsigned long diff_idle = idle - prev_idle;
    unsigned long diff_total = total - prev_total;
    double cpu_usage = 100.0 * (1.0 - (double)diff_idle / diff_total);

    prev_idle = idle;
    prev_total = total;

    return cpu_usage;
}

// Get fan speed (Armor Lite V5)
int get_fan_speed()
{
    int speed = read_file_int("/sys/devices/platform/cooling_fan/hwmon/hwmon1/fan1_input");
    if (speed < 0)
    {
        // Alternative paths
        speed = read_file_int("/sys/class/hwmon/hwmon0/fan1_input");
    }
    return speed;
}

// Get memory info
void get_memory_info(unsigned long *total, unsigned long *free, unsigned long *available)
{
    FILE *fp = fopen("/proc/meminfo", "r");
    if (!fp)
        return;

    char line[256];
    while (fgets(line, sizeof(line), fp))
    {
        if (strncmp(line, "MemTotal:", 9) == 0)
        {
            sscanf(line + 9, "%lu", total);
        }
        else if (strncmp(line, "MemFree:", 8) == 0)
        {
            sscanf(line + 8, "%lu", free);
        }
        else if (strncmp(line, "MemAvailable:", 13) == 0)
        {
            sscanf(line + 13, "%lu", available);
        }
    }
    fclose(fp);
}

// Get disk usage
void get_disk_usage(const char *path, unsigned long *total, unsigned long *used, unsigned long *free)
{
    struct statvfs stat;
    if (statvfs(path, &stat) != 0)
        return;

    // Divide the block size to MB first, then multiply to prevent overflows
    unsigned long gigabytes_per_block = (stat.f_frsize) / (1024 * 1024 * 1024);

    // If the block size itself is smaller than 1GB (standard), use this safer ordering:
    double bytes_per_gb = 1024.0 * 1024.0 * 1024.0;

    *total = (unsigned long)((double)stat.f_blocks * stat.f_frsize / bytes_per_gb);
    *free = (unsigned long)((double)stat.f_bfree * stat.f_frsize / bytes_per_gb);
    *used = *total - *free;
}

// Get process counts
void get_process_counts(int *total, int *running)
{
    DIR *dir = opendir("/proc");
    if (!dir)
        return;

    struct dirent *entry;
    *total = 0;
    *running = 0;

    while ((entry = readdir(dir)) != NULL)
    {
        if (stat(entry->d_name, &statbuf) == 0)
        {
            if (S_ISDIR(statbuf.st_mode))
            {
                (*total)++;
                char path[256];
                snprintf(path, sizeof(path), "/proc/%s/stat", entry->d_name);
                FILE *fp = fopen(path, "r");
                if (fp)
                {
                    char state;
                    if (fscanf(fp, "%*d %*s %c", &state) == 1)
                    {
                        if (state == 'R')
                            (*running)++;
                    }
                    fclose(fp);
                }
            }
        }
    }
    closedir(dir);
}

// Get network stats
void get_network_stats(unsigned long *rx, unsigned long *tx)
{
    FILE *fp = fopen("/proc/net/dev", "r");
    if (!fp)
        return;

    char line[512];
    *rx = 0;
    *tx = 0;

    // Skip header
    fgets(line, sizeof(line), fp);
    fgets(line, sizeof(line), fp);

    while (fgets(line, sizeof(line), fp))
    {
        char iface[32];
        unsigned long rx_bytes, tx_bytes;
        if (sscanf(line, "%31s %lu %*lu %*lu %*lu %*lu %*lu %*lu %*lu %lu",
                   iface, &rx_bytes, &tx_bytes) == 3)
        {
            // Skip loopback
            if (strcmp(iface, "lo:") != 0)
            {
                *rx += rx_bytes;
                *tx += tx_bytes;
            }
        }
    }
    fclose(fp);
}

// Get GPU temp (if available)
double get_gpu_temp()
{
    char *temp_str = read_file_string("/sys/class/thermal/thermal_zone0/temp");
    if (!temp_str)
        return -1.0;

    double temp = atof(temp_str) / 1000.0;
    free(temp_str);
    return temp;
}

// Get OS version
char *get_os_version()
{
    char *version = read_file_string("/etc/os-release");
    if (!version)
        return strdup("Unknown");

    char *line = strtok(version, "\n");
    while (line)
    {
        if (strncmp(line, "PRETTY_NAME=", 12) == 0)
        {
            char *pretty = strdup(line + 13);
            // Remove quotes
            char *quotes = strchr(pretty, '"');
            if (quotes)
                *quotes = '\0';
            free(version);
            return pretty;
        }
        line = strtok(NULL, "\n");
    }
    free(version);
    return strdup("Raspberry Pi OS");
}

// Main collection function
SystemMetrics collect_metrics()
{
    SystemMetrics metrics = {0};
    struct sysinfo info;
    unsigned long mem_total_kb = 0, mem_free_kb = 0, mem_avail_kb = 0;
    unsigned long rx_bytes = 0, tx_bytes = 0;

    // Hostname
    gethostname(metrics.hostname, sizeof(metrics.hostname));

    // Timestamp
    metrics.timestamp = time(NULL);

    // CPU
    metrics.cpu_usage = get_cpu_usage();
    metrics.cpu_temp = get_cpu_temp();
    metrics.cpu_freq_mhz = get_cpu_freq();
    metrics.cpu_cores = sysconf(_SC_NPROCESSORS_ONLN);

    // Fan
    metrics.fan_speed_rpm = get_fan_speed();

    // Memory
    get_memory_info(&mem_total_kb, &mem_free_kb, &mem_avail_kb);
    metrics.total_mem_mb = mem_total_kb / 1024;
    metrics.free_mem_mb = mem_free_kb / 1024;
    metrics.used_mem_mb = metrics.total_mem_mb - metrics.free_mem_mb;
    if (metrics.total_mem_mb > 0)
    {
        metrics.mem_usage_percent = 100.0 * (double)metrics.used_mem_mb / metrics.total_mem_mb;
    }

    // Disk
    get_disk_usage("/", &metrics.total_disk_gb, &metrics.used_disk_gb, &metrics.free_disk_gb);
    if (metrics.total_disk_gb > 0)
    {
        metrics.disk_usage_percent = 100.0 * (double)metrics.used_disk_gb / metrics.total_disk_gb;
    }

    // Processes
    get_process_counts(&metrics.total_processes, &metrics.running_processes);

    // Uptime
    if (sysinfo(&info) == 0)
    {
        metrics.uptime_hours = (double)info.uptime / 3600.0;
    }

    // Network
    get_network_stats(&rx_bytes, &tx_bytes);
    metrics.rx_bytes = rx_bytes;
    metrics.tx_bytes = tx_bytes;
    // Convert to MB/s (approximate, since we don't have time delta here)
    // For actual rates, you'd want to calculate delta between calls

    // GPU
    metrics.gpu_temp = get_gpu_temp();

    // OS Version
    char *os = get_os_version();
    strncpy(metrics.os_version, os, sizeof(metrics.os_version) - 1);
    free(os);

    return metrics;
}

// Write metrics to JSON file
void write_json(SystemMetrics metrics)
{
    FILE *fp = fopen(DATA_FILE, "w");
    if (!fp)
    {
        fprintf(stderr, "Error opening %s for writing\n", DATA_FILE);
        return;
    }

    fprintf(fp, "{\n");
    fprintf(fp, "  \"timestamp\": %ld,\n", metrics.timestamp);
    fprintf(fp, "  \"hostname\": \"%s\",\n", metrics.hostname);
    fprintf(fp, "  \"os\": \"%s\",\n", metrics.os_version);
    fprintf(fp, "  \"uptime_hours\": %.2f,\n", metrics.uptime_hours);
    fprintf(fp, "  \"cpu\": {\n");
    fprintf(fp, "    \"usage_percent\": %.2f,\n", metrics.cpu_usage);
    fprintf(fp, "    \"temperature_c\": %.2f,\n", metrics.cpu_temp);
    fprintf(fp, "    \"frequency_mhz\": %.2f,\n", metrics.cpu_freq_mhz);
    fprintf(fp, "    \"cores\": %d\n", metrics.cpu_cores);
    fprintf(fp, "  },\n");
    fprintf(fp, "  \"memory\": {\n");
    fprintf(fp, "    \"total_mb\": %lu,\n", metrics.total_mem_mb);
    fprintf(fp, "    \"used_mb\": %lu,\n", metrics.used_mem_mb);
    fprintf(fp, "    \"free_mb\": %lu,\n", metrics.free_mem_mb);
    fprintf(fp, "    \"usage_percent\": %.2f\n", metrics.mem_usage_percent);
    fprintf(fp, "  },\n");
    fprintf(fp, "  \"disk\": {\n");
    fprintf(fp, "    \"total_gb\": %lu,\n", metrics.total_disk_gb);
    fprintf(fp, "    \"used_gb\": %lu,\n", metrics.used_disk_gb);
    fprintf(fp, "    \"free_gb\": %lu,\n", metrics.free_disk_gb);
    fprintf(fp, "    \"usage_percent\": %.2f\n", metrics.disk_usage_percent);
    fprintf(fp, "  },\n");
    fprintf(fp, "  \"fan\": {\n");
    fprintf(fp, "    \"speed_rpm\": %d\n", metrics.fan_speed_rpm);
    fprintf(fp, "  },\n");
    fprintf(fp, "  \"processes\": {\n");
    fprintf(fp, "    \"total\": %d,\n", metrics.total_processes);
    fprintf(fp, "    \"running\": %d\n", metrics.running_processes);
    fprintf(fp, "  },\n");
    fprintf(fp, "  \"network\": {\n");
    fprintf(fp, "    \"rx_bytes\": %lu,\n", metrics.rx_bytes);
    fprintf(fp, "    \"tx_bytes\": %lu\n", metrics.tx_bytes);
    fprintf(fp, "  },\n");
    fprintf(fp, "  \"gpu\": {\n");
    fprintf(fp, "    \"temperature_c\": %.2f\n", metrics.gpu_temp);
    fprintf(fp, "  }\n");
    fprintf(fp, "}\n");

    fclose(fp);
}

int main()
{
    // Create data directory if it doesn't exist
    struct stat st;
    if (stat(DATA_DIR, &st) != 0)
    {
        mkdir(DATA_DIR, 0755);
    }

    // Collect and write metrics
    SystemMetrics metrics = collect_metrics();
    write_json(metrics);

    printf("System metrics written to %s\n", DATA_FILE);
    return 0;
}