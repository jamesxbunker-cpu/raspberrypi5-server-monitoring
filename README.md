# Raspberry Pi 5 Server Monitoring

A lightweight C program for reading system metrics from a Raspberry Pi 5.

> **Completed** — Development currently finished, may create new program or add onto this in the future for additional functionality.

## About

This project reads local system metrics on a Raspberry Pi 5 to track server stats. It's written in C and runs directly on the Pi, pulling data from `/proc` and other local sources rather than over the network.

## Status

- [x] Access local folders on the Pi to gather server metrics
- [x] CPU usage collection (`get_cpu_usage`)
- [ ] *(more to come)*

## Project Structure

    raspberrypi5-server-monitoring
    ├── system_monitor.c   # Main program — reads system metrics
    ├── .gitignore
    └── README.md

## Metrics Collected

- **CPU Usage** 
- **Memory Usage**
- **Storage Usage**
- **Core Temperature**
- **Fan Speed**
- **System Uptime**

## Getting Started

### Requirements

- Raspberry Pi 5 (Raspberry Pi OS or another Linux distro)
- A C compiler (`gcc`)
- `make` (optional, for the Makefile — not yet in the repo)

### Build

    gcc -o system_monitor system_monitor.c

### Run

    ./system_monitor

> Run with appropriate permissions if any metrics require elevated access.

## Roadmap

- [ ] Add server metrics

## Contributing

Early-stage personal project. Feel free to open an issue with ideas or questions.

## License

TBD
