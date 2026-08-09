// Connection handling for the SQL Server instance backing the poll board and analytics.
// The tables themselves are created by migrate.mjs — nothing here does DDL.
import sql from 'mssql';

export { sql };

export const DATABASE = process.env.MSSQL_DATABASE || 'manafoundry';

function baseConfig(database) {
  const password = process.env.MSSQL_PASSWORD;
  if (!password) throw new Error('MSSQL_PASSWORD is not set');
  return {
    server: process.env.MSSQL_HOST || 'mssql',
    port: Number(process.env.MSSQL_PORT || 1433),
    user: process.env.MSSQL_USER || 'sa',
    password,
    database,
    // Local-only container-to-container traffic against SQL Server's self-signed cert.
    options: { encrypt: false, trustServerCertificate: true, appName: 'manafoundry-mock-api' },
    pool: { max: 10, min: 0, idleTimeoutMillis: 30000 },
  };
}

/**
 * SQL Server accepts TCP connections a good while before it will accept logins, and on
 * Apple Silicon it's running emulated, so first start is slow. Retry rather than crash-loop.
 */
export async function connect(database = DATABASE, { retries = 60, delayMs = 2000 } = {}) {
  let lastErr;
  for (let attempt = 1; attempt <= retries; attempt++) {
    try {
      return await new sql.ConnectionPool(baseConfig(database)).connect();
    } catch (err) {
      lastErr = err;
      if (attempt === 1 || attempt % 5 === 0) {
        console.log(`waiting for SQL Server (attempt ${attempt}/${retries}): ${err.message}`);
      }
      await new Promise((r) => setTimeout(r, delayMs));
    }
  }
  throw lastErr;
}

let poolPromise = null;

/** Lazily-opened shared pool for the application database. */
export function getPool() {
  if (!poolPromise) {
    poolPromise = connect().catch((err) => {
      poolPromise = null; // let the next request retry instead of caching the failure
      throw err;
    });
  }
  return poolPromise;
}

/**
 * Run a parameterised query. `params` is {name: value} or {name: {type, value}} when the
 * inferred type isn't what we want (uniqueidentifier and date both need to be explicit).
 */
export async function query(text, params = {}) {
  const pool = await getPool();
  const request = pool.request();
  for (const [name, raw] of Object.entries(params)) {
    if (raw && typeof raw === 'object' && 'type' in raw) request.input(name, raw.type, raw.value);
    else request.input(name, raw);
  }
  return request.query(text);
}

export const uid = (value) => ({ type: sql.UniqueIdentifier, value });
export const date = (value) => ({ type: sql.Date, value });
export const dt = (value) => ({ type: sql.DateTime2, value });
export const text = (value) => ({ type: sql.NVarChar(sql.MAX), value });

/**
 * DATETIME2 round-trips as a JS Date; the API contract is ISO-8601 strings with milliseconds
 * ("2026-08-08T18:36:07.272Z"), which is exactly what toISOString produces.
 */
export const iso = (value) => (value instanceof Date ? value.toISOString() : value ?? undefined);

/** Deletes rows past their retention, replacing DynamoDB's TTL expiry. */
export async function purgeExpired() {
  const res = await query(`
    DELETE FROM analytics_events WHERE expires_at < SYSUTCDATETIME();
    DELETE FROM rate_limits WHERE day_bucket < CAST(SYSUTCDATETIME() AS DATE);
  `);
  return (res.rowsAffected || []).reduce((a, b) => a + b, 0);
}
