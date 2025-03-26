#include "monitor.h"
#include "webui.h"
#include "storage.h"
#include <pthread.h> // Or use separate processes / cron

int main() {
    // Initialize data storage (e.g., open SQLite DB)
    setup_storage();

    // Start monitoring thread/process
    pthread_t monitor_thread;
    pthread_create(&monitor_thread, NULL, run_monitor_loop, NULL);

    // Start the Web UI server (blocking call)
    start_web_server(); // This function would use libmicrohttpd/CivetWeb/etc.

    // Wait for monitor thread (if applicable) and cleanup
    pthread_join(monitor_thread, NULL);
    cleanup_storage();
    return 0;
}