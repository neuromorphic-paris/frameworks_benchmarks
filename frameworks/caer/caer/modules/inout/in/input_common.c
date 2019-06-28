#include "input_common.h"

#include "caer-sdk/cross/portable_threads.h"
#include "caer-sdk/cross/portable_time.h"
#include "caer-sdk/mainloop.h"

#include "ext/net_rw.h"
#include "ext/uthash/utlist.h"

#ifdef ENABLE_INOUT_PNG_COMPRESSION
#	include <png.h>
#endif

#include <libcaer/events/common.h>
#include <libcaer/events/frame.h>
#include <libcaer/events/packetContainer.h>
#include <libcaer/events/polarity.h>
#include <libcaer/events/special.h>

#include <libcaer/devices/dynapse.h> // CONSTANTS only.

#include <stdatomic.h>

#define MAX_HEADER_LINE_SIZE 1024

enum input_reader_state {
	READER_OK    = 0,
	EOF_REACHED  = 1,
	ERROR_READ   = -1,
	ERROR_HEADER = -2,
	ERROR_DATA   = -3,
};

static bool newInputBuffer(inputCommonState state);
static bool parseNetworkHeader(inputCommonState state);
static char *getFileHeaderLine(inputCommonState state);
static void parseSourceString(char *sourceString, inputCommonState state);
static bool parseFileHeader(inputCommonState state);
static bool parseHeader(inputCommonState state);
static bool parseData(inputCommonState state);
static int aedat2GetPacket(inputCommonState state, int16_t chipID);
static int aedat3GetPacket(inputCommonState state, bool isAEDAT30);
static void aedat30ChangeOrigin(inputCommonState state, caerEventPacketHeader packet);
static bool decompressTimestampSerialize(inputCommonState state, caerEventPacketHeader packet, size_t packetSize);
static bool decompressEventPacket(inputCommonState state, caerEventPacketHeader packet, size_t packetSize);
static int inputReaderThread(void *stateArg);

static bool addToPacketContainer(inputCommonState state, caerEventPacketHeader newPacket, packetData newPacketData);
static caerEventPacketContainer generatePacketContainer(inputCommonState state, bool forceFlush);
static void commitPacketContainer(inputCommonState state, bool forceFlush);
static void doTimeDelay(inputCommonState state);
static void doPacketContainerCommit(inputCommonState state, caerEventPacketContainer packetContainer, bool force);
static bool handleTSReset(inputCommonState state);
static void getPacketInfo(caerEventPacketHeader packet, packetData packetInfoData);
static int inputAssemblerThread(void *stateArg);

static void caerInputCommonConfigListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue);
static int packetsFirstTypeThenSizeCmp(const void *a, const void *b);

static bool newInputBuffer(inputCommonState state) {
	// First check if the size really changed.
	size_t newBufferSize = (size_t) sshsNodeGetInt(state->parentModule->moduleNode, "bufferSize");

	if (state->dataBuffer != NULL && state->dataBuffer->bufferSize == newBufferSize) {
		// Yeah, we're already where we want to be!
		return (true);
	}

	// So we have to change the size, let's see if the new number makes any sense.
	// We want reasonably sized buffers as minimum, that must fit at least the
	// event packet header and the network header fully (so 28 bytes), as well as
	// the standard AEDAT 2.0 and 3.1 headers, so a couple hundred bytes, and that
	// will maintain good performance. 512 seems a good compromise.
	if (newBufferSize < 512) {
		newBufferSize = 512;
	}

	// Allocate new buffer.
	simpleBuffer newBuffer = simpleBufferInit(newBufferSize);
	if (newBuffer == NULL) {
		return (false);
	}

	// Commit previous buffer content and then free the memory.
	if (state->dataBuffer != NULL) {
		// We just free here, there's nothing to do, since the buffer can only get
		// reallocated when it's empty (either at start or after it has been read).
		free(state->dataBuffer);
	}

	// Use new buffer.
	state->dataBuffer = newBuffer;

	return (true);
}

static bool parseNetworkHeader(inputCommonState state) {
	// Network header is 20 bytes long. Use struct to interpret.
	struct aedat3_network_header networkHeader = caerParseNetworkHeader(state->dataBuffer->buffer);
	state->dataBuffer->bufferPosition += AEDAT3_NETWORK_HEADER_LENGTH;

	// Check header values.
	if (networkHeader.magicNumber != AEDAT3_NETWORK_MAGIC_NUMBER) {
		caerModuleLog(state->parentModule, CAER_LOG_ERROR, "AEDAT 3.X magic number not found. Invalid network stream.");
		return (false);
	}

	state->header.isAEDAT3     = true;
	state->header.majorVersion = 3;

	if (state->isNetworkMessageBased) {
		// For message based streams, use the sequence number.
		// TODO: Network: check this for missing packets in message mode!
		state->header.networkSequenceNumber = networkHeader.sequenceNumber;
	}
	else {
		// For stream based transports, this is always zero.
		if (networkHeader.sequenceNumber != 0) {
			caerModuleLog(state->parentModule, CAER_LOG_ERROR, "SequenceNumber is not zero. Invalid network stream.");
			return (false);
		}
	}

	if (networkHeader.versionNumber != AEDAT3_NETWORK_VERSION) {
		caerModuleLog(state->parentModule, CAER_LOG_ERROR, "Unsupported AEDAT version. Invalid network stream.");
		return (false);
	}

	state->header.minorVersion = networkHeader.versionNumber;

	// All formats are supported.
	state->header.formatID = networkHeader.formatNumber;

	// TODO: Network: get sourceInfo node info via config-server side-channel.
	state->header.sourceID = networkHeader.sourceID;

	sshsNodeCreateInt(state->sourceInfoNode, "polaritySizeX", 240, 1, INT16_MAX,
		SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT, "Polarity events width.");
	sshsNodeCreateInt(state->sourceInfoNode, "polaritySizeY", 180, 1, INT16_MAX,
		SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT, "Polarity events height.");
	sshsNodeCreateInt(state->sourceInfoNode, "frameSizeX", 240, 1, INT16_MAX,
		SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT, "Frame events width.");
	sshsNodeCreateInt(state->sourceInfoNode, "frameSizeY", 180, 1, INT16_MAX,
		SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT, "Frame events height.");
	sshsNodeCreateInt(state->sourceInfoNode, "dataSizeX", 240, 1, INT16_MAX,
		SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT, "Data width.");
	sshsNodeCreateInt(state->sourceInfoNode, "dataSizeY", 180, 1, INT16_MAX,
		SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT, "Data height.");
	sshsNodeCreateInt(state->sourceInfoNode, "visualizerSizeX", 240, 1, INT16_MAX,
		SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT, "Visualization width.");
	sshsNodeCreateInt(state->sourceInfoNode, "visualizerSizeY", 180, 1, INT16_MAX,
		SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT, "Visualization height.");

	// TODO: Network: add sourceString.

	// We're done!
	atomic_store(&state->header.isValidHeader, true);

	return (true);
}

static char *getFileHeaderLine(inputCommonState state) {
	simpleBuffer buf = state->dataBuffer;

	if (buf->buffer[buf->bufferPosition] == '#') {
		size_t headerLinePos = 0;
		char *headerLine     = malloc(MAX_HEADER_LINE_SIZE);
		if (headerLine == NULL) {
			// Failed to allocate memory.
			return (NULL);
		}

		headerLine[headerLinePos++] = '#';
		buf->bufferPosition++;

		while (buf->buffer[buf->bufferPosition] != '\n') {
			if (headerLinePos >= (MAX_HEADER_LINE_SIZE - 2)) { // -1 for terminating new-line, -1 for end NUL char.
				// Overlong header line, refuse it.
				free(headerLine);
				return (NULL);
			}

			headerLine[headerLinePos++] = (char) buf->buffer[buf->bufferPosition];
			buf->bufferPosition++;
		}

		// Found terminating new-line character.
		headerLine[headerLinePos++] = '\n';
		buf->bufferPosition++;

		// Now let's just verify that the previous character was indeed a carriage-return.
		if (headerLine[headerLinePos - 2] == '\r') {
			// Valid, terminate it and return it.
			headerLine[headerLinePos] = '\0';

			return (headerLine);
		}
		else {
			// Invalid header line. No Windows line-ending.
			free(headerLine);
			return (NULL);
		}
	}

	// Invalid header line. Doesn't begin with #.
	return (NULL);
}

static void parseSourceString(char *sourceString, inputCommonState state) {
	// Create SourceInfo node.
	int16_t dvsSizeX = 0, dvsSizeY = 0;
	int16_t apsSizeX = 0, apsSizeY = 0;
	int16_t dataSizeX = 0, dataSizeY = 0;
	int16_t visualizerSizeX = 0, visualizerSizeY = 0;

	// Determine sizes via known chip information.
	if (caerStrEquals(sourceString, "DVS128")) {
		dvsSizeX = dvsSizeY = 128;
	}
	else if (caerStrEquals(sourceString, "DAVIS240A") || caerStrEquals(sourceString, "DAVIS240B")
			 || caerStrEquals(sourceString, "DAVIS240C")) {
		dvsSizeX = apsSizeX = 240;
		dvsSizeY = apsSizeY = 180;
	}
	else if (caerStrEquals(sourceString, "DAVIS128")) {
		dvsSizeX = apsSizeX = dvsSizeY = apsSizeY = 128;
	}
	else if (caerStrEquals(sourceString, "DAVIS346A") || caerStrEquals(sourceString, "DAVIS346B")
			 || caerStrEquals(sourceString, "DAVIS346Cbsi") || caerStrEquals(sourceString, "DAVIS346")
			 || caerStrEquals(sourceString, "DAVIS346bsi")) {
		dvsSizeX = apsSizeX = 346;
		dvsSizeY = apsSizeY = 260;
	}
	else if (caerStrEquals(sourceString, "DAVIS640")) {
		dvsSizeX = apsSizeX = 640;
		dvsSizeY = apsSizeY = 480;
	}
	else if (caerStrEquals(sourceString, "DAVISHet640") || caerStrEquals(sourceString, "DAVIS640het")) {
		dvsSizeX = 320;
		dvsSizeY = 240;
		apsSizeX = 640;
		apsSizeY = 480;
	}
	else if (caerStrEquals(sourceString, "DAVIS208")) {
		dvsSizeX = apsSizeX = 208;
		dvsSizeY = apsSizeY = 192;
	}
	else if (caerStrEquals(sourceString, "DYNAPSE")) {
		dataSizeX = DYNAPSE_X4BOARD_NEUX;
		dataSizeY = DYNAPSE_X4BOARD_NEUY;
	}
	else if (caerStrEqualsUpTo(sourceString, "File,", 5)) {
		sscanf(sourceString + 5,
			"dvsSizeX=%" SCNi16 ",dvsSizeY=%" SCNi16 ",apsSizeX=%" SCNi16 ",apsSizeY=%" SCNi16 ","
			"dataSizeX=%" SCNi16 ",dataSizeY=%" SCNi16 ",visualizerSizeX=%" SCNi16 ",visualizerSizeY=%" SCNi16 "\r\n",
			&dvsSizeX, &dvsSizeY, &apsSizeX, &apsSizeY, &dataSizeX, &dataSizeY, &visualizerSizeX, &visualizerSizeY);
	}
	else if (caerStrEqualsUpTo(sourceString, "Network,", 8)) {
		sscanf(sourceString + 8,
			"dvsSizeX=%" SCNi16 ",dvsSizeY=%" SCNi16 ",apsSizeX=%" SCNi16 ",apsSizeY=%" SCNi16 ","
			"dataSizeX=%" SCNi16 ",dataSizeY=%" SCNi16 ",visualizerSizeX=%" SCNi16 ",visualizerSizeY=%" SCNi16 "\r\n",
			&dvsSizeX, &dvsSizeY, &apsSizeX, &apsSizeY, &dataSizeX, &dataSizeY, &visualizerSizeX, &visualizerSizeY);
	}
	else if (caerStrEqualsUpTo(sourceString, "Processor,", 10)) {
		sscanf(sourceString + 10,
			"dvsSizeX=%" SCNi16 ",dvsSizeY=%" SCNi16 ",apsSizeX=%" SCNi16 ",apsSizeY=%" SCNi16 ","
			"dataSizeX=%" SCNi16 ",dataSizeY=%" SCNi16 ",visualizerSizeX=%" SCNi16 ",visualizerSizeY=%" SCNi16 "\r\n",
			&dvsSizeX, &dvsSizeY, &apsSizeX, &apsSizeY, &dataSizeX, &dataSizeY, &visualizerSizeX, &visualizerSizeY);
	}
	else {
		// Default fall-back of 640x480 (VGA).
		caerModuleLog(state->parentModule, CAER_LOG_WARNING,
			"Impossible to determine display sizes from Source information/string. Falling back to 640x480 (VGA).");
		dvsSizeX = apsSizeX = 640;
		dvsSizeY = apsSizeY = 480;
	}

	// Put size information inside sourceInfo node.
	if (dvsSizeX != 0 && dvsSizeY != 0) {
		sshsNodeCreateInt(state->sourceInfoNode, "polaritySizeX", dvsSizeX, 1, INT16_MAX,
			SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT, "Polarity events width.");
		sshsNodeCreateInt(state->sourceInfoNode, "polaritySizeY", dvsSizeY, 1, INT16_MAX,
			SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT, "Polarity events height.");
	}

	if (apsSizeX != 0 && apsSizeY != 0) {
		sshsNodeCreateInt(state->sourceInfoNode, "frameSizeX", apsSizeX, 1, INT16_MAX,
			SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT, "Frame events width.");
		sshsNodeCreateInt(state->sourceInfoNode, "frameSizeY", apsSizeY, 1, INT16_MAX,
			SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT, "Frame events height.");
	}

	if (dataSizeX == 0 && dataSizeY == 0) {
		// Try to auto-discover dataSize, if it was not previously set, based on the
		// presence of DVS or APS sizes. If they don't exist either, this will be 0.
		dataSizeX = (dvsSizeX > apsSizeX) ? (dvsSizeX) : (apsSizeX);
		dataSizeY = (dvsSizeY > apsSizeY) ? (dvsSizeY) : (apsSizeY);
	}

	if (dataSizeX != 0 && dataSizeY != 0) {
		sshsNodeCreateInt(state->sourceInfoNode, "dataSizeX", dataSizeX, 1, INT16_MAX,
			SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT, "Data width.");
		sshsNodeCreateInt(state->sourceInfoNode, "dataSizeY", dataSizeY, 1, INT16_MAX,
			SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT, "Data height.");
	}

	if (visualizerSizeX != 0 && visualizerSizeY != 0) {
		sshsNodeCreateInt(state->sourceInfoNode, "visualizerSizeX", visualizerSizeX, 1, INT16_MAX,
			SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT, "Visualization width.");
		sshsNodeCreateInt(state->sourceInfoNode, "visualizerSizeY", visualizerSizeY, 1, INT16_MAX,
			SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT, "Visualization height.");
	}

	// Generate source string for output modules.
	size_t sourceStringFileLength = (size_t) snprintf(NULL, 0,
		"#Source %" PRIu16 ": File,"
		"dvsSizeX=%" PRIi16 ",dvsSizeY=%" PRIi16 ",apsSizeX=%" PRIi16 ",apsSizeY=%" PRIi16 ","
		"dataSizeX=%" PRIi16 ",dataSizeY=%" PRIi16 ",visualizerSizeX=%" PRIi16 ",visualizerSizeY=%" PRIi16 "\r\n"
		"#-Source %" PRIi16 ": %s\r\n",
		state->parentModule->moduleID, dvsSizeX, dvsSizeY, apsSizeX, apsSizeY, dataSizeX, dataSizeY, visualizerSizeX,
		visualizerSizeY, state->header.sourceID, sourceString);

	char sourceStringFile[sourceStringFileLength + 1];
	snprintf(sourceStringFile, sourceStringFileLength + 1,
		"#Source %" PRIu16 ": File,"
		"dvsSizeX=%" PRIi16 ",dvsSizeY=%" PRIi16 ",apsSizeX=%" PRIi16 ",apsSizeY=%" PRIi16 ","
		"dataSizeX=%" PRIi16 ",dataSizeY=%" PRIi16 ",visualizerSizeX=%" PRIi16 ",visualizerSizeY=%" PRIi16 "\r\n"
		"#-Source %" PRIi16 ": %s\r\n",
		state->parentModule->moduleID, dvsSizeX, dvsSizeY, apsSizeX, apsSizeY, dataSizeX, dataSizeY, visualizerSizeX,
		visualizerSizeY, state->header.sourceID, sourceString);
	sourceStringFile[sourceStringFileLength] = '\0';

	sshsNodeCreateString(state->sourceInfoNode, "sourceString", sourceStringFile, 1, 2048,
		SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_NO_EXPORT, "Device source information.");
}

static bool parseFileHeader(inputCommonState state) {
	// We expect that the full header part is contained within
	// this one data buffer.
	// File headers are part of the AEDAT 3.X specification.
	// Start with #, go until '\r\n' (Windows EOL). First must be
	// version header !AER-DATx.y, last must be end-of-header
	// marker with !END-HEADER (AEDAT 3.1 only).
	bool versionHeader = false;
	bool formatHeader  = false;
	bool sourceHeader  = false;
	bool endHeader     = false;

	while (!endHeader) {
		char *headerLine = getFileHeaderLine(state);
		if (headerLine == NULL) {
			// Failed to parse header line; this is an invalid header for AEDAT 3.1!
			// For AEDAT 2.0 and 3.0, since there is no END-HEADER, this might be
			// the right way for headers to stop, so we consider this valid IFF we
			// already got the version header for AEDAT 2.0, and for AEDAT 3.0 if we
			// also got the required headers Format and Source at least.
			if ((state->header.majorVersion == 2 && state->header.minorVersion == 0) && versionHeader) {
				// Parsed AEDAT 2.0 header successfully (version).
				atomic_store(&state->header.isValidHeader, true);
				return (true);
			}

			if ((state->header.majorVersion == 3 && state->header.minorVersion == 0) && versionHeader && formatHeader
				&& sourceHeader) {
				// Parsed AEDAT 3.0 header successfully (version, format, source).
				atomic_store(&state->header.isValidHeader, true);
				return (true);
			}

			return (false);
		}

		if (!versionHeader) {
			// First thing we expect is the version header. We don't support files not having it.
			if (sscanf(headerLine, "#!AER-DAT%" SCNi16 ".%" SCNi8 "\r\n", &state->header.majorVersion,
					&state->header.minorVersion)
				== 2) {
				versionHeader = true;

				// Check valid versions.
				switch (state->header.majorVersion) {
					case 2:
						// AEDAT 2.0 is supported. No revisions exist.
						if (state->header.minorVersion != 0) {
							goto noValidVersionHeader;
						}
						break;

					case 3:
						state->header.isAEDAT3 = true;

						// AEDAT 3.0 and 3.1 are supported.
						if (state->header.minorVersion != 0 && state->header.minorVersion != 1) {
							goto noValidVersionHeader;
						}
						break;

					default:
						// Versions other than 2.0 and 3.X are not supported.
						goto noValidVersionHeader;
						break;
				}

				caerModuleLog(state->parentModule, CAER_LOG_DEBUG, "Found AEDAT%" PRIi16 ".%" PRIi8 " version header.",
					state->header.majorVersion, state->header.minorVersion);
			}
			else {
			noValidVersionHeader:
				free(headerLine);

				caerModuleLog(
					state->parentModule, CAER_LOG_ERROR, "No compliant AEDAT version header found. Invalid file.");
				return (false);
			}
		}
		else if (state->header.isAEDAT3 && !formatHeader) {
			// Then the format header. Only with AEDAT 3.X.
			char formatString[1024 + 1];

			if (sscanf(headerLine, "#Format: %1024s\r\n", formatString) == 1) {
				formatHeader = true;

				// Parse format string to format ID.
				// We support either only RAW, or a mixture of the various compression modes.
				if (caerStrEquals(formatString, "RAW")) {
					state->header.formatID = 0x00;
				}
				else {
					state->header.formatID = 0x00;

					if (strstr(formatString, "SerializedTS") != NULL) {
						state->header.formatID |= 0x01;
					}

					if (strstr(formatString, "PNGFrames") != NULL) {
						state->header.formatID |= 0x02;
					}

					if (!state->header.formatID) {
						// No valid format found.
						free(headerLine);

						caerModuleLog(state->parentModule, CAER_LOG_ERROR,
							"No compliant Format type found. Format '%s' is invalid.", formatString);

						return (false);
					}
				}

				caerModuleLog(state->parentModule, CAER_LOG_DEBUG,
					"Found Format header with value '%s', Format ID %" PRIi8 ".", formatString, state->header.formatID);
			}
			else {
				free(headerLine);

				caerModuleLog(state->parentModule, CAER_LOG_ERROR, "No compliant Format header found. Invalid file.");
				return (false);
			}
		}
		else if (state->header.isAEDAT3 && !sourceHeader) {
			// Then the source header. Only with AEDAT 3.X. We only support one active source.
			char sourceString[1024 + 1];

			if (sscanf(headerLine, "#Source %" SCNi16 ": %1024[^\r]s\n", &state->header.sourceID, sourceString) == 2) {
				sourceHeader = true;

				// Parse source string to get needed sourceInfo parameters.
				parseSourceString(sourceString, state);

				caerModuleLog(state->parentModule, CAER_LOG_DEBUG,
					"Found Source header with value '%s', Source ID %" PRIi16 ".", sourceString,
					state->header.sourceID);
			}
			else {
				free(headerLine);

				caerModuleLog(state->parentModule, CAER_LOG_ERROR, "No compliant Source header found. Invalid file.");
				return (false);
			}
		}
		else {
			// Now we either have other header lines with AEDAT 2.0/AEDAT 3.X, or
			// the END-HEADER with AEDAT 3.1. We check this before the other possible,
			// because it terminates the AEDAT 3.1 header, so we stop in that case.
			if (caerStrEquals(headerLine, "#!END-HEADER\r\n")) {
				endHeader = true;

				caerModuleLog(state->parentModule, CAER_LOG_DEBUG, "Found END-HEADER header.");
			}
			else {
				// Then other headers, like Start-Time.
				if (caerStrEqualsUpTo(headerLine, "#Start-Time: ", 13)) {
					char startTimeString[1024 + 1];

					if (sscanf(headerLine, "#Start-Time: %1024[^\r]s\n", startTimeString) == 1) {
						caerModuleLog(
							state->parentModule, CAER_LOG_INFO, "Recording was taken on %s.", startTimeString);
					}
				}
				else if (caerStrEqualsUpTo(headerLine, "#-Source ", 9)) {
					// Detect negative source strings (#-Source) and add them to sourceInfo.
					// Previous sources are simply appended to the sourceString string in order.
					char *currSourceString        = sshsNodeGetString(state->sourceInfoNode, "sourceString");
					size_t currSourceStringLength = strlen(currSourceString);

					size_t addSourceStringLength = strlen(headerLine);

					char *newSourceString = realloc(currSourceString,
						currSourceStringLength + addSourceStringLength + 1); // +1 for NUL termination.
					if (newSourceString == NULL) {
						// Memory reallocation failure, skip this negative source string.
						free(currSourceString);
					}
					else {
						// Concatenate negative source string and commit as new sourceString.
						memcpy(newSourceString + currSourceStringLength, headerLine, addSourceStringLength);
						newSourceString[currSourceStringLength + addSourceStringLength] = '\0';

						sshsNodeUpdateReadOnlyAttribute(state->sourceInfoNode, "sourceString", SSHS_STRING,
							(union sshs_node_attr_value){.string = newSourceString});

						free(newSourceString);
					}
				}
				else {
					headerLine[strlen(headerLine) - 2] = '\0'; // Shorten string to avoid printing ending \r\n.
					caerModuleLog(state->parentModule, CAER_LOG_DEBUG, "Header line: '%s'.", headerLine);
				}
			}
		}

		free(headerLine);
	}

	// Parsed AEDAT 3.1 header successfully.
	atomic_store(&state->header.isValidHeader, true);
	return (true);
}

static bool parseHeader(inputCommonState state) {
	if (state->isNetworkStream) {
		return (parseNetworkHeader(state));
	}
	else {
		return (parseFileHeader(state));
	}
}

static bool parseData(inputCommonState state) {
	while (state->dataBuffer->bufferPosition < state->dataBuffer->bufferUsedSize) {
		int pRes = -1;

		// Try getting packet and packetData from buffer.
		if (state->header.majorVersion == 2 && state->header.minorVersion == 0) {
			pRes = aedat2GetPacket(state, 0);
		}
		else if (state->header.majorVersion == 3) {
			pRes = aedat3GetPacket(state, (state->header.minorVersion == 0));
		}
		else {
			// No parseable format found!
			return (false);
		}

		// Check packet parser return value.
		if (pRes < 0) {
			// Error in parsing buffer to get packet.
			return (false);
		}
		else if (pRes == 1) {
			// Finished parsing this buffer with no new packet.
			// Exit to get next buffer.
			break;
		}
		else if (pRes == 2) {
			// Skip requested, run again.
			continue;
		}

		caerModuleLog(state->parentModule, CAER_LOG_DEBUG,
			"New packet read - ID: %zu, Offset: %zu, Size: %zu, Events: %" PRIi32 ", Type: %" PRIi16
			", StartTS: %" PRIi64 ", EndTS: %" PRIi64 ".",
			state->packets.currPacketData->id, state->packets.currPacketData->offset,
			state->packets.currPacketData->size, state->packets.currPacketData->eventNumber,
			state->packets.currPacketData->eventType, state->packets.currPacketData->startTimestamp,
			state->packets.currPacketData->endTimestamp);

		// New packet information, add it to the global packet info list.
		// This is done here to prevent ambiguity about the ownership of the involved memory block:
		// it either is inside the global list with state->packets.currPacketData NULL, or it is not
		// in the list, but in state->packets.currPacketData itself. So if, on exit, we clear both,
		// we'll free all the memory and have no fear of a double-free happening.
		DL_APPEND(state->packets.packetsList, state->packets.currPacketData);
		state->packets.currPacketData = NULL;

		// New packet from stream, send it off to the input assembler thread. Same memory
		// related considerations as above for state->packets.currPacketData apply here too!
		while (!caerRingBufferPut(state->transferRingPackets, state->packets.currPacket)) {
			// We ensure all read packets are sent to the Assembler stage.
			if (!atomic_load_explicit(&state->running, memory_order_relaxed)) {
				// On normal termination, just return without errors. The Reader thread
				// will then also exit without errors and clean up in Exit().
				return (true);
			}

			// Delay by 10 µs if no change, to avoid a wasteful busy loop.
			struct timespec retrySleep = {.tv_sec = 0, .tv_nsec = 10000};
			thrd_sleep(&retrySleep, NULL);
		}

		state->packets.currPacket = NULL;
	}

	// All good, get next buffer.
	return (true);
}

/**
 * Parse the current buffer and try to extract the AEDAT 2.0
 * data contained within, to form a compliant AEDAT 3.1 packet,
 * and then update the packet meta-data list with it.
 *
 * @param state common input data structure.
 * @param chipID chip identifier to decide sizes, ordering and
 * features of the data stream for conversion to AEDAT 3.1.
 *
 * @return 0 on successful packet extraction.
 * Positive numbers for special conditions:
 * 1 if more data needed.
 * Negative numbers on error conditions:
 * -1 on memory allocation failure.
 */
static int aedat2GetPacket(inputCommonState state, int16_t chipID) {
	UNUSED_ARGUMENT(chipID);

	// TODO: AEDAT 2.0 not yet supported.
	caerModuleLog(state->parentModule, CAER_LOG_ERROR, "Reading AEDAT 2.0 data not yet supported.");
	return (-1);
}

/**
 * Parse the current buffer and try to extract the AEDAT 3.X
 * packet contained within, as well as updating the packet
 * meta-data list.
 *
 * @param state common input data structure.
 * @param isAEDAT30 change the X/Y coordinate origin for Frames and Polarity
 * events, as this changed from 3.0 (lower left) to 3.1 (upper left).
 *
 * @return 0 on successful packet extraction.
 * Positive numbers for special conditions:
 * 1 if more data needed.
 * 2 if skip requested (call again).
 * Negative numbers on error conditions:
 * -1 on memory allocation failure.
 * -2 on decompression failure.
 */
static int aedat3GetPacket(inputCommonState state, bool isAEDAT30) {
	simpleBuffer buf = state->dataBuffer;

	// So now we're somewhere inside the buffer (usually at start), and want to
	// read in a very long sequence of event packets.
	// An event packet is made up of header + data, and the header contains the
	// information needed to decode the data and its length, to know then where
	// the next event packet boundary is. So we get the full header first, then
	// the data, but careful, it can all be split across two (header+data) or
	// more (data) buffers, so we need to reassemble!
	size_t remainingData = buf->bufferUsedSize - buf->bufferPosition;

	// First thing, handle skip packet requests. This can happen if packets
	// from another source are mixed in, or we forbid some packet types.
	// In that case, we just skip over all their bytes and try to get the next
	// good packet (header).
	if (state->packets.skipSize != 0) {
		if (state->packets.skipSize >= remainingData) {
			state->packets.skipSize -= remainingData;

			// Go and get next buffer. bufferPosition is at end of buffer.
			buf->bufferPosition += remainingData;
			return (1);
		}
		else {
			buf->bufferPosition += state->packets.skipSize;
			remainingData -= state->packets.skipSize;
			state->packets.skipSize = 0; // Don't skip anymore, continue as usual.
		}
	}

	// Get 28 bytes common packet header.
	if (state->packets.currPacketHeaderSize != CAER_EVENT_PACKET_HEADER_SIZE) {
		if (remainingData < CAER_EVENT_PACKET_HEADER_SIZE) {
			// Reaching end of buffer, the header is split across two buffers!
			memcpy(state->packets.currPacketHeader, buf->buffer + buf->bufferPosition, remainingData);

			state->packets.currPacketHeaderSize = remainingData;

			// Go and get next buffer. bufferPosition is at end of buffer.
			buf->bufferPosition += remainingData;
			return (1);
		}
		else {
			// Either a full header, or the second part of one.
			size_t dataToRead = CAER_EVENT_PACKET_HEADER_SIZE - state->packets.currPacketHeaderSize;

			memcpy(state->packets.currPacketHeader + state->packets.currPacketHeaderSize,
				buf->buffer + buf->bufferPosition, dataToRead);

			state->packets.currPacketHeaderSize += dataToRead;
			buf->bufferPosition += dataToRead;
			remainingData -= dataToRead;
		}

		// So now that we have a full header, let's look at it.
		caerEventPacketHeader packet = (caerEventPacketHeader) state->packets.currPacketHeader;

		int16_t eventType     = caerEventPacketHeaderGetEventType(packet);
		bool isCompressed     = (eventType & 0x8000);
		int16_t eventSource   = caerEventPacketHeaderGetEventSource(packet);
		int32_t eventCapacity = caerEventPacketHeaderGetEventCapacity(packet);
		int32_t eventNumber   = caerEventPacketHeaderGetEventNumber(packet);
		int32_t eventValid    = caerEventPacketHeaderGetEventValid(packet);
		int32_t eventSize     = caerEventPacketHeaderGetEventSize(packet);

		// First we verify that the source ID remained unique (only one source per I/O module supported!).
		if (state->header.sourceID != eventSource) {
			caerModuleLog(state->parentModule, CAER_LOG_ERROR,
				"An input module can only handle packets from the same source! "
				"A packet with source %" PRIi16
				" was read, but this input module expects only packets from source %" PRIi16 ". "
				"Discarding event packet.",
				eventSource, state->header.sourceID);

			// Skip packet. If packet is compressed, eventCapacity carries the size.
			state->packets.skipSize = (isCompressed) ? (size_t)(eventCapacity) : (size_t)(eventNumber * eventSize);
			state->packets.currPacketHeaderSize = 0; // Get new header after skipping.

			// Run function again to skip data. bufferPosition is already up-to-date.
			return (2);
		}

		// If packet is compressed, eventCapacity carries the size in bytes to read.
		state->packets.currPacketDataSize
			= (isCompressed) ? (size_t)(eventCapacity) : (size_t)(eventNumber * eventSize);

		// Allocate space for the full packet, so we can reassemble it (and decompress it later).
		state->packets.currPacket = malloc(CAER_EVENT_PACKET_HEADER_SIZE + (size_t)(eventNumber * eventSize));
		if (state->packets.currPacket == NULL) {
			caerModuleLog(state->parentModule, CAER_LOG_ERROR, "Failed to allocate memory for new event packet.");
			return (-1);
		}

		// First we copy the header in.
		memcpy(state->packets.currPacket, state->packets.currPacketHeader, CAER_EVENT_PACKET_HEADER_SIZE);

		state->packets.currPacketDataOffset = CAER_EVENT_PACKET_HEADER_SIZE;

		// Rewrite event source to reflect this module, not the original one.
		caerEventPacketHeaderSetEventSource(state->packets.currPacket, I16T(state->parentModule->moduleID));

		// If packet was compressed, restore original eventType and eventCapacity,
		// for in-memory usage (no mark bit, eventCapacity == eventNumber).
		if (isCompressed) {
			state->packets.currPacket->eventType
				= htole16(le16toh(state->packets.currPacket->eventType) & I16T(0x7FFF));
			state->packets.currPacket->eventCapacity = htole32(eventNumber);
		}

		// Now we can also start keeping track of this packet's meta-data.
		state->packets.currPacketData = calloc(1, sizeof(struct input_packet_data));
		if (state->packets.currPacketData == NULL) {
			free(state->packets.currPacket);
			state->packets.currPacket = NULL;

			caerModuleLog(
				state->parentModule, CAER_LOG_ERROR, "Failed to allocate memory for new event packet meta-data.");
			return (-1);
		}

		// Fill out meta-data fields with proper information gained from current event packet.
		state->packets.currPacketData->id = state->packets.packetCount++;
		state->packets.currPacketData->offset
			= (state->isNetworkStream)
				  ? (0)
				  : (state->dataBufferOffset + buf->bufferPosition - CAER_EVENT_PACKET_HEADER_SIZE);
		state->packets.currPacketData->size         = CAER_EVENT_PACKET_HEADER_SIZE + state->packets.currPacketDataSize;
		state->packets.currPacketData->isCompressed = isCompressed;
		state->packets.currPacketData->eventType    = caerEventPacketHeaderGetEventType(state->packets.currPacket);
		state->packets.currPacketData->eventSize    = eventSize;
		state->packets.currPacketData->eventNumber  = eventNumber;
		state->packets.currPacketData->eventValid   = eventValid;
		state->packets.currPacketData->startTimestamp = -1; // Invalid for now.
		state->packets.currPacketData->endTimestamp   = -1; // Invalid for now.
	}

	// Now get the data from the buffer to the new event packet. We have to take care of
	// data being split across multiple buffers, as above.
	if (state->packets.currPacketDataSize > remainingData) {
		// We need to copy more data than in this buffer.
		memcpy(((uint8_t *) state->packets.currPacket) + state->packets.currPacketDataOffset,
			buf->buffer + buf->bufferPosition, remainingData);

		state->packets.currPacketDataOffset += remainingData;
		state->packets.currPacketDataSize -= remainingData;

		// Go and get next buffer. bufferPosition is at end of buffer.
		buf->bufferPosition += remainingData;
		return (1);
	}
	else {
		// We copy the last bytes of data and we're done.
		memcpy(((uint8_t *) state->packets.currPacket) + state->packets.currPacketDataOffset,
			buf->buffer + buf->bufferPosition, state->packets.currPacketDataSize);

		// This packet is fully copied and done, so reset variables for next iteration.
		state->packets.currPacketHeaderSize = 0; // Get new header next iteration.
		buf->bufferPosition += state->packets.currPacketDataSize;

		// Decompress packet.
		if (state->packets.currPacketData->isCompressed) {
			if (!decompressEventPacket(state, state->packets.currPacket, state->packets.currPacketData->size)) {
				// Failed to decompress packet. Error exit.
				free(state->packets.currPacket);
				state->packets.currPacket = NULL;
				free(state->packets.currPacketData);
				state->packets.currPacketData = NULL;

				caerModuleLog(state->parentModule, CAER_LOG_ERROR, "Failed to decompress event packet.");
				return (-2);
			}
		}

		// Update timestamp information and insert packet into meta-data list.
		const void *firstEvent = caerGenericEventGetEvent(state->packets.currPacket, 0);
		state->packets.currPacketData->startTimestamp
			= caerGenericEventGetTimestamp64(firstEvent, state->packets.currPacket);

		const void *lastEvent
			= caerGenericEventGetEvent(state->packets.currPacket, state->packets.currPacketData->eventNumber - 1);
		state->packets.currPacketData->endTimestamp
			= caerGenericEventGetTimestamp64(lastEvent, state->packets.currPacket);

		// If the file was in AEDAT 3.0 format, we must change X/Y coordinate origin
		// for Polarity and Frame events. We do this after parsing and decompression.
		if (isAEDAT30) {
			aedat30ChangeOrigin(state, state->packets.currPacket);
		}

		// New packet parsed!
		return (0);
	}
}

static void aedat30ChangeOrigin(inputCommonState state, caerEventPacketHeader packet) {
	if (caerEventPacketHeaderGetEventType(packet) == POLARITY_EVENT) {
		// We need to know the DVS resolution to invert the polarity Y address.
		int16_t dvsSizeY = I16T(sshsNodeGetInt(state->sourceInfoNode, "dvsSizeY") - 1);

		CAER_POLARITY_ITERATOR_ALL_START((caerPolarityEventPacket) packet)
		uint16_t newYAddress = U16T(dvsSizeY - caerPolarityEventGetY(caerPolarityIteratorElement));
		caerPolarityEventSetY(caerPolarityIteratorElement, newYAddress);
	}
}

if (caerEventPacketHeaderGetEventType(packet) == FRAME_EVENT) {
	// For frames, the resolution and size information is carried in each event.
	CAER_FRAME_ITERATOR_ALL_START((caerFrameEventPacket) packet)
	int32_t lengthX                               = caerFrameEventGetLengthX(caerFrameIteratorElement);
	int32_t lengthY                               = caerFrameEventGetLengthY(caerFrameIteratorElement);
	enum caer_frame_event_color_channels channels = caerFrameEventGetChannelNumber(caerFrameIteratorElement);

	size_t rowSize = (size_t) lengthX * channels;

	uint16_t *pixels = caerFrameEventGetPixelArrayUnsafe(caerFrameIteratorElement);

	// Invert position of entire rows.
	for (size_t y = 0; y < (size_t) lengthY; y++) {
		size_t invY = (size_t) lengthY - 1 - y;

		// Don't invert if no position change, this happens in the exact
		// middle if lengthY is uneven.
		if (y != invY) {
			memcpy(&pixels[y * rowSize], &pixels[invY * rowSize], rowSize);
		}
	}
}
}
}

#ifdef ENABLE_INOUT_PNG_COMPRESSION

static void caerLibPNGReadBuffer(png_structp png_ptr, png_bytep data, png_size_t length);
static bool decompressFramePNG(inputCommonState state, caerEventPacketHeader packet, size_t packetSize);

// Simple structure to store PNG image bytes.
struct caer_libpng_buffer {
	uint8_t *buffer;
	size_t size;
	size_t pos;
};

static void caerLibPNGReadBuffer(png_structp png_ptr, png_bytep data, png_size_t length) {
	struct caer_libpng_buffer *p = (struct caer_libpng_buffer *) png_get_io_ptr(png_ptr);
	size_t newPos                = p->pos + length;

	// Detect attempts to read past buffer end.
	if (newPos > p->size) {
		png_error(png_ptr, "Read Buffer Error");
	}

	memcpy(data, p->buffer + p->pos, length);
	p->pos += length;
}

static inline enum caer_frame_event_color_channels caerFrameEventColorFromLibPNG(int channels) {
	switch (channels) {
		case PNG_COLOR_TYPE_GRAY:
			return (GRAYSCALE);
			break;

		case PNG_COLOR_TYPE_RGB:
			return (RGB);
			break;

		case PNG_COLOR_TYPE_RGBA:
		default:
			return (RGBA);
			break;
	}
}

static inline bool caerFrameEventPNGDecompress(uint8_t *inBuffer, size_t inSize, uint16_t *outBuffer, int32_t xSize,
	int32_t ySize, enum caer_frame_event_color_channels channels) {
	png_structp png_ptr = NULL;
	png_infop info_ptr  = NULL;

	// Initialize the write struct.
	png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	if (png_ptr == NULL) {
		return (false);
	}

	// Initialize the info struct.
	info_ptr = png_create_info_struct(png_ptr);
	if (info_ptr == NULL) {
		png_destroy_read_struct(&png_ptr, NULL, NULL);
		return (false);
	}

	// Set up error handling.
	if (setjmp(png_jmpbuf(png_ptr))) {
		png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
		return (false);
	}

	// Handle endianness of 16-bit depth pixels correctly.
	// PNG assumes big-endian, our Frame Event is always little-endian.
	png_set_swap(png_ptr);

	// Set read function to buffer one.
	struct caer_libpng_buffer state = {.buffer = inBuffer, .size = inSize, .pos = 0};
	png_set_read_fn(png_ptr, &state, &caerLibPNGReadBuffer);

	// Read the whole PNG image.
	png_read_png(png_ptr, info_ptr, PNG_TRANSFORM_IDENTITY, NULL);

	// Extract header info.
	png_uint_32 width = 0, height = 0;
	int bitDepth = 0;
	int color    = -1;
	png_get_IHDR(png_ptr, info_ptr, &width, &height, &bitDepth, &color, NULL, NULL, NULL);

	// Check header info against known values from our frame event header.
	if ((I32T(width) != xSize) || (I32T(height) != ySize) || (bitDepth != 16)
		|| (caerFrameEventColorFromLibPNG(color) != channels)) {
		png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
		return (false);
	}

	// Extract image data, row by row.
	png_size_t row_bytes    = png_get_rowbytes(png_ptr, info_ptr);
	png_bytepp row_pointers = png_get_rows(png_ptr, info_ptr);

	for (size_t y = 0; y < (size_t) ySize; y++) {
		memcpy(&outBuffer[y * row_bytes], row_pointers[y], row_bytes);
	}

	// Destroy main structs.
	png_destroy_read_struct(&png_ptr, &info_ptr, NULL);

	return (true);
}

static bool decompressFramePNG(inputCommonState state, caerEventPacketHeader packet, size_t packetSize) {
	// We want to avoid allocating new memory for each PNG decompression, and moving around things
	// to much. So we first go through the compressed header+data blocks, and move them to their
	// correct position for an in-memory frame packet (so at N*eventSize). Then we decompress the
	// PNG block and directly copy the results into the space that it was occupying (plus extra for
	// the uncompressed pixels).
	// First we go once through the events to know where they are, and where they should go.
	// Then we do memory move + PNG decompression, starting from the last event (back-side), so as
	// to not overwrite memory we still need and haven't moved yet.
	int32_t eventSize   = caerEventPacketHeaderGetEventSize(packet);
	int32_t eventNumber = caerEventPacketHeaderGetEventNumber(packet);

	struct {
		size_t offsetDestination;
		size_t offset;
		size_t size;
		bool isCompressed;
	} eventMemory[eventNumber];

	size_t currPacketOffset = CAER_EVENT_PACKET_HEADER_SIZE; // Start here, no change to header.
	// '- sizeof(uint16_t)' to compensate for pixels[1] at end of struct for C++ compatibility.
	size_t frameEventHeaderSize = (sizeof(struct caer_frame_event) - sizeof(uint16_t));

	// Gather information on events.
	for (int32_t i = 0; i < eventNumber; i++) {
		// In-memory packet's events have to go where N*eventSize is.
		eventMemory[i].offsetDestination = CAER_EVENT_PACKET_HEADER_SIZE + (size_t)(i * eventSize);
		eventMemory[i].offset            = currPacketOffset;

		caerFrameEvent frameEvent = (caerFrameEvent)(((uint8_t *) packet) + currPacketOffset);

		// Bit 31 of info signals if event is PNG-compressed or not.
		eventMemory[i].isCompressed = GET_NUMBITS32(frameEvent->info, 31, 0x01);

		if (eventMemory[i].isCompressed) {
			// Clear compression enabled bit.
			CLEAR_NUMBITS32(frameEvent->info, 31, 0x01);

			// Compressed block size is held in an integer right after the header.
			int32_t pngSize = le32toh(*((int32_t *) (((uint8_t *) frameEvent) + frameEventHeaderSize)));

			// PNG size is header plus integer plus compressed block size.
			eventMemory[i].size = frameEventHeaderSize + sizeof(int32_t) + (size_t) pngSize;
		}
		else {
			// Normal size is header plus uncompressed pixels.
			eventMemory[i].size = frameEventHeaderSize + caerFrameEventGetPixelsSize(frameEvent);
		}

		// Update counter.
		currPacketOffset += eventMemory[i].size;
	}

	// Check that we indeed parsed everything correctly.
	if (currPacketOffset != packetSize) {
		caerModuleLog(state->parentModule, CAER_LOG_ERROR,
			"Failed to decompress frame event. "
			"Size after event parsing and packet size don't match.");
		return (false);
	}

	// Now move memory and decompress in reverse order.
	for (int32_t i = eventNumber; i >= 0; i--) {
		// Move memory from compressed position to uncompressed, in-memory position.
		memmove(((uint8_t *) packet) + eventMemory[i].offsetDestination, ((uint8_t *) packet) + eventMemory[i].offset,
			eventMemory[i].size);

		// If event is PNG-compressed, decompress it now.
		if (eventMemory[i].isCompressed) {
			uint8_t *pngBuffer
				= ((uint8_t *) packet) + eventMemory[i].offsetDestination + frameEventHeaderSize + sizeof(int32_t);
			size_t pngBufferSize = eventMemory[i].size - frameEventHeaderSize - sizeof(int32_t);

			caerFrameEvent frameEvent = (caerFrameEvent)(((uint8_t *) packet) + eventMemory[i].offsetDestination);

			if (!caerFrameEventPNGDecompress(pngBuffer, pngBufferSize, caerFrameEventGetPixelArrayUnsafe(frameEvent),
					caerFrameEventGetLengthX(frameEvent), caerFrameEventGetLengthY(frameEvent),
					caerFrameEventGetChannelNumber(frameEvent))) {
				// Failed to decompress PNG.
				caerModuleLog(state->parentModule, CAER_LOG_ERROR,
					"Failed to decompress frame event. "
					"PNG decompression failure.");
				return (false);
			}
		}

		// Uncompressed size will always be header + uncompressed pixels.
		caerFrameEvent frameEvent = (caerFrameEvent)(((uint8_t *) packet) + eventMemory[i].offsetDestination);
		size_t uncompressedSize   = frameEventHeaderSize + caerFrameEventGetPixelsSize(frameEvent);

		// Initialize the rest of the memory of the event to zeros, to comply with spec
		// that says non-pixels at the end, if they exist, are always zero.
		memset(((uint8_t *) packet) + eventMemory[i].offsetDestination + uncompressedSize, 0,
			(size_t) eventSize - uncompressedSize);
	}

	return (true);
}

#endif

static bool decompressTimestampSerialize(inputCommonState state, caerEventPacketHeader packet, size_t packetSize) {
	// To decompress this, we have to allocate memory to hold the expanded events. There is
	// no efficient way to avoid this; working backwards from the last compressed event might
	// be an option, but you'd have to track where all the events are during a first forward
	// pass, and keeping track of offset, ts, numEvents for each group would incur similar
	// memory consumption, while considerably increasing complexity. So let's just do the
	// simple thing.
	int32_t eventSize     = caerEventPacketHeaderGetEventSize(packet);
	int32_t eventNumber   = caerEventPacketHeaderGetEventNumber(packet);
	int32_t eventTSOffset = caerEventPacketHeaderGetEventTSOffset(packet);

	uint8_t *events = malloc((size_t)(eventNumber * eventSize));
	if (events == NULL) {
		// Memory allocation failure.
		caerModuleLog(state->parentModule, CAER_LOG_ERROR,
			"Failed to decode serialized timestamp. "
			"Memory allocation failure.");
		return (false);
	}

	size_t currPacketOffset        = CAER_EVENT_PACKET_HEADER_SIZE; // Start here, no change to header.
	size_t recoveredEventsPosition = 0;
	size_t recoveredEventsNumber   = 0;

	while (currPacketOffset < packetSize) {
		void *firstEvent = ((uint8_t *) packet) + currPacketOffset;
		int32_t currTS   = caerGenericEventGetTimestamp(firstEvent, packet);

		if (currTS & I32T(0x80000000)) {
			// Compressed run starts here! Must clear the compression bit from
			// this first timestamp and restore the timestamp to the others.
			// So first we clean the timestamp.
			currTS &= I32T(0x7FFFFFFF);

			// Then we fix the first event timestamp and copy it over.
			caerGenericEventSetTimestamp(firstEvent, packet, currTS);
			memcpy(events + recoveredEventsPosition, firstEvent, (size_t) eventSize);

			currPacketOffset += (size_t) eventSize;
			recoveredEventsPosition += (size_t) eventSize;
			recoveredEventsNumber++;

			// Then we get the second event, and get its timestamp, which is
			// actually the size of the following compressed run.
			void *secondEvent = ((uint8_t *) packet) + currPacketOffset;
			int32_t tsRun     = caerGenericEventGetTimestamp(secondEvent, packet);

			// And fix its own timestamp back to what it should be, and copy it.
			caerGenericEventSetTimestamp(secondEvent, packet, currTS);
			memcpy(events + recoveredEventsPosition, secondEvent, (size_t) eventSize);

			currPacketOffset += (size_t) eventSize;
			recoveredEventsPosition += (size_t) eventSize;
			recoveredEventsNumber++;

			// Now go through the compressed, data-only events, and restore their
			// timestamp. We do this by copying the data and then adding the timestamp,
			// which is always the last in an event.
			while (tsRun > 0) {
				void *thirdEvent = ((uint8_t *) packet) + currPacketOffset;
				memcpy(events + recoveredEventsPosition, thirdEvent, (size_t) eventTSOffset);

				currPacketOffset += (size_t) eventTSOffset;
				recoveredEventsPosition += (size_t) eventTSOffset;

				int32_t *newTS = (int32_t *) (events + recoveredEventsPosition);
				*newTS         = currTS;

				recoveredEventsPosition += sizeof(int32_t);

				recoveredEventsNumber++;
				tsRun--;
			}
		}
		else {
			// Normal event, nothing compressed.
			// Just copy and advance.
			memcpy(events + recoveredEventsPosition, firstEvent, (size_t) eventSize);

			currPacketOffset += (size_t) eventSize;
			recoveredEventsPosition += (size_t) eventSize;
			recoveredEventsNumber++;
		}
	}

	// Check we really recovered all events from compression.
	if (currPacketOffset != packetSize) {
		caerModuleLog(state->parentModule, CAER_LOG_ERROR,
			"Failed to decode serialized timestamp. "
			"Length of compressed packet and read data don't match.");
		return (false);
	}

	if ((size_t)(eventNumber * eventSize) != recoveredEventsPosition) {
		caerModuleLog(state->parentModule, CAER_LOG_ERROR,
			"Failed to decode serialized timestamp. "
			"Length of uncompressed packet and uncompressed data don't match.");
		return (false);
	}

	if ((size_t) eventNumber != recoveredEventsNumber) {
		caerModuleLog(state->parentModule, CAER_LOG_ERROR,
			"Failed to decode serialized timestamp. "
			"Number of expected and recovered events don't match.");
		return (false);
	}

	// Copy recovered event packet into original.
	memcpy(((uint8_t *) packet) + CAER_EVENT_PACKET_HEADER_SIZE, events, recoveredEventsPosition);

	free(events);

	return (true);
}

static bool decompressEventPacket(inputCommonState state, caerEventPacketHeader packet, size_t packetSize) {
	bool retVal = false;

	// Data compression technique 1: serialized timestamps.
	if ((state->header.formatID & 0x01) && caerEventPacketHeaderGetEventType(packet) == POLARITY_EVENT) {
		retVal = decompressTimestampSerialize(state, packet, packetSize);
	}

#ifdef ENABLE_INOUT_PNG_COMPRESSION
	// Data compression technique 2: frame PNG compression.
	if ((state->header.formatID & 0x02) && caerEventPacketHeaderGetEventType(packet) == FRAME_EVENT) {
		retVal = decompressFramePNG(state, packet, packetSize);
	}
#endif

	return (retVal);
}

static int inputReaderThread(void *stateArg) {
	inputCommonState state = stateArg;

	// Set thread name.
	size_t threadNameLength = strlen(state->parentModule->moduleSubSystemString);
	char threadName[threadNameLength + 1 + 8]; // +1 for NUL character.
	strcpy(threadName, state->parentModule->moduleSubSystemString);
	strcat(threadName, "[Reader]");
	portable_thread_set_name(threadName);

	// Set thread priority to high. This may fail depending on your OS configuration.
	if (!portable_thread_set_priority_highest()) {
		caerModuleLog(state->parentModule, CAER_LOG_INFO,
			"Failed to raise thread priority for Input Reader thread. You may experience lags and delays.");
	}

	while (atomic_load_explicit(&state->running, memory_order_relaxed)) {
		// Handle configuration changes affecting buffer management.
		if (atomic_load_explicit(&state->bufferUpdate, memory_order_relaxed)) {
			atomic_store(&state->bufferUpdate, false);

			if (!newInputBuffer(state)) {
				caerModuleLog(state->parentModule, CAER_LOG_ERROR,
					"Failed to allocate new input data buffer. Continue using old one.");
			}
		}

		// Read data from disk or socket.
		ssize_t result = readUntilDone(state->fileDescriptor, state->dataBuffer->buffer, state->dataBuffer->bufferSize);
		if (result <= 0) {
			// Error or EOF with no data. Let's just stop at this point.
			close(state->fileDescriptor);
			state->fileDescriptor = -1;

			// Distinguish EOF from errors based upon errno value.
			if (result == 0) {
				caerModuleLog(state->parentModule, CAER_LOG_INFO, "Reached End of File.");
				atomic_store(&state->inputReaderThreadState, EOF_REACHED); // EOF
			}
			else {
				caerModuleLog(state->parentModule, CAER_LOG_ERROR, "Error while reading data, error: %d.", errno);
				atomic_store(&state->inputReaderThreadState, ERROR_READ); // Error
			}
			break;
		}
		state->dataBuffer->bufferUsedSize = (size_t) result;

		// Parse header and setup header info structure.
		if (!atomic_load_explicit(&state->header.isValidHeader, memory_order_relaxed) && !parseHeader(state)) {
			// Header invalid, exit.
			caerModuleLog(state->parentModule, CAER_LOG_ERROR,
				"Failed to parse header. Only AEDAT 2.X and 3.x compliant files are supported.");
			atomic_store(&state->inputReaderThreadState, ERROR_HEADER); // Error in Header
			break;
		}

		// Parse event data now.
		if (!parseData(state)) {
			// Packets invalid, exit.
			caerModuleLog(state->parentModule, CAER_LOG_ERROR, "Failed to parse event data.");
			atomic_store(&state->inputReaderThreadState, ERROR_DATA); // Error in Data
			break;
		}

		// Go and get a full buffer on next iteration again, starting at position 0.
		state->dataBuffer->bufferPosition = 0;

		// Update offset. Makes sense for files only.
		if (!state->isNetworkStream) {
			state->dataBufferOffset += state->dataBuffer->bufferUsedSize;
		}
	}

	return (thrd_success);
}

static inline void updateSizeCommitCriteria(inputCommonState state, caerEventPacketHeader newPacket) {
	if ((state->packetContainer.newContainerSizeLimit > 0)
		&& (caerEventPacketHeaderGetEventNumber(newPacket) >= state->packetContainer.newContainerSizeLimit)) {
		const void *sizeLimitEvent
			= caerGenericEventGetEvent(newPacket, state->packetContainer.newContainerSizeLimit - 1);
		int64_t sizeLimitTimestamp = caerGenericEventGetTimestamp64(sizeLimitEvent, newPacket);

		// Reject the size limit if its corresponding timestamp isn't smaller than the time limit.
		// If not (>=), then the time limit will hit first anyway and take precedence.
		if (sizeLimitTimestamp < state->packetContainer.newContainerTimestampEnd) {
			state->packetContainer.sizeLimitHit = true;

			if (sizeLimitTimestamp < state->packetContainer.sizeLimitTimestamp) {
				state->packetContainer.sizeLimitTimestamp = sizeLimitTimestamp;
			}
		}
	}
}

/**
 * Add the given packet to a packet container that acts as accumulator. This way all
 * events are in a common place, from which the right event amounts/times can be sliced.
 * Packets are unique by type and event size, since for a packet of the same type, the
 * only global things that can change are the source ID and the event size (like for
 * Frames). The source ID is guaranteed to be the same from one source only when using
 * the input module, so we only have to check for the event size in addition to the type.
 *
 * @param state common input data structure.
 * @param newPacket packet to add/merge with accumulator packet container.
 * @param newPacketData information on the new packet.
 *
 * @return true on successful packet merge, false on failure (memory allocation).
 */
static bool addToPacketContainer(inputCommonState state, caerEventPacketHeader newPacket, packetData newPacketData) {
	bool packetAlreadyExists      = false;
	caerEventPacketHeader *packet = NULL;
	while ((packet = (caerEventPacketHeader *) utarray_next(state->packetContainer.eventPackets, packet)) != NULL) {
		int16_t packetEventType = caerEventPacketHeaderGetEventType(*packet);
		int32_t packetEventSize = caerEventPacketHeaderGetEventSize(*packet);

		if (packetEventType == newPacketData->eventType && packetEventSize == newPacketData->eventSize) {
			// Packet with this type and event size already present.
			packetAlreadyExists = true;
			break;
		}
	}

	// Packet with same type and event size as newPacket found, do merge operation.
	if (packetAlreadyExists) {
		// Merge newPacket with '*packet'. Since packets from the same source,
		// and having the same time, are guaranteed to have monotonic timestamps,
		// the merge operation becomes a simple append operation.
		caerEventPacketHeader mergedPacket = caerEventPacketAppend(*packet, newPacket);
		if (mergedPacket == NULL) {
			caerModuleLog(state->parentModule, CAER_LOG_ERROR,
				"%s: Failed to allocate memory for packet merge operation.", __func__);
			return (false);
		}

		// Merged content with existing packet, data copied: free new one.
		// Update references to old/new packets to point to merged one.
		free(newPacket);
		*packet   = mergedPacket;
		newPacket = mergedPacket;
	}
	else {
		// No previous packet of this type and event size found, use this one directly.
		utarray_push_back(state->packetContainer.eventPackets, &newPacket);

		utarray_sort(state->packetContainer.eventPackets, &packetsFirstTypeThenSizeCmp);
	}

	// Update size commit criteria, if size limit is enabled and not already hit by a previous packet.
	updateSizeCommitCriteria(state, newPacket);

	return (true);
}

static caerEventPacketContainer generatePacketContainer(inputCommonState state, bool forceFlush) {
	// Let's generate a packet container, use the size of the event packets array as upper bound.
	int32_t packetContainerPosition = 0;
	caerEventPacketContainer packetContainer
		= caerEventPacketContainerAllocate((int32_t) utarray_len(state->packetContainer.eventPackets));
	if (packetContainer == NULL) {
		return (NULL);
	}

	// When we force a flush commit, we put everything currently there in the packet
	// container and return it, with no slicing being done at all.
	if (forceFlush) {
		caerEventPacketHeader *currPacket = NULL;
		while ((currPacket = (caerEventPacketHeader *) utarray_next(state->packetContainer.eventPackets, currPacket))
			   != NULL) {
			caerEventPacketContainerSetEventPacket(packetContainer, packetContainerPosition++, *currPacket);
		}

		// Clean packets array, they are all being sent out now.
		utarray_clear(state->packetContainer.eventPackets);
	}
	else {
		// Iterate over each event packet, and slice out the relevant part in time.
		caerEventPacketHeader *currPacket = NULL;
		while ((currPacket = (caerEventPacketHeader *) utarray_next(state->packetContainer.eventPackets, currPacket))
			   != NULL) {
			// Search for cutoff point, either reaching the size limit first, or then the time limit.
			// Also count valid events encountered for later setting the right values in the packet.
			int32_t cutoffIndex     = -1;
			int32_t validEventsSeen = 0;

			CAER_ITERATOR_ALL_START(*currPacket, const void *)
			int64_t caerIteratorElementTimestamp = caerGenericEventGetTimestamp64(caerIteratorElement, *currPacket);

			if ((state->packetContainer.sizeLimitHit
					&& ((caerIteratorCounter >= state->packetContainer.newContainerSizeLimit)
						   || (caerIteratorElementTimestamp > state->packetContainer.sizeLimitTimestamp)))
				|| (caerIteratorElementTimestamp > state->packetContainer.newContainerTimestampEnd)) {
				cutoffIndex = caerIteratorCounter;
				break;
			}

			if (caerGenericEventIsValid(caerIteratorElement)) {
				validEventsSeen++;
			}
		}

		// If there is no cutoff point, we can just send on the whole packet with no changes.
		if (cutoffIndex == -1) {
			caerEventPacketContainerSetEventPacket(packetContainer, packetContainerPosition++, *currPacket);

			// Erase slot from packets array.
			utarray_erase(state->packetContainer.eventPackets,
				(size_t) utarray_eltidx(state->packetContainer.eventPackets, currPacket), 1);
			currPacket = (caerEventPacketHeader *) utarray_prev(state->packetContainer.eventPackets, currPacket);
			continue;
		}

		// If there is one on the other hand, we can only send up to that event.
		// Special case is if the cutoff point is zero, meaning there's nothing to send.
		if (cutoffIndex == 0) {
			continue;
		}

		int32_t currPacketEventSize   = caerEventPacketHeaderGetEventSize(*currPacket);
		int32_t currPacketEventValid  = caerEventPacketHeaderGetEventValid(*currPacket);
		int32_t currPacketEventNumber = caerEventPacketHeaderGetEventNumber(*currPacket);

		// Allocate a new packet, with space for the remaining events that we don't send off
		// (the ones after cutoff point).
		int32_t nextPacketEventNumber = currPacketEventNumber - cutoffIndex;
		caerEventPacketHeader nextPacket
			= malloc(CAER_EVENT_PACKET_HEADER_SIZE + (size_t)(currPacketEventSize * nextPacketEventNumber));
		if (nextPacket == NULL) {
			caerModuleLog(state->parentModule, CAER_LOG_CRITICAL,
				"Failed memory allocation for nextPacket. Discarding remaining data.");
		}
		else {
			// Copy header and remaining events to new packet, set header sizes correctly.
			memcpy(nextPacket, *currPacket, CAER_EVENT_PACKET_HEADER_SIZE);
			memcpy(((uint8_t *) nextPacket) + CAER_EVENT_PACKET_HEADER_SIZE,
				((uint8_t *) *currPacket) + CAER_EVENT_PACKET_HEADER_SIZE + (currPacketEventSize * cutoffIndex),
				(size_t)(currPacketEventSize * nextPacketEventNumber));

			caerEventPacketHeaderSetEventValid(nextPacket, currPacketEventValid - validEventsSeen);
			caerEventPacketHeaderSetEventNumber(nextPacket, nextPacketEventNumber);
			caerEventPacketHeaderSetEventCapacity(nextPacket, nextPacketEventNumber);
		}

		// Resize current packet to include only the events up until cutoff point.
		caerEventPacketHeader currPacketResized
			= realloc(*currPacket, CAER_EVENT_PACKET_HEADER_SIZE + (size_t)(currPacketEventSize * cutoffIndex));
		if (currPacketResized == NULL) {
			// This is unlikely to happen as we always shrink here!
			caerModuleLog(state->parentModule, CAER_LOG_CRITICAL,
				"Failed memory allocation for currPacketResized. Discarding current data.");
			free(*currPacket);
		}
		else {
			// Set header sizes for resized packet correctly.
			caerEventPacketHeaderSetEventValid(currPacketResized, validEventsSeen);
			caerEventPacketHeaderSetEventNumber(currPacketResized, cutoffIndex);
			caerEventPacketHeaderSetEventCapacity(currPacketResized, cutoffIndex);

			// Update references: the currPacket, after resizing, goes into the packet container for output.
			caerEventPacketContainerSetEventPacket(packetContainer, packetContainerPosition++, currPacketResized);
		}

		// Update references: the nextPacket goes into the eventPackets array at the currPacket position,
		// if it exists. Else we just delete that position.
		if (nextPacket != NULL) {
			*currPacket = nextPacket;
		}
		else {
			// Erase slot from packets array.
			utarray_erase(state->packetContainer.eventPackets,
				(size_t) utarray_eltidx(state->packetContainer.eventPackets, currPacket), 1);
			currPacket = (caerEventPacketHeader *) utarray_prev(state->packetContainer.eventPackets, currPacket);
		}
	}
}

return (packetContainer);
}

static void commitPacketContainer(inputCommonState state, bool forceFlush) {
	// Check if we hit any of the size limits (no more than X events per packet type).
	// Check if we have read and accumulated all the event packets with a main first timestamp smaller
	// or equal than what we want. We know this is the case when the last seen main timestamp is clearly
	// bigger than the wanted one. If this is true, it means we do have all the possible events of all
	// types that happen up until that point, and we can split that time range off into a packet container.
	// If not, we just go get the next event packet.
	bool sizeCommit = false, timeCommit = false;

redo:
	sizeCommit = state->packetContainer.sizeLimitHit
				 && (state->packetContainer.lastPacketTimestamp > state->packetContainer.sizeLimitTimestamp);
	timeCommit = (state->packetContainer.lastPacketTimestamp > state->packetContainer.newContainerTimestampEnd);

	if (!forceFlush && !sizeCommit && !timeCommit) {
		return;
	}

	caerEventPacketContainer packetContainer = generatePacketContainer(state, forceFlush);
	if (packetContainer == NULL) {
		// Memory allocation or other error.
		return;
	}

	// Update wanted timestamp for next time slice.
	// Only do this if size limit was not active, since size limit can only be active
	// if the slice would (in time) be smaller than the time limit end, so the next run
	// must again check and comb through the same time window.
	// Also don't update if forceFlush is true, for the same reason of the next call
	// having to again comb through the same time window for any of the size or time
	// limits to hit again (on TS Overflow, on TS Reset everything just resets anyway).
	if (!sizeCommit && !forceFlush) {
		state->packetContainer.newContainerTimestampEnd
			+= I32T(atomic_load_explicit(&state->packetContainer.timeSlice, memory_order_relaxed));

		// Only do time delay operation if time is actually changing. On size hits or
		// full flushes, this would slow down everything incorrectly as it would be an
		// extra delay operation inside the same time window.
		doTimeDelay(state);
	}

	doPacketContainerCommit(state, packetContainer, atomic_load_explicit(&state->keepPackets, memory_order_relaxed));

	// Update size slice for next packet container.
	state->packetContainer.newContainerSizeLimit
		= I32T(atomic_load_explicit(&state->packetContainer.sizeSlice, memory_order_relaxed));

	state->packetContainer.sizeLimitHit       = false;
	state->packetContainer.sizeLimitTimestamp = INT32_MAX;

	if (!forceFlush) {
		// Check if any of the remaining packets still would trigger an early size limit.
		caerEventPacketHeader *currPacket = NULL;
		while ((currPacket = (caerEventPacketHeader *) utarray_next(state->packetContainer.eventPackets, currPacket))
			   != NULL) {
			updateSizeCommitCriteria(state, *currPacket);
		}

		// Run the above again, to make sure we do exhaust all possible size and time commits
		// possible with the data we have now, before going and getting new data.
		goto redo;
	}
}

static void doTimeDelay(inputCommonState state) {
	// Got packet container, delay it until user-defined time.
	uint64_t timeDelay = U64T(atomic_load_explicit(&state->packetContainer.timeDelay, memory_order_relaxed));

	if (timeDelay != 0) {
		// Get current time (nanosecond resolution).
		struct timespec currentTime;
		portable_clock_gettime_monotonic(&currentTime);

		// Calculate elapsed time since last commit, based on that then either
		// wait to meet timing, or log that it's impossible with current settings.
		uint64_t diffNanoTime
			= (uint64_t)(((int64_t)(currentTime.tv_sec - state->packetContainer.lastCommitTime.tv_sec) * 1000000000LL)
						 + (int64_t)(currentTime.tv_nsec - state->packetContainer.lastCommitTime.tv_nsec));
		uint64_t diffMicroTime = diffNanoTime / 1000;

		if (diffMicroTime >= timeDelay) {
			caerModuleLog(state->parentModule, CAER_LOG_WARNING,
				"Impossible to meet timeDelay timing specification with current settings.");

			// Don't delay any more by requesting time again, use old one.
			state->packetContainer.lastCommitTime = currentTime;
		}
		else {
			// Sleep for the remaining time.
			uint64_t sleepMicroTime    = timeDelay - diffMicroTime;
			uint64_t sleepMicroTimeSec = sleepMicroTime / 1000000; // Seconds part.
			uint64_t sleepMicroTimeNsec
				= (sleepMicroTime * 1000) - (sleepMicroTimeSec * 1000000000); // Nanoseconds part.

			struct timespec delaySleep = {.tv_sec = I64T(sleepMicroTimeSec), .tv_nsec = I64T(sleepMicroTimeNsec)};

			thrd_sleep(&delaySleep, NULL);

			// Update stored time.
			portable_clock_gettime_monotonic(&state->packetContainer.lastCommitTime);
		}
	}
}

static void doPacketContainerCommit(inputCommonState state, caerEventPacketContainer packetContainer, bool force) {
	// Could be that the packet container is empty of events. Don't commit empty containers.
	if (caerEventPacketContainerGetEventsNumber(packetContainer) == 0) {
		return;
	}

retry:
	if (!caerRingBufferPut(state->transferRingPacketContainers, packetContainer)) {
		if (force && atomic_load_explicit(&state->running, memory_order_relaxed)) {
			// Retry forever if requested, at least while the module is running.
			goto retry;
		}

		caerEventPacketContainerFree(packetContainer);

		caerModuleLog(
			state->parentModule, CAER_LOG_NOTICE, "Failed to put new packet container on transfer ring-buffer: full.");
	}
	else {
		// Signal availability of new data to the mainloop on packet container commit.
		atomic_fetch_add_explicit(&state->dataAvailableModule, 1, memory_order_release);
		caerMainloopDataNotifyIncrease(NULL);

		caerModuleLog(state->parentModule, CAER_LOG_DEBUG, "Submitted packet container successfully.");
	}
}

static bool handleTSReset(inputCommonState state) {
	// Commit all current content.
	commitPacketContainer(state, true);

	// Send lone packet container with just TS_RESET.
	// Allocate packet container just for this event.
	caerEventPacketContainer tsResetContainer = caerEventPacketContainerAllocate(1);
	if (tsResetContainer == NULL) {
		caerModuleLog(state->parentModule, CAER_LOG_CRITICAL, "Failed to allocate tsReset event packet container.");
		return (false);
	}

	// Allocate special packet just for this event.
	caerSpecialEventPacket tsResetPacket = caerSpecialEventPacketAllocate(
		1, I16T(state->parentModule->moduleID), state->packetContainer.lastTimestampOverflow);
	if (tsResetPacket == NULL) {
		caerModuleLog(state->parentModule, CAER_LOG_CRITICAL, "Failed to allocate tsReset special event packet.");
		return (false);
	}

	// Create timestamp reset event.
	caerSpecialEvent tsResetEvent = caerSpecialEventPacketGetEvent(tsResetPacket, 0);
	caerSpecialEventSetTimestamp(tsResetEvent, INT32_MAX);
	caerSpecialEventSetType(tsResetEvent, TIMESTAMP_RESET);
	caerSpecialEventValidate(tsResetEvent, tsResetPacket);

	// Assign special packet to packet container.
	caerEventPacketContainerSetEventPacket(tsResetContainer, SPECIAL_EVENT, (caerEventPacketHeader) tsResetPacket);

	// Guaranteed commit of timestamp reset container.
	doPacketContainerCommit(state, tsResetContainer, true);

	// Prepare for the new event timeline coming with the next packet.
	// Reset all time related counters to initial state.
	state->packetContainer.lastPacketTimestamp      = 0;
	state->packetContainer.lastTimestampOverflow    = 0;
	state->packetContainer.newContainerTimestampEnd = -1;

	return (true);
}

static void getPacketInfo(caerEventPacketHeader packet, packetData packetInfoData) {
	// Get data from new packet.
	packetInfoData->eventType   = caerEventPacketHeaderGetEventType(packet);
	packetInfoData->eventSize   = caerEventPacketHeaderGetEventSize(packet);
	packetInfoData->eventValid  = caerEventPacketHeaderGetEventValid(packet);
	packetInfoData->eventNumber = caerEventPacketHeaderGetEventNumber(packet);

	const void *firstEvent         = caerGenericEventGetEvent(packet, 0);
	packetInfoData->startTimestamp = caerGenericEventGetTimestamp64(firstEvent, packet);

	const void *lastEvent        = caerGenericEventGetEvent(packet, packetInfoData->eventNumber - 1);
	packetInfoData->endTimestamp = caerGenericEventGetTimestamp64(lastEvent, packet);
}

static int inputAssemblerThread(void *stateArg) {
	inputCommonState state = stateArg;

	// Set thread name.
	size_t threadNameLength = strlen(state->parentModule->moduleSubSystemString);
	char threadName[threadNameLength + 1 + 11]; // +1 for NUL character.
	strcpy(threadName, state->parentModule->moduleSubSystemString);
	strcat(threadName, "[Assembler]");
	portable_thread_set_name(threadName);

	// Set thread priority to high. This may fail depending on your OS configuration.
	if (!portable_thread_set_priority_highest()) {
		caerModuleLog(state->parentModule, CAER_LOG_INFO,
			"Failed to raise thread priority for Input Assembler thread. You may experience lags and delays.");
	}

	// Delay by 1 µs if no data, to avoid a wasteful busy loop.
	struct timespec noDataSleep = {.tv_sec = 0, .tv_nsec = 1000};

	while (atomic_load_explicit(&state->running, memory_order_relaxed)) {
		// Support pause: don't get and send out new data while in pause mode.
		if (atomic_load_explicit(&state->pause, memory_order_relaxed)) {
			// Wait for 1 ms in pause mode, to avoid a wasteful busy loop.
			struct timespec pauseSleep = {.tv_sec = 0, .tv_nsec = 1000000};
			thrd_sleep(&pauseSleep, NULL);

			continue;
		}

		// Get parsed packets from Reader thread.
		caerEventPacketHeader currPacket = caerRingBufferGet(state->transferRingPackets);
		if (currPacket == NULL) {
			// Let's see why there are no more packets to read, maybe the reader failed.
			// Also EOF could have been reached, in which case the reader would have committed its last
			// packet before setting the flag, and so we must have already seen that one too, due to
			// visibility between threads of the data put on a ring-buffer.
			if (atomic_load_explicit(&state->inputReaderThreadState, memory_order_relaxed) != READER_OK) {
				break;
			}

			// Delay by 1 µs if no data, to avoid a wasteful busy loop.
			thrd_sleep(&noDataSleep, NULL);

			continue;
		}

		// If validOnly flag is enabled, clean the packets up here, removing all
		// invalid events prior to the get info and merge steps.
		if (atomic_load_explicit(&state->validOnly, memory_order_relaxed)) {
			caerEventPacketClean(currPacket);
		}

		// Get info on the new packet.
		struct input_packet_data currPacketData;
		getPacketInfo(currPacket, &currPacketData);

		// Check timestamp constraints as per AEDAT 3.X format: order-relevant timestamps
		// of each packet (the first timestamp) must be smaller or equal than next packet's.
		if (currPacketData.startTimestamp < state->packetContainer.lastPacketTimestamp) {
			// Discard non-compliant packets.
			free(currPacket);

			caerModuleLog(state->parentModule, CAER_LOG_NOTICE,
				"Dropping packet due to incorrect timestamp order. "
				"Order-relevant timestamp is %" PRIi64 ", but expected was at least %" PRIi64 ".",
				currPacketData.startTimestamp, state->packetContainer.lastPacketTimestamp);
			continue;
		}

		// Remember the main timestamp of the first event of the new packet. That's the
		// order-relevant timestamp for files/streams.
		state->packetContainer.lastPacketTimestamp = currPacketData.startTimestamp;

		// Initialize time slice counters with first packet.
		if (state->packetContainer.newContainerTimestampEnd == -1) {
			// -1 because newPacketFirstTimestamp itself is part of the set!
			state->packetContainer.newContainerTimestampEnd
				= currPacketData.startTimestamp
				  + (atomic_load_explicit(&state->packetContainer.timeSlice, memory_order_relaxed) - 1);

			portable_clock_gettime_monotonic(&state->packetContainer.lastCommitTime);
		}

		// If it's a special packet, it might contain TIMESTAMP_RESET as event, which affects
		// how things are mixed and parsed. This needs to be detected first, before merging.
		bool tsReset = false;

		if ((currPacketData.eventType == SPECIAL_EVENT)
			&& (caerSpecialEventPacketFindValidEventByType((caerSpecialEventPacket) currPacket, TIMESTAMP_RESET)
				   != NULL)) {
			tsReset = true;
			caerModuleLog(state->parentModule, CAER_LOG_INFO, "Timestamp Reset detected in stream.");

			if (currPacketData.eventNumber != 1) {
				caerModuleLog(state->parentModule, CAER_LOG_WARNING,
					"Timpestamp Reset detected, but it is not alone in its Special Event packet. "
					"This may lead to issues and should never happen.");
			}
		}

		// Support the big timestamp wrap, which changes tsOverflow, and affects
		// how things are mixed and parsed. This needs to be detected first, before merging.
		// TS Overflow can either be equal (okay) or bigger (detected here), never smaller due
		// to monotonic timestamps, as checked above (lastPacketTimestamp check).
		bool tsOverflow = false;

		if (caerEventPacketHeaderGetEventTSOverflow(currPacket) > state->packetContainer.lastTimestampOverflow) {
			state->packetContainer.lastTimestampOverflow = caerEventPacketHeaderGetEventTSOverflow(currPacket);

			tsOverflow = true;
			caerModuleLog(state->parentModule, CAER_LOG_INFO, "Timestamp Overflow detected in stream.");
		}

		// Now we have all the information and must do some merge and commit operations.
		// There are four cases:
		// a) no reset and no overflow - just merge and follow usual commit scheme
		// b) reset and no overflow - commit current content, commit lone container with just reset
		// c) no reset and overflow - commit current content, re-initialize merger, follow usual scheme
		// d) reset and overflow - the reset would be the first (and last!) thing in the new overflow
		//                         epoch, so we just follow standard reset procedure (above)

		if (tsReset) {
			// Current packet not used.
			free(currPacket);

			// We don't merge the current packet, that should only contain the timestamp reset,
			// but instead generate one to ensure that's the case. Also, all counters are reset.
			if (!handleTSReset(state)) {
				// Critical error, exit.
				break;
			}

			continue;
		}

		if (tsOverflow) {
			// On TS Overflow, commit all current data, and then afterwards normally
			// merge the current packet witht the (now empty) packet container.
			commitPacketContainer(state, true);
		}

		// We've got a full event packet, store it (merge with current).
		if (!addToPacketContainer(state, currPacket, &currPacketData)) {
			// Discard on merge failure.
			free(currPacket);

			continue;
		}

		// Generate a proper packet container and commit it to the Mainloop
		// if the user-set time/size limits are reached.
		commitPacketContainer(state, false);
	}

	// At this point we either got terminated (running=false) or we stopped for some
	// reason: no more packets due to error or End-of-File.
	// If we got hard-terminated, we empty the ring-buffer in the Exit() state.
	// If we hit EOF/errors though, we want the consumers to be able to finish
	// consuming the already produced data, so we wait for the ring-buffer to be empty.
	if (atomic_load(&state->running)) {
		while (atomic_load(&state->running) && atomic_load(&state->dataAvailableModule) != 0) {
			// Delay by 1 ms if no change, to avoid a wasteful busy loop.
			struct timespec waitSleep = {.tv_sec = 0, .tv_nsec = 1000000};
			thrd_sleep(&waitSleep, NULL);
		}

		// Ensure parent also shuts down, for example on read failures or EOF.
		sshsNodePutBool(state->parentModule->moduleNode, "running", false);
	}

	return (thrd_success);
}

static const UT_icd ut_caerEventPacketHeader_icd = {sizeof(caerEventPacketHeader), NULL, NULL, NULL};

bool caerInputCommonInit(caerModuleData moduleData, int readFd, bool isNetworkStream, bool isNetworkMessageBased) {
	inputCommonState state = moduleData->moduleState;

	state->parentModule   = moduleData;
	state->sourceInfoNode = sshsGetRelativeNode(moduleData->moduleNode, "sourceInfo/");

	// Check for invalid file descriptors.
	if (readFd < -1) {
		caerModuleLog(state->parentModule, CAER_LOG_ERROR, "Invalid file descriptor.");
		return (false);
	}

	state->fileDescriptor = readFd;

	// Store network/file, message-based or not information.
	state->isNetworkStream       = isNetworkStream;
	state->isNetworkMessageBased = isNetworkMessageBased;

	// Add auto-restart setting.
	sshsNodeCreateBool(
		moduleData->moduleNode, "autoRestart", true, SSHS_FLAGS_NORMAL, "Automatically restart module after shutdown.");

	// Handle configuration.
	sshsNodeCreateBool(moduleData->moduleNode, "validOnly", false, SSHS_FLAGS_NORMAL, "Only read valid events.");
	sshsNodeCreateBool(moduleData->moduleNode, "keepPackets", false, SSHS_FLAGS_NORMAL,
		"Ensure all packets are kept (stall input if transfer-buffer full).");
	sshsNodeCreateBool(moduleData->moduleNode, "pause", false, SSHS_FLAGS_NORMAL, "Pause the event stream.");
	sshsNodeCreateInt(moduleData->moduleNode, "bufferSize", 65536, 512, 512 * 1024, SSHS_FLAGS_NORMAL,
		"Size of read data buffer in bytes.");
	sshsNodeCreateInt(moduleData->moduleNode, "ringBufferSize", 128, 8, 1024, SSHS_FLAGS_NORMAL,
		"Size of EventPacketContainer and EventPacket queues, used for transfers between input threads and mainloop.");

	sshsNodeCreateInt(moduleData->moduleNode, "PacketContainerMaxPacketSize", 0, 0, 10 * 1024 * 1024, SSHS_FLAGS_NORMAL,
		"Maximum packet size in events, when any packet reaches this size, the EventPacketContainer is sent for "
		"processing.");
	sshsNodeCreateInt(moduleData->moduleNode, "PacketContainerInterval", 10000, 1, 120 * 1000 * 1000, SSHS_FLAGS_NORMAL,
		"Time interval in µs, each sent EventPacketContainer will span this interval.");
	sshsNodeCreateInt(moduleData->moduleNode, "PacketContainerDelay", 10000, 1, 120 * 1000 * 1000, SSHS_FLAGS_NORMAL,
		"Time delay in µs between consecutive EventPacketContainers sent for processing.");

	atomic_store(&state->validOnly, sshsNodeGetBool(moduleData->moduleNode, "validOnly"));
	atomic_store(&state->keepPackets, sshsNodeGetBool(moduleData->moduleNode, "keepPackets"));
	atomic_store(&state->pause, sshsNodeGetBool(moduleData->moduleNode, "pause"));
	int ringSize = sshsNodeGetInt(moduleData->moduleNode, "ringBufferSize");

	atomic_store(
		&state->packetContainer.sizeSlice, sshsNodeGetInt(moduleData->moduleNode, "PacketContainerMaxPacketSize"));
	atomic_store(&state->packetContainer.timeSlice, sshsNodeGetInt(moduleData->moduleNode, "PacketContainerInterval"));
	atomic_store(&state->packetContainer.timeDelay, sshsNodeGetInt(moduleData->moduleNode, "PacketContainerDelay"));

	// Initialize transfer ring-buffers. ringBufferSize only changes here at init time!
	state->transferRingPackets = caerRingBufferInit((size_t) ringSize);
	if (state->transferRingPackets == NULL) {
		caerModuleLog(state->parentModule, CAER_LOG_ERROR, "Failed to allocate packets transfer ring-buffer.");
		return (false);
	}

	state->transferRingPacketContainers = caerRingBufferInit((size_t) ringSize);
	if (state->transferRingPacketContainers == NULL) {
		caerModuleLog(
			state->parentModule, CAER_LOG_ERROR, "Failed to allocate packet containers transfer ring-buffer.");
		return (false);
	}

	// Allocate data buffer. bufferSize is updated here.
	if (!newInputBuffer(state)) {
		caerRingBufferFree(state->transferRingPackets);
		caerRingBufferFree(state->transferRingPacketContainers);

		caerModuleLog(state->parentModule, CAER_LOG_ERROR, "Failed to allocate input data buffer.");
		return (false);
	}

	// Initialize array for packets -> packet container.
	utarray_new(state->packetContainer.eventPackets, &ut_caerEventPacketHeader_icd);

	state->packetContainer.newContainerTimestampEnd = -1;
	state->packetContainer.newContainerSizeLimit
		= I32T(atomic_load_explicit(&state->packetContainer.sizeSlice, memory_order_relaxed));
	state->packetContainer.sizeLimitTimestamp = INT32_MAX;

	// Start input handling threads.
	atomic_store(&state->running, true);

	if (thrd_create(&state->inputAssemblerThread, &inputAssemblerThread, state) != thrd_success) {
		caerRingBufferFree(state->transferRingPackets);
		caerRingBufferFree(state->transferRingPacketContainers);
		free(state->dataBuffer);

		caerModuleLog(state->parentModule, CAER_LOG_ERROR, "Failed to start input assembler thread.");
		return (false);
	}

	if (thrd_create(&state->inputReaderThread, &inputReaderThread, state) != thrd_success) {
		caerRingBufferFree(state->transferRingPackets);
		caerRingBufferFree(state->transferRingPacketContainers);
		free(state->dataBuffer);

		// Stop assembler thread (started just above) and wait on it.
		atomic_store(&state->running, false);

		if ((errno = thrd_join(state->inputAssemblerThread, NULL)) != thrd_success) {
			// This should never happen!
			caerModuleLog(
				state->parentModule, CAER_LOG_CRITICAL, "Failed to join input assembler thread. Error: %d.", errno);
		}

		caerModuleLog(state->parentModule, CAER_LOG_ERROR, "Failed to start input reader thread.");
		return (false);
	}

	// Wait for header to be parsed. TODO: this can block indefinitely, better solution needed!
	while (!atomic_load_explicit(&state->header.isValidHeader, memory_order_relaxed)) {
		if (atomic_load_explicit(&state->inputReaderThreadState, memory_order_relaxed) != READER_OK) {
			caerRingBufferFree(state->transferRingPackets);
			caerRingBufferFree(state->transferRingPacketContainers);
			free(state->dataBuffer);

			// Stop assembler thread (started just above) and wait on it.
			atomic_store(&state->running, false);

			if ((errno = thrd_join(state->inputAssemblerThread, NULL)) != thrd_success) {
				// This should never happen!
				caerModuleLog(
					state->parentModule, CAER_LOG_CRITICAL, "Failed to join input assembler thread. Error: %d.", errno);
			}

			if ((errno = thrd_join(state->inputReaderThread, NULL)) != thrd_success) {
				// This should never happen!
				caerModuleLog(
					state->parentModule, CAER_LOG_CRITICAL, "Failed to join input reader thread. Error: %d.", errno);
			}

			caerModuleLog(state->parentModule, CAER_LOG_ERROR, "Failed to start input reader thread.");
			return (false);
		}
	}

	// Add config listeners last, to avoid having them dangling if Init doesn't succeed.
	sshsNodeAddAttributeListener(moduleData->moduleNode, moduleData, &caerInputCommonConfigListener);

	return (true);
}

void caerInputCommonExit(caerModuleData moduleData) {
	// Remove listener, which can reference invalid memory in userData.
	sshsNodeRemoveAttributeListener(moduleData->moduleNode, moduleData, &caerInputCommonConfigListener);

	inputCommonState state = moduleData->moduleState;

	// Stop input threads and wait on them.
	atomic_store(&state->running, false);

	if ((errno = thrd_join(state->inputReaderThread, NULL)) != thrd_success) {
		// This should never happen!
		caerModuleLog(state->parentModule, CAER_LOG_CRITICAL, "Failed to join input reader thread. Error: %d.", errno);
	}

	if ((errno = thrd_join(state->inputAssemblerThread, NULL)) != thrd_success) {
		// This should never happen!
		caerModuleLog(
			state->parentModule, CAER_LOG_CRITICAL, "Failed to join input assembler thread. Error: %d.", errno);
	}

	// Now clean up the transfer ring-buffers and its contents.
	caerEventPacketContainer packetContainer;
	while ((packetContainer = caerRingBufferGet(state->transferRingPacketContainers)) != NULL) {
		caerEventPacketContainerFree(packetContainer);

		// If we're here, then nobody will (or even can) consume this data afterwards.
		caerMainloopDataNotifyDecrease(NULL);
		atomic_fetch_sub_explicit(&state->dataAvailableModule, 1, memory_order_relaxed);
	}

	caerRingBufferFree(state->transferRingPacketContainers);

	// Check we indeed removed all data and counters match this expectation.
	if (atomic_load(&state->dataAvailableModule) != 0) {
		// This should never happen!
		caerModuleLog(state->parentModule, CAER_LOG_CRITICAL,
			"After cleanup, data is still available for consumption. Counter value: %" PRIu32 ".",
			U32T(atomic_load(&state->dataAvailableModule)));
	}

	caerEventPacketHeader packet;
	while ((packet = caerRingBufferGet(state->transferRingPackets)) != NULL) {
		free(packet);
	}

	caerRingBufferFree(state->transferRingPackets);

	// Free all waiting packets.
	caerEventPacketHeader *packetPtr = NULL;
	while (
		(packetPtr = (caerEventPacketHeader *) utarray_next(state->packetContainer.eventPackets, packetPtr)) != NULL) {
		free(*packetPtr);
	}

	// Clear and free packet array used for packet container construction.
	utarray_free(state->packetContainer.eventPackets);

	// Close file descriptors.
	if (state->fileDescriptor >= 0) {
		close(state->fileDescriptor);
	}

	// Free allocated memory.
	free(state->dataBuffer);

	// Remove lingering packet parsing data.
	packetData curr, curr_tmp;
	DL_FOREACH_SAFE(state->packets.packetsList, curr, curr_tmp) {
		DL_DELETE(state->packets.packetsList, curr);
		free(curr);
	}

	free(state->packets.currPacketData);
	free(state->packets.currPacket);

	// Clear sourceInfo node.
	sshsNode sourceInfoNode = sshsGetRelativeNode(moduleData->moduleNode, "sourceInfo/");
	sshsNodeRemoveAllAttributes(sourceInfoNode);

	if (sshsNodeGetBool(moduleData->moduleNode, "autoRestart")) {
		// Prime input module again so that it will try to restart automatically.
		sshsNodePutBool(moduleData->moduleNode, "running", true);
	}
}

void caerInputCommonRun(caerModuleData moduleData, caerEventPacketContainer in, caerEventPacketContainer *out) {
	UNUSED_ARGUMENT(in);

	inputCommonState state = moduleData->moduleState;

	*out = caerRingBufferGet(state->transferRingPacketContainers);

	if (*out != NULL) {
		// No special memory order for decrease, because the acquire load to even start running
		// through a mainloop already synchronizes with the release store above.
		caerMainloopDataNotifyDecrease(NULL);
		atomic_fetch_sub_explicit(&state->dataAvailableModule, 1, memory_order_relaxed);

		caerEventPacketHeaderConst special = caerEventPacketContainerFindEventPacketByTypeConst(*out, SPECIAL_EVENT);

		if ((special != NULL) && (caerEventPacketHeaderGetEventNumber(special) == 1)
			&& (caerSpecialEventPacketFindValidEventByTypeConst((caerSpecialEventPacketConst) special, TIMESTAMP_RESET)
				   != NULL)) {
			caerMainloopModuleResetOutputRevDeps(moduleData->moduleID);
		}
	}
}

static void caerInputCommonConfigListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue) {
	UNUSED_ARGUMENT(node);

	caerModuleData moduleData = userData;
	inputCommonState state    = moduleData->moduleState;

	if (event == SSHS_ATTRIBUTE_MODIFIED) {
		if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "validOnly")) {
			// Set valid only flag to given value.
			atomic_store(&state->validOnly, changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "keepPackets")) {
			// Set keep packets flag to given value.
			atomic_store(&state->keepPackets, changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "pause")) {
			// Set pause flag to given value.
			atomic_store(&state->pause, changeValue.boolean);
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "bufferSize")) {
			// Set buffer update flag.
			atomic_store(&state->bufferUpdate, true);
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "PacketContainerMaxPacketSize")) {
			atomic_store(&state->packetContainer.sizeSlice, changeValue.iint);
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "PacketContainerInterval")) {
			atomic_store(&state->packetContainer.timeSlice, changeValue.iint);
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "PacketContainerDelay")) {
			atomic_store(&state->packetContainer.timeDelay, changeValue.iint);
		}
	}
}

static int packetsFirstTypeThenSizeCmp(const void *a, const void *b) {
	const caerEventPacketHeader *aa = a;
	const caerEventPacketHeader *bb = b;

	// Sort first by type ID.
	int16_t eventTypeA = caerEventPacketHeaderGetEventType(*aa);
	int16_t eventTypeB = caerEventPacketHeaderGetEventType(*bb);

	if (eventTypeA < eventTypeB) {
		return (-1);
	}
	else if (eventTypeA > eventTypeB) {
		return (1);
	}
	else {
		// If equal, further sort by event size.
		int32_t eventSizeA = caerEventPacketHeaderGetEventSize(*aa);
		int32_t eventSizeB = caerEventPacketHeaderGetEventSize(*bb);

		if (eventSizeA < eventSizeB) {
			return (-1);
		}
		else if (eventSizeA > eventSizeB) {
			return (1);
		}
		else {
			return (0);
		}
	}
}
