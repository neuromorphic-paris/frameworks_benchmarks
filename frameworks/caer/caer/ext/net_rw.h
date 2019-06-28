#ifndef CAER_SDK_NET_RW_H_
#define CAER_SDK_NET_RW_H_

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>

/**
 * Write N bytes to the socket S from buffer B.
 *
 * @param sock socket S.
 * @param buffer buffer B.
 * @param bytesToWrite number of bytes to write.
 *
 * @return Return true on success, false on failure.
 */
static inline bool sendUntilDone(int sock, const uint8_t *buffer, size_t bytesToWrite) {
	size_t curWritten = 0;

	while (curWritten < bytesToWrite) {
		ssize_t sendResult = send(sock, buffer + curWritten, bytesToWrite - curWritten, 0);
		if (sendResult <= 0) {
			return (false);
		}

		curWritten += (size_t) sendResult;
	}

	return (true);
}

/**
 * Read N bytes from the socket S into buffer B.
 *
 * @param sock socket S.
 * @param buffer buffer B.
 * @param bytesToRead number of bytes to read.
 *
 * @return Return true on success, false on failure.
 */
static inline bool recvUntilDone(int sock, uint8_t *buffer, size_t bytesToRead) {
	size_t curRead = 0;

	while (curRead < bytesToRead) {
		ssize_t recvResult = recv(sock, buffer + curRead, bytesToRead - curRead, 0);
		if (recvResult <= 0) {
			return (false);
		}

		curRead += (size_t) recvResult;
	}

	return (true);
}

/**
 * Write N bytes to the file descriptor FD from buffer B.
 *
 * @param fd file descriptor FD.
 * @param buffer buffer B.
 * @param bytesToWrite number of bytes to write.
 *
 * @return Return true on success, false on failure.
 */
static inline bool writeUntilDone(int fd, const uint8_t *buffer, size_t bytesToWrite) {
	size_t curWritten = 0;

	while (curWritten < bytesToWrite) {
		ssize_t writeResult = write(fd, buffer + curWritten, bytesToWrite - curWritten);
		if (writeResult < 0) {
			// Error.
			return (false);
		}
		else if (writeResult == 0) {
			// Nothing was written, but also no errors, so we try again.
			continue;
		}

		curWritten += (size_t) writeResult;
	}

	return (true);
}

/**
 * Read N bytes from the file descriptor FD into buffer B.
 *
 * @param fd file descriptor FD.
 * @param buffer buffer B.
 * @param bytesToRead number of bytes to read.
 *
 * @return Return bytesToRead if all bytes were successfully read, or a smaller
 * value (down to and including zero) if EOF is reached. Return -1 on any kind of error.
 */
static inline ssize_t readUntilDone(int fd, uint8_t *buffer, size_t bytesToRead) {
	size_t curRead = 0;

	while (curRead < bytesToRead) {
		ssize_t readResult = read(fd, buffer + curRead, bytesToRead - curRead);
		if (readResult < 0) {
			// Error. Sets errno.
			return (-1);
		}
		else if (readResult == 0) {
			// End-of-file (also for sockets).
			break;
		}

		curRead += (size_t) readResult;
	}

	return ((ssize_t) curRead);
}

#endif /* CAER_SDK_NET_RW_H_ */
