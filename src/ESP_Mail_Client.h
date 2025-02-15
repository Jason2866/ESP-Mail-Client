#ifndef ESP_MAIL_CLIENT_H
#define ESP_MAIL_CLIENT_H

/**
 * Mail Client Arduino Library for Espressif's ESP32 and ESP8266 and SAMD21 with u-blox NINA-W102 WiFi/Bluetooth module
 *
 * Created June 22, 2022
 *
 * This library allows Espressif's ESP32, ESP8266 and SAMD devices to send and read Email through the SMTP and IMAP servers.
 *
 * The MIT License (MIT)
 * Copyright (c) 2022 K. Suwatchai (Mobizt)
 *
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <Arduino.h>
#include "extras/RFC2047.h"
#include <time.h>
#include <ctype.h>
#if !defined(__AVR__)
#include <algorithm>
#include <string>
#include <vector>
#endif

#include "extras/ESPTimeHelper/ESPTimeHelper.h"
#include "extras/MIMEInfo.h"

#if defined(ESP32) || defined(ESP8266)

#define UPLOAD_CHUNKS_NUM 12
#define ESP_MAIL_PRINTF ESP_MAIL_DEFAULT_DEBUG_PORT.printf

#if defined(ESP32)

#include <WiFi.h>
#include <ETH.h>
#define ESP_MAIL_MIN_MEM 70000

#elif defined(ESP8266)

#include <ESP8266WiFi.h>
#define SD_CS_PIN 15
#define ESP_MAIL_MIN_MEM 4000

#endif

#else

#undef min
#undef max
#define ESP_MAIL_MIN_MEM 3000
#define UPLOAD_CHUNKS_NUM 5

#include "extras/mb_print/mb_print.h"

extern "C" __attribute__((weak)) void
mb_print_putchar(char c)
{
  ESP_MAIL_DEFAULT_DEBUG_PORT.print(c);
}

#define ESP_MAIL_PRINTF mb_print_printf

#ifdef __arm__
// should use uinstd.h to define sbrk but Due causes a conflict
extern "C" char *sbrk(int incr);
#else  // __ARM__
extern char *__brkval;
#endif // __arm__

#endif

#include "wcs/ESP_TCP_Clients.h"

using namespace mb_string;

class IMAPSession;
class SMTPSession;
class SMTP_Status;
class DownloadProgress;
class MessageData;

#if defined(ENABLE_IMAP)

class MessageList
{
public:
  friend class IMAPSession;
  MessageList(){};
  ~MessageList() { clear(); };
  void add(int uid)
  {
    if (uid > 0)
      _list.push_back(uid);
  }

  void clear() { _list.clear(); }

private:
  MB_VECTOR<int> _list;
};

/* The class that provides the info of selected or opened mailbox folder */
class SelectedFolderInfo
{
public:
  friend class ESP_Mail_Client;
  friend class IMAPSession;
  SelectedFolderInfo(){};
  ~SelectedFolderInfo() { clear(); };

  /* Get the flags count for this mailbox */
  size_t flagCount() { return _flags.size(); };

  /* Get the numbers of messages in this mailbox */
  size_t msgCount() { return _msgCount; };

  /* Get the numbers of messages in this mailbox that recent flag was set */
  size_t recentCount() { return _recentCount; };

  /* Get the order of message in number of message in this mailbox that reoved */
  /**
   * The IMAP_Polling_Status has the properties e.g. type, messageNum, and argument.
   *
   * The type property is the type of status e.g.imap_polling_status_type_undefined, imap_polling_status_type_new_message,
   * imap_polling_status_type_remove_message, and imap_polling_status_type_fetch_message.
   *
   * The messageNum property is message number or order from the total number of message that added, fetched or deleted.
   *
   * The argument property is the argument of commands e.g. FETCH
   */
  IMAP_Polling_Status pollingStatus() { return _polling_status; };

  /* Get the predict next message UID */
  size_t nextUID() { return _nextUID; };

  /* Get the index of first unseen message */
  size_t unseenIndex() { return _unseenMsgIndex; };

  /* Get the numbers of messages from search result based on the search criteria
   */
  size_t searchCount() { return _searchCount; };

  /* Get the numbers of messages to be stored in the ressult */
  size_t availableMessages() { return _availableItems; };

  /* Get the flag argument at the specified index */
  String flag(size_t index)
  {
    if (index < _flags.size())
      return _flags[index].c_str();
    return "";
  }

private:
  void addFlag(const char *flag)
  {
    MB_String s = flag;
    _flags.push_back(s);
  };
  void clear()
  {
    for (size_t i = 0; i < _flags.size(); i++)
      _flags[i].clear();
    _flags.clear();
  }
  size_t _msgCount = 0;
  size_t _recentCount = 0;
  size_t _nextUID = 0;
  size_t _unseenMsgIndex = 0;
  size_t _searchCount = 0;
  size_t _availableItems = 0;
  unsigned long _idleTimeMs = 0;
  bool _folderChanged = false;
  bool _floderChangedState = false;
  IMAP_Polling_Status _polling_status;
  MB_VECTOR<MB_String> _flags;
};

/* The class that provides the list of FolderInfo e.g. name, attributes and
 * delimiter */
class FoldersCollection
{
public:
  friend class ESP_Mail_Client;
  friend class IMAPSession;
  FoldersCollection(){};
  ~FoldersCollection() { clear(); };
  size_t size() { return _folders.size(); };

  struct esp_mail_folder_info_item_t info(size_t index)
  {
    struct esp_mail_folder_info_item_t fd;
    if (index < _folders.size())
    {
      fd.name = _folders[index].name.c_str();
      fd.attributes = _folders[index].attributes.c_str();
      fd.delimiter = _folders[index].delimiter.c_str();
    }
    return fd;
  }

private:
  void add(struct esp_mail_folder_info_t &fd) { _folders.push_back(fd); };
  void clear()
  {
    for (size_t i = 0; i < _folders.size(); i++)
    {
      if (_folders[i].name.length() > 0)
        _folders[i].name.clear();
      if (_folders[i].attributes.length() > 0)
        _folders[i].attributes.clear();
      if (_folders[i].delimiter.length() > 0)
        _folders[i].delimiter.clear();
    }
    _folders.clear();
  }
  MB_VECTOR<esp_mail_folder_info_t> _folders;
};

/* The class that provides the status of message feching and searching */
class IMAP_Status
{
public:
  IMAP_Status();
  ~IMAP_Status();
  const char *info();
  bool success();
  void empty();
  friend class IMAPSession;

  MB_String _info;
  bool _success = false;
};

typedef void (*imapStatusCallback)(IMAP_Status);
typedef void (*imapResponseCallback)(IMAP_Response);
typedef void (*MIMEDataStreamCallback)(MIME_Data_Stream_Info);
typedef void (*imapCharacterDecodingCallback)(IMAP_Decoding_Info *);

#endif

#if defined(ENABLE_SMTP)

/* The SMTP message class */
class SMTP_Message
{
public:
  SMTP_Message(){};
  ~SMTP_Message() { clear(); };

  void resetAttachItem(SMTP_Attachment &att)
  {
    att.blob.size = 0;
    att.blob.data = nullptr;
    att.file.path.clear();
    att.file.storage_type = esp_mail_file_storage_type_none;
    att.descr.name.clear();
    att.descr.filename.clear();
    att.descr.transfer_encoding.clear();
    att.descr.content_encoding.clear();
    att.descr.mime.clear();
    att.descr.content_id.clear();
    att._int.att_type = esp_mail_att_type_none;
    att._int.index = 0;
    att._int.msg_uid = 0;
    att._int.flash_blob = false;
    att._int.xencoding = esp_mail_msg_xencoding_none;
    att._int.parallel = false;
    att._int.cid.clear();
  }

  void clear()
  {
    sender.name.clear();
    sender.email.clear();
    subject.clear();
    text.charSet.clear();
    text.content.clear();
    text.content_type.clear();
    text.embed.enable = false;
    html.charSet.clear();
    html.content.clear();
    html.content_type.clear();
    html.embed.enable = false;
    response.reply_to.clear();
    response.notify = esp_mail_smtp_notify::esp_mail_smtp_notify_never;
    priority = esp_mail_smtp_priority::esp_mail_smtp_priority_normal;

    for (size_t i = 0; i < _rcp.size(); i++)
    {
      _rcp[i].name.clear();
      _rcp[i].email.clear();
    }

    for (size_t i = 0; i < _cc.size(); i++)
      _cc[i].email.clear();

    for (size_t i = 0; i < _bcc.size(); i++)
      _bcc[i].email.clear();

    for (size_t i = 0; i < _hdr.size(); i++)
      _hdr[i].clear();

    for (size_t i = 0; i < _att.size(); i++)
    {
      _att[i].descr.filename.clear();
      _att[i].blob.data = nullptr;
      _att[i].descr.mime.clear();
      _att[i].descr.name.clear();
      _att[i].blob.size = 0;
      _att[i].descr.transfer_encoding.clear();
      _att[i].file.path.clear();
      _att[i].file.storage_type = esp_mail_file_storage_type_none;
    }

    for (size_t i = 0; i < _parallel.size(); i++)
    {
      _parallel[i].descr.filename.clear();
      _parallel[i].blob.data = nullptr;
      _parallel[i].descr.mime.clear();
      _parallel[i].descr.name.clear();
      _parallel[i].blob.size = 0;
      _parallel[i].descr.transfer_encoding.clear();
      _parallel[i].file.path.clear();
      _parallel[i].file.storage_type = esp_mail_file_storage_type_none;
    }
    _rcp.clear();
    _cc.clear();
    _bcc.clear();
    _hdr.clear();
    _att.clear();
    _parallel.clear();
  }

  /** Clear all the inline images
   */
  void clearInlineimages()
  {
    for (int i = (int)_att.size() - 1; i >= 0; i--)
    {
      if (_att[i]._int.att_type == esp_mail_att_type_inline)
        _att.erase(_att.begin() + i);
    }
  };

  /* Clear all the attachments */
  void clearAttachments()
  {
    for (int i = (int)_att.size() - 1; i >= 0; i--)
    {
      if (_att[i]._int.att_type == esp_mail_att_type_attachment)
        _att.erase(_att.begin() + i);
    }

    for (int i = (int)_parallel.size() - 1; i >= 0; i--)
      _parallel.erase(_parallel.begin() + i);
  };

  /** Clear all rfc822 message attachment
   */
  void clearRFC822Messages()
  {
    for (int i = (int)_rfc822.size() - 1; i >= 0; i--)
    {
      _rfc822[i].clear();
      _rfc822.erase(_rfc822.begin() + i);
    }
  };

  /** Clear the primary recipient mailboxes
   */
  void clearRecipients() { _rcp.clear(); };

  /** Clear the Carbon-copy recipient mailboxes
   */
  void clearCc() { _cc.clear(); };

  /** Clear the Blind-carbon-copy recipient mailboxes
   */
  void clearBcc() { _bcc.clear(); };

  /** Clear the custom message headers
   */
  void clearHeader() { _hdr.clear(); };

  /** Add attachment to the message
   *
   * @param att The SMTP_Attachment data item
   */
  void addAttachment(SMTP_Attachment &att)
  {
    att._int.att_type = esp_mail_att_type_attachment;
    att._int.parallel = false;
    att._int.flash_blob = true;
    _att.push_back(att);
  };

  /** Add parallel attachment to the message
   *
   * @param att The SMTP_Attachment data item
   */
  void addParallelAttachment(SMTP_Attachment &att)
  {
    att._int.att_type = esp_mail_att_type_attachment;
    att._int.parallel = true;
    att._int.flash_blob = true;
    _parallel.push_back(att);
  };

  /** Add inline image to the message
   *
   * @param att The SMTP_Attachment data item
   */
  void addInlineImage(SMTP_Attachment &att)
  {
    att._int.flash_blob = true;
    att._int.parallel = false;
    att._int.att_type = esp_mail_att_type_inline;
    att._int.cid = random(2000, 4000);
    _att.push_back(att);
  };

  /** Add rfc822 message to the message
   *
   * @param msg The RFC822_Message class object
   */
  void addMessage(SMTP_Message &msg) { _rfc822.push_back(msg); }

  /** Add the primary recipient mailbox to the message
   *
   * @param name The name of primary recipient
   * @param email The Email address of primary recipient
   */
  template <typename T1 = const char *, typename T2 = const char *>
  void addRecipient(T1 name, T2 email)
  {
    struct esp_mail_smtp_recipient_t rcp;
    rcp.name = toStringPtr(name);
    rcp.email = toStringPtr(email);
    _rcp.push_back(rcp);
  };

  /** Add Carbon-copy recipient mailbox
   *
   * @param email The Email address of the secondary recipient
   */
  template <typename T = const char *>
  void addCc(T email)
  {
    struct esp_mail_smtp_recipient_address_t cc;
    cc.email = toStringPtr(email);
    _cc.push_back(cc);
  };

  /** Add Blind-carbon-copy recipient mailbox
   *
   * @param email The Email address of the tertiary recipient
   */
  template <typename T = const char *>
  void addBcc(T email)
  {
    struct esp_mail_smtp_recipient_address_t bcc;
    bcc.email = toStringPtr(email);
    _bcc.push_back(bcc);
  };

  /** Add the custom header to the message
   *
   * @param hdr The header name and value
   */
  template <typename T = const char *>
  void addHeader(T hdr)
  {
    _hdr.push_back(MB_String().setPtr(toStringPtr(hdr)));
  };

  /* The message author config */
  struct esp_mail_email_info_t sender;

  /* The topic of message */
  MB_String subject;

  /* The message type */
  byte type = esp_mail_msg_type_none;

  /* The PLAIN text message */
  struct esp_mail_plain_body_t text;

  /* The HTML text message */
  struct esp_mail_html_body_t html;

  /* The response config */
  struct esp_mail_smtp_msg_response_t response;

  /* The priority of the message */
  esp_mail_smtp_priority priority = esp_mail_smtp_priority::esp_mail_smtp_priority_normal;

  /* The enable options */
  struct esp_mail_smtp_enable_option_t enable;

  /* The message from config */
  struct esp_mail_email_info_t from;

  /* The message identifier */
  MB_String messageID;

  /* The keywords or phrases, separated by commas */
  MB_String keywords;

  /* The comments about message */
  MB_String comments;

  /* The date of message */
  MB_String date;

  /* The field that contains the parent's message ID of the message to which this one is a reply */
  MB_String in_reply_to;

  /* The field that contains the parent's references (if any) and followed by the parent's message ID (if any) of the message to which this one is a reply */
  MB_String references;

private:
  friend class ESP_Mail_Client;
  MB_VECTOR<struct esp_mail_smtp_recipient_t> _rcp;
  MB_VECTOR<struct esp_mail_smtp_recipient_address_t> _cc;
  MB_VECTOR<struct esp_mail_smtp_recipient_address_t> _bcc;
  MB_VECTOR<MB_String> _hdr;
  MB_VECTOR<SMTP_Attachment> _att;
  MB_VECTOR<SMTP_Attachment> _parallel;
  MB_VECTOR<SMTP_Message> _rfc822;
};

class SMTP_Status
{
public:
  friend class SMTPSession;
  friend class ESP_Mail_Client;

  SMTP_Status();
  ~SMTP_Status();
  const char *info();
  bool success();
  void empty();
  size_t completedCount();
  size_t failedCount();

private:
  MB_String _info;
  bool _success = false;
  size_t _sentSuccess = 0;
  size_t _sentFailed = 0;
};

typedef void (*smtpStatusCallback)(SMTP_Status);
typedef void (*smtpResponseCallback)(SMTP_Response);

#endif

class ESP_Mail_Client
{

public:
  ESP_Mail_Client()
  {
    mbfs = new MB_FS();
  };
  ~ESP_Mail_Client()
  {
    if (mbfs)
      delete mbfs;
  };

#if defined(ENABLE_SMTP)
  /** Sending Email through the SMTP server
   *
   * @param smtp The pointer to SMTP session object which holds the data and the
   * TCP client.
   * @param msg The pointer to SMTP_Message class which contains the header,
   * body, and attachments.
   * @param closeSession The option to Close the SMTP session after sent.
   * @return The boolean value indicates the success of operation.
   */
  bool sendMail(SMTPSession *smtp, SMTP_Message *msg, bool closeSession = true);
#endif

#if defined(ENABLE_SMTP) && defined(ENABLE_IMAP)
  /** Append message to the mailbox
   *
   * @param imap The pointer to IMAP session object which holds the data and the
   * TCP client.
   * @param msg The pointer to SMTP_Message class which contains the header,
   * body, and attachments.
   * @param lastAppend The last message to append (optional). In case of MULTIAPPEND extension
   * is supported, set this to false will append messages in single APPEND command.
   * @param flags The flags to set to this message (optional).
   * @param dateTime The date/time to set to this message (optional).
   * @return The boolean value indicates the success of operation.
   */
  template <typename T1 = const char *, typename T2 = const char *>
  bool appendMessage(IMAPSession *imap, SMTP_Message *msg, bool lastAppend = true, T1 flags = "", T2 dateTime = "") { return mAppendMessage(imap, msg, lastAppend, toStringPtr(flags), toStringPtr(dateTime)); }
#endif

#if defined(ENABLE_IMAP)
  /** Reading Email through IMAP server.
   *
   * @param imap The pointer to IMAP session object which holds the data and
   the TCP client.

   * @param closeSession The option to close the IMAP session after fetching or
   searching the Email.
   * @return The boolean value indicates the success of operation.
  */
  bool readMail(IMAPSession *imap, bool closeSession = true);

  /** Set the argument to the Flags for the specified message.
   *
   * @param imap The pointer to IMAP session object which holds the data and the
   * TCP client.
   * @param msgUID The UID of the message.
   * @param flags The flag list to set.
   * @param closeSession The option to close the IMAP session after set flag.
   * @return The boolean value indicates the success of operation.
   */
  template <typename T = const char *>
  bool setFlag(IMAPSession *imap, int msgUID, T flags, bool closeSession) { return mSetFlag(imap, msgUID, toStringPtr(flags), 0, closeSession); }

  /** Add the argument to the Flags for the specified message.
   *
   * @param imap The pointer to IMAP session object which holds the data and the
   * TCP client.
   * @param msgUID The UID of the message.
   * @param flags The flag list to set.
   * @param closeSession The option to close the IMAP session after add flag.
   * @return The boolean value indicates the success of operation.
   */
  template <typename T = const char *>
  bool addFlag(IMAPSession *imap, int msgUID, T flags, bool closeSession) { return mSetFlag(imap, msgUID, toStringPtr(flags), 1, closeSession); }

  /** Remove the argument from the Flags for the specified message.
   *
   * @param imap The pointer to IMAP session object which holds the data and the
   * TCP client.
   * @param msgUID The UID of the message that flags to be removed.
   * @param flags The flag list to remove.
   * @param closeSession The option to close the IMAP session after remove flag.
   * @return The boolean value indicates the success of operation.
   */
  template <typename T = const char *>
  bool removeFlag(IMAPSession *imap, int msgUID, T flags, bool closeSession) { return mSetFlag(imap, msgUID, toStringPtr(flags), 2, closeSession); }
#endif

#if defined(MBFS_SD_FS) && defined(MBFS_CARD_TYPE_SD)

  /** SD card config with GPIO pins.
   *
   * @param ss SPI Chip/Slave Select pin.
   * @param sck SPI Clock pin.
   * @param miso SPI MISO pin.
   * @param mosi SPI MOSI pin.
   * @param frequency The SPI frequency
   * @return Boolean type status indicates the success of the operation.
   */
  bool sdBegin(int8_t ss = -1, int8_t sck = -1, int8_t miso = -1, int8_t mosi = -1, uint32_t frequency = 4000000);

#if defined(ESP8266)

  /** SD card config with SD FS configurations (ESP8266 only).
   *
   * @param ss SPI Chip/Slave Select pin.
   * @param sdFSConfig The pointer to SDFSConfig object (ESP8266 only).
   * @return Boolean type status indicates the success of the operation.
   */
  bool sdBegin(SDFSConfig *sdFSConfig);

#endif

#if defined(ESP32)
  /** SD card config with chip select and SPI configuration (ESP32 only).
   *
   * @param ss SPI Chip/Slave Select pin.
   * @param spiConfig The pointer to SPIClass object for SPI configuartion.
   * @param frequency The SPI frequency.
   * @return Boolean type status indicates the success of the operation.
   */
  bool sdBegin(int8_t ss, SPIClass *spiConfig = nullptr, uint32_t frequency = 4000000);
#endif

#if defined(MBFS_ESP32_SDFAT_ENABLED) || defined(MBFS_SDFAT_ENABLED)
  /** SD card config with SdFat SPI and pins configurations (ESP32 with SdFat included only).
   *
   * @param sdFatSPIConfig The pointer to SdSpiConfig object for SdFat SPI configuration.
   * @param ss SPI Chip/Slave Select pin.
   * @param sck SPI Clock pin.
   * @param miso SPI MISO pin.
   * @param mosi SPI MOSI pin.
   * @return Boolean type status indicates the success of the operation.
   */
  bool sdBegin(SdSpiConfig *sdFatSPIConfig, int8_t ss = -1, int8_t sck = -1, int8_t miso = -1, int8_t mosi = -1);
#endif

#endif

#if defined(ESP32) && defined(MBFS_SD_FS) && defined(MBFS_CARD_TYPE_SD_MMC)
  /** Initialize the SD_MMC card (ESP32 only).
   *
   * @param mountpoint The mounting point.
   * @param mode1bit Allow 1 bit data line (SPI mode).
   * @param format_if_mount_failed Format SD_MMC card if mount failed.
   * @return The boolean value indicates the success of operation.
   */
  bool sdMMCBegin(const char *mountpoint = "/sdcard", bool mode1bit = false, bool format_if_mount_failed = false);
#endif

  /** Get free Heap memory.
   *
   * @return Free memory amount in byte.
   */
  int getFreeHeap();

  /** Get base64 encode string.
   *
   * @return String of base64 encoded string.
   */
  template <typename T = const char *>
  String toBase64(T str) { return mGetBase64(toStringPtr(str)).c_str(); }

  ESPTimeHelper Time;

private:
  friend class SMTPSession;
  friend class IMAPSession;

  MB_FS *mbfs = nullptr;
  bool _clockReady = false;
  time_t ts = 0;

#if defined(ENABLE_IMAP)
#define IMAP_SESSION IMAPSession
#else
#define IMAP_SESSION void
#endif

  IMAP_SESSION *imap = nullptr;
  bool calDataLen = false;
  uint32_t dataLen = 0;
  uint32_t imap_ts = 0;

#if defined(ENABLE_SMTP) || defined(ENABLE_IMAP)

  unsigned long _lastReconnectMillis = 0;
  uint16_t _reconnectTimeout = ESP_MAIL_NETWORK_RECONNECT_TIMEOUT;

  // Get the CRLF ending string w/wo CRLF included. Return the size of string read and the current octet read.
  int readLine(ESP_MAIL_TCP_CLIENT *client, char *buf, int bufLen, bool crlf, int &count);

  // PGM string replacement
  void strReplaceP(MB_String &buf, PGM_P key, PGM_P value);

  // Check for XOAUTH2 log in error response
  bool authFailed(char *buf, int bufLen, int &chunkIdx, int ofs);

  // Get SASL XOAUTH2 string
  MB_String getXOAUTH2String(const MB_String &email, const MB_String &accessToken);

#if defined(ESP32_TCP_CLIENT) || defined(ESP8266_TCP_CLIENT)
  // Set Root CA cert or CA Cert for server authentication
  void setCACert(ESP_MAIL_TCP_CLIENT &client, ESP_Mail_Session *session, std::shared_ptr<const char> caCert);
#endif

  // Get the memory allocation block size of multiple of 4
  size_t getReservedLen(size_t len);

  // Print PGM string with new line via debug port (println)
  void debugInfoP(PGM_P info);

  // Check Email for valid format
  bool validEmail(const char *s);

  // Get random UID for SMTP content ID and IMAP attachment default file name
  char *getRandomUID();

  // Spit the string into token strings
  void splitTk(MB_String &str, MB_VECTOR<MB_String> &tk, const char *delim);

  // Decode base64 encoded string
  unsigned char *decodeBase64(const unsigned char *src, size_t len, size_t *out_len);

  // Decode base64 encoded string
  MB_String encodeBase64Str(const unsigned char *src, size_t len);

  // Decode base64 encoded string
  MB_String encodeBase64Str(uint8_t *src, size_t len);

  // Decode base64 encoded string
  MB_String mGetBase64(MB_StringPtr str);

  // Sub string
  char *subStr(const char *buf, PGM_P beginH, PGM_P endH, int beginPos, int endPos = 0, bool caseSensitive = true);

  // Append char to the null terminated string buffer
  void strcat_c(char *str, char c);

  // Find string
  int strpos(const char *haystack, const char *needle, int offset, bool caseSensitive = true);

  // Memory allocation
  void *newP(size_t len);

  // Memory deallocation
  void delP(void *ptr);

  // PGM string compare
  bool strcmpP(const char *buf, int ofs, PGM_P beginH, bool caseSensitive = true);

  // Find PGM string
  int strposP(const char *buf, PGM_P beginH, int ofs, bool caseSensitive = true);

  // Memory allocation for PGM string
  char *strP(PGM_P pgm);

  // Set or sync device system time with NTP server
  void setTime(float gmt_offset, float day_light_offset, const char *ntp_server, const char *TZ_Var, const char *TZ_file, bool wait);

  // Set the device time zone via TZ environment variable
  void setTimezone(const char *TZ_Var, const char *TZ_file);

  // Get TZ environment variable from file
  void getTimezone(const char *TZ_file, MB_String &out);

  // Get header content from response based on the field name
  bool getHeader(const char *buf, PGM_P beginH, MB_String &out, bool caseSensitive);

  // Get file extension with dot from MIME string
  void getExtfromMIME(const char *mime, MB_String &ext);

#endif

#if defined(ENABLE_SMTP)

  // Encode Quoted Printable string
  void encodeQP(const char *buf, char *out);

  // Add the soft line break to the long text line rfc 3676
  void formatFlowedText(MB_String &content);

  // Insert soft break
  void softBreak(MB_String &content, const char *quoteMarks);

  // Get content type (MIME) from file extension
  void getMIME(const char *ext, MB_String &mime);

  // Get content type (MIME) from file name
  void mimeFromFile(const char *name, MB_String &mime);

  // Get MIME boundary string
  MB_String getMIMEBoundary(size_t len);

  // Send Email function
  bool mSendMail(SMTPSession *smtp, SMTP_Message *msg, bool closeSession = true);

  // Reconnect the network if it disconnected
  bool reconnect(SMTPSession *smtp, unsigned long dataTime = 0);

  // Close TCP session
  void closeTCPSession(SMTPSession *smtp);

  // Send the error status callback
  void errorStatusCB(SMTPSession *smtp, int error);

  // SMTP send PGM string
  size_t smtpSendP(SMTPSession *smtp, PGM_P v, bool newline = false);

  // SMTP send data
  size_t smtpSend(SMTPSession *smtp, const char *data, bool newline = false);

  // SMTP send data
  size_t smtpSend(SMTPSession *smtp, int data, bool newline = false);

  // SMTP send data
  size_t smtpSend(SMTPSession *smtp, uint8_t *data, size_t size);

  // Handle the error by sending callback and close session
  bool handleSMTPError(SMTPSession *smtp, int err, bool ret = false);

  // Send parallel attachment RFC1521
  bool sendParallelAttachments(SMTPSession *smtp, SMTP_Message *msg, const MB_String &boundary);

  // Send attachment
  bool sendAttachments(SMTPSession *smtp, SMTP_Message *msg, const MB_String &boundary, bool parallel = false);

  // Send message content
  bool sendContent(SMTPSession *smtp, SMTP_Message *msg, bool closeSession, bool rfc822MSG);

  // Send imap or smtp callback
  void altSendCallback(SMTPSession *smtp, PGM_P s1, PGM_P s2, bool newline1, bool newline2);

  // Send message data
  bool sendMSGData(SMTPSession *smtp, SMTP_Message *msg, bool closeSession, bool rfc822MSG);

  // Send RFC 822 message
  bool sendRFC822Msg(SMTPSession *smtp, SMTP_Message *msg, const MB_String &boundary, bool closeSession, bool rfc822MSG);

  // Get RFC 822 message envelope
  void getRFC822MsgEnvelope(SMTPSession *smtp, SMTP_Message *msg, MB_String &buf);

  // Send BDAT command RFC 3030
  bool sendBDAT(SMTPSession *smtp, SMTP_Message *msg, int len, bool last);

  // Set the unencoded xencoding enum for html, text and attachment from its xencoding string
  void checkUnencodedData(SMTPSession *smtp, SMTP_Message *msg);

  // Check imap or smtp has callback set
  bool altIsCB(SMTPSession *smtp);

  // Check imap or smtp has debug set
  bool altIsDebug(SMTPSession *smtp);

  // Send BLOB attachment
  bool sendBlobAttachment(SMTPSession *smtp, SMTP_Message *msg, SMTP_Attachment *att);

  // Send file content
  bool sendFile(SMTPSession *smtp, SMTP_Message *msg, SMTP_Attachment *att);

  // Send imap or smtp storage error callback
  void altSendStorageErrorCB(SMTPSession *smtp, int err);

  // Open file to send an attachment
  bool openFileRead(SMTPSession *smtp, SMTP_Message *msg, SMTP_Attachment *att, MB_String &buf, const MB_String &boundary, bool inlined);

  // Open text file or html file for to send message
  bool openFileRead2(SMTPSession *smtp, SMTP_Message *msg, const char *path, esp_mail_file_storage_type storageType);

  // Send inline attachments
  bool sendInline(SMTPSession *smtp, SMTP_Message *msg, const MB_String &boundary, byte type);

  // Send storage error callback
  void sendStorageNotReadyError(SMTPSession *smtp, esp_mail_file_storage_type storageType);

  // Append message
  bool mAppendMessage(IMAPSession *imap, SMTP_Message *msg, bool lastAppend, MB_StringPtr flags, MB_StringPtr dateTime);

  // Get numbers of attachment based on type
  size_t numAtt(SMTPSession *smtp, esp_mail_attach_type type, SMTP_Message *msg);

  // Check for valid recipient Email
  bool checkEmail(SMTPSession *smtp, SMTP_Message *msg);

  // Send text parts MIME message
  bool sendPartText(SMTPSession *smtp, SMTP_Message *msg, byte type, const char *boundary);

  // Send imap APPEND data or smtp data
  bool altSendData(MB_String &s, bool newLine, SMTPSession *smtp, SMTP_Message *msg, bool addSendResult, bool getResponse, esp_mail_smtp_command cmd, esp_mail_smtp_status_code respCode, int errCode);

  // Send imap APPEND data or smtp data
  bool altSendData(uint8_t *data, size_t size, SMTPSession *smtp, SMTP_Message *msg, bool addSendResult, bool getResponse, esp_mail_smtp_command cmd, esp_mail_smtp_status_code respCode, int errCode);

  // Send MIME message
  bool sendMSG(SMTPSession *smtp, SMTP_Message *msg, const MB_String &boundary);

  // Get an attachment part header string
  void getAttachHeader(MB_String &header, const MB_String &boundary, SMTP_Attachment *attach, size_t size);

  // Get RFC 8222 part header string
  void getRFC822PartHeader(SMTPSession *smtp, MB_String &header, const MB_String &boundary);

  // Get an inline attachment header string
  void getInlineHeader(MB_String &header, const MB_String &boundary, SMTP_Attachment *inlineAttach, size_t size);

  // Send BLOB type text part or html part MIME message
  bool sendBlobBody(SMTPSession *smtp, SMTP_Message *msg, uint8_t type);

  // Send file type text part or html part MIME message
  bool sendFileBody(SMTPSession *smtp, SMTP_Message *msg, uint8_t type);

  // Base64 and QP encodings for text and html messages and replace embeded attachment file name with content ID
  void encodingText(SMTPSession *smtp, SMTP_Message *msg, uint8_t type, MB_String &content);
 
  // Blob or Stream available
  int chunkAvailable(SMTPSession *smtp, esp_mail_smtp_send_base64_data_info_t &data_info);

  // Read chunk data of blob or file
  int getChunk(SMTPSession *smtp, esp_mail_smtp_send_base64_data_info_t &data_info, unsigned char *rawChunk, bool base64);

  // Terminate chunk reading
  void closeChunk(esp_mail_smtp_send_base64_data_info_t &data_info);
  
  // Get base64 encoded buffer or raw buffer
  void getBuffer(bool base64, uint8_t *out, uint8_t *in, int &encodedCount, int &bufIndex, bool &dataReady, int &size, size_t chunkSize);

  // Send blob or file as base64 encoded chunk
  bool sendBase64(SMTPSession *smtp, SMTP_Message *msg, esp_mail_smtp_send_base64_data_info_t &data_info, bool base64, bool report);

  // Get imap or smtp report progress var pointer
  uint32_t altProgressPtr(SMTPSession *smtp);

  // Send PGM data
  void smtpCBP(SMTPSession *smtp, PGM_P info, bool success = false);

  // Send callback
  void smtpCB(SMTPSession *smtp, const char *info, bool success = false);

  // Get SMTP response status (respCode and text)
  void getResponseStatus(const char *buf, esp_mail_smtp_status_code respCode, int beginPos, struct esp_mail_smtp_response_status_t &status);

  // Parse SMTP authentication capability
  void parseAuthCapability(SMTPSession *smtp, char *buf);

  // Get TCP connected status
  bool connected(SMTPSession *smtp);

  // Add the sending result
  bool addSendingResult(SMTPSession *smtp, SMTP_Message *msg, bool result);

  // Handle SMTP server authentication
  bool smtpAuth(SMTPSession *smtp, bool &ssl);

  // Handle SMTP response
  bool handleSMTPResponse(SMTPSession *smtp, esp_mail_smtp_command cmd, esp_mail_smtp_status_code respCode, int errCode);

  // Print the upload status to the debug port
  void uploadReport(const char *filename, uint32_t pgAddr, int progress);

  // Get MB_FS object pointer
  MB_FS *getMBFS();

  // Set device system time
  int setTimestamp(time_t ts);
#endif

#if defined(ENABLE_IMAP)

  // handle rfc2047 Q (quoted printable) and B (base64) decodings
  RFC2047_Decoder RFC2047Decoder;

  // Check if child part (part number string) is a member of the parent part (part number string)
  // part number string format: <part number>.<sub part number>.<sub part number>
  bool multipartMember(const MB_String &parent, const MB_String &child);

  // Decode string
  int decodeChar(const char *s);

  // Decode Quoted Printable string
  void decodeQP_UTF8(const char *buf, char *out);

  // Actually not decode because 7bit string is enencode string unless prepare valid 7bit string and do qp decoding
  char *decode7Bit_UTF8(char *buf);

  // Actually not decode because 8bit string is enencode string unless prepare valid 8bit string
  char *decode8Bit_UTF8(char *buf);

  // Get encoding type from character set string
  esp_mail_char_decoding_scheme getEncodingFromCharset(const char *enc);

  // Decode header field string
  void decodeHeader(IMAPSession *imap, MB_String &headerField);

  // Decode Latin1 to UTF-8
  int decodeLatin1_UTF8(unsigned char *out, int *outlen, const unsigned char *in, int *inlen);

  // Decode TIS620 to UTF-8
  void decodeTIS620_UTF8(char *out, const char *in, size_t len);

  // Reconnect the network if it disconnected
  bool reconnect(IMAPSession *imap, unsigned long dataTime = 0, bool downloadRequestuest = false);

  // Get the TCP connection status
  bool connected(IMAPSession *imap);

  // Close TCP session
  void closeTCPSession(IMAPSession *imap);

  // Get multipart MIME fetch command
  bool getMultipartFechCmd(IMAPSession *imap, int msgIdx, MB_String &partText);

  // Fetch multipart MIME body header
  bool fetchMultipartBodyHeader(IMAPSession *imap, int msgIdx);

  // Handle IMAP server authentication
  bool imapAuth(IMAPSession *imap, bool &ssl);

  // Send IMAP command
  bool sendIMAPCommand(IMAPSession *imap, int msgIndex, int cmdCase);

  // Send error callback
  void errorStatusCB(IMAPSession *imap, int error);

  // Send PGM data
  size_t imapSendP(IMAPSession *imap, PGM_P v, bool newline = false);

  // Send data
  size_t imapSend(IMAPSession *imap, const char *data, bool newline = false);

  // Send data
  size_t imapSend(IMAPSession *imap, int data, bool newline = false);

  // Send data
  size_t imapSend(IMAPSession *imap, uint8_t *data, size_t size);

  // Log out
  bool imapLogout(IMAPSession *imap);

  // Send PGM string to callback
  void imapCBP(IMAPSession *imap, PGM_P info, bool success);

  // Send callback
  void imapCB(IMAPSession *imap, const char *info, bool success);

  // Send storage error callback
  void sendStorageNotReadyError(IMAPSession *imap, esp_mail_file_storage_type storageType);

  // Parse search response
  int parseSearchResponse(IMAPSession *imap, char *buf, int bufLen, int &chunkIdx, PGM_P tag, bool &endSearch, int &nump, const char *key, const char *pc);

  // Parse header state
  bool parseHeaderState(IMAPSession *imap, const char *buf, PGM_P beginH, bool caseSensitive, struct esp_mail_message_header_t &header, int &headerState, esp_mail_imap_header_state state);

  // Parse header response
  void parseHeaderResponse(IMAPSession *imap, char *buf, int bufLen, int &chunkIdx, struct esp_mail_message_header_t &header, int &headerState, int &octetCount, bool caseSensitive = true);

  // Set the header based on state parsed
  void setHeader(IMAPSession *imap, char *buf, struct esp_mail_message_header_t &header, int state);

  // Get decoded header
  bool getDecodedHeader(IMAPSession *imap, const char *buf, PGM_P beginH, MB_String &out, bool caseSensitive);

  // Parse part header response
  void parsePartHeaderResponse(IMAPSession *imap, const char *buf, int &chunkIdx, struct esp_mail_message_part_info_t &part, int &octetCount, bool caseSensitive = true);

  // Count char in string
  int countChar(const char *buf, char find);

  // Store the value to string via its the pointer
  bool storeStringPtr(IMAPSession *imap, uint32_t addr, MB_String &value, const char *buf);

  // Get part header properties
  bool getPartHeaderProperties(IMAPSession *imap, const char *buf, PGM_P p, PGM_P e, bool num, MB_String &value, MB_String &old_value, esp_mail_char_decoding_scheme &scheme, bool caseSensitive);

  // Url decode for UTF-8 encoded header text
  char *urlDecode(const char *str);

  // Reset the pointer to multiline response keeping string
  void resetStringPtr(struct esp_mail_message_part_info_t &part);

  // Get current part
  struct esp_mail_message_part_info_t *cPart(IMAPSession *imap);

  // Get current header
  struct esp_mail_message_header_t *cHeader(IMAPSession *imap);

  // Handle IMAP response
  bool handleIMAPResponse(IMAPSession *imap, int errCode, bool closeSession);

  // Print the file download status via debug port
  void downloadReport(IMAPSession *imap, int progress);

  // Print the message fetch status via debug port
  void fetchReport(IMAPSession *imap, int progress, bool html);

  // Print the message search status via debug port
  void searchReport(int progress, const char *percent);

  // Get current message num item
  struct esp_mail_imap_msg_num_t cMSG(IMAPSession *imap);

  // Get current message Index
  int cIdx(IMAPSession *imap);

  // Get IMAP response status e.g. OK, NO and Bad status enum value
  esp_mail_imap_response_status imapResponseStatus(IMAPSession *imap, char *response, PGM_P tag);

  // Add header item to string buffer to save to file
  void addHeaderItem(MB_String &str, esp_mail_message_header_t *header, bool json);

  // Add RFC822 headers to string buffer save to file
  void addRFC822Headers(MB_String &s, esp_mail_imap_rfc822_msg_header_item_t *header, bool json);

  // Add header string by name and value to string buffer to save to file
  void addHeader(MB_String &s, const char *name, const MB_String &value, bool trim, bool json);

  // Add header string by name and value to string buffer to save to file
  void addHeader(MB_String &s, const char *name, int value, bool json);

  // Save header string buffer to file
  void saveHeader(IMAPSession *imap, bool json);

  // Send MIME stream to callback
  void sendStreamCB(IMAPSession *imap, void *buf, size_t len, int chunkIndex, bool hrdBrk);

  // Prepare file path for saving
  void prepareFilePath(IMAPSession *imap, MB_String &filePath, bool header);

  // Decode text and store it to buffer or file
  void decodeText(IMAPSession *imap, char *buf, int bufLen, int &chunkIdx, MB_String &filePath, bool &downloadRequest, int &octetLength, int &readDataLen);

  // Handle atachment parsing and download
  bool parseAttachmentResponse(IMAPSession *imap, char *buf, int bufLen, int &chunkIdx, MB_String &filePath, bool &downloadRequest, int &octetCount, int &octetLength);

  // Parse mailbox folder open response
  void parseFoldersResponse(IMAPSession *imap, char *buf);

  // Prepare alias (short name) file list for unsupported long file name filesystem
  void prepareFileList(IMAPSession *imap, MB_String &filePath);

  // Parse capability response
  bool parseCapabilityResponse(IMAPSession *imap, char *buf, int &chunkIdx);

  // Parse Idle response
  bool parseIdleResponse(IMAPSession *imap);

  // Parse Get UID response
  void parseGetUIDResponse(IMAPSession *imap, char *buf);

  // Parse Get Flags response
  void parseGetFlagsResponse(IMAPSession *imap, char *buf);

  // Parse examine response
  void parseExamineResponse(IMAPSession *imap, char *buf);

  // Handle the error by sending callback and close session
  bool handleIMAPError(IMAPSession *imap, int err, bool ret);

  // Set Flag
  bool mSetFlag(IMAPSession *imap, int msgUID, MB_StringPtr flags, uint8_t action, bool closeSession);

#endif
};

#if defined(ENABLE_IMAP)

class IMAPSession
{
public:
  IMAPSession(Client *client);
  IMAPSession();
  ~IMAPSession();

  /** Assign custom Client from Arduino Clients.
   *
   * @param client The pointer to Arduino Client derived class e.g. WiFiClient, WiFiClientSecure, EthernetClient or GSMClient.
   */
  void setClient(Client *client);

  /** Assign the callback function to handle the server connection for custom Client.
   *
   * @param connectionCB The function that handles the server connection.
   */
  void connectionRequestCallback(ConnectionRequestCallback connectionCB);

  /** Assign the callback function to handle the server upgrade connection for custom Client.
   *
   * @param upgradeCB The function that handles existing connection upgrade.
   */
  void connectionUpgradeRequestCallback(ConnectionUpgradeRequestCallback upgradeCB);

  /** Assign the callback function to handle the network connection for custom Client.
   *
   * @param networkConnectionCB The function that handles the network connection.
   */
  void networkConnectionRequestCallback(NetworkConnectionRequestCallback networkConnectionCB);

  /** Assign the callback function to handle the network connection status acknowledgement.
   *
   * @param networkStatusCB The function that handle the network connection status acknowledgement.
   */
  void networkStatusRequestCallback(NetworkStatusRequestCallback networkStatusCB);

  /** Set the network status acknowledgement.
   *
   * @param status The network status.
   */
  void setNetworkStatus(bool status);

  /** Begin the IMAP server connection.
   *
   * @param session The pointer to ESP_Mail_Session structured data that keeps
   * the server and log in details.
   * @param config The pointer to IMAP_Config structured data that keeps the
   * operation options.
   * @return The boolean value which indicates the success of operation.
   */
  bool connect(ESP_Mail_Session *session, IMAP_Config *config);

  /** Begin the IMAP server connection without authentication.
   *
   * @param session The pointer to ESP_Mail_Session structured data that keeps
   * the server and log in details.
   * @param callback The callback function that accepts IMAP_Response as parameter.
   * @param tag The tag that pass to the callback function.
   * @return The boolean value indicates the success of operation.
   */
  template <typename T = const char *>
  bool customConnect(ESP_Mail_Session *session, imapResponseCallback callback, T tag = "") { return mCustomConnect(session, callback, toStringPtr(tag)); };

  /** Close the IMAP session.
   *
   * @return The boolean value which indicates the success of operation.
   */
  bool closeSession();

  /** Get TCP connection status.
   *
   * @return The boolean value indicates the connection status.
   */
  bool connected();

  /** Set to enable the debug.
   *
   * @param level The level to enable the debug message
   * level = 0, no debugging
   * level = 1, basic level debugging
   */
  void debug(int level);

  /** Get the list of all the mailbox folders since the TCP session was opened
   * and user was authenticated.
   *
   * @param folders The FoldersCollection class that contains the collection of
   * the
   * FolderInfo structured data.
   * @return The boolean value which indicates the success of operation.
   */
  bool getFolders(FoldersCollection &folders);

  /** Select or open the mailbox folder to search or fetch the message inside.
   *
   * @param folderName The known mailbox folder name. The default name is INBOX.
   * @param readOnly The option to open the mailbox for read only. Set this
   * option to false when you wish
   * to modify the Flags using the setFlag, addFlag and removeFlag functions.
   * @return The boolean value which indicates the success of operation.
   *
   * @note: the function will exit immediately and return true if the time since previous success folder selection (open)
   * with the same readOnly mode, is less than 5 seconds.
   */
  template <typename T = const char *>
  bool selectFolder(T folderName, bool readOnly = true) { return mSelectFolder(toStringPtr(folderName), readOnly); }

  /** Open the mailbox folder to read or search the mesages.
   *
   * @param folderName The name of known mailbox folder to be opened.
   * @param readOnly The option to open the mailbox for reading only. Set this
   * option to false when you wish
   * to modify the flags using the setFlag, addFlag and removeFlag functions.
   * @return The boolean value which indicates the success of operation.
   *
   * @note: the function will exit immediately and return true if the time since previous success folder selection (open)
   * with the same readOnly mode, is less than 5 seconds.
   */
  template <typename T = const char *>
  bool openFolder(T folderName, bool readOnly = true) { return mOpenFolder(toStringPtr(folderName), readOnly); }

  /** Close the mailbox folder that was opened.
   *
   * @param folderName The known mailbox folder name.
   * @return The boolean value which indicates the success of operation.
   */
  template <typename T = const char *>
  bool closeFolder(T folderName) { return mCloseFolder(toStringPtr(folderName)); }

  /** Create folder.
   *
   * @param folderName The name of folder to create.
   * @return The boolean value which indicates the success of operation.
   */
  template <typename T = const char *>
  bool createFolder(T folderName) { return mCreateFolder(toStringPtr(folderName)); }

  /** Delete folder.
   *
   * @param folderName The name of folder to delete.
   * @return The boolean value which indicates the success of operation.
   */
  template <typename T = const char *>
  bool deleteFolder(T folderName) { return mDeleteFolder(toStringPtr(folderName)); }

  /** Get UID number in selected or opened mailbox.
   *
   * @param msgNum The message number or order in the total message numbers.
   * @return UID number in selected or opened mailbox.
   *
   * @note Returns 0 when fail to get UID.
   */
  int getUID(int msgNum);

  /** Get message flags in selected or opened mailbox.
   *
   * @param msgNum The message number or order in the total message numbers.
   * @return Message flags in selected or opened mailbox.
   *
   * @note Returns empty string when fail to get flags.
   */
  const char *getFlags(int msgNum);

  /** Send the custom IMAP command and get the result via callback.
   *
   * @param cmd The command string.
   * @param callback The callback function that accepts IMAP_Response as parameter.
   * @param tag The tag string to pass to the callback function.
   * @return The boolean value which indicates the success of operation.
   *
   * @note imap.connect and imap.selectFolder or imap.openFolder are needed to call once prior to call this function.
   */
  template <typename T1 = const char *, typename T2 = const char *>
  bool sendCustomCommand(T1 cmd, imapResponseCallback callback, T2 tag = "") { return mSendCustomCommand(toStringPtr(cmd), callback, toStringPtr(tag)); }

  /** Send the custom IMAP command data string.
   *
   * @param data The string data.
   * @param last The flag represents the last data to send (optional).
   * @return The boolean value which indicates the success of operation.
   *
   * @note Should be used after calling sendCustomCommand("APPEND xxxxxx");
   */
  template <typename T = const char *>
  bool sendCustomData(T data, bool lastData = false) { return mSendData(toStringPtr(data), lastData, esp_mail_imap_cmd_custom); }

  /** Send the custom IMAP command data.
   *
   * @param data The byte data.
   * @param size The data size.
   * @param lastData The flag represents the last data to send (optional).
   * @return The boolean value which indicates the success of operation.
   *
   * @note Should be used after calling ssendCustomCommand("APPEND xxxxxx");
   */
  bool sendCustomData(uint8_t *data, size_t size, bool lastData = false) { return mSendData(data, size, lastData, esp_mail_imap_cmd_custom); }

  /** Copy the messages to the defined mailbox folder.
   *
   * @param toCopy The pointer to the MessageListList class that contains the
   * list of messages to copy.
   * @param dest The destination folder that the messages to copy to.
   * @return The boolean value which indicates the success of operation.
   */
  template <typename T = const char *>
  bool copyMessages(MessageList *toCopy, T dest) { return mCopyMessages(toCopy, toStringPtr(dest)); }

  /** Delete the messages in the opened mailbox folder.
   *
   * @param toDelete The pointer to the MessageListList class that contains the
   * list of messages to delete.
   * @param expunge The boolean option to expunge all messages.
   * @return The boolean value which indicates the success of operation.
   */
  bool deleteMessages(MessageList *toDelete, bool expunge = false);

  /** Listen for the selected or open mailbox for updates.
   * @return The boolean value which indicates the success of operation.
   */
  bool listen() { return mListen(false); };

  /** Stop listen for the mailbox for updates.
   * @return The boolean value which indicates the success of operation.
   */
  bool stopListen() { return mStopListen(false); };

  /** Check for the selected or open mailbox updates.
   * @return The boolean value which indicates the changes status of mailbox.
   */
  bool folderChanged();

  /** Assign the callback function that returns the operating status when
   * fetching or reading the Email.
   *
   * @param imapCallback The function that accepts the imapStatusCallback as
   * parameter.
   */
  void callback(imapStatusCallback imapCallback);

  /** Assign the callback function to decode the string based on the character set.
   *
   * @param callback The function that accepts the pointer to IMAP_Decoding_Info as parameter.
   */
  void characterDecodingCallback(imapCharacterDecodingCallback callback);

  /** Assign the callback function that returns the MIME data stream from
   * fetching or reading the Email.
   *
   * @param mimeDataStreamCallback The function that accepts the MIME_Stream_Info as
   * parameter.
   */
  void mimeDataStreamCallback(MIMEDataStreamCallback mimeDataStreamCallback);

  /** Determine if no message body contained in the search result and only the
   * message header is available.
   */
  bool headerOnly();

  /** Get the message list from search or fetch the Emails
   *
   * @return The IMAP_MSG_List structured data which contains text and html
   * contents,
   * attachments, inline images, embedded rfc822 messages details for each
   * message.
   */
  IMAP_MSG_List data();

  /** Get the details of the selected or opned mailbox folder
   *
   * @return The SelectedFolderInfo class which contains the info about flags,
   * total messages, next UID,
   * search count and the available messages count.
   */
  SelectedFolderInfo selectedFolder();

  /** Get the error details when readingg the Emails
   *
   * @return The string of error details.
   */
  String errorReason();

  /** Clear all the cache data stored in the IMAP session object.
   */
  void empty();

  /** Get the JSON string of file name list of files that stored in SD card.
   *
   * @return The JSON string of filenames.
   * @note This will available only when standard SD library was used and file storage is SD.
   */
  String fileList();

  /** Set the current timestamp.
   *
   * @param ts The current timestamp.
   */
  void setSystemTime(time_t ts);


  friend class ESP_Mail_Client;
  friend class foldderList;

private:
  // Clear message data
  void clearMessageData();

  // Check for valid UID or set wildcard * as UID
  void checkUID();

  // Check for valid saving file path or prepend /
  void checkPath();

  // Get message item by index
  void getMessages(uint16_t messageIndex, struct esp_mail_imap_msg_item_t &msg);

  // Get RFC822 message item by index
  void getRFC822Messages(uint16_t messageIndex, struct esp_mail_imap_msg_item_t &msg);

  // Close mailbox
  bool closeMailbox();

  // Open mailbox
  bool openMailbox(MB_StringPtr folder, esp_mail_imap_auth_mode mode, bool waitResponse);

  // Get folders list
  bool getMailboxes(FoldersCollection &flders);

  // Prepend TAG for response status parsing
  MB_String prependTag(PGM_P tag, PGM_P cmd);

  // Check capabilities
  bool checkCapabilities();

  // Listen mailbox changes
  bool mListen(bool recon);

  // Stop listen mailbox
  bool mStopListen(bool recon);

  // Send custom command
  bool mSendCustomCommand(MB_StringPtr cmd, imapResponseCallback callback, MB_StringPtr tag);

  // Send data after sending APPEND command
  bool mSendData(MB_StringPtr data, bool lastData, esp_mail_imap_command cmd);

  // Send data after sending APPEND command
  bool mSendData(uint8_t *data, size_t size, bool lastData, esp_mail_imap_command cmd);

  // Delete folder
  bool mDeleteFolder(MB_StringPtr folderName);

  // Create folder
  bool mCreateFolder(MB_StringPtr folderName);

  // Copy message
  bool mCopyMessages(MessageList *toCopy, MB_StringPtr dest);

  // Close folder
  bool mCloseFolder(MB_StringPtr folderName);

  // Open folder
  bool mOpenFolder(MB_StringPtr folderName, bool readOnly);

  // Select folder
  bool mSelectFolder(MB_StringPtr folderName, bool readOnly);

  // Custom TCP connection
  bool mCustomConnect(ESP_Mail_Session *session, imapResponseCallback callback, MB_StringPtr tag);

  // Handle connection
  bool handleConnection(ESP_Mail_Session *session, IMAP_Config *config, bool &ssl);

  // Start TCP connection
  bool connect(bool &ssl);

  bool _tcpConnected = false;
  unsigned long _last_polling_error_ms = 0;
  unsigned long _last_host_check_ms = 0;
  struct esp_mail_imap_response_status_t _imapStatus;
  int _cMsgIdx = 0;
  int _cPartIdx = 0;
  int _totalRead = 0;
  MB_VECTOR<struct esp_mail_message_header_t> _headers;

  esp_mail_imap_command _imap_cmd = esp_mail_imap_command::esp_mail_imap_cmd_login;
  esp_mail_imap_command _prev_imap_cmd = esp_mail_imap_command::esp_mail_imap_cmd_login;
  esp_mail_imap_command _imap_custom_cmd = esp_mail_imap_cmd_custom;
  esp_mail_imap_command _prev_imap_custom_cmd = esp_mail_imap_cmd_custom;
  bool _idle = false;
  MB_String _cmd;
  MB_VECTOR<struct esp_mail_imap_multipart_level_t> _multipart_levels;
  int _rfc822_part_count = 0;
  bool _unseen = false;
  bool _readOnlyMode = true;
  struct esp_mail_auth_capability_t _auth_capability;
  struct esp_mail_imap_capability_t _read_capability;
  ESP_Mail_Session *_sesson_cfg;
  MB_String _currentFolder;
  bool _mailboxOpened = false;
  unsigned long _lastSameFolderOpenMillis = 0;
  MB_String _nextUID;
  MB_String _unseenMsgIndex;
  MB_String _flags_tmp;
  MB_String _sdFileList;

  struct esp_mail_imap_read_config_t *_config = nullptr;

  bool _headerOnly = true;
  bool _uidSearch = false;
  bool _headerSaved = false;
  bool _debug = false;
  int _debugLevel = 0;
  bool _secure = false;
  imapStatusCallback _readCallback = NULL;
  imapResponseCallback _customCmdResCallback = NULL;
  MIMEDataStreamCallback _mimeDataStreamCallback = NULL;
  imapCharacterDecodingCallback _charDecCallback = NULL;

  MB_VECTOR<struct esp_mail_imap_msg_num_t> _imap_msg_num;

  FoldersCollection _folders;
  SelectedFolderInfo _mbif;
  int _uid_tmp = 0;
  int _lastProgress = -1;
  int _certType = -1;
#if defined(ESP32) || defined(ESP8266)
  std::shared_ptr<const char> _caCert = nullptr;
#endif

  ESP_MAIL_TCP_CLIENT client;

  IMAP_Status _cbData;
};

#endif

#if defined(ENABLE_SMTP)

class SendingResult
{
private:
  MB_VECTOR<SMTP_Result> _result;

  void add(SMTP_Result *r)
  {
    _result.push_back(*r);
  }

public:
  friend class SMTPSession;
  friend class ESP_Mail_Client;
  SendingResult(){};
  ~SendingResult() { clear(); };

  void clear()
  {
    for (size_t i = 0; i < _result.size(); i++)
    {
      _result[i].recipients.clear();
      _result[i].subject.clear();
      _result[i].timestamp = 0;
      _result[i].completed = false;
    }
    _result.clear();
  }

  SMTP_Result getItem(size_t index)
  {
    SMTP_Result r;
    if (index < _result.size())
      return _result[index];
    return r;
  }
  size_t size() { return _result.size(); };
};

class SMTPSession
{
public:
  SMTPSession(Client *client);
  SMTPSession();
  ~SMTPSession();

  /** Assign custom Client from Arduino Clients.
   *
   * @param client The pointer to Arduino Client derived class e.g. WiFiClient, WiFiClientSecure, EthernetClient or GSMClient.
   */
  void setClient(Client *client);

  /** Assign the callback function to handle the server connection for custom Client.
   *
   * @param connectionCB The function that handles the server connection.
   */
  void connectionRequestCallback(ConnectionRequestCallback connectionCB);

  /** Assign the callback function to handle the server upgrade connection for custom Client.
   *
   * @param upgradeCB The function that handles existing connection upgrade.
   */
  void connectionUpgradeRequestCallback(ConnectionUpgradeRequestCallback upgradeCB);

  /** Assign the callback function to handle the network connection for custom Client.
   *
   * @param networkConnectionCB The function that handles the network connection.
   */
  void networkConnectionRequestCallback(NetworkConnectionRequestCallback networkConnectionCB);

  /** Assign the callback function to handle the network connection status acknowledgement.
   *
   * @param networkStatusCB The function that handle the network connection status acknowledgement.
   */
  void networkStatusRequestCallback(NetworkStatusRequestCallback networkStatusCB);

  /** Set the network status acknowledgement.
   *
   * @param status The network status.
   */
  void setNetworkStatus(bool status);

  /** Begin the SMTP server connection.
   *
   * @param session The pointer to ESP_Mail_Session structured data that keeps
   * the server and log in details.
   * @return The boolean value indicates the success of operation.
   */
  bool connect(ESP_Mail_Session *session);

  /** Begin the SMTP server connection without authentication.
   *
   * @param session The pointer to ESP_Mail_Session structured data that keeps
   * the server and log in details.
   * @param callback The callback function that accepts the SMTP_Response as parameter.
   * @param commandID The command identifier number that will pass to the callback.
   * @return The int value of status code.
   *
   * @note If commandID was not set or set to -1, the command identifier will be auto increased started from zero.
   */
  int customConnect(ESP_Mail_Session *config, smtpResponseCallback callback, int commandID = -1);

  /** Close the SMTP session.
   *
   */
  bool closeSession();

  /** Get TCP connection status.
   *
   * @return The boolean value indicates the connection status.
   */
  bool connected();

  /** Send the custom SMTP command and get the result via callback.
   *
   * @param cmd The command string.
   * @param callback The function that accepts the SMTP_Response as parameter.
   * @param commandID The command identifier number that will pass to the callback.
   * @return The integer value of response code.
   *
   * @note smtp.connect or smtp.customConnect is needed to call once prior to call this function.
   *
   * If commandID was not set or set to -1, the command identifier will be auto increased started from zero.
   */
  template <typename T = const char *>
  int sendCustomCommand(T cmd, smtpResponseCallback callback, int commandID = -1) { return mSendCustomCommand(toStringPtr(cmd), callback, commandID); }

  /** Send the custom SMTP command data string.
   *
   * @param data The string data.
   * @return The boolean value which indicates the success of operation.
   *
   * @note Should be used after calling sendCustomCommand("DATA");
   */
  template <typename T = const char *>
  bool sendCustomData(T data) { return mSendData(toStringPtr(data)); }

  /** Send the custom SMTP command data.
   *
   * @param data The byte data.
   * @param size The data size.
   * @return The boolean value which indicates the success of operation.
   *
   * @note Should be used after calling sendCustomCommand("DATA");
   */
  bool sendCustomData(uint8_t *data, size_t size) { return mSendData(data, size); }

  /** Set to enable the debug.
   *
   * @param level The level to enable the debug message
   * level = 0, no debugging
   * level = 1, basic level debugging
   */
  void debug(int level);

  /** Get the error details when sending the Email
   *
   * @return The string of error details.
   */
  String errorReason();

  /** Set the Email sending status callback function.
   *
   * @param smtpCallback The callback function that accept the
   * smtpStatusCallback param.
   */
  void callback(smtpStatusCallback smtpCallback);

  /** Set the current timestamp.
   *
   * @param ts The current timestamp.
   */
  void setSystemTime(time_t ts);

  SendingResult sendingResult;

  friend class ESP_Mail_Client;

private:
  bool _tcpConnected = false;
  struct esp_mail_smtp_response_status_t _smtpStatus;
  int _sentSuccessCount = 0;
  int _sentFailedCount = 0;
  bool _chunkedEnable = false;
  int _chunkCount = 0;
  uint32_t ts = 0;

  esp_mail_smtp_command _smtp_cmd = esp_mail_smtp_command::esp_mail_smtp_cmd_greeting;
  struct esp_mail_auth_capability_t _auth_capability;
  struct esp_mail_smtp_capability_t _send_capability;
  ESP_Mail_Session *_sesson_cfg = NULL;

  bool _debug = false;
  int _debugLevel = 0;
  bool _secure = false;
  smtpStatusCallback _sendCallback = NULL;
  smtpResponseCallback _customCmdResCallback = NULL;
  int _commandID = -1;

  SMTP_Status _cbData;
  struct esp_mail_smtp_msg_type_t _msgType;
  int _lastProgress = -1;

  int _certType = -1;
#if defined(ESP32) || defined(ESP8266)
  std::shared_ptr<const char> _caCert = nullptr;
#endif

  ESP_MAIL_TCP_CLIENT client;

  // Start TCP connection
  bool connect(bool &ssl);

  // Handle TCP connection
  bool handleConnection(ESP_Mail_Session *config, bool &ssl);

  // Send custom command
  int mSendCustomCommand(MB_StringPtr cmd, smtpResponseCallback callback, int commandID = -1);

  // Send data after sending DATA command
  bool mSendData(MB_StringPtr data);

  // Send data after sending DATA command
  bool mSendData(uint8_t *data, size_t size);
};

#endif

#if defined(ENABLE_SMTP) && defined(ENABLE_IMAP)

class ESP_Mail_Message : public SMTP_Message
{
public:
  ESP_Mail_Message(){};
  ~ESP_Mail_Message(){};
};

#endif

extern ESP_Mail_Client MailClient;

#endif /* ESP_MAIL_CLIENT_H */
