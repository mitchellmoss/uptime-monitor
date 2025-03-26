module.exports = {
  apps : [{
    name   : "uptime-monitor-c", // A name for your application in PM2
    script : "./uptime_monitor", // Path to the compiled executable relative to cwd
    cwd    : ".",                // Set the current working directory to the project root
    exec_mode: "fork",           // Use fork mode for binaries/non-Node.js scripts
    instances: 1,                // Run a single instance
    autorestart: true,           // Automatically restart if the app crashes
    watch  : false,              // Disable watching files by default
    max_memory_restart: '100M', // Optional: Restart if memory usage exceeds 100MB
    // interpreter: 'none',     // Usually not needed for binaries in fork mode
    // args: "",                // No command-line arguments needed for uptime_monitor
    log_date_format: "YYYY-MM-DD HH:mm:ss Z", // Optional: Add timestamps to logs
    out_file: "./logs/uptime-monitor-out.log", // Path for standard output logs
    error_file: "./logs/uptime-monitor-err.log", // Path for standard error logs
    merge_logs: true,            // Merge logs from different instances (if instances > 1)
  }]
};
