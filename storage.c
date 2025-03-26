#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "storage.h"

static sqlite3 *db = NULL;

void setup_storage() {
    int rc = sqlite3_open("uptime.db", &db);
    
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        exit(1);
    }
    
    // Create table if it doesn't exist
    const char *sql = "CREATE TABLE IF NOT EXISTS status_checks ("
                      "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                      "timestamp INTEGER NOT NULL,"
                      "url TEXT NOT NULL,"
                      "is_up INTEGER NOT NULL,"
                      "response_code INTEGER NOT NULL,"
                      "response_time REAL NOT NULL"
                      ");";
                      
    char *err_msg = NULL;
    rc = sqlite3_exec(db, sql, 0, 0, &err_msg);
    
    if (rc != SQLITE_OK) {
        fprintf(stderr, "SQL error: %s\n", err_msg);
        sqlite3_free(err_msg);
        sqlite3_close(db);
        exit(1);
    }
    
    printf("Database setup complete.\n");
}

void cleanup_storage() {
    if (db) {
        sqlite3_close(db);
        db = NULL;
    }
}

void record_status(const char* url, int is_up, long code, double response_time) {
    if (!db) {
        fprintf(stderr, "Database not initialized.\n");
        return;
    }
    
    // Get current Unix timestamp
    time_t now = time(NULL);
    
    char *sql = "INSERT INTO status_checks (timestamp, url, is_up, response_code, response_time) "
                "VALUES (?, ?, ?, ?, ?);";
    
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to prepare statement: %s\n", sqlite3_errmsg(db));
        return;
    }
    
    // Bind parameters
    sqlite3_bind_int64(stmt, 1, (sqlite3_int64)now);
    sqlite3_bind_text(stmt, 2, url, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 3, is_up);
    sqlite3_bind_int(stmt, 4, (int)code);
    sqlite3_bind_double(stmt, 5, response_time);
    
    // Execute
    rc = sqlite3_step(stmt);
    
    if (rc != SQLITE_DONE) {
        fprintf(stderr, "Failed to execute statement: %s\n", sqlite3_errmsg(db));
    } else {
        printf("Status recorded for %s: %s (code: %ld, time: %.2fs)\n", 
               url, is_up ? "UP" : "DOWN", code, response_time);
    }
    
    sqlite3_finalize(stmt);
}

// Get the latest status for a URL
int get_latest_status(const char* url, long* code, double* time) {
    if (!db) {
        fprintf(stderr, "Database not initialized.\n");
        return -1;
    }
    
    char *sql = "SELECT is_up, response_code, response_time, timestamp FROM status_checks "
                "WHERE url = ? ORDER BY timestamp DESC LIMIT 1;";
    
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to prepare statement: %s\n", sqlite3_errmsg(db));
        return -1;
    }
    
    // Bind parameters
    sqlite3_bind_text(stmt, 1, url, -1, SQLITE_STATIC);
    
    // Execute
    rc = sqlite3_step(stmt);
    
    int is_up = -1;
    
    if (rc == SQLITE_ROW) {
        is_up = sqlite3_column_int(stmt, 0);
        if (code) *code = sqlite3_column_int(stmt, 1);
        if (time) *time = sqlite3_column_double(stmt, 2);
    } else {
        fprintf(stderr, "No records found for URL: %s\n", url);
    }
    
    sqlite3_finalize(stmt);
    return is_up;
}

// Get the last check timestamp for a URL
time_t get_last_check_time(const char* url) {
    if (!db) {
        fprintf(stderr, "Database not initialized.\n");
        return 0;
    }
    
    char *sql = "SELECT timestamp FROM status_checks "
                "WHERE url = ? ORDER BY timestamp DESC LIMIT 1;";
    
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to prepare statement: %s\n", sqlite3_errmsg(db));
        return 0;
    }
    
    // Bind parameters
    sqlite3_bind_text(stmt, 1, url, -1, SQLITE_STATIC);
    
    // Execute
    rc = sqlite3_step(stmt);
    
    time_t last_check = 0;
    
    if (rc == SQLITE_ROW) {
        last_check = (time_t)sqlite3_column_int64(stmt, 0);
    } else {
        fprintf(stderr, "No records found for URL: %s\n", url);
    }
    
    sqlite3_finalize(stmt);
    return last_check;
}


// Get recent status history for a URL
// Returns a dynamically allocated array of StatusHistoryEntry.
// The number of entries actually retrieved is returned via num_entries.
// Returns NULL on error or if no entries found. Caller must free the returned array.
StatusHistoryEntry* get_recent_history(const char* url, int limit, int* num_entries) {
    *num_entries = 0;
    if (!db || limit <= 0) {
        fprintf(stderr, "Database not initialized or invalid limit.\n");
        return NULL;
    }

    // Query to get the last 'limit' entries, ordered oldest first for easier display processing later
    char *sql = "SELECT timestamp, is_up FROM ("
                "  SELECT timestamp, is_up FROM status_checks "
                "  WHERE url = ? ORDER BY timestamp DESC LIMIT ?"
                ") ORDER BY timestamp ASC;";

    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);

    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to prepare history statement: %s\n", sqlite3_errmsg(db));
        return NULL;
    }

    // Bind parameters
    sqlite3_bind_text(stmt, 1, url, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, limit);

    // Count rows first to allocate exact memory
    int count = 0;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        count++;
    }

    if (rc != SQLITE_DONE) {
        fprintf(stderr, "Error stepping through history results: %s\n", sqlite3_errmsg(db));
        sqlite3_finalize(stmt);
        return NULL;
    }

    if (count == 0) {
        // fprintf(stderr, "No history records found for URL: %s\n", url); // Optional: less verbose
        sqlite3_finalize(stmt);
        return NULL; // No history found
    }

    // Allocate memory for results
    StatusHistoryEntry* history = malloc(count * sizeof(StatusHistoryEntry));
    if (!history) {
        fprintf(stderr, "Failed to allocate memory for history results.\n");
        sqlite3_finalize(stmt);
        return NULL;
    }

    // Reset and re-execute the statement to fetch data
    sqlite3_reset(stmt);
    // Re-bind parameters (needed after reset)
    sqlite3_bind_text(stmt, 1, url, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, limit);

    int index = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW && index < count) {
        history[index].timestamp = (time_t)sqlite3_column_int64(stmt, 0);
        history[index].is_up = sqlite3_column_int(stmt, 1);
        index++;
    }

    sqlite3_finalize(stmt);
    *num_entries = count; // Set the output parameter
    return history;
}
