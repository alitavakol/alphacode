# Introduction

This solution is a simple ticket generation and scan application for small events. 

The ticket is encoded as a series of alphanumeric characters to be sent to mobile devices (even legacy mobile phones) as short message text. 

The scanner (which is a PC with webcam for experiments, or an embedded device like a *Beaglebone Black* with a camera in action) can read the ticket from display of the mobile device when exposed.


# How Does it Work?


# Requirements

a UNIX-based operating system with required library packages:

- libopencv-dev
- libboost-thread-dev
- libboost-system-dev
- libboost-chrono-dev
- libboost-filesystem-dev
- libgsl0-dev
- libsqlite3-dev


# Hardware Requirements

a camera/webcam attached to PC for image capture and process by alphacode ticket scanner (which is part of this application).


# Build Instructions

## Alphacode Short Message Text Generator

In terminal, be in root directory of the project and then type:

```bash
cd ./GENERATOR_DEBUG
make clean  # clean output files
make  # build the application
cd ..  # change working directory
./GENERATOR_RELEASE/alphacode-gen --help  # to run the generated binary
```

### Example 1- Generate configuration code

```bash
# to generate configuration code for event number 345:
./GENERATOR_RELEASE/alphacode-gen --configurator --event-id 345 -N 3 -C 14 -R 10 -D 5 -L 3
# in order to configure scanner to accept tickets for this event, expose this generated code to the scanner just after running alphanumeric code scanner
```

### Example 2- Generate a SMS Ticket
```bash
# to generate alphanumeric ticket number 44 for event number 345:
./GENERATOR_RELEASE/alphacode-gen --event-id 345 -N 3 -C 14 -R 10 -D 5 -L 3 --input-number 44
```


## Alphacode Scanner

```
# in terminal, be in root directory of the project and then type:
cd ./LIVE_DEBUG
make clean  # clean output files
make  # build the application
cd ..  # change working directory
./LIVE_DEBUG/alphacode  # to run alphanumeric code scanner
# place generated alphanumeric codes in front of attached camera. this application tries to detect aphanumeric codes
```
