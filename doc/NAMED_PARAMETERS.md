# Named Parameters Support in FBPP

## Overview

FBPP now supports named parameters in SQL queries, allowing you to use `:param_name` or `@param_name` syntax instead of positional `?` placeholders. This feature makes queries more readable and maintainable, especially for complex queries with many parameters.

## Features

- **Two syntax styles**: `:param_name` (Oracle-style) and `@param_name` (SQL Server-style)
- **Case-insensitive parameter names**: `:UserId`, `:userid`, and `:USERID` all refer to the same parameter
- **Parameter reuse**: The same parameter can be used multiple times in a query
- **NULL handling**: Unspecified parameters default to NULL
- **Transparent caching**: The original SQL with named parameters is used as the cache key
- **JSON integration**: Works seamlessly with JSON objects for parameter values

## Usage Examples

### Basic Usage with JSON

```cpp
// Prepare statement with named parameters
auto stmt = connection->prepareStatement(
    "INSERT INTO users (id, name, email) VALUES (:id, :name, :email)"
);

// Use JSON object with parameter names as keys
json params;
params["id"] = 1;
params["name"] = "John Doe";
params["email"] = "john@example.com";

// Execute with named parameters
transaction->execute(stmt, params);
```

### Using @ Prefix

```cpp
auto stmt = connection->prepareStatement(
    "SELECT * FROM users WHERE status = @status AND age > @min_age"
);

json params;
params["status"] = "active";
params["min_age"] = 18;

auto cursor = transaction->openCursor(stmt, params);
```

### Repeated Parameters

A single parameter can be used multiple times in a query:

```cpp
auto stmt = connection->prepareStatement(
    "SELECT * FROM orders WHERE user_id = :user_id OR manager_id = :user_id"
);

json params;
params["user_id"] = 42;  // Will be used for both occurrences
```

### Case-Insensitive Parameters

Parameter names are case-insensitive:

```cpp
auto stmt = connection->prepareStatement(
    "SELECT * FROM users WHERE id = :UserId"
);

json params;
params["userid"] = 123;  // Works - different case
```

### NULL Parameters

Unspecified parameters default to NULL:

```cpp
auto stmt = connection->prepareStatement(
    "INSERT INTO users (id, name, email) VALUES (:id, :name, :email)"
);

json params;
params["id"] = 1;
params["name"] = "Jane";
// email not specified - will be NULL
```

## Implementation Details

### How It Works

1. **Parsing**: When a statement is prepared, the SQL is parsed to identify named parameters
2. **Conversion**: Named parameters are converted to positional `?` placeholders for Firebird
3. **Mapping**: A mapping from parameter names to positions is maintained
4. **JSON Conversion**: When executing with JSON, the named values are mapped to positional array
5. **Caching**: The original SQL with named parameters is used as the cache key

### Parser Features

The named parameter parser handles:
- String literals (parameters inside strings are ignored)
- Comments (both `--` single-line and `/* */` multi-line)
- Mixed named and positional parameters
- Parameter names with letters, numbers, and underscores

### Performance

- **Zero overhead for positional parameters**: If no named parameters are detected, the original SQL is used directly
- **Minimal parsing overhead**: The parser is optimized for single-pass parsing
- **Cache-friendly**: Named parameter SQL is used as cache key, improving cache hit rate

## API Reference

### Statement Methods

```cpp
// Check if statement has named parameters
bool hasNamedParameters() const;

// Get the parameter name to position mapping
const std::unordered_map<std::string, std::vector<size_t>>&
    getNamedParamMapping() const;
```

### Internal Classes

- **`NamedParamParser`**: Parses SQL and extracts named parameters
- **`NamedParamHelper`**: Converts JSON objects to positional arrays based on parameter mapping

## Migration Guide

### From Positional Parameters

Before:
```cpp
auto stmt = connection->prepareStatement(
    "INSERT INTO users (id, name, email) VALUES (?, ?, ?)"
);

json params = json::array({1, "John", "john@example.com"});
transaction->execute(stmt, params);
```

After:
```cpp
auto stmt = connection->prepareStatement(
    "INSERT INTO users (id, name, email) VALUES (:id, :name, :email)"
);

json params;
params["id"] = 1;
params["name"] = "John";
params["email"] = "john@example.com";
transaction->execute(stmt, params);
```

### Maintaining Backward Compatibility

Positional parameters continue to work as before. You can mix both approaches in the same application:

```cpp
// Still works with positional parameters
auto stmt1 = connection->prepareStatement("SELECT * FROM users WHERE id = ?");
json params1 = json::array({123});

// Also works with named parameters
auto stmt2 = connection->prepareStatement("SELECT * FROM users WHERE id = :id");
json params2 = json::object({{"id", 123}});
```

## Best Practices

1. **Use meaningful parameter names**: `:user_id` is better than `:p1`
2. **Be consistent with naming**: Stick to one style (`:param` or `@param`) within a project
3. **Document parameter expectations**: Especially for complex queries with many parameters
4. **Consider NULL handling**: Remember that unspecified parameters default to NULL
5. **Use parameter reuse**: Don't duplicate parameter values when the same value is needed multiple times

## Limitations

- **Firebird native support**: Firebird database doesn't natively support named parameters; FBPP provides this as a client-side feature
- **Tuple parameters**: Named parameters only work with JSON objects, not with tuples
- **Performance**: There's a small overhead for parsing SQL with named parameters (typically < 1ms)

## Examples

See `examples/06_named_parameters.cpp` for a complete working example demonstrating all features of named parameter support.