# Сборка fbpp в Linux и Windows

- **Linux**: пользуемся `./build.sh <тип>`. Скрипт ожидает Firebird в системных путях `/usr/include`, `/usr/lib` и проводит Conan + CMake сборку с автоматическим прогоном `ctest`. Примеры собираются параметром `-DBUILD_EXAMPLES=ON`.
- **Windows**: используем `powershell -ExecutionPolicy Bypass -File .\build_windows.ps1 -Configuration <тип>`. Скрипт настраивает переменные среды, ищет Firebird в `C:/fb50`, копирует `config/test_config.json`, вызывает Conan/CMake, собирает библиотеку, примеры и запускает `ctest`.
- **Общее**: исходный `FindFirebird.cmake` теперь ищет клиент как в Unix-путях, так и в типичных Windows-директориях; в коде тестов заменён `getpid()` на кроссплатформенную версию. Дополнительные шаги требуются только под Windows – Linux-поток не изменён.

## Основные «косяки» Windows-сборки

1. `QueryGeneratorTest.GeneratesHeadersForSelect` не находил `query_generator.exe`, потому что путь собирался без расширения (как в Unix). Решение: добавлять `.exe` внутри `#ifdef _WIN32`, прежде чем проверять существование файла или запускать утилиту.
2. `test_config` вызывал POSIX-функции `setenv`/`unsetenv`, которых нет в MSVC. Исправление: обёртки с `_putenv_s` и очисткой переменных через пустое значение.
3. `test_firebird_exception` подключал `firebird/iberror.h`, хотя стандартная Windows-установка Firebird кладёт `iberror.h` в корень `C:\fb50\include`. Нужна условная директива подключения для Windows, чтобы использовать корректный путь.
4. Цели из `examples/` тянут POSIX-заголовки (`arpa/inet.h`), `pthread` и типы вроде `__int128`, поэтому полная сборка `build_windows.ps1` валится, когда включены `-DBUILD_EXAMPLES=ON`. До добавления Windows-аналогов примеров проще отключить (`BUILD_EXAMPLES=OFF`) либо оградить их в CMake `if(NOT WIN32)`.

### Распространённый подход к решению

Самый рабочий путь — сразу оборачивать POSIX-зависимости в `#ifdef _WIN32`, предоставлять локальные помощники для особенностей MSVC (например, `_putenv_s`, WinSock вместо `arpa/inet.h`), а спорные цели отключать или заменять на Windows-аналоги при конфигурации (`BUILD_EXAMPLES=OFF`, `if(WIN32)` в CMake). Такой подход даёт возможность собирать и тестировать библиотеку на Windows, постепенно устраняя POSIX-специфичные участки по мере необходимости.
