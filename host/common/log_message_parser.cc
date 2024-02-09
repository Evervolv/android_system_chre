/*
 * Copyright (C) 2020 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "chre_host/log_message_parser.h"

#include <endian.h>
#include <string.h>

#include "chre/util/macros.h"
#include "chre/util/time.h"
#include "chre_host/daemon_base.h"
#include "chre_host/file_stream.h"
#include "chre_host/log.h"
#include "include/chre_host/log_message_parser.h"

#include "pw_result/result.h"
#include "pw_span/span.h"
#include "pw_tokenizer/detokenize.h"

using chre::kOneMillisecondInNanoseconds;
using chre::kOneSecondInMilliseconds;

namespace android {
namespace chre {

namespace {
#if defined(LOG_NDEBUG) || LOG_NDEBUG != 0
constexpr bool kVerboseLoggingEnabled = true;
#else
constexpr bool kVerboseLoggingEnabled = false;
#endif

//! Offset in bytes between the address and real start of a nanoapp binary.
constexpr size_t kImageHeaderSize = 0x1000;
//! The number of bytes in a tokenized log entry in addition to the log payload.
//! The value indicate the size of the uint8_t logSize field.
constexpr size_t kSystemTokenizedLogOffset = 1;
//! The number of bytes in a nanoapp tokenized log entry in addition to the log
//! payload. The value accounts for the size of the uint8_t logSize field and
//! the uint16_t instanceId field.
constexpr size_t kNanoappTokenizedLogOffset = 3;
}  // anonymous namespace

LogMessageParser::LogMessageParser()
    : mVerboseLoggingEnabled(kVerboseLoggingEnabled) {}

std::unique_ptr<Detokenizer> LogMessageParser::logDetokenizerInit() {
#ifdef CHRE_TOKENIZED_LOGGING_ENABLED
  constexpr const char kLogDatabaseFilePath[] =
      "/vendor/etc/chre/libchre_log_database.bin";
  std::vector<uint8_t> tokenData;
  if (readFileContents(kLogDatabaseFilePath, tokenData)) {
    pw::tokenizer::TokenDatabase database =
        pw::tokenizer::TokenDatabase::Create(tokenData);
    if (database.ok()) {
      LOGD("Log database initialized, creating detokenizer");
      return std::make_unique<Detokenizer>(database);
    } else {
      LOGE("CHRE Token database creation not OK");
    }
  } else {
    LOGE("Failed to read CHRE Token database file");
  }
#endif
  return std::unique_ptr<Detokenizer>(nullptr);
}

void LogMessageParser::init(size_t nanoappImageHeaderSize) {
  mSystemDetokenizer = logDetokenizerInit();
  mNanoappImageHeaderSize = nanoappImageHeaderSize;
}

void LogMessageParser::dump(const uint8_t *buffer, size_t size) {
  if (mVerboseLoggingEnabled) {
    char line[32];
    char lineChars[32];
    int offset = 0;
    int offsetChars = 0;

    size_t orig_size = size;
    if (size > 128) {
      size = 128;
      LOGV("Dumping first 128 bytes of buffer of size %zu", orig_size);
    } else {
      LOGV("Dumping buffer of size %zu bytes", size);
    }
    for (size_t i = 1; i <= size; ++i) {
      offset += snprintf(&line[offset], sizeof(line) - offset, "%02x ",
                         buffer[i - 1]);
      offsetChars +=
          snprintf(&lineChars[offsetChars], sizeof(lineChars) - offsetChars,
                   "%c", (isprint(buffer[i - 1])) ? buffer[i - 1] : '.');
      if ((i % 8) == 0) {
        LOGV("  %s\t%s", line, lineChars);
        offset = 0;
        offsetChars = 0;
      } else if ((i % 4) == 0) {
        offset += snprintf(&line[offset], sizeof(line) - offset, " ");
      }
    }

    if (offset > 0) {
      char tabs[8];
      char *pos = tabs;
      while (offset < 28) {
        *pos++ = '\t';
        offset += 8;
      }
      *pos = '\0';
      LOGV("  %s%s%s", line, tabs, lineChars);
    }
  }
}

android_LogPriority LogMessageParser::chreLogLevelToAndroidLogPriority(
    uint8_t level) {
  switch (level) {
    case LogLevel::ERROR:
      return ANDROID_LOG_ERROR;
    case LogLevel::WARNING:
      return ANDROID_LOG_WARN;
    case LogLevel::INFO:
      return ANDROID_LOG_INFO;
    case LogLevel::DEBUG:
      return ANDROID_LOG_DEBUG;
    default:
      return ANDROID_LOG_SILENT;
  }
}

uint8_t LogMessageParser::getLogLevelFromMetadata(uint8_t metadata) {
  // The lower nibble of the metadata denotes the loglevel, as indicated
  // by the schema in host_messages.fbs.
  return metadata & 0xf;
}

bool LogMessageParser::isLogMessageEncoded(uint8_t metadata) {
  // The upper nibble of the metadata denotes the encoding, as indicated
  // by the schema in host_messages.fbs.
  return (metadata & 0x10) != 0;
}

bool LogMessageParser::isBtSnoopLogMessage(uint8_t metadata) {
  // The upper nibble of the metadata denotes the encoding, as indicated
  // by the schema in host_messages.fbs.
  return (metadata & 0x20) != 0;
}

bool LogMessageParser::isNanoappTokenizedLogMessage(uint8_t metadata) {
  // The upper nibble of the metadata denotes the encoding, as indicated
  // by the schema in host_messages.fbs.
  return ((metadata & 0x20) != 0) && ((metadata & 0x10) != 0);
}

void LogMessageParser::log(const uint8_t *logBuffer, size_t logBufferSize) {
  size_t bufferIndex = 0;
  while (bufferIndex < logBufferSize) {
    const LogMessage *message =
        reinterpret_cast<const LogMessage *>(&logBuffer[bufferIndex]);
    uint64_t timeNs = le64toh(message->timestampNanos);
    emitLogMessage(message->logLevel, timeNs / kOneMillisecondInNanoseconds,
                   message->logMessage);
    bufferIndex += sizeof(LogMessage) +
                   strnlen(message->logMessage, logBufferSize - bufferIndex) +
                   1;
  }
}

size_t LogMessageParser::parseAndEmitTokenizedLogMessageAndGetSize(
    const LogMessageV2 *message) {
  size_t logMessageSize = 0;
  auto detokenizer = mSystemDetokenizer.get();
  if (detokenizer != nullptr) {
    auto *encodedLog =
        reinterpret_cast<const EncodedLog *>(message->logMessage);
    DetokenizedString detokenizedString =
        detokenizer->Detokenize(encodedLog->data, encodedLog->size);
    std::string decodedString = detokenizedString.BestStringWithErrors();
    emitLogMessage(getLogLevelFromMetadata(message->metadata),
                   le32toh(message->timestampMillis), decodedString.c_str());
    logMessageSize = encodedLog->size + kSystemTokenizedLogOffset;
  } else {
    // TODO(b/327515992): Stop decoding and emitting system log messages if
    // detokenizer is null .
    LOGE("Null detokenizer! Cannot decode log message");
  }
  return logMessageSize;
}

size_t LogMessageParser::parseAndEmitNanoappTokenizedLogMessageAndGetSize(
    const LogMessageV2 *message) {
  auto *tokenizedLog =
      reinterpret_cast<const NanoappTokenizedLog *>(message->logMessage);
  auto detokenizerIter = mNanoappDetokenizers.find(tokenizedLog->instanceId);
  if (detokenizerIter == mNanoappDetokenizers.end()) {
    LOGE(
        "Unable to find nanoapp log detokenizer associated with instance ID: "
        "%" PRIu16,
        tokenizedLog->instanceId);
  } else {
    auto detokenizer = detokenizerIter->second.detokenizer.get();
    DetokenizedString detokenizedString =
        detokenizer->Detokenize(tokenizedLog->data, tokenizedLog->size);
    std::string decodedString = detokenizedString.BestStringWithErrors();
    emitLogMessage(getLogLevelFromMetadata(message->metadata),
                   le32toh(message->timestampMillis), decodedString.c_str());
  }
  return tokenizedLog->size + kNanoappTokenizedLogOffset;
}

void LogMessageParser::parseAndEmitLogMessage(const LogMessageV2 *message) {
  emitLogMessage(getLogLevelFromMetadata(message->metadata),
                 le32toh(message->timestampMillis), message->logMessage);
}

void LogMessageParser::updateAndPrintDroppedLogs(uint32_t numLogsDropped) {
  if (numLogsDropped < mNumLogsDropped) {
    LOGE(
        "The numLogsDropped value received from CHRE is less than the last "
        "value received. Received: %" PRIu32 " Last value: %" PRIu32,
        numLogsDropped, mNumLogsDropped);
  }
  // Log the number of logs dropped once before logging remaining logs from CHRE
  uint32_t diffLogsDropped = numLogsDropped - mNumLogsDropped;
  mNumLogsDropped = numLogsDropped;
  if (diffLogsDropped > 0) {
    LOGI("# logs dropped: %" PRIu32, diffLogsDropped);
  }
}

void LogMessageParser::emitLogMessage(uint8_t level, uint32_t timestampMillis,
                                      const char *logMessage) {
  constexpr const char kLogTag[] = "CHRE";
  uint32_t timeSec = timestampMillis / kOneSecondInMilliseconds;
  uint32_t timeMsRemainder = timestampMillis % kOneSecondInMilliseconds;
  android_LogPriority priority = chreLogLevelToAndroidLogPriority(level);
  LOG_PRI(priority, kLogTag, kHubLogFormatStr, timeSec, timeMsRemainder,
          logMessage);
}

void LogMessageParser::logV2(const uint8_t *logBuffer, size_t logBufferSize,
                             uint32_t numLogsDropped) {
  // Size of the struct with an empty string.
  constexpr size_t kMinLogMessageV2Size = sizeof(LogMessageV2) + 1;

  updateAndPrintDroppedLogs(numLogsDropped);

  size_t bufferIndex = 0;
  while (bufferIndex + kMinLogMessageV2Size <= logBufferSize) {
    auto message =
        reinterpret_cast<const LogMessageV2 *>(&logBuffer[bufferIndex]);

    size_t logMessageSize = 0;
    if (isNanoappTokenizedLogMessage(message->metadata)) {
      logMessageSize =
          parseAndEmitNanoappTokenizedLogMessageAndGetSize(message);
    } else if (isBtSnoopLogMessage(message->metadata)) {
      logMessageSize = mBtLogParser.log(message->logMessage);
    } else if (isLogMessageEncoded(message->metadata)) {
      logMessageSize = parseAndEmitTokenizedLogMessageAndGetSize(message);
    } else {
      size_t maxLogMessageLen =
          (logBufferSize - bufferIndex) - kMinLogMessageV2Size;
      size_t logMessageLen = strnlen(message->logMessage, maxLogMessageLen);
      if (message->logMessage[logMessageLen] != '\0') {
        LOGE("Dropping log due to invalid buffer structure");
        break;
      }
      parseAndEmitLogMessage(message);
      // Account for the terminating '\0'
      logMessageSize = logMessageLen + 1;
    }
    bufferIndex += sizeof(LogMessageV2) + logMessageSize;
  }
}

void LogMessageParser::addNanoappDetokenizer(uint64_t appId,
                                             uint16_t instanceId,
                                             uint64_t databaseOffset,
                                             size_t databaseSize) {
  auto appBinaryIter = mNanoappAppIdToBinary.find(appId);
  if (appBinaryIter == mNanoappAppIdToBinary.end()) {
    LOGE("Unable to find nanoapp binary with app ID 0x%016" PRIx64, appId);
  } else if (checkTokenDatabaseOverflow(databaseOffset, databaseSize,
                                        appBinaryIter->second->size())) {
    LOGE(
        "Token database fails memory bounds check for nanoapp with app ID "
        "0x%016" PRIx64 ". Token database offset received: %" PRIu32
        "; size received: %zu; Size of the appBinary: %zu.",
        appId, databaseOffset, databaseSize, appBinaryIter->second->size());
  } else {
    const uint8_t *tokenDatabaseBinaryStart =
        appBinaryIter->second->data() + kImageHeaderSize + databaseOffset;

    pw::span<const uint8_t> tokenEntries(tokenDatabaseBinaryStart,
                                         databaseSize);
    pw::Result<Detokenizer> nanoappDetokenizer =
        pw::tokenizer::Detokenizer::FromElfSection(tokenEntries);

    // Clear out any stale detokenizer instance and clean up memory.
    appBinaryIter->second.reset();
    removeNanoappDetokenizerAndBinary(appId);

    if (nanoappDetokenizer.ok()) {
      NanoappDetokenizer detokenizer;
      detokenizer.appId = appId;
      detokenizer.detokenizer =
          std::make_unique<Detokenizer>(std::move(*nanoappDetokenizer));
      mNanoappDetokenizers[instanceId] = std::move(detokenizer);
    } else {
      LOGE("Unable to parse log detokenizer for app with ID: 0x%016" PRIx64,
           appId);
    }
  }
}

void LogMessageParser::removeNanoappDetokenizerAndBinary(uint64_t appId) {
  for (const auto &item : mNanoappDetokenizers) {
    if (item.second.appId == appId) {
      mNanoappDetokenizers.erase(item.first);
    }
  }
  mNanoappAppIdToBinary.erase(appId);
}

void LogMessageParser::resetNanoappDetokenizerState() {
  mNanoappDetokenizers.clear();
  mNanoappAppIdToBinary.clear();
}

void LogMessageParser::onNanoappLoadStarted(
    uint64_t appId, std::shared_ptr<const std::vector<uint8_t>> nanoappBinary) {
  mNanoappAppIdToBinary[appId] = nanoappBinary;
}

void LogMessageParser::onNanoappLoadFailed(uint64_t appId) {
  removeNanoappDetokenizerAndBinary(appId);
}

void LogMessageParser::onNanoappUnloaded(uint64_t appId) {
  removeNanoappDetokenizerAndBinary(appId);
}

bool LogMessageParser::checkTokenDatabaseOverflow(uint32_t databaseOffset,
                                                  size_t databaseSize,
                                                  size_t binarySize) {
  return databaseOffset > binarySize || databaseSize > binarySize ||
         databaseOffset + databaseSize > binarySize ||
         databaseOffset + databaseSize < databaseOffset;
}

}  // namespace chre
}  // namespace android
