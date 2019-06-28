![banner](banner.png)

Sepia implements the [Event Stream](https://github.com/neuromorphic-paris/event_stream) specification, and provides components on top of which a communication library with actual event-based cameras can be built.

# install

Within a Git repository, run the commands:

```sh
mkdir -p third_party
cd third_party
git submodule add https://github.com/neuromorphic-paris/sepia.git
git submodule update --init --recursive
```

On __Linux__, an application using Sepia must link to pthread.

# user guides and documentation

User guides and code documentation are held in the [wiki](https://github.com/neuromorphic-paris/sepia/wiki).

# contribute

## development dependencies

### Debian / Ubuntu

Open a terminal and run:
```sh
sudo apt install premake4 # cross-platform build configuration
sudo apt install clang-format # formatting tool
```

### macOS

Open a terminal and run:
```sh
brew install premake # cross-platform build configuration
brew install clang-format # formatting tool
```
If the command is not found, you need to install Homebrew first with the command:
```sh
ruby -e "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/master/install)"
```

### Windows

Download and install:
- [Visual Studio Community](https://visualstudio.microsoft.com/vs/community/). Select at least __Desktop development with C++__ when asked.
- [git](https://git-scm.com)
- [premake 4.x](https://premake.github.io/download.html). In order to use it from the command line, the *premake4.exe* executable must be copied to a directory in your path. After downloading and decompressing *premake-4.4-beta5-windows.zip*, run in the command prompt:
```sh
copy "%userprofile%\Downloads\premake-4.4-beta5-windows\premake4.exe" "%userprofile%\AppData\Local\Microsoft\WindowsApps"
```

## test

To test the library, run from the *sepia* directory:
```sh
premake4 gmake
cd build
make
cd release
./sepia
```

__Windows__ users must run `premake4 vs2010` instead, and open the generated solution with Visual Studio.

After changing the code, format the source files by running from the *sepia* directory:
```sh
clang-format -i source/sepia.hpp
clang-format -i test/sepia.cpp
```

__Windows__ users must run *Edit* > *Advanced* > *Format Document* from the Visual Studio menu instead.

# license

See the [LICENSE](LICENSE.txt) file for license rights and limitations (GNU GPLv3).
