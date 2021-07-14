#include <stdio.h>
#include <stdarg.h>
#include <Arduino.h>

#include "Syslog.h"

// Public Methods //////////////////////////////////////////////////////////////


Syslog(Stream &fh, timestampFunc tfunc = NULL, const char* hostname = SYSLOG_NILVALUE, const char* appName = SYSLOG_NILVALUE, uint16_t logLevel = LOG_ERR, uint16_t priDefault = LOG_KERN, uint8_t protocol = SYSLOG_PROTO_IETF);
  this->_fh = &fh;
  this->_protocol = protocol;
  this->_appName = (appName == NULL) ? SYSLOG_NILVALUE : appName;
  this->_priDefault = priDefault;
  this->_tfunc = tfunc;
  this->_deviceHostname = hostname;
  this->setLogLevel(logLevel);
}

Syslog &Syslog::appName(const char* appName) {
  this->_appName = (appName == NULL) ? SYSLOG_NILVALUE : appName;
  return *this;
}

Syslog &Syslog::defaultPriority(uint16_t pri) {
  this->_priDefault = pri;
  return *this;
}

Syslog &Syslog::logMask(uint8_t priMask) {
  this->_priMask = priMask;
  return *this;
}

Syslog &Syslog::setLogLevel(uint16_t level) {
  uint8_t mask;
  this->_logLevel = level;
  mask = LOG_PRI(level);
  this->_priMask = mask | (mask-1); //Anything at this level or lower will be masked for display
  return *this;
}

uint16_t Syslog::getLogLevel(void) {
  return(this->_logLevel);
}

bool Syslog::log(uint16_t pri, const __FlashStringHelper *message) {
  return this->_sendLog(pri, message);
}

bool Syslog::log(uint16_t pri, const String &message) {
  return this->_sendLog(pri, message.c_str());
}

bool Syslog::log(uint16_t pri, const char *message) {
  return this->_sendLog(pri, message);
}


bool Syslog::vlogf(uint16_t pri, const char *fmt, va_list args) {
  char *message;
  size_t initialLen;
  size_t len;
  bool result;

  initialLen = strlen(fmt);

  message = new char[initialLen + 1];

  len = vsnprintf(message, initialLen + 1, fmt, args);
  if (len > initialLen) {
    delete[] message;
    message = new char[len + 1];

    vsnprintf(message, len + 1, fmt, args);
  }

  result = this->_sendLog(pri, message);

  delete[] message;
  return result;
}

bool Syslog::vlogf_P(uint16_t pri, PGM_P fmt_P, va_list args) {
  char *message;
  size_t initialLen;
  size_t len;
  bool result;

  initialLen = strlen_P(fmt_P);

  message = new char[initialLen + 1];

  len = vsnprintf_P(message, initialLen + 1, fmt_P, args);
  if (len > initialLen) {
    delete[] message;
    message = new char[len + 1];

    vsnprintf(message, len + 1, fmt_P, args);
  }

  result = this->_sendLog(pri, message);

  delete[] message;
  return result;
}


bool Syslog::logf(uint16_t pri, const char *fmt, ...) {
  va_list args;
  bool result;

  va_start(args, fmt);
  result = this->vlogf(pri, fmt, args);
  va_end(args);
  return result;
}

bool Syslog::logf(const char *fmt, ...) {
  va_list args;
  bool result;

  va_start(args, fmt);
  result = this->vlogf(this->_priDefault, fmt, args);
  va_end(args);
  return result;
}

bool Syslog::logf_P(uint16_t pri, PGM_P fmt_P, ...) {
  va_list args;
  bool result;

  va_start(args, fmt_P);
  result = this->vlogf_P(pri, fmt_P, args);
  va_end(args);
  return result;
}

bool Syslog::logf_P(PGM_P fmt_P, ...) {
  va_list args;
  bool result;

  va_start(args, fmt_P);
  result = this->vlogf_P(this->_priDefault, fmt_P, args);
  va_end(args);
  return result;
}

bool Syslog::log(const __FlashStringHelper *message) {
  return this->_sendLog(this->_priDefault, message);
}

bool Syslog::log(const String &message) {
  return this->_sendLog(this->_priDefault, message.c_str());
}

bool Syslog::log(const char *message) {
  return this->_sendLog(this->_priDefault, message);
}

// Private Methods /////////////////////////////////////////////////////////////

inline bool Syslog::_sendHeader(uint16_t pri, const char* procid, const char* msgid) {
  char* timestampBuf[40];

  // Check priority against priMask values.
  if ((LOG_MASK(LOG_PRI(pri)) & this->_priMask) == 0)
    return false; //Indicates that we were booted out

  // Set default facility if none specified.
  if ((pri & LOG_FACMASK) == 0)
    pri = LOG_MAKEPRI(LOG_FAC(this->_priDefault), pri);

  // IETF Doc: https://tools.ietf.org/html/rfc5424
  // BSD Doc: https://tools.ietf.org/html/rfc3164
  this->_fh->print('<');
  this->_fh->print(pri);
  if (this->_protocol == SYSLOG_PROTO_IETF) {
    this->_fh->print(F(">1 "));
    if( NULL == this._tfunc ) {
        this->_fh->print(F(SYSLOG_NILVALUE));
    } else {
        this->_tfunc(timestampBuf, (size_t)sizeof(timestampBuf));
    }
  } else {
    this->_fh->print(F(">"));
  }
  this->_fh->print(this->_deviceHostname);
  this->_fh->print(' ');
  this->_fh->print(this->_appName);
  if (this->_protocol == SYSLOG_PROTO_IETF) {
    this->_fh->print(" ");
    this->_fh->print(procid);
    this->_fh->print(" ");
    this->_fh->print(msgid);
    this->_fh->print(" ");
  } else {
    this->_fh->print(F("[0]: "));
  }
  this->_fh->print(message);

  return true;
}

//Send structured data
inline bool Syslog::_sendSds(sdIds* sds) {
    this->_fh->print(F(" "));
    if( (NULL == sds) || (sds->empty()) ) {
        this->_fh->print(F(SYSLOG_NILVALUE));
    } else {
        for (const auto& [sdId, sdEls] : *sds) {
            if( !sdEls.empty() ) {
                this->_fh->print(F("["));
                this->_fh->print(sdId);
                for (const auto& [sdPName, sdPValue] : sdEls) {
                    this->_fh->print(F(" "));
                    this->_fh->print(sdPName);
                    this->_fh->print(F('="'));
                    this->_fh->print(sdPValue);
                    this->_fh->print(F('"'));
                }
                this->_fh->print("]");
            }
        }
    }
    this->_fh->print(F(" "));

    return(true);
}

bool Syslog::_sendLog(uint16_t pri, const char *message, sdIds* sds, const char* procid = SYSLOG_NILVALUE, const char* msgid = SYSLOG_NILVALUE) {
  bool result;

  result = _sendHeader(pri, procid, msgid);
  if( false != result ) {
    if (this->_protocol == SYSLOG_PROTO_IETF) {
        _sendSds(sds);
        if( strlen(message) != 0 ) {
            this->_fh->print(F("\xEF\xBB\xBF")); //No structured data
        }
    } else {
        this->_fh->print(F("[0]: "));
    }
    this->_fh->print(message);
  }

  return(result);
}

bool Syslog::_sendLog(uint16_t pri, const __FlashStringHelper *message, sdIds* sds, const char* procid = SYSLOG_NILVALUE, const char* msgid = SYSLOG_NILVALUE) {
    return(_sendLog(pri, (const char *)message, sdIds, procid, msgid));
}

bool Syslog::_sendLog(uint16_t pri, const char* message, const char* procid = SYSLOG_NILVALUE, const char* msgid = SYSLOG_NILVALUE) {
    return(_sendLog(pri, message, sdIds, procid, msgid, sdIds)); //Empty message
}

bool Syslog::_sendLog(uint16_t pri, const __FlashStringHelper *message, const char* procid = SYSLOG_NILVALUE, const char* msgid = SYSLOG_NILVALUE) {
    return(_sendLog(pri, (const char*)message, sdIds, procid, msgid)); //Empty message
}

bool Syslog::_sendLog(uint16_t pri, sdIds* sds, const char* procid = SYSLOG_NILVALUE, const char* msgid = SYSLOG_NILVALUE) {
    return(_sendLog(pri, F(""), sdIds, procid, msgid)); //Empty message
}
