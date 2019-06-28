#ifndef EXT_LIBUV_H_
#define EXT_LIBUV_H_

#include "caer-sdk/buffers.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <uv.h>

#define UV_RET_CHECK(RET_VAL, SUBSYS_NAME, FUNC_NAME, CLEANUP_ACTIONS)                                            \
	if (RET_VAL < 0) {                                                                                            \
		caerLog(CAER_LOG_ERROR, SUBSYS_NAME, FUNC_NAME " failed, error %d (%s).", RET_VAL, uv_err_name(RET_VAL)); \
		CLEANUP_ACTIONS;                                                                                          \
	}

#define UV_RET_CHECK_STDERR(RET_VAL, FUNC_NAME, CLEANUP_ACTIONS)                               \
	if (RET_VAL < 0) {                                                                         \
		fprintf(stderr, FUNC_NAME " failed, error %d (%s).\n", RET_VAL, uv_err_name(RET_VAL)); \
		CLEANUP_ACTIONS;                                                                       \
	}

static inline bool simpleBufferFileWrite(uv_loop_t *loop, uv_file file, int64_t fileOffset, simpleBuffer buffer) {
	if (buffer->bufferUsedSize > buffer->bufferSize) {
		// Using more memory than available, this can't work!
		return (false);
	}

	if (buffer->bufferPosition >= buffer->bufferUsedSize) {
		// Position is after any valid data, this can't work!
		return (false);
	}

	int retVal;
	uv_fs_t fileWrite;
	uv_buf_t writeBuffer;

	size_t curWritten   = 0;
	size_t bytesToWrite = buffer->bufferUsedSize - buffer->bufferPosition;

	while (curWritten < bytesToWrite) {
		writeBuffer.base = (char *) buffer->buffer + buffer->bufferPosition + curWritten;
		writeBuffer.len  = bytesToWrite - curWritten;

		retVal = uv_fs_write(loop, &fileWrite, file, &writeBuffer, 1, fileOffset + (int64_t) curWritten, NULL);
		if (retVal < 0) {
			errno = retVal;
			break;
		}

		if (fileWrite.result < 0) {
			// Error.
			errno = (int) fileWrite.result;
			break;
		}
		else if (fileWrite.result == 0) {
			// Nothing was written, but also no errors, so we try again.
			continue;
		}

		curWritten += (size_t) fileWrite.result;
	}

	uv_fs_req_cleanup(&fileWrite);

	return (curWritten == bytesToWrite);
}

static inline bool simpleBufferFileRead(uv_loop_t *loop, uv_file file, int64_t fileOffset, simpleBuffer buffer) {
	if (buffer->bufferPosition >= buffer->bufferSize) {
		// Position is after maximum capacity, this can't work!
		return (false);
	}

	int retVal;
	uv_fs_t fileRead;
	uv_buf_t readBuffer;

	size_t curRead     = 0;
	size_t bytesToRead = buffer->bufferSize - buffer->bufferPosition;

	while (curRead < bytesToRead) {
		readBuffer.base = (char *) buffer->buffer + buffer->bufferPosition + curRead;
		readBuffer.len  = bytesToRead - curRead;

		retVal = uv_fs_read(loop, &fileRead, file, &readBuffer, 1, fileOffset + (int64_t) curRead, NULL);
		if (retVal < 0) {
			errno = retVal;
			break;
		}

		if (fileRead.result < 0) {
			// Error.
			errno = (int) fileRead.result;
			break;
		}
		else if (fileRead.result == 0) {
			// End of File reached.
			errno = 0;
			break;
		}

		curRead += (size_t) fileRead.result;
	}

	uv_fs_req_cleanup(&fileRead);

	if (curRead == bytesToRead) {
		// Actual data, update UsedSize.
		buffer->bufferUsedSize = buffer->bufferPosition + (size_t) curRead;
		return (true);
	}
	else {
		return (false);
	}
}

struct libuvWriteBufStruct {
	uv_buf_t buf;
	void *freeBuf;
};

typedef struct libuvWriteBufStruct *libuvWriteBuf;

struct libuvWriteMultiBufStruct {
	void *data;      // Allow arbitrary data to be attached to buffers for callback. Must be on heap for free().
	size_t refCount; // Reference count this to allow efficient multiple destination writes.
	void (*statusCheck)(uv_handle_t *handle, int status);
	size_t buffersSize;
	struct libuvWriteBufStruct buffers[];
};

typedef struct libuvWriteMultiBufStruct *libuvWriteMultiBuf;

static inline libuvWriteMultiBuf libuvWriteBufAlloc(size_t number) {
	libuvWriteMultiBuf writeBufs
		= calloc(1, sizeof(struct libuvWriteMultiBufStruct) + (number * sizeof(struct libuvWriteBufStruct)));
	if (writeBufs == NULL) {
		return (NULL);
	}

	writeBufs->refCount    = 1; // Start at one. There must be at least one reference.
	writeBufs->buffersSize = number;

	return (writeBufs);
}

static inline void libuvWriteBufFree(libuvWriteMultiBuf buffers) {
	if (buffers == NULL) {
		return;
	}

	// If reference count is one (this is the only reference), we free the
	// memory, else we just decrease the reference count. Since this is called
	// within one thread's event loop, no locking is needed.
	if (buffers->refCount == 1) {
		for (size_t i = 0; i < buffers->buffersSize; i++) {
			free(buffers->buffers[i].freeBuf);
		}

		free(buffers->data);
		free(buffers);
	}
	else {
		buffers->refCount--;
	}
}

static inline void libuvWriteBufInternalInit(
	libuvWriteBuf writeBuf, void *buffer, size_t bufferSize, void *bufferToFree) {
	writeBuf->buf.base = (char *) buffer;
	writeBuf->buf.len  = bufferSize;

	if (bufferToFree == NULL) {
		writeBuf->freeBuf = buffer;
	}
	else {
		writeBuf->freeBuf = bufferToFree;
	}
}

static inline void libuvWriteBufInit(libuvWriteBuf writeBuf, size_t size) {
	if (size == 0) {
		return;
	}

	uint8_t *dataBuf = malloc(size);
	if (dataBuf == NULL) {
		return;
	}

	libuvWriteBufInternalInit(writeBuf, dataBuf, size, NULL);
}

static inline void libuvWriteBufInitWithSimpleBuffer(libuvWriteBuf writeBuf, simpleBuffer sBuffer) {
	if (sBuffer == NULL) {
		return;
	}

	libuvWriteBufInternalInit(writeBuf, sBuffer->buffer, sBuffer->bufferUsedSize, sBuffer);
}

static inline void libuvWriteBufInitWithAnyBuffer(libuvWriteBuf writeBuf, void *buffer, size_t bufferSize) {
	if (buffer == NULL || bufferSize == 0) {
		return;
	}

	libuvWriteBufInternalInit(writeBuf, buffer, bufferSize, NULL);
}

static inline void libuvWriteFree(uv_write_t *writeRequest, int status) {
	libuvWriteMultiBuf buffers = writeRequest->data;

	if (buffers != NULL && buffers->statusCheck != NULL) {
		(*buffers->statusCheck)((uv_handle_t *) writeRequest->handle, status);
	}

	libuvWriteBufFree(buffers);

	free(writeRequest);
}

// buffer has to be dynamically allocated (on heap). On success, will get free'd
// automatically. On failure, buffer won't be touched.
static inline int libuvWrite(uv_stream_t *dest, libuvWriteMultiBuf buffers) {
	uv_write_t *writeRequest = calloc(1, sizeof(*writeRequest));
	if (writeRequest == NULL) {
		return (UV_ENOMEM);
	}

	writeRequest->data = buffers;

	uv_buf_t uvBuffers[buffers->buffersSize];

	for (size_t i = 0; i < buffers->buffersSize; i++) {
		uvBuffers[i] = buffers->buffers[i].buf;
	}

	int retVal = uv_write(writeRequest, dest, uvBuffers, (unsigned int) buffers->buffersSize, &libuvWriteFree);
	if (retVal < 0) {
		free(writeRequest);
	}

	return (retVal);
}

static inline void libuvWriteFreeUDP(uv_udp_send_t *sendRequest, int status) {
	libuvWriteMultiBuf buffers = sendRequest->data;

	if (buffers != NULL && buffers->statusCheck != NULL) {
		(*buffers->statusCheck)((uv_handle_t *) sendRequest->handle, status);
	}

	libuvWriteBufFree(buffers);

	free(sendRequest);
}

// buffer has to be dynamically allocated (on heap). On success, will get free'd
// automatically. On failure, buffer won't be touched.
static inline int libuvWriteUDP(uv_udp_t *dest, const struct sockaddr *destAddress, libuvWriteMultiBuf buffers) {
	uv_udp_send_t *sendRequest = calloc(1, sizeof(*sendRequest));
	if (sendRequest == NULL) {
		return (UV_ENOMEM);
	}

	sendRequest->data = buffers;

	uv_buf_t uvBuffers[buffers->buffersSize];

	for (size_t i = 0; i < buffers->buffersSize; i++) {
		uvBuffers[i] = buffers->buffers[i].buf;
	}

	int retVal = uv_udp_send(
		sendRequest, dest, uvBuffers, (unsigned int) buffers->buffersSize, destAddress, &libuvWriteFreeUDP);
	if (retVal < 0) {
		free(sendRequest);
	}

	return (retVal);
}

static inline void libuvCloseFree(uv_handle_t *handle) {
	free(handle);
}

static inline void libuvCloseFreeData(uv_handle_t *handle) {
	free(handle->data);
	libuvCloseFree(handle);
}

static inline void libuvCloseLoopWalk(uv_handle_t *handle, void *arg) {
	(void) (arg); // UNUSED.

	if (!uv_is_closing(handle)) {
		uv_close(handle, &libuvCloseFree);
	}
}

static inline int libuvCloseLoopHandles(uv_loop_t *loop) {
	uv_walk(loop, &libuvCloseLoopWalk, NULL);

	return (uv_run(loop, UV_RUN_DEFAULT));
}

#endif /* EXT_LIBUV_H_ */
