# Памятка программиста: Полная система типов Firebird API

## 📋 Быстрый справочник

### Иерархия типов Firebird

```
Firebird Types
├── Базовые типы (SQL_xxx константы 4xx-5xx)
│   ├── Числовые
│   │   ├── Целые: SHORT(16), LONG(32), INT64(64)
│   │   ├── Плавающие: FLOAT(32), DOUBLE(64)
│   │   └── Масштабированные: NUMERIC/DECIMAL (через scale)
│   ├── Строковые
│   │   ├── SQL_TEXT (фиксированная длина)
│   │   └── SQL_VARYING (переменная длина)
│   ├── Дата/Время
│   │   ├── SQL_TYPE_DATE
│   │   ├── SQL_TYPE_TIME
│   │   └── SQL_TIMESTAMP
│   └── Сложные
│       ├── SQL_BLOB (с подтипами)
│       └── SQL_ARRAY
└── Расширенные типы (SQL_xxx константы 32xxx, Firebird 4.0+)
    ├── SQL_BOOLEAN
    ├── SQL_INT128
    ├── SQL_DEC16 (DECFLOAT16)
    ├── SQL_DEC34 (DECFLOAT34)
    └── Timezone типы (TIME_TZ, TIMESTAMP_TZ, и _EX варианты)
```

## 🔢 Полная таблица SQL типов

### Базовые типы
| Константа | Значение | Тип | Размер | Описание |
|-----------|----------|-----|--------|----------|
| `SQL_TEXT` | 452 | CHAR | N байт | Фиксированная строка |
| `SQL_VARYING` | 448 | VARCHAR | 2+N байт | Переменная строка |
| `SQL_SHORT` | 500 | SMALLINT | 2 байта | 16-битное целое |
| `SQL_LONG` | 496 | INTEGER | 4 байта | 32-битное целое |
| `SQL_INT64` | 580 | BIGINT | 8 байт | 64-битное целое |
| `SQL_FLOAT` | 482 | FLOAT | 4 байта | Одинарная точность |
| `SQL_DOUBLE` | 480 | DOUBLE | 8 байт | Двойная точность |
| `SQL_TIMESTAMP` | 510 | TIMESTAMP | 8 байт | Дата + время |
| `SQL_TYPE_DATE` | 570 | DATE | 4 байта | Только дата |
| `SQL_TYPE_TIME` | 560 | TIME | 4 байта | Только время |
| `SQL_BLOB` | 520 | BLOB | 8 байт | ID блоба |
| `SQL_ARRAY` | 540 | ARRAY | 8 байт | ID массива |

### Расширенные типы (Firebird 4.0+)
| Константа | Значение | Тип | Размер | Описание |
|-----------|----------|-----|--------|----------|
| `SQL_BOOLEAN` | 32764 | BOOLEAN | 1 байт | true/false/null |
| `SQL_INT128` | 32752 | INT128 | 16 байт | 128-битное целое |
| `SQL_DEC16` | 32760 | DECFLOAT(16) | 8 байт | Десятичное 16 цифр |
| `SQL_DEC34` | 32762 | DECFLOAT(34) | 16 байт | Десятичное 34 цифры |
| `SQL_TIME_TZ` | 32756 | TIME WITH TZ | 6 байт | Время с зоной |
| `SQL_TIMESTAMP_TZ` | 32754 | TIMESTAMP WITH TZ | 10 байт | Метка с зоной |
| `SQL_TIME_TZ_EX` | 32750 | TIME WITH TZ EX | 8 байт | Расширенное |
| `SQL_TIMESTAMP_TZ_EX` | 32748 | TIMESTAMP WITH TZ EX | 12 байт | Расширенное |

## 🏷️ Система подтипов

### Подтипы текстовых полей (`sqlsubtype`)
```cpp
// Для SQL_TEXT и SQL_VARYING
CHARSET_ID charset = sqlvar->sqlsubtype & 0xFF;        // Младший байт
COLLATION_ID collation = (sqlvar->sqlsubtype >> 8) & 0xFF; // Старший байт
```

### Подтипы BLOB (`sqlsubtype` для SQL_BLOB)
```cpp
#define isc_blob_untyped  0  // Бинарные данные
#define isc_blob_text     1  // Текстовые данные (charset в sqlscale)
#define isc_blob_blr      2  // BLR код
#define isc_blob_acl      3  // Списки доступа
// ... и другие предопределенные подтипы до 10
```

### Подтипы числовых типов (`sqlsubtype` для целых)
```cpp
#define dsc_num_type_none     0  // Обычное целое
#define dsc_num_type_numeric  1  // NUMERIC(p,s)
#define dsc_num_type_decimal  2  // DECIMAL(p,s)
```

## 🔧 Работа с типами через API

### 1. Правильная проверка типа

```cpp
// ⚠️ ВАЖНО: Бит 0 в sqltype - флаг NULL
ISC_SHORT base_type = sqlvar->sqltype & ~1;  // Убираем флаг NULL
bool nullable = (sqlvar->sqltype & 1) != 0;  // Проверяем nullable

// Проверка на NULL
bool is_null = sqlvar->sqlind && (*sqlvar->sqlind == -1);

// Безопасная обработка
if (!is_null) {
    switch (base_type) {
        case SQL_SHORT:
            // Обработка SMALLINT
            break;
        case SQL_LONG:
            // Обработка INTEGER
            break;
        // ...
    }
}
```

### 2. Работа с масштабированными числами

```cpp
// NUMERIC/DECIMAL хранятся как целые со scale < 0
if ((base_type == SQL_SHORT || base_type == SQL_LONG || 
     base_type == SQL_INT64 || base_type == SQL_INT128) && 
    sqlvar->sqlscale < 0) {
    
    // Пример для NUMERIC(10,2): значение 123.45 хранится как 12345
    SINT64 raw_value = /* получить из sqldata */;
    double actual = raw_value / pow(10.0, abs(sqlvar->sqlscale));
}
```

### 3. Работа со строками

```cpp
// VARCHAR - переменная длина
if (base_type == SQL_VARYING) {
    ISC_USHORT len = *(ISC_USHORT*)sqlvar->sqldata;
    char* text = (char*)(sqlvar->sqldata + sizeof(ISC_USHORT));
    // Использовать len байт из text
}

// CHAR - фиксированная длина
if (base_type == SQL_TEXT) {
    char* text = (char*)sqlvar->sqldata;
    ISC_SHORT len = sqlvar->sqllen;
    // text содержит len байт, дополнено пробелами
}
```

### 4. Работа с расширенными типами

```cpp
// Получение утилит для расширенных типов
Firebird::IUtil* util = master->getUtilInterface();

// INT128
if (base_type == SQL_INT128) {
    FB_I128* value = (FB_I128*)sqlvar->sqldata;
    Firebird::IInt128* i128 = util->getInt128(status);
    
    char buffer[46];
    i128->toString(status, value, sqlvar->sqlscale, sizeof(buffer), buffer);
}

// DECFLOAT(16)
if (base_type == SQL_DEC16) {
    FB_DEC16* value = (FB_DEC16*)sqlvar->sqldata;
    Firebird::IDecFloat16* df16 = util->getDecFloat16(status);
    
    char buffer[24];
    df16->toString(status, value, sizeof(buffer), buffer);
}

// TIME WITH TIME ZONE
if (base_type == SQL_TIME_TZ) {
    ISC_TIME_TZ* value = (ISC_TIME_TZ*)sqlvar->sqldata;
    unsigned h, m, s, f;
    char tz[64];
    util->decodeTimeTz(status, value, &h, &m, &s, &f, sizeof(tz), tz);
}
```

## 📊 Структура XSQLVAR

```cpp
typedef struct {
    ISC_SHORT   sqltype;     // Тип с флагом NULL в бите 0
    ISC_SHORT   sqlscale;    // Scale для чисел, charset для BLOB
    ISC_SHORT   sqlsubtype;  // Подтип (charset для текста, subtype для BLOB)
    ISC_SHORT   sqllen;      // Длина данных
    ISC_SCHAR*  sqldata;     // Указатель на данные
    ISC_SHORT*  sqlind;      // Указатель на индикатор NULL
    // ... имена полей
} XSQLVAR;
```

## ⚡ Критически важные правила

### ✅ Правильно
```cpp
// 1. Всегда убирать флаг NULL при проверке типа
if ((sqlvar->sqltype & ~1) == SQL_LONG) { }

// 2. Всегда проверять NULL перед доступом к данным
if (sqlvar->sqlind && *sqlvar->sqlind == -1) {
    // NULL значение
} else {
    // Безопасный доступ к sqldata
}

// 3. Учитывать scale для числовых типов
if (sqlvar->sqlscale < 0) {
    // Десятичное число
}

// 4. Правильно работать с VARCHAR
ISC_USHORT len = *(ISC_USHORT*)sqlvar->sqldata;
char* data = sqlvar->sqldata + sizeof(ISC_USHORT);
```

### ❌ Неправильно
```cpp
// 1. Не учитывает флаг NULL
if (sqlvar->sqltype == SQL_LONG) { }  // ОШИБКА!

// 2. Не проверяет NULL
int value = *(int*)sqlvar->sqldata;  // Может упасть!

// 3. Игнорирует scale
int value = *(int*)sqlvar->sqldata;  // Потеря дробной части!

// 4. Неправильная работа с VARCHAR
memcpy(buf, sqlvar->sqldata, sqlvar->sqllen);  // Копирует длину!
```

## 🔄 Матрица преобразований типов

| Из ↓ В → | Числа | Строки | Даты | BLOB |
|----------|-------|--------|------|------|
| **Числа** | ✅ Авто | ✅ Формат | ❌ | ❌ |
| **Строки** | ✅ Парсинг | ✅ | ✅ Парсинг | ⚠️ TEXT BLOB |
| **Даты** | ❌ | ✅ Формат | ✅ Между типами | ❌ |
| **BLOB** | ❌ | ⚠️ TEXT BLOB | ❌ | ✅ |

## 📝 Примеры полной обработки

### Универсальная функция обработки типа
```cpp
void processField(XSQLVAR* var, Firebird::IUtil* util) {
    // 1. Извлекаем базовый тип
    ISC_SHORT type = var->sqltype & ~1;
    
    // 2. Проверяем NULL
    if (var->sqlind && *var->sqlind == -1) {
        printf("NULL\n");
        return;
    }
    
    // 3. Обрабатываем по типу
    switch (type) {
        case SQL_SHORT:
        case SQL_LONG:
        case SQL_INT64: {
            SINT64 value = 0;
            switch (type) {
                case SQL_SHORT: value = *(ISC_SHORT*)var->sqldata; break;
                case SQL_LONG:  value = *(ISC_LONG*)var->sqldata; break;
                case SQL_INT64: value = *(ISC_INT64*)var->sqldata; break;
            }
            
            if (var->sqlscale < 0) {
                // Десятичное число
                double decimal = value / pow(10.0, abs(var->sqlscale));
                printf("DECIMAL: %f\n", decimal);
            } else {
                // Целое число
                printf("INTEGER: %lld\n", value);
            }
            break;
        }
        
        case SQL_VARYING: {
            ISC_USHORT len = *(ISC_USHORT*)var->sqldata;
            char* text = var->sqldata + sizeof(ISC_USHORT);
            printf("VARCHAR(%d): %.*s\n", len, len, text);
            break;
        }
        
        case SQL_TEXT: {
            // Убираем пробелы справа
            char* text = var->sqldata;
            int len = var->sqllen;
            while (len > 0 && text[len-1] == ' ') len--;
            printf("CHAR: %.*s\n", len, text);
            break;
        }
        
        case SQL_BLOB: {
            ISC_QUAD* blob_id = (ISC_QUAD*)var->sqldata;
            printf("BLOB ID: %08X:%08X (subtype=%d)\n", 
                   blob_id->gds_quad_high, blob_id->gds_quad_low, 
                   var->sqlsubtype);
            break;
        }
        
        // Расширенные типы (Firebird 4.0+)
        case SQL_INT128: {
            FB_I128* value = (FB_I128*)var->sqldata;
            Firebird::IInt128* i128 = util->getInt128(status);
            char buffer[46];
            i128->toString(status, value, var->sqlscale, sizeof(buffer), buffer);
            printf("INT128: %s\n", buffer);
            break;
        }
        
        default:
            printf("Type %d not handled\n", type);
    }
}
```

## 🚀 Быстрый старт: минимальный код

```cpp
// Подготовка SQLDA
XSQLDA* sqlda = (XSQLDA*)malloc(XSQLDA_LENGTH(10));
sqlda->version = SQLDA_VERSION1;
sqlda->sqln = 10;

// Выполнение запроса
isc_dsql_describe(status, &stmt, 1, sqlda);
isc_dsql_execute(status, &trans, &stmt, 1, NULL);

// Обработка результатов
while (isc_dsql_fetch(status, &stmt, 1, sqlda) == 0) {
    for (int i = 0; i < sqlda->sqld; i++) {
        XSQLVAR* var = &sqlda->sqlvar[i];
        processField(var, util);  // Наша функция выше
    }
}
```

## 🎯 Дополнение: Работа с конкретными типами

### Работа с NUMERIC/DECIMAL через INT128
```cpp
// NUMERIC(38,2) хранится как INT128 со scale=-2
if (base_type == SQL_INT128 && var->sqlscale < 0) {
    FB_I128* raw = (FB_I128*)var->sqldata;
    Firebird::IInt128* i128 = util->getInt128(status);
    
    // Преобразование в строку с учетом scale
    char buffer[46];
    i128->toString(status, raw, var->sqlscale, sizeof(buffer), buffer);
    // buffer содержит "12345.67" для значения 1234567 при scale=-2
}
```

### Работа с DECFLOAT (десятичная плавающая точка)
```cpp
// DECFLOAT(16) - 16 десятичных цифр, 8 байт
if (base_type == SQL_DEC16) {
    FB_DEC16* value = (FB_DEC16*)var->sqldata;
    Firebird::IDecFloat16* df16 = util->getDecFloat16(status);
    
    // В строку
    char str[24];
    df16->toString(status, value, sizeof(str), str);
    
    // Из строки
    FB_DEC16 newValue;
    df16->fromString(status, "123.456789012345", &newValue);
}

// DECFLOAT(34) - 34 десятичные цифры, 16 байт
if (base_type == SQL_DEC34) {
    FB_DEC34* value = (FB_DEC34*)var->sqldata;
    Firebird::IDecFloat34* df34 = util->getDecFloat34(status);
    
    char str[43];
    df34->toString(status, value, sizeof(str), str);
}
```

### Работа с датой/временем с часовыми поясами
```cpp
// TIME WITH TIME ZONE
if (base_type == SQL_TIME_TZ) {
    ISC_TIME_TZ* timeTz = (ISC_TIME_TZ*)var->sqldata;
    
    // Декодирование
    unsigned hours, minutes, seconds, fractions;
    char timezone[64];
    util->decodeTimeTz(status, timeTz, &hours, &minutes, &seconds, 
                      &fractions, sizeof(timezone), timezone);
    
    // Кодирование
    ISC_TIME_TZ newTime;
    util->encodeTimeTz(status, &newTime, 14, 30, 45, 5000, "Europe/Moscow");
}

// TIMESTAMP WITH TIME ZONE
if (base_type == SQL_TIMESTAMP_TZ) {
    ISC_TIMESTAMP_TZ* tsTz = (ISC_TIMESTAMP_TZ*)var->sqldata;
    
    unsigned year, month, day, hours, minutes, seconds, fractions;
    char timezone[64];
    util->decodeTimeStampTz(status, tsTz, &year, &month, &day,
                           &hours, &minutes, &seconds, &fractions,
                           sizeof(timezone), timezone);
}
```

### Работа с BLOB
```cpp
if (base_type == SQL_BLOB) {
    ISC_QUAD* blob_id = (ISC_QUAD*)var->sqldata;
    
    // Проверка подтипа
    if (var->sqlsubtype == isc_blob_text) {
        // Текстовый BLOB
        // Charset в var->sqlscale для текстовых BLOB
        int charset = var->sqlscale;
        
        // Открытие BLOB для чтения
        isc_blob_handle blob = 0;
        isc_open_blob2(status, &db, &trans, &blob, blob_id, 0, NULL);
        
        // Чтение данных
        char buffer[1024];
        ISC_USHORT actual_len;
        while (isc_get_segment(status, &blob, &actual_len, 
                               sizeof(buffer), buffer) == 0) {
            // Обработка сегмента
        }
        
        isc_close_blob(status, &blob);
    }
}
```

### Обработка массивов
```cpp
if (base_type == SQL_ARRAY) {
    ISC_QUAD* array_id = (ISC_QUAD*)var->sqldata;
    
    // Получение дескриптора массива
    ISC_ARRAY_DESC desc;
    isc_array_lookup_desc(status, &db, &trans, 
                         var->relname, var->sqlname, &desc);
    
    // Чтение массива
    void* array_buffer = malloc(desc.array_desc_length);
    ISC_LONG slice_length;
    isc_array_get_slice(status, &db, &trans, array_id,
                        &desc, array_buffer, &slice_length);
    
    // Обработка элементов массива...
    
    free(array_buffer);
}
```

## 📚 Полезные макросы и функции

```cpp
// Макросы для работы с SQLDA
#define XSQLDA_LENGTH(n) (sizeof(XSQLDA) + ((n)-1) * sizeof(XSQLVAR))

// Проверка версии для расширенных типов
bool supportsExtendedTypes(Firebird::IUtil* util) {
    unsigned version = util->getClientVersion();
    return version >= (4 << 8);  // Firebird 4.0+
}

// Получение читаемого имени типа
const char* getTypeName(ISC_SHORT sqltype) {
    switch (sqltype & ~1) {
        case SQL_TEXT: return "CHAR";
        case SQL_VARYING: return "VARCHAR";
        case SQL_SHORT: return "SMALLINT";
        case SQL_LONG: return "INTEGER";
        case SQL_INT64: return "BIGINT";
        case SQL_INT128: return "INT128";
        case SQL_FLOAT: return "FLOAT";
        case SQL_DOUBLE: return "DOUBLE";
        case SQL_DEC16: return "DECFLOAT(16)";
        case SQL_DEC34: return "DECFLOAT(34)";
        case SQL_TIMESTAMP: return "TIMESTAMP";
        case SQL_TYPE_DATE: return "DATE";
        case SQL_TYPE_TIME: return "TIME";
        case SQL_TIMESTAMP_TZ: return "TIMESTAMP WITH TIME ZONE";
        case SQL_TIME_TZ: return "TIME WITH TIME ZONE";
        case SQL_BLOB: return "BLOB";
        case SQL_ARRAY: return "ARRAY";
        case SQL_BOOLEAN: return "BOOLEAN";
        default: return "UNKNOWN";
    }
}
```

---

*Эта памятка основана на анализе исходного кода Firebird и покрывает полную систему типов, включая базовые и расширенные типы, подтипы, и практические примеры использования через API.*