#pragma once

// Firebird header compatibility layer
// Ubuntu's firebird-dev package places headers differently than custom installations:
// - Ubuntu: /usr/include/ibase.h, /usr/include/iberror.h
// - Custom: /opt/firebird/include/firebird/ibase.h, /opt/firebird/include/firebird/iberror.h

// Firebird Interface (OO API)
#if __has_include(<firebird/Interface.h>)
#include <firebird/Interface.h>
#elif __has_include(<Interface.h>)
#include <Interface.h>
#else
#error "Firebird Interface.h not found"
#endif

// ibase.h (legacy C API types and constants)
#if __has_include(<firebird/ibase.h>)
#include <firebird/ibase.h>
#elif __has_include(<ibase.h>)
#include <ibase.h>
#else
#error "Firebird ibase.h not found"
#endif

// iberror.h (error codes)
#if __has_include(<firebird/iberror.h>)
#include <firebird/iberror.h>
#elif __has_include(<iberror.h>)
#include <iberror.h>
#endif

// sqlda_pub.h (SQL constants)
#if __has_include(<firebird/impl/sqlda_pub.h>)
#include <firebird/impl/sqlda_pub.h>
#elif __has_include(<impl/sqlda_pub.h>)
#include <impl/sqlda_pub.h>
#endif
