# Frameworks benchmarks

The frameworks benchmarks compare the performance (duration and latency) of different frameworks. The pipeline components (partial event handlers) use the same implementation in each framework. Full copies of the frameworks are provided with this repository (except for the close-source *kAER* framework).

The install instructions target Ubuntu 16.04 LTS only. Operating system support varies among frameworks, and this version of Ubuntu happens to be compatible with all of them. The benchmarks should work with other systems, provided that dependencies are met.

The frameworks are written in C and C++. Node.js is used as a glue language to automate the benchmarking process.

## clone

This repository uses Git LFS to track large media files. To clone it, you must first [install the Git LFS extension](https://help.github.com/en/articles/installing-git-large-file-storage). Then, run:
```
git clone https://github.com/neuromorphic-paris/frameworks_benchmarks
```

## dependencies (Ubuntu 16.04 LTS)

```sh
# install common dependencies
sudo apt install -y curl build-essential premake4
curl -sL https://deb.nodesource.com/setup_12.x | sudo -E bash -
sudo apt install -y nodejs

# install YARP dependencies
sudo apt install -y libace-dev

# install cAER dependencies
sudo apt install -y libusb-1.0-0-dev libboost-all-dev libuv1-dev
wget -O libpng.tar.gz https://downloads.sourceforge.net/project/libpng/libpng16/older-releases/1.6.21/libpng-1.6.21.tar.gz
tar -xvf libpng.tar.gz
cd libpng-1.6.21
./configure
make
sudo make install

# install kAER dependencies (requires the close-source libraries provided by Prophesee)
wget -O opencv2.zip http://downloads.sourceforge.net/project/opencvlibrary/opencv-unix/2.4.13/opencv-2.4.13.zip
unzip opencv2.zip
cd opencv-2.4.13
sed -i '1s/^/set(OPENCV_VCSVERSION "2.4.13")\n/' cmake/OpenCVPackaging.cmake
sed -i 's/dumpversion/dumpfullversion/g' cmake/OpenCVDetectCXXCompiler.cmake
mkdir -p build
cd build
cmake -DENABLE_PRECOMPILED_HEADERS=OFF ..
make
sudo dpkg -i libatis-0.6-Linux.deb
sudo apt install -f -y
sudo dpkg -i kAER-0.6-Linux.deb
sudo apt install -f -y
```

## install

The __install.js__ file at the root of this repository compiles all the frameworks and their modules one-by-one. It is possible to compile the code on a distant machine. The two first lines of the __install.js__ script can be edited to copy the local folder with `rsync` and compile the code over `ssh`. The default script compiles the code locally. The flag `do-not-compile` can be used to only copy over rsync (for non-local compilations only).
```sh
node install.js
# or
node install.js do-not-compile
```

## run

To run the benchmarks, run from the machine where the code is installed:
```sh
node --max-old-space-size=16384 --expose-gc benchmark.js | tee output.log
```
With the default parameters, the benchmarks take half a day on a standard desktop computer.

## process the results

Results are written in the __results__ directory (one file per task) in JSON format. Filenames have the structure `[pipeline]::[experiment]::[stream]::[framework]::[trial].json`. Hashes are calculated with the MurmurHash3 (128 bits, x64 version) algorithm.

When the experiment is `duration`, the file has the following content:
```yml
{
    "duration": 3488163355, # total duration measurement in nanoseconds
    "hashes": { # the hashes should not depend on the framework, the exact list depends on the pipeline
        "events": 3082164, # number of events
        "t_hash": "1cf0dcc2dde63251b54530f2ac7ef176", # hash of the output timestamps
        "x_hash": "9e9b9ffb80bdd01c53685da59b98cefb", # hash of the output x coordinates
        "y_hash": "a363a29a58c1e408d4b918f4af2783b2"  # hash of the output y coordinates
    }
}
```

When the experiment is `latency`, the file has the following content:
```yml
{
    "hashes": { # the hashes should not depend on the framework, the exact list depends on the pipeline
        "events": 3082164, # number of events
        "t_hash": "1cf0dcc2dde63251b54530f2ac7ef176", # hash of the output timestamps
        "x_hash": "9e9b9ffb80bdd01c53685da59b98cefb", # hash of the output x coordinates
        "y_hash": "a363a29a58c1e408d4b918f4af2783b2"  # hash of the output y coordinates
    },
    "points": [ # latency measurements
        [3729835, 3726393444], # [t in microseconds, clock in nanoseconds]
        [4216753, 4216723165],
        [4216769, 4216723763],
        [4217296, 4216726129],
        ...
    ]
}
```
`t` is the output event timestamp in microseconds. `clock` is a measurement of the duration since the dispatch of the first packet, in nanoseconds.

In order to calculate latencies, one must first compute the input packets timestamps for each stream (defined as the timestamp of the last event in each packet). This can be done using the program `common/build/release/packetize`, which generates a JSON array of packet timestamps. To generate the latter for each stream, run:

```sh
common/build/release/packetize media/squares.es > squares_packet_ts.json
common/build/release/packetize media/street.es > street_packet_ts.json
common/build/release/packetize media/car.es > car_packet_ts.json
```

Given a list of latency measurements `points` and a list of packet timestamps `packets_ts`, proceed as follows to calculate the framework latency `latencies[k]` of the output event with index `k` is given by (in microseconds):
```js
latencies[k] = points[k][1] / 1000 - (packets_ts[i] - packets_ts[0])
```
where `i` is the index such that:
```js
packets_ts[i - 1] < points[k][0] <= packets_ts[i]
```
The total latency `total_latencies[k]` of the output event with index `k` (which takes into accounts both the framework latency and the packetization latency) is given by:
```js
total_latencies[k] = latencies[k] + (packets_ts[i] - points[k][0])
                   = points[k][1] / 1000 - (packets_ts[i] - packets_ts[0]) + (packets_ts[i] - points[k][0])
                   = points[k][1] / 1000 - (points[k][0] - packets_ts[0])
```

## relevant files per framework

### cAER (version 1.1.2)

The pipeline XML configurations are located in __frameworks/caer/configurations/__.

The modules are located in __frameworks/caer/benchmark/__.

### kAER (version 0.6)

Both the pipelines and filters are located in __frameworks/kaer/source/__.

### tarsier (2019-06)

The pipelines are located in __frameworks/tarsier/source/__.

The event handlers are located in __frameworks/tarsier/third_party/tarsier/source/__.

### event-driven YARP (2019-06)

Both the pipelines and filters are located in __frameworks/yarp/event-driven/src/benchmark/__.

The pipelines are assembled in C++ rather than XML, since the latter creates multiple program which are more difficult to start and stop automatically from the benchmark script.

We modified the source file __frameworks/yarp/event-driven/libraries/include/iCub/eventdriven/vPort.h__ in the YARP codebase to allow empty packets, which are required to count the number of packets from our memory sink component in order to gracefully exit the program. The modifications are preceded by a comment tagged `@BENCHMARK`, lines 96 and 200.
