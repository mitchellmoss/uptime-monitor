# Website Uptime Monitor

A simple web service uptime monitoring tool that checks website status at regular intervals and provides a web UI to view the status.

## Features

- Periodic checks of website status using libcurl
- Status storage in SQLite database
- Simple web UI to view current and historical status
- Multi-threaded design (monitor thread + web server thread)

## Dependencies

This project requires the following libraries to be installed:

- libcurl (for HTTP requests)
- libmicrohttpd (for the web server)
- libsqlite3 (for data storage)
- pthread (part of libc)

### Installation on macOS

```bash
brew install curl libmicrohttpd sqlite
```

### Installation on Ubuntu/Debian

```bash
apt-get install libcurl4-openssl-dev libmicrohttpd-dev libsqlite3-dev
```

## Building

```bash
make
```

## Running

```bash
./uptime_monitor
```

By default, the web interface will be available at http://localhost:8080

## Configuration

Edit the URL in monitor.c to monitor a different website.

## License

MIT