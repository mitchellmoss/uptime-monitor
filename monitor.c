#include <curl/curl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h> // Needed for strtok, strcspn, strchr
#include <ctype.h>  // Needed for isspace
#include "monitor.h"
#include "storage.h" // Header for functions to write to DB/file

#define MAX_SITES 50
#define MAX_URL_LEN 2048
#define CONFIG_FILE "sites.conf"
#define CHECK_INTERVAL_SECONDS 60

// Structure to hold site configuration if needed later (e.g., specific timeouts)
// typedef struct {
//     char url[MAX_URL_LEN];
// } site_config;

void *run_monitor_loop(void *arg) {
    char sites[MAX_SITES][MAX_URL_LEN];
    int num_sites = 0;

    curl_global_init(CURL_GLOBAL_ALL);

    while (1) {
        // --- Read configuration file ---
        FILE *fp = fopen(CONFIG_FILE, "r");
        if (fp == NULL) {
            perror("Error opening config file");
            // Decide how to handle: exit, retry, use defaults? For now, sleep and retry.
            sleep(CHECK_INTERVAL_SECONDS);
            continue;
        }

        num_sites = 0; // Reset site count for each read
        char line[MAX_URL_LEN + 2]; // Allow space for newline and null terminator

        while (fgets(line, sizeof(line), fp) != NULL && num_sites < MAX_SITES) {
            // Remove trailing newline character, if present
            line[strcspn(line, "\r\n")] = 0;

            // Remove comments (anything after '#')
            char *comment_start = strchr(line, '#');
            if (comment_start != NULL) {
                *comment_start = '\0'; // Terminate the string at the comment
            }

            // Trim trailing whitespace
            int len = strlen(line);
            while (len > 0 && isspace((unsigned char)line[len - 1])) {
                line[--len] = '\0';
            }


            // Skip empty lines (after comment/whitespace removal)
            if (line[0] == '\0') {
                continue;
            }

            // Basic validation (starts with http:// or https://) - could be more robust
            if (strncmp(line, "http://", 7) != 0 && strncmp(line, "https://", 8) != 0) {
                 fprintf(stderr, "Skipping invalid URL in %s: %s\n", CONFIG_FILE, line);
                 continue;
            }


            if (strlen(line) < MAX_URL_LEN) {
                 strncpy(sites[num_sites], line, MAX_URL_LEN -1);
                 sites[num_sites][MAX_URL_LEN - 1] = '\0'; // Ensure null termination
                 num_sites++;
            } else {
                fprintf(stderr, "URL too long in %s: %s\n", CONFIG_FILE, line);
                // Skip this line
            }
        }
        fclose(fp);

        if (num_sites == 0) {
             fprintf(stderr, "No valid sites found in %s. Sleeping.\n", CONFIG_FILE);
        }

        // --- Check each site ---
        for (int i = 0; i < num_sites; i++) {
            printf("Checking: %s\n", sites[i]); // Log which site is being checked
            check_website_status(sites[i]);
            // Optional: Add a small delay between checks if desired
            // sleep(1);
        }

        // --- Wait before next round of checks ---
        sleep(CHECK_INTERVAL_SECONDS);
    }

    // This part is now unreachable because of the infinite loop,
    // but kept for conceptual correctness if the loop were breakable.
    curl_global_cleanup();
    return NULL;
}

void check_website_status(const char *url) {
    CURL *curl = curl_easy_init();
    long response_code = 0;
    double response_time = 0.0;
    int is_up = 0;

    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 10000L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
        curl_easy_setopt(curl, CURLOPT_NOBODY, 1L); // Just check headers/status
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

        CURLcode res = curl_easy_perform(curl);

        if (res == CURLE_OK) {
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
            curl_easy_getinfo(curl, CURLINFO_TOTAL_TIME, &response_time);
            if (response_code >= 200 && response_code < 400) { // 2xx/3xx = Up
                is_up = 1;
            }
        } else {
            // Log curl error: curl_easy_strerror(res)
            printf("Curl error: %s\n", curl_easy_strerror(res));
            response_code = -1; // Indicate curl error
        }
        curl_easy_cleanup(curl);
    }
    // Record result (timestamp, url, is_up, response_code, response_time)
    record_status(url, is_up, response_code, response_time);
}
