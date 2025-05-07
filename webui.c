#include <microhttpd.h> // Or CivetWeb header, etc.
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdlib.h> // For malloc, free, realloc
#include <ctype.h>  // For isspace
#include <microhttpd.h> // Or CivetWeb header, etc. - Ensure this is included
#include "webui.h"
#include "storage.h" // Header for functions to read from DB/file

#define MAX_SITES 50        // Should match monitor.c or be centralized
#define MAX_URL_LEN 2048    // Should match monitor.c or be centralized
#define CONFIG_FILE "sites.conf" // Should match monitor.c or be centralized
#define INITIAL_HTML_BUF_SIZE 4096
#define MAX_HTML_BUF_SIZE 1024*1024 // 1MB limit to prevent excessive allocation
#define HISTORY_POINTS 24 // Number of history points to show (e.g., last 24 checks)

// Global storage for site configurations
static SiteConfig g_site_configs[MAX_SITES];
static int g_num_sites = 0;

// Function to load site configurations from CONFIG_FILE
// Returns the number of sites loaded, or -1 on error.
int load_site_configurations() {
    FILE *fp = fopen(CONFIG_FILE, "r");
    if (fp == NULL) {
        perror("Error opening config file for loading");
        return -1;
    }

    g_num_sites = 0; // Reset before loading
    char line[MAX_URL_LEN + 2];
    while (fgets(line, sizeof(line), fp) != NULL && g_num_sites < MAX_SITES) {
        line[strcspn(line, "\r\n")] = 0; // Remove newline
        char *comment_start = strchr(line, '#'); // Remove comments
        if (comment_start != NULL) *comment_start = '\0';
        int len = strlen(line); // Trim trailing whitespace
        while (len > 0 && isspace((unsigned char)line[len - 1])) line[--len] = '\0';
        if (line[0] == '\0') continue; // Skip empty lines

        if (strlen(line) < MAX_URL_LEN) {
             strncpy(g_site_configs[g_num_sites].url, line, MAX_URL_LEN -1);
             g_site_configs[g_num_sites].url[MAX_URL_LEN - 1] = '\0';
             g_num_sites++;
        } else {
             fprintf(stderr, "Config Load: URL too long in %s: %s\n", CONFIG_FILE, line);
        }
    }
    fclose(fp);
    printf("Loaded %d sites from %s\n", g_num_sites, CONFIG_FILE);
    return g_num_sites;
}

// Helper to append string safely to a dynamic buffer
// Returns 0 on success, -1 on failure (e.g., allocation failed)
static int append_to_buffer(char **buffer, size_t *buffer_size, size_t *current_len, const char *str_to_append) {
    size_t append_len = strlen(str_to_append);
    size_t required_size = *current_len + append_len + 1; // +1 for null terminator

    if (required_size > *buffer_size) {
        size_t new_size = *buffer_size;
        while (new_size < required_size) {
            new_size *= 2; // Double the buffer size
        }

        // Prevent excessive allocation
        if (new_size > MAX_HTML_BUF_SIZE) {
             fprintf(stderr, "HTML buffer size exceeds limit (%d bytes)\n", MAX_HTML_BUF_SIZE);
             // Optionally truncate or handle error differently
             // For now, just prevent reallocation beyond limit
             if (*buffer_size >= MAX_HTML_BUF_SIZE) return -1; // Already at limit
             new_size = MAX_HTML_BUF_SIZE;
             if (new_size < required_size) return -1; // Still not enough space even at limit
        }


        char *new_buffer = realloc(*buffer, new_size);
        if (!new_buffer) {
            fprintf(stderr, "Failed to reallocate HTML buffer to %zu bytes\n", new_size);
            return -1; // Allocation failure
        }
        *buffer = new_buffer;
        *buffer_size = new_size;
    }

    // Use memcpy for potentially better performance with known lengths
    memcpy(*buffer + *current_len, str_to_append, append_len);
    *current_len += append_len;
    (*buffer)[*current_len] = '\0'; // Ensure null termination

    return 0;
}


// Example using libmicrohttpd
enum MHD_Result request_handler(void *cls, struct MHD_Connection *connection,
                    const char *url, const char *method,
                    const char *version, const char *upload_data,
                    size_t *upload_data_size, void **con_cls) {
    // Suppress unused parameter warnings
    (void)cls; (void)url; (void)version; (void)upload_data; 
    (void)upload_data_size; (void)con_cls;

    if (0 != strcmp(method, "GET")) return MHD_NO; // Only GET

    // --- Use pre-loaded site configurations ---
    if (g_num_sites <= 0) {
        // This case should ideally be handled at startup,
        // but as a fallback, show an error if no sites are loaded.
        fprintf(stderr, "WebUI: No site configurations loaded.\n");
        const char *error_page = "<html><body><h1>Error</h1><p>No site configurations available. Check server logs.</p></body></html>";
        struct MHD_Response *response = MHD_create_response_from_buffer(strlen(error_page), (void *)error_page, MHD_RESPMEM_MUST_COPY);
        MHD_add_response_header(response, "Content-Type", "text/html");
        enum MHD_Result ret = MHD_queue_response(connection, MHD_HTTP_INTERNAL_SERVER_ERROR, response);
        MHD_destroy_response(response);
        return ret;
    }

    // --- Dynamically generate HTML ---
    size_t html_buf_size = INITIAL_HTML_BUF_SIZE;
    size_t html_len = 0;
    char *html_buffer = malloc(html_buf_size);
    if (!html_buffer) {
        fprintf(stderr, "Failed to allocate initial HTML buffer\n");
        // Handle allocation failure - return error response
        const char *error_page = "<html><body><h1>Error</h1><p>Internal server error (memory allocation failed).</p></body></html>";
        struct MHD_Response *response = MHD_create_response_from_buffer(strlen(error_page), (void *)error_page, MHD_RESPMEM_MUST_COPY);
        MHD_add_response_header(response, "Content-Type", "text/html");
        enum MHD_Result ret = MHD_queue_response(connection, MHD_HTTP_INTERNAL_SERVER_ERROR, response);
        MHD_destroy_response(response);
        return ret;
    }
    html_buffer[0] = '\0'; // Start with an empty string

    // Increase temp_buf size: Max URL length + ~1KB for other fields, history bar, and tags
    char temp_buf[MAX_URL_LEN + 1024]; 
    char header_buf[256]; // Buffer specifically for the table header

    // --- Start HTML Document ---
    // Append initial HTML structure
    if (append_to_buffer(&html_buffer, &html_buf_size, &html_len,
                     "<html><head><title>Uptime Status</title>"
                     "<meta http-equiv='refresh' content='30'>" // Auto-refresh every 30 seconds
                     "<style>"
                     "body{font-family:Arial,sans-serif;margin:20px;line-height:1.4;background-color:#f4f4f4;color:#333;}"
                     "h1{color:#333;text-align:center;}"
                     "table{width:100%;border-collapse:collapse;margin-top:20px;box-shadow:0 2px 5px rgba(0,0,0,0.1);}"
                     "th,td{padding:12px 15px;text-align:left;border-bottom:1px solid #ddd;}"
                     "th{background-color:#4CAF50;color:white;}"
                     "tr:nth-child(even){background-color:#f9f9f9;}"
                     "tr:hover{background-color:#f1f1f1;}"
                     ".status-up{color:green;font-weight:bold;}"
                     ".status-down{color:red;font-weight:bold;}"
                     ".status-unknown{color:gray;font-weight:bold;}"
                     ".url-link{color:#007bff;text-decoration:none;word-break:break-all;}" // Added word-break
                     ".url-link:hover{text-decoration:underline;}"
                     ".footer{text-align:center;margin-top:20px;font-size:0.9em;color:#777;}"
                     ".refresh-btn{display:block;width:120px;margin:20px auto;padding:10px 15px;background:#4CAF50;color:white;border:none;cursor:pointer;border-radius:4px;text-align:center;text-decoration:none;}"
                     ".refresh-btn:hover{background:#45a049;}"
                     ".history-bar{margin-top:5px;line-height:0;white-space:nowrap;}" /* Container for history blocks */
                     ".history-block{display:inline-block;width:8px;height:12px;margin-right:1px;border:1px solid #ccc;vertical-align:middle;}" /* Individual block */
                     ".history-up{background-color:rgba(0,128,0,0.7);border-color:#5a5;}" /* Green with some transparency */
                     ".history-down{background-color:rgba(255,0,0,0.7);border-color:#a55;}" /* Red with some transparency */
                     ".history-unknown{background-color:rgba(128,128,128,0.7);border-color:#888;}" /* Gray with some transparency */
                     "</style></head><body>"
                     "<h1>Website Uptime Monitor</h1>"
                     "<a href='/' class='refresh-btn'>Refresh Now</a>"
                     "<table><thead><tr>") != 0) { // Stop before closing </tr>
        // Handle initial append failure
        free(html_buffer);
        const char *error_page = "<html><body><h1>Error</h1><p>Internal server error (buffer append failed).</p></body></html>";
        struct MHD_Response *response = MHD_create_response_from_buffer(strlen(error_page), (void *)error_page, MHD_RESPMEM_MUST_COPY);
        MHD_add_response_header(response, "Content-Type", "text/html");
        enum MHD_Result ret = MHD_queue_response(connection, MHD_HTTP_INTERNAL_SERVER_ERROR, response);
        MHD_destroy_response(response);
        return ret;
    }

    // Format the table header cells and the closing tags
    snprintf(header_buf, sizeof(header_buf),
             "<th>URL</th><th>Status</th><th>Code</th><th>Time (s)</th><th>Last Check</th><th>History (%d checks)</th>" // Header cells
             "</tr></thead><tbody>", // Close header row, header section, and open body
             HISTORY_POINTS);

    // Append the formatted header part
    if (append_to_buffer(&html_buffer, &html_buf_size, &html_len, header_buf) != 0) {
        // Handle header append failure
        free(html_buffer);
        const char *error_page = "<html><body><h1>Error</h1><p>Internal server error (header append failed).</p></body></html>";
        struct MHD_Response *response = MHD_create_response_from_buffer(strlen(error_page), (void *)error_page, MHD_RESPMEM_MUST_COPY);
        MHD_add_response_header(response, "Content-Type", "text/html");
        enum MHD_Result ret = MHD_queue_response(connection, MHD_HTTP_INTERNAL_SERVER_ERROR, response);
        MHD_destroy_response(response);
        return ret;
    }


    // --- Generate Table Rows for each site ---
    for (int i = 0; i < g_num_sites; i++) {
        const char* target_url = g_site_configs[i].url;
        long response_code = 0;
        double response_time = 0.0;
        int is_up = get_latest_status(target_url, &response_code, &response_time);
        time_t last_check_time = get_last_check_time(target_url);

        const char* status_text = (is_up == 1) ? "UP" : (is_up == 0 ? "DOWN" : "UNKNOWN");
        const char* status_class = (is_up == 1) ? "up" : (is_up == 0 ? "down" : "unknown");

        char last_check_time_str[64];
        if (last_check_time > 0) {
            // Use localtime_r for thread safety if needed, though MHD might handle requests serially depending on mode
            struct tm timeinfo;
            localtime_r(&last_check_time, &timeinfo);
            strftime(last_check_time_str, sizeof(last_check_time_str), "%Y-%m-%d %H:%M:%S", &timeinfo);
        } else {
            snprintf(last_check_time_str, sizeof(last_check_time_str), "Never");
        }

        // --- Generate History Bar ---
        int num_history_entries = 0;
        StatusHistoryEntry* history = get_recent_history(target_url, HISTORY_POINTS, &num_history_entries);
        char history_html[HISTORY_POINTS * 150]; // Estimate buffer size for history spans
        history_html[0] = '\0'; // Start empty

        if (history && num_history_entries > 0) {
            strcat(history_html, "<div class='history-bar'>");
            char block_buf[150];
            char time_title[64];
            for (int j = 0; j < num_history_entries; j++) {
                const char* history_status_class;
                if (history[j].is_up == 1) history_status_class = "up";
                else if (history[j].is_up == 0) history_status_class = "down";
                else history_status_class = "unknown";

                // Format timestamp for title attribute
                struct tm history_timeinfo;
                localtime_r(&history[j].timestamp, &history_timeinfo);
                strftime(time_title, sizeof(time_title), "%Y-%m-%d %H:%M:%S", &history_timeinfo);

                snprintf(block_buf, sizeof(block_buf), "<span class='history-block history-%s' title='%s'></span>",
                         history_status_class, time_title);
                // Basic check to prevent overflow, though unlikely with estimated size
                if (strlen(history_html) + strlen(block_buf) < sizeof(history_html) - 10) {
                     strcat(history_html, block_buf);
                }
            }
             strcat(history_html, "</div>");
            free(history); // Free the memory allocated by get_recent_history
        } else {
             // Optionally add a placeholder if no history
             // strcat(history_html, "<div class='history-bar'>No history data</div>");
        }
        // --- End History Bar ---


        // Format the complete table row including the history bar
        snprintf(temp_buf, sizeof(temp_buf),
                 "<tr>"
                 "<td><a href='%s' target='_blank' class='url-link'>%s</a></td>" // History removed from here
                 "<td class='status-%s'>%s</td>"
                 "<td>%ld</td>"
                 "<td>%.3f</td>"
                 "<td>%s</td>"
                 "<td>%s</td>" // New cell for history
                 "</tr>",
                 target_url, target_url, // URL info
                 status_class, status_text, // Status info
                 response_code,             // Code
                 response_time,             // Time
                 last_check_time_str,       // Last check timestamp
                 history_html);             // History bar HTML moved here


        // Append the formatted row to the main buffer
        if (append_to_buffer(&html_buffer, &html_buf_size, &html_len, temp_buf) != 0) {
             // Handle append failure (e.g., buffer limit reached)
             fprintf(stderr, "Failed to append row for %s to HTML buffer\n", target_url);
             break; // Stop processing more sites if buffer fails
        }
    }

    // --- Finish HTML Document ---
    time_t current_time = time(NULL);
    char current_time_str[64];
    struct tm current_timeinfo;
    localtime_r(&current_time, &current_timeinfo); // Use thread-safe version
    strftime(current_time_str, sizeof(current_time_str), "%Y-%m-%d %H:%M:%S %Z", &current_timeinfo);

    snprintf(temp_buf, sizeof(temp_buf),
             "</tbody></table>"
             "<div class='footer'>Page generated: %s</div>"
             "</body></html>",
             current_time_str);
    // Append footer, check for errors
    if (append_to_buffer(&html_buffer, &html_buf_size, &html_len, temp_buf) != 0) {
         fprintf(stderr, "Failed to append footer to HTML buffer\n");
         // The response might be incomplete, but try sending what we have
    }


    // --- Send Response ---
    // Use MHD_RESPMEM_MUST_FREE because buffer was malloc'd
    struct MHD_Response *response = MHD_create_response_from_buffer(html_len, (void *)html_buffer, MHD_RESPMEM_MUST_FREE);
    if (!response) {
        free(html_buffer); // Free buffer if response creation fails
        // Return internal server error
        const char *error_page = "<html><body><h1>Error</h1><p>Internal server error (response creation failed).</p></body></html>";
        struct MHD_Response *err_response = MHD_create_response_from_buffer(strlen(error_page), (void *)error_page, MHD_RESPMEM_MUST_COPY);
        MHD_add_response_header(err_response, "Content-Type", "text/html");
        enum MHD_Result ret_err = MHD_queue_response(connection, MHD_HTTP_INTERNAL_SERVER_ERROR, err_response);
        MHD_destroy_response(err_response);
        return ret_err;
    }

    MHD_add_response_header(response, "Content-Type", "text/html; charset=utf-8");
    enum MHD_Result ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
    // MHD frees the buffer when MHD_RESPMEM_MUST_FREE is used and queueing succeeds.
    // If queueing fails (ret != MHD_YES), MHD might not free it, but destroying the response should handle it.
    // Explicitly freeing here is generally incorrect with MHD_RESPMEM_MUST_FREE.
    MHD_destroy_response(response);

    return ret;
}

void start_web_server() {
    // Load site configurations at startup
    if (load_site_configurations() < 0) {
        fprintf(stderr, "Failed to load site configurations. Web server will not start with site data.\n");
        // Depending on desired behavior, you might choose to exit or run without sites.
        // For now, we'll print an error and continue, the request_handler will show an error page.
    } else if (g_num_sites == 0) {
        fprintf(stdout, "Warning: No sites found in %s or all were invalid.\n", CONFIG_FILE);
    }


    struct MHD_Daemon *daemon = MHD_start_daemon(MHD_USE_SELECT_INTERNALLY, 8080, NULL, NULL,
                                                 &request_handler, NULL, MHD_OPTION_END);
    if (NULL == daemon) { /* Handle error */
        fprintf(stderr, "Failed to start web server daemon.\n");
        return;
    }
    printf("Web server started on port 8080. Press Enter to stop.\n");
    (void)getchar(); // Keep server running until Enter is pressed
    MHD_stop_daemon(daemon);
    // Note: Memory for g_site_configs is static, so no explicit free needed here unless
    // it was dynamically allocated (which it isn't in this static array approach).
    // If SiteConfig structs contained dynamically allocated members, they'd need freeing.
}
