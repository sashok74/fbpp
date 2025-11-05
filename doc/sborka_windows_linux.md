# Сборка fbpp в Linux и Windows

- **Linux**: пользуемся `./build.sh <тип>`. Скрипт ожидает Firebird в системных путях `/usr/include`, `/usr/lib` и проводит Conan + CMake сборку с автоматическим прогоном `ctest`. Примеры собираются параметром `-DBUILD_EXAMPLES=ON`.
- **Windows**: используем `powershell -ExecutionPolicy Bypass -File .\build_windows.ps1 -Configuration <тип>`. Скрипт настраивает переменные среды, ищет Firebird в `C:/fb50`, копирует `config/test_config.json`, вызывает Conan/CMake, собирает библиотеку, примеры и запускает `ctest`.
- **Общее**: исходный `FindFirebird.cmake` теперь ищет клиент как в Unix-путях, так и в типичных Windows-директориях; в коде тестов заменён `getpid()` на кроссплатформенную версию. Дополнительные шаги требуются только под Windows – Linux-поток не изменён.

