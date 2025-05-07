#ifndef WEBUI_H
#define WEBUI_H

// Structure to hold site configuration
typedef struct {
    char url[2048]; // Corresponds to MAX_URL_LEN, ensure this is consistent
} SiteConfig;

// Web UI functions
void start_web_server();
int load_site_configurations(); // Returns number of sites loaded, or -1 on error

#endif // WEBUI_H
