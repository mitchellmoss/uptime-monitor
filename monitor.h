#ifndef MONITOR_H
#define MONITOR_H

// Monitoring functions
void *run_monitor_loop(void *arg);
void check_website_status(const char *url);

#endif // MONITOR_H
