#ifndef CAER_SDK_BUFFERS_H_
#define CAER_SDK_BUFFERS_H_

#ifdef __cplusplus

#include <cstdint>
#include <cstdlib>
#include <cstring>

#else

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#endif

#ifdef __cplusplus
extern "C" {
#endif

struct simple_buffer {
	/// Current position inside buffer.
	size_t bufferPosition;
	/// Size of data currently inside buffer, in bytes.
	size_t bufferUsedSize;
	/// Size of buffer, in bytes.
	size_t bufferSize;
	/// Buffer for R/W to file descriptor (buffered I/O).
	uint8_t buffer[];
};

typedef struct simple_buffer *simpleBuffer;

static inline simpleBuffer simpleBufferInit(size_t size) {
	// Allocate new buffer.
	simpleBuffer newBuffer = (simpleBuffer) calloc(1, sizeof(*newBuffer) + (size * sizeof(uint8_t)));
	if (newBuffer == NULL) {
		return (NULL);
	}

	// Update new buffer size information.
	newBuffer->bufferSize     = size;
	newBuffer->bufferUsedSize = 0;
	newBuffer->bufferPosition = 0;

	return (newBuffer);
}

// Initialize double-indirection contiguous 2D array, so that array[x][y]
// is possible, see http://c-faq.com/aryptr/dynmuldimary.html for info.
#define buffers_define_2d_typed(TYPE, NAME)                                                                          \
                                                                                                                     \
	struct simple_2d_buffer_##TYPE {                                                                                 \
		size_t sizeX;                                                                                                \
		size_t sizeY;                                                                                                \
		TYPE *buffer2d[];                                                                                            \
	};                                                                                                               \
                                                                                                                     \
	typedef struct simple_2d_buffer_##TYPE *simple2DBuffer##NAME;                                                    \
                                                                                                                     \
	static inline simple2DBuffer##NAME simple2DBufferInit##NAME(size_t sizeX, size_t sizeY) {                        \
		simple2DBuffer##NAME buffer2d = (simple2DBuffer##NAME) malloc(sizeof(*buffer2d) + (sizeX * sizeof(TYPE *))); \
		if (buffer2d == NULL) {                                                                                      \
			return (NULL);                                                                                           \
		}                                                                                                            \
                                                                                                                     \
		buffer2d->buffer2d[0] = (TYPE *) calloc(sizeX * sizeY, sizeof(TYPE));                                        \
		if (buffer2d->buffer2d[0] == NULL) {                                                                         \
			free(buffer2d);                                                                                          \
			return (NULL);                                                                                           \
		}                                                                                                            \
                                                                                                                     \
		for (size_t i = 1; i < sizeX; i++) {                                                                         \
			buffer2d->buffer2d[i] = buffer2d->buffer2d[0] + (i * sizeY);                                             \
		}                                                                                                            \
                                                                                                                     \
		buffer2d->sizeX = sizeX;                                                                                     \
		buffer2d->sizeY = sizeY;                                                                                     \
                                                                                                                     \
		return (buffer2d);                                                                                           \
	}                                                                                                                \
                                                                                                                     \
	static inline void simple2DBufferFree##NAME(simple2DBuffer##NAME buffer2d) {                                     \
		if (buffer2d != NULL) {                                                                                      \
			free(buffer2d->buffer2d[0]);                                                                             \
			free(buffer2d);                                                                                          \
		}                                                                                                            \
	}                                                                                                                \
                                                                                                                     \
	static inline void simple2DBufferReset##NAME(simple2DBuffer##NAME buffer2d) {                                    \
		if (buffer2d != NULL) {                                                                                      \
			memset(buffer2d->buffer2d[0], 0, buffer2d->sizeX * buffer2d->sizeY * sizeof(TYPE));                      \
		}                                                                                                            \
	}

buffers_define_2d_typed(int8_t, Byte) buffers_define_2d_typed(int16_t, Short) buffers_define_2d_typed(int32_t, Int)
	buffers_define_2d_typed(int64_t, Long) buffers_define_2d_typed(float, Float) buffers_define_2d_typed(double, Double)

#ifdef __cplusplus
}
#endif

#endif /* CAER_SDK_BUFFERS_H_ */
