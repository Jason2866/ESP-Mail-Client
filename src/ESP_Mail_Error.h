/**
 * Created May 22, 2022
 */
#pragma once

#ifndef ESP_MAIL_ERROR_H
#define ESP_MAIL_ERROR_H


#define TCP_CLIENT_ERROR_CONNECTION_REFUSED (-1)
#define TCP_CLIENT_ERROR_SEND_DATA_FAILED (-2)
#define TCP_CLIENT_ERROR_NOT_INITIALIZED (-3)
#define TCP_CLIENT_ERROR_NOT_CONNECTED (-4)

#if defined(ENABLE_SMTP)

#define SMTP_STATUS_SERVER_CONNECT_FAILED -100
#define SMTP_STATUS_SMTP_GREETING_GET_RESPONSE_FAILED -101
#define SMTP_STATUS_SMTP_GREETING_SEND_ACK_FAILED -102
#define SMTP_STATUS_AUTHEN_NOT_SUPPORT -103
#define SMTP_STATUS_AUTHEN_FAILED -104
#define SMTP_STATUS_USER_LOGIN_FAILED -105
#define SMTP_STATUS_PASSWORD_LOGIN_FAILED -106
#define SMTP_STATUS_SEND_HEADER_SENDER_FAILED -107
#define SMTP_STATUS_SEND_HEADER_RECIPIENT_FAILED -108
#define SMTP_STATUS_SEND_BODY_FAILED -109
#define SMTP_STATUS_SERVER_OAUTH2_LOGIN_DISABLED -110
#define SMTP_STATUS_NO_VALID_RECIPIENTS_EXISTED -111
#define SMTP_STATUS_NO_VALID_SENDER_EXISTED -112
#define SMTP_STATUS_NO_SUPPORTED_AUTH -113
#define SMTP_STATUS_SEND_CUSTOM_COMMAND_FAILED -114
#define SMTP_STATUS_UNDEFINED -115
#endif

#if defined(ENABLE_IMAP)

#define IMAP_STATUS_SERVER_CONNECT_FAILED -200
#define IMAP_STATUS_IMAP_RESPONSE_FAILED -201
#define IMAP_STATUS_LOGIN_FAILED -202
#define IMAP_STATUS_BAD_COMMAND -203
#define IMAP_STATUS_PARSE_FLAG_FAILED -204
#define IMAP_STATUS_SERVER_OAUTH2_LOGIN_DISABLED -205
#define IMAP_STATUS_NO_MESSAGE -206
#define IMAP_STATUS_ERROR_DOWNLAD_TIMEOUT -207
#define IMAP_STATUS_CLOSE_MAILBOX_FAILED -208
#define IMAP_STATUS_OPEN_MAILBOX_FAILED -209
#define IMAP_STATUS_LIST_MAILBOXS_FAILED -210
#define IMAP_STATUS_CHECK_CAPABILITIES_FAILED -211
#define IMAP_STATUS_NO_SUPPORTED_AUTH -212
#define IMAP_STATUS_NO_MAILBOX_FOLDER_OPENED -213

#endif

#if defined(ENABLE_SMTP) || defined(ENABLE_IMAP)

#define MAIL_CLIENT_ERROR_CONNECTION_CLOSED -28
#define MAIL_CLIENT_ERROR_READ_TIMEOUT -29
#define MAIL_CLIENT_ERROR_SERVER_CONNECTION_FAILED -31
#define MAIL_CLIENT_ERROR_SSL_TLS_STRUCTURE_SETUP -32
#define MAIL_CLIENT_ERROR_OUT_OF_MEMORY -33
#define MAIL_CLIENT_ERROR_CUSTOM_CLIENT_DISABLED -34
#define MAIL_CLIENT_ERROR_NTP_TIME_SYNC_TIMED_OUT -35
#endif

#endif