-- Test schema for query_generator with TTMath adapter support
-- This schema covers all TTMath adapter scenarios

RECREATE TABLE TTMATH_TEST (
    -- Pure INT128 (no scale)
    F_INT128_PURE INT128 NOT NULL,
    F_INT128_NULLABLE INT128,

    -- NUMERIC(38, x) with various scales (stored as INT128)
    F_NUMERIC38_2 NUMERIC(38, 2) NOT NULL,  -- Scale -2 (money)
    F_NUMERIC38_4 NUMERIC(38, 4),           -- Scale -4 (extended precision)
    F_NUMERIC38_8 NUMERIC(38, 8) NOT NULL,  -- Scale -8 (high precision)
    F_NUMERIC38_18 NUMERIC(38, 18),         -- Scale -18 (very high precision)

    -- NUMERIC(18, x) with scale (stored as INT64)
    F_NUMERIC18_2 NUMERIC(18, 2) NOT NULL,  -- Scale -2
    F_NUMERIC18_6 NUMERIC(18, 6),           -- Scale -6

    -- Regular types for context
    F_ID INTEGER NOT NULL PRIMARY KEY,
    F_NAME VARCHAR(100),
    F_TIMESTAMP TIMESTAMP DEFAULT 'NOW'
);

COMMIT;
