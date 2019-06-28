#include "caer-sdk/cross/portable_io.h"
#include "caer-sdk/cross/portable_threads.h"
#include "caer-sdk/cross/portable_time.h"
#include "caer-sdk/utils.h"

#include <boost/filesystem.hpp>
#include <cstring>

#if defined(OS_UNIX)
#include <pthread.h>
#include <pwd.h>
#include <sched.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#if defined(OS_LINUX)
#include <sys/prctl.h>
#include <sys/resource.h>
#endif
#elif defined(OS_WINDOWS)
#define WIN32_LEAN_AND_MEAN
#include <errno.h>
#include <io.h>
#include <windows.h>
#endif

char *portable_realpath(const char *path) {
#if defined(OS_UNIX)
	return (realpath(path, nullptr));
#elif defined(OS_WINDOWS)
	return (_fullpath(nullptr, path, _MAX_PATH));
#else
#error "No portable realpath() found."
#endif
}

int portable_fsync(int fd) {
#if defined(OS_UNIX)
	return (fsync(fd));
#elif defined(OS_WINDOWS)
	return (_commit(fd));
#else
#error "No portable fsync() found."
#endif
}

static inline bool checkPath(boost::filesystem::path &path) {
	return (!path.empty() && boost::filesystem::exists(path) && boost::filesystem::is_directory(path));
}

// Remember to free strings returned by this.
char *portable_get_user_home_directory(void) {
	char *homeDir = nullptr;

#if defined(OS_UNIX)
	// Unix: First check the environment for $HOME.
	boost::filesystem::path envHome(getenv("HOME"));

	if (checkPath(envHome)) {
		homeDir = strdup(envHome.string().c_str());
	}

	// Else try to get it from the user data storage.
	if (homeDir == nullptr) {
		struct passwd userPasswd;
		struct passwd *userPasswdPtr;
		char userPasswdBuf[4096];

		if (getpwuid_r(getuid(), &userPasswd, userPasswdBuf, sizeof(userPasswdBuf), &userPasswdPtr) == 0) {
			boost::filesystem::path passwdPwDir(userPasswd.pw_dir);

			if (checkPath(passwdPwDir)) {
				homeDir = strdup(passwdPwDir.string().c_str());
			}
		}
	}
#elif defined(OS_WINDOWS)
	// Windows: First check the environment for $USERPROFILE.
	boost::filesystem::path envUserProfile(getenv("USERPROFILE"));

	if (checkPath(envUserProfile)) {
		homeDir = strdup(envUserProfile.string().c_str());
	}

	// Else try the concatenation of $HOMEDRIVE and $HOMEPATH.
	if (homeDir == nullptr) {
		boost::filesystem::path envHomeDrive(getenv("HOMEDRIVE"));
		boost::filesystem::path envHomePath(getenv("HOMEPATH"));

		envHomeDrive /= envHomePath;

		if (checkPath(envHomeDrive)) {
			homeDir = strdup(envHomeDrive.string().c_str());
		}
	}

	// And last try $HOME.
	if (homeDir == nullptr) {
		boost::filesystem::path envHome(getenv("HOME"));

		if (checkPath(envHome)) {
			homeDir = strdup(envHome.string().c_str());
		}
	}
#endif

	// If nothing else worked, try getting a temporary path.
	if (homeDir == nullptr) {
		boost::filesystem::path tempDir = boost::filesystem::temp_directory_path();

		if (checkPath(tempDir)) {
			homeDir = strdup(tempDir.string().c_str());
		}
	}

	// Absolutely nothing worked: stop and return NULL.
	if (homeDir == nullptr) {
		return (nullptr);
	}

	char *realHomeDir = portable_realpath(homeDir);
	if (realHomeDir == nullptr) {
		free(homeDir);

		return (nullptr);
	}

	free(homeDir);

	return (realHomeDir);
}

#if defined(OS_MACOSX)
#include <mach/clock.h>
#include <mach/clock_types.h>
#include <mach/mach.h>
#include <mach/mach_host.h>
#include <mach/mach_port.h>
#include <mach/mach_time.h>

bool portable_clock_gettime_monotonic(struct timespec *monoTime) {
	kern_return_t kRet;
	clock_serv_t clockRef;
	mach_timespec_t machTime;

	mach_port_t host = mach_host_self();

	kRet = host_get_clock_service(host, SYSTEM_CLOCK, &clockRef);
	mach_port_deallocate(mach_task_self(), host);

	if (kRet != KERN_SUCCESS) {
		errno = EINVAL;
		return (false);
	}

	kRet = clock_get_time(clockRef, &machTime);
	mach_port_deallocate(mach_task_self(), clockRef);

	if (kRet != KERN_SUCCESS) {
		errno = EINVAL;
		return (false);
	}

	monoTime->tv_sec  = machTime.tv_sec;
	monoTime->tv_nsec = machTime.tv_nsec;

	return (true);
}

bool portable_clock_gettime_realtime(struct timespec *realTime) {
	kern_return_t kRet;
	clock_serv_t clockRef;
	mach_timespec_t machTime;

	mach_port_t host = mach_host_self();

	kRet = host_get_clock_service(host, CALENDAR_CLOCK, &clockRef);
	mach_port_deallocate(mach_task_self(), host);

	if (kRet != KERN_SUCCESS) {
		errno = EINVAL;
		return (false);
	}

	kRet = clock_get_time(clockRef, &machTime);
	mach_port_deallocate(mach_task_self(), clockRef);

	if (kRet != KERN_SUCCESS) {
		errno = EINVAL;
		return (false);
	}

	realTime->tv_sec  = machTime.tv_sec;
	realTime->tv_nsec = machTime.tv_nsec;

	return (true);
}
#elif ((defined(_POSIX_C_SOURCE) && _POSIX_C_SOURCE >= 200112L) || (defined(_XOPEN_SOURCE) && _XOPEN_SOURCE >= 600) \
	   || (defined(_WIN32) && defined(__MINGW32__)))
bool portable_clock_gettime_monotonic(struct timespec *monoTime) {
	return (clock_gettime(CLOCK_MONOTONIC, monoTime) == 0);
}

bool portable_clock_gettime_realtime(struct timespec *realTime) {
	return (clock_gettime(CLOCK_REALTIME, realTime) == 0);
}
#else
#error "No portable way of getting absolute monotonic time found."
#endif

bool portable_thread_set_name(const char *name) {
#if defined(OS_LINUX)
	if (prctl(PR_SET_NAME, name) != 0) {
		return (false);
	}

	return (true);
#elif defined(OS_MACOSX)
	if (pthread_setname_np(name) != 0) {
		return (false);
	}

	return (true);
#elif defined(OS_WINDOWS)
	// Windows: this is not possible, only for debugging.
	UNUSED_ARGUMENT(name);
	return (false);
#else
#error "No portable way of setting thread name found."
#endif
}

bool portable_thread_set_priority_highest(void) {
#if defined(OS_UNIX)
	int sched_policy = 0;
	struct sched_param sched_priority;
	memset(&sched_priority, 0, sizeof(struct sched_param));

	if (pthread_getschedparam(pthread_self(), &sched_policy, &sched_priority) != 0) {
		return (false);
	}

	sched_priority.sched_priority = sched_get_priority_max(sched_policy);

	if (pthread_setschedparam(pthread_self(), sched_policy, &sched_priority) != 0) {
		return (false);
	}

	return (true);
#elif defined(OS_WINDOWS)
	if (SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST) == 0) {
		return (false);
	}

	return (true);
#else
#error "No portable way of raising thread priority found."
#endif
}
