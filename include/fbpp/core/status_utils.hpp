#pragma once
#include <firebird/Interface.h>
#include <firebird/iberror.h>
#include <cstddef>
#include <cerrno>
#include <sstream>
#include <functional>
#include <string>

namespace fbpp {
namespace core {
namespace fb_status {

 //функция смотрит вектор статуса Firebird после attachDatabase() и отвечает, 
 // упал ли attach из-за того, что файла БД нет (или путь не существует). 
 // Если да — true, иначе false.
inline bool isDbMissingOnAttach(const Firebird::IStatus* st) {
    if (!st) return false;
    const intptr_t* v = st->getErrors();
    if (!v) return false;

    auto ieq = [](char a, char b){ return std::tolower((unsigned char)a) == std::tolower((unsigned char)b); };
    auto contains_ic = [&](std::string_view hay, std::string_view needle){
        return std::search(hay.begin(), hay.end(), needle.begin(), needle.end(), ieq) != hay.end();
    };

    bool seen_io_error = false;
    bool seen_open_err = false;     // isc_open_err
    long unix_errno    = -1;
    long win32_errno   = -1;
    bool saw_no_such_file_text = false;

    for (size_t i = 0; v[i] != isc_arg_end; ) {
        const intptr_t tag = v[i++];
        switch (tag) {
            case isc_arg_gds: {
                const intptr_t g = v[i++];
                if (g == isc_io_error) seen_io_error = true;
                // isc_open_err может не быть объявлен в одних сборках; подстрахуемся литералом.
#ifdef isc_open_err
                if (g == isc_open_err) seen_open_err = true;
#else
                if (g == 335544734)    seen_open_err = true; // известное значение для isc_open_err
#endif
                break;
            }
            case isc_arg_unix:   unix_errno  = static_cast<long>(v[i++]); break;
#ifdef _WIN32
            case isc_arg_win32:  win32_errno = static_cast<long>(v[i++]); break;
#endif
            case isc_arg_interpreted: {
                const char* s = reinterpret_cast<const char*>(v[i++]);
                if (s && contains_ic(s, "No such file or directory")) {
                    saw_no_such_file_text = true;
                }
                break;
            }
            case isc_arg_string:
            case isc_arg_number:
            case isc_arg_sql_state:
                ++i; break; // пропускаем полезные нагрузки, они нам тут не нужны
            default:
                ++i; break; // консервативно
        }
    }

    if (!seen_io_error) return false;

#ifdef _WIN32
    if (win32_errno == 2 || win32_errno == 3) return true; // FILE_NOT_FOUND / PATH_NOT_FOUND
#endif
    if (unix_errno == ENOENT || unix_errno == ENOTDIR) return true;

    // Ваш кейс: io_error + open_err без OS-кодов
    if (seen_open_err
#ifdef _WIN32
        && win32_errno == -1
#endif
        && unix_errno == -1)
    {
        return true;
    }

    // Запасной вариант — интерпретированная строка от движка (без локализации)
    if (saw_no_such_file_text) return true;

    return false;
}


inline const char* tagName(intptr_t tag) {
    switch (tag) {
        case isc_arg_gds:         return "gds";
        case isc_arg_string:      return "string";
        case isc_arg_cstring:     return "cstring";
        case isc_arg_number:      return "number";
        case isc_arg_interpreted: return "interpreted";
        case isc_arg_vms:         return "vms";
        case isc_arg_unix:        return "unix";
        case isc_arg_win32:       return "win32";
        case isc_arg_sql_state:   return "sqlstate";
        case isc_arg_end:         return "end";
        default:                  return "?";
    }
}

// Что в статусе.
inline void dumpStatusVector(const Firebird::IStatus* st,
                             const std::function<void(const std::string&)>& log)
{
    std::ostringstream os;
    if (!st) { log("IStatus=null"); return; }
    const intptr_t* v = st->getErrors();
    if (!v) { log("status->getErrors() = null"); return; }

    os << "Status vector dump:";
    for (size_t i = 0; v[i] != isc_arg_end; ) {
        const intptr_t tag = v[i++];
        os << "\n  tag=" << tagName(tag) << " (" << tag << ")";

        // почти все теги несут одно «значение» следом
        switch (tag) {
            case isc_arg_gds:
            case isc_arg_unix:
#ifdef _WIN32
            case isc_arg_win32:
#endif
            case isc_arg_number: {
                const intptr_t val = v[i++];
                os << " val=" << val;
                break;
            }
            case isc_arg_string:
            case isc_arg_interpreted:
            case isc_arg_sql_state: {
                const char* s = reinterpret_cast<const char*>(v[i++]);
                os << " val=\"" << (s ? s : "") << "\"";
                break;
            }
            default:
                // консервативно съедаем один элемент
                os << " (skip payload)";
                ++i;
                break;
        }
    }
    log(os.str());
}


} // namespace fb_status
} // namespace core
} // namespace fbpp
