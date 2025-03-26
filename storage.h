#ifndef STORAGE_H
#define STORAGE_H

#include <time.h>
#include <stdlib.h> // For size_t

// Structure to hold a single history point
typedef struct {
    time_t timestamp;
    int is_up; // 1 = UP, 0 = DOWN, -1 = UNKNOWN/ERROR during check
} StatusHistoryEntry;


// Storage functions
void setup_storage();
void cleanup_storage();
void record_status(const char* url, int is_up, long code, double response_time);
int get_latest_status(const char* url, long* code, double* time);
time_t get_last_check_time(const char* url);

// Get recent status history for a URL (returns malloc'd array, caller must free)
// Returns the number of entries retrieved via num_entries pointer.
StatusHistoryEntry* get_recent_history(const char* url, int limit, int* num_entries);


#endif // STORAGE_H
