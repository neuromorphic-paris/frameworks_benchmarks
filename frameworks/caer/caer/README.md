# caer

Event-based processing framework for neuromorphic devices, written in C/C++, targeting embedded and desktop systems. <br />
[![Build Status](https://travis-ci.org/inivation/caer.svg?branch=master)](https://travis-ci.org/inivation/caer)

# Dependencies:

NOTE: if you intend to install the latest cAER git master checkout, please also make sure to use the latest
libcaer git master checkout, as we often depend on new features of the libcare development version.

Linux, MacOS X or Windows (for Windows build instructions see README.Windows) <br />
gcc >= 5.2 or clang >= 3.6 <br />
cmake >= 2.6 <br />
Boost >= 1.50 (with system, filesystem, iostreams, program_options) <br />
libcaer = 3.1.0 (currently ONLY libcaer 3.1.0)<br />
Optional: tcmalloc >= 2.2 (faster memory allocation) <br />
Optional: SFML >= 2.3.0 (visualizer module) <br />
Optional: OpenCV >= 3.1 (cameracalibration, framestatistics modules) <br />
Optional: libpng >= 1.6 (input/output frame PNG compression) <br />
Optional: libuv >= 1.7.5 (output module, deprecated) <br />

Install all dependencies manually on Ubuntu Bionic:
$ sudo apt install git cmake build-essential pkg-config libboost-all-dev libusb-1.0-0-dev libserialport-dev libopencv-contrib-dev libopencv-dev libuv1-dev libsfml-dev libglew-dev

# Installation

1) configure: <br />

$ cmake -DCMAKE_INSTALL_PREFIX=/usr <OPTIONS> . <br />

The following options are currently supported: <br />
-DUSE_TCMALLOC=1 -- Enables usage of TCMalloc from Google to allocate memory. <br />
-DUSE_OPENCV=1 -- Compiles modules depending on OpenCV: camera calibration/undistortion and frame histogram statisics. <br />
-DVISUALIZER=1 -- Open windows in which to visualize data. <br />

To enable all just type: <br />
  cmake -DCMAKE_INSTALL_PREFIX=/usr -DUSE_OPENCV=1 -DVISUALIZER=1 .
<br />
2) build:
<br />
$ make
<br />
3) install:
<br />
$ make install
<br />

# Usage

You will need a 'caer-config.xml' file that specifies which and how modules
should be interconnected. A good starting point is 'docs/davis-config.xml',
please also read through 'docs/modules.txt' for an explanation of the modules
system and its configuration syntax.
<br />
$ caer-bin (see docs/ for more info on how to use cAER) <br />
$ caer-ctl (command-line run-time control program, optional) <br />

# Help

Please use our GitHub bug tracker to report issues and bugs, or
our Google Groups mailing list for discussions and announcements.

BUG TRACKER: https://github.com/inivation/caer/issues/
MAILING LIST: https://groups.google.com/d/forum/caer-users/

BUILD STATUS: https://travis-ci.org/inivation/caer/
CODE ANALYSIS: https://sonarcloud.io/dashboard?id=com.inivation.caer
