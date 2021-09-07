#include "Syslog.h"
#include <cstdio>
#include <cstdarg>
#include <Arduino.h>


// Public Methods //////////////////////////////////////////////////////////////

Syslog::Syslog(Stream **fhs, uint8_t num_fh, timestampFunc tfunc, const char* hostname, const char* appName, uint16_t logLevel, uint16_t priDefault, uint8_t protocol) {
  this->_fhs = fhs;
  this->_fhl = num_fh; //Only one file handle
  this->_protocol = protocol;
  this->_deviceHostname = hostname;
  this->_appName = (appName == NULL) ? SYSLOG_NILVALUE : appName;
  this->_priDefault = priDefault;
  this->_tfunc = tfunc;
  this->setLogLevel(logLevel);
}

Syslog::Syslog(Stream **fh, timestampFunc tfunc, const char* hostname, const char* appName, uint16_t logLevel, uint16_t priDefault, uint8_t protocol) {
    Syslog(fh, (uint8_t)1, tfunc, hostname, appName, logLevel, priDefault, protocol);
}

Syslog &Syslog::timeStampFunc(timestampFunc tfunc) {
  this->_tfunc = tfunc;
  return *this;
}

Syslog &Syslog::includeBOM(bool _include) {
  this->_includeBOM = _include;
  return *this;
}

Syslog &Syslog::hostname(const char* deviceHostname) {
  this->_deviceHostname = (deviceHostname == NULL) ? SYSLOG_NILVALUE : deviceHostname;
  return *this;
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
  mask = LOG_MASK(level);
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

bool Syslog::log(uint16_t pri, sdIds* sds, const char* message) {
  return this->_sendLog(pri, sds, message, SYSLOG_NILVALUE, SYSLOG_NILVALUE);
}

bool Syslog::log(uint16_t pri, sdIds* sds, const __FlashStringHelper *message) {
  return this->_sendLog(pri, sds, (const char*)message, SYSLOG_NILVALUE, SYSLOG_NILVALUE);
}

bool Syslog::log(uint16_t pri, sdIds* sds) {
  return this->_sendLog(pri, sds);
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
  Stream* _fh;
  char timestampBuf[40];

  // Check priority against priMask values.
  if ((LOG_MASK(LOG_PRI(pri)) & this->_priMask) == 0) {
    return false; //Indicates that we were booted out
  }

  // Set default facility if none specified.
  if ((pri & LOG_FACMASK) == 0)
    pri = LOG_MAKEPRI(LOG_FAC(this->_priDefault), pri);

  // IETF Doc: https://tools.ietf.org/html/rfc5424
  // BSD Doc: https://tools.ietf.org/html/rfc3164
  for( int i = 0; i < this->_fhl; i++ ) {
    _fh = this->_fhs[i];
    _fh->print('<');
    _fh->print(pri);
    if (this->_protocol == SYSLOG_PROTO_IETF) {
        _fh->print(F(">1 "));
        if( NULL == this->_tfunc ) {
            _fh->print(F(SYSLOG_NILVALUE));
        } else {
            this->_tfunc(timestampBuf, (size_t)sizeof(timestampBuf));
        }
    } else {
        _fh->print(F(">"));
    }
    _fh->print(' ');
    _fh->print(this->_deviceHostname);
    _fh->print(' ');
    _fh->print(this->_appName);
    if (this->_protocol == SYSLOG_PROTO_IETF) {
        _fh->print(" ");
        _fh->print(procid);
        _fh->print(" ");
        _fh->print(msgid);
        _fh->print(" ");
    } else {
        _fh->print(F("[0]: "));
    }
  }

  return true;
}

//Send structured data
inline bool Syslog::_sendSds(sdIds* sds) {
    Stream* _fh;

    for( int i = 0; i < this->_fhl; i++ ) {
        _fh = this->_fhs[i];
        if( (NULL == sds) || (sds->empty()) ) {
            _fh->print(F(SYSLOG_NILVALUE));
        } else {
            for (const auto& [sdId, sdEls] : *sds) {
                if( !sdEls.empty() ) {
                    _fh->print(F("["));
                    _fh->print(sdId);
                    for (const auto& [sdPName, sdPValue] : sdEls) {
                        _fh->print(F(" "));
                        _fh->print(sdPName);
                        _fh->print(F("=\""));
                        _fh->print(sdPValue);
                        _fh->print(F("\""));
                    }
                    _fh->print("]");
                }
            }
        }
        _fh->print(F(" "));
    }

    return(true);
}

bool Syslog::_sendLog(uint16_t pri, sdIds* sds, const char *message, const char* procid, const char* msgid) {
  bool result;
  Stream* _fh;

  result = _sendHeader(pri, procid, msgid);
  if( false != result ) {
    if (this->_protocol == SYSLOG_PROTO_IETF) {
        _sendSds(sds);
        if( (this->_includeBOM == true) && (strlen(message) != 0) ) {
            for( int i = 0; i < this->_fhl; i++ ) {
                _fh = this->_fhs[i];
                _fh->print(F("\xEF\xBB\xBF")); //No structured data
            }
        }
    }
    for( int i = 0; i < this->_fhl; i++ ) {
        _fh = this->_fhs[i];
        _fh->print(message);
        _fh->flush();
    }
  }

  return(result);
}

bool Syslog::_sendLog(uint16_t pri, sdIds* sds, const __FlashStringHelper *message, const char* procid, const char* msgid) {
    return(_sendLog(pri, sds, (const char *)message, procid, msgid));
}

bool Syslog::_sendLog(uint16_t pri, const char* message, const char* procid, const char* msgid) {
    return(_sendLog(pri, NULL, message, procid, msgid)); //Empty message
}

bool Syslog::_sendLog(uint16_t pri, const __FlashStringHelper *message, const char* procid, const char* msgid) {
    return(_sendLog(pri, NULL, (const char*)message, procid, msgid)); //Empty message
}

bool Syslog::_sendLog(uint16_t pri, sdIds* sds, const char* procid, const char* msgid) {
    return(_sendLog(pri, sds, F(""), procid, msgid)); //Empty message
}

bool Syslog::_sendLog(uint16_t pri, const __FlashStringHelper *message) {
    return(_sendLog(pri, NULL, message, SYSLOG_NILVALUE, SYSLOG_NILVALUE)); //Empty message
}

bool Syslog::_sendLog(uint16_t pri, const char* message) {
    return(_sendLog(pri, NULL, message, SYSLOG_NILVALUE, SYSLOG_NILVALUE)); //Empty message
}

bool Syslog::_sendLog(uint16_t pri, sdIds* sds) {
    return(_sendLog(pri, sds, F(""), SYSLOG_NILVALUE, SYSLOG_NILVALUE)); //Empty message
}
