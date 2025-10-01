# Security Policy

## Supported Versions

| Version | Supported          |
| ------- | ------------------ |
| 1.0.x   | :white_check_mark: |

## Reporting a Vulnerability

If you discover a security vulnerability in fbpp, please follow these steps:

1. **DO NOT** open a public issue
2. Email the maintainers at: [Create email address or use GitHub Security Advisories]
3. Or use [GitHub Security Advisories](https://github.com/sashok74/fbpp/security/advisories/new) (private disclosure)

### What to Include

- Description of the vulnerability
- Steps to reproduce
- Potential impact
- Suggested fix (if any)

### Response Timeline

- **Initial response**: Within 48 hours
- **Status update**: Within 7 days
- **Fix timeline**: Depends on severity
  - Critical: Within 7 days
  - High: Within 14 days
  - Medium: Within 30 days
  - Low: Next release

## Security Considerations

When using fbpp in production:

### 1. Always Use Parameterized Queries

**❌ WRONG - SQL Injection vulnerable:**
```cpp
std::string user_input = get_user_input();
std::string sql = "SELECT * FROM users WHERE name = '" + user_input + "'";
conn.Execute(sql);
```

**✅ CORRECT - Safe with parameters:**
```cpp
std::string user_input = get_user_input();
auto stmt = conn.prepareStatement("SELECT * FROM users WHERE name = ?");
stmt->executeQuery(txn, std::make_tuple(user_input));
```

### 2. Store Credentials Securely

**❌ WRONG - Hardcoded credentials:**
```cpp
Connection conn(ConnectionParams{
    .database = "localhost:mydb.fdb",
    .user = "SYSDBA",
    .password = "masterkey"  // Hardcoded!
});
```

**✅ CORRECT - Environment variables:**
```cpp
Connection conn(ConnectionParams{
    .database = std::getenv("DB_PATH"),
    .user = std::getenv("DB_USER"),
    .password = std::getenv("DB_PASSWORD")
});
```

### 3. Use SSL/TLS for Remote Connections

Configure Firebird server for encrypted connections and use appropriate connection strings.

### 4. Follow Principle of Least Privilege

- Create database users with minimal required permissions
- Don't use SYSDBA in production
- Use role-based access control

### 5. Keep Dependencies Updated

- Regularly update Firebird client library
- Update fbpp to latest version
- Monitor security advisories

### 6. Input Validation

Even with parameterized queries, validate input:

```cpp
if (user_id < 0 || user_id > MAX_ID) {
    throw std::invalid_argument("Invalid user ID");
}
```

### 7. Connection Timeout

Set reasonable connection timeouts to prevent resource exhaustion:

```cpp
// Future feature - not yet implemented
ConnectionParams params;
params.timeout = std::chrono::seconds{30};
```

## Known Security Limitations

See [PROJECT_ANALYSIS.md](doc/PROJECT_ANALYSIS.md) for:

- Current architectural limitations
- Known issues
- Planned security improvements

### Current Limitations

1. **No connection pool** - May lead to connection exhaustion in high-load scenarios
2. **No prepared statement limit** - Statement cache could grow unbounded (use cache config)
3. **BLOB handling** - Current implementation loads entire BLOB into memory

These are documented and will be addressed in future releases.

## Security Best Practices

### Application Level

- Use HTTPS for web applications
- Implement authentication and authorization
- Rate limit database operations
- Log security-relevant events
- Regular security audits

### Database Level

- Enable Firebird authentication
- Use encrypted connections
- Regular backups
- Monitor database logs
- Keep Firebird updated

### Code Level

- Code reviews for database interactions
- Static analysis (clang-tidy, cppcheck)
- Dynamic analysis (sanitizers)
- Fuzzing for input validation

## Security Checklist

Before deploying to production:

- [ ] All credentials are stored securely (not in code)
- [ ] SSL/TLS enabled for remote connections
- [ ] Database user has minimal required privileges
- [ ] All queries use parameterized statements
- [ ] Input validation implemented
- [ ] Connection timeout configured
- [ ] Error messages don't expose sensitive data
- [ ] Logging configured appropriately
- [ ] Dependencies are up to date
- [ ] Security testing completed

## Disclosure Policy

We follow [Coordinated Vulnerability Disclosure](https://vuls.cert.org/confluence/display/CVD/Executive+Summary):

1. Report received and acknowledged
2. Issue investigated and confirmed
3. Fix developed and tested
4. Security advisory prepared
5. Fix released
6. Public disclosure (after fix available)

## Attribution

We appreciate security researchers who responsibly disclose vulnerabilities. With your permission, we'll acknowledge your contribution in:

- Security advisory
- Release notes
- Hall of fame (if we create one)

## Contact

For security issues only:
- GitHub Security Advisories (preferred)
- Email: [To be determined]

For general issues:
- [GitHub Issues](https://github.com/sashok74/fbpp/issues)

---

**Note:** This security policy applies to fbpp library code. For Firebird database security, consult [Firebird documentation](https://firebirdsql.org/en/firebird-security/).
