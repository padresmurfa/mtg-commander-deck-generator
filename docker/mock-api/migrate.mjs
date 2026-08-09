// Migration runner. Creates the database if it doesn't exist, then applies every .sql file in
// migrations/ that hasn't been applied yet, in filename order, recording each in
// schema_migrations. Idempotent, so it's safe to run on every `docker compose up`.
import { readdir, readFile } from 'node:fs/promises';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { connect, DATABASE, sql } from './db.mjs';

const MIGRATIONS_DIR = path.join(path.dirname(fileURLToPath(import.meta.url)), 'migrations');

/**
 * T-SQL batch separator. GO isn't a SQL statement — it's a client-side marker — so the driver
 * needs the file split on it. Statements that can't share a batch (CREATE INDEX on a table
 * created above it) rely on this.
 */
function splitBatches(sqlText) {
  return sqlText
    .split(/^\s*GO\s*$/gim)
    .map((b) => b.trim())
    .filter((b) => b.length > 0);
}

async function ensureDatabase() {
  const master = await connect('master');
  try {
    // CREATE DATABASE can't be parameterised, hence the identifier quoting.
    const name = DATABASE.replace(/]/g, ']]');
    await master.request().batch(
      `IF DB_ID(${`'${DATABASE.replace(/'/g, "''")}'`}) IS NULL CREATE DATABASE [${name}];`
    );
    console.log(`database [${DATABASE}] ready`);
  } finally {
    await master.close();
  }
}

async function main() {
  await ensureDatabase();

  const pool = await connect();
  try {
    await pool.request().batch(`
      IF OBJECT_ID('schema_migrations', 'U') IS NULL
        CREATE TABLE schema_migrations (
          name        NVARCHAR(255) NOT NULL CONSTRAINT pk_schema_migrations PRIMARY KEY,
          applied_at  DATETIME2(3)  NOT NULL CONSTRAINT df_schema_migrations_at DEFAULT SYSUTCDATETIME()
        );
    `);

    const applied = new Set(
      (await pool.request().query('SELECT name FROM schema_migrations')).recordset.map((r) => r.name)
    );

    const files = (await readdir(MIGRATIONS_DIR)).filter((f) => f.endsWith('.sql')).sort();
    let ran = 0;

    for (const file of files) {
      if (applied.has(file)) {
        console.log(`skip    ${file} (already applied)`);
        continue;
      }

      const text = await readFile(path.join(MIGRATIONS_DIR, file), 'utf8');
      const tx = pool.transaction();
      await tx.begin();
      try {
        for (const batch of splitBatches(text)) {
          await tx.request().batch(batch);
        }
        await tx.request()
          .input('name', sql.NVarChar(255), file)
          .query('INSERT INTO schema_migrations (name) VALUES (@name)');
        await tx.commit();
        console.log(`applied ${file}`);
        ran++;
      } catch (err) {
        await tx.rollback();
        throw new Error(`migration ${file} failed: ${err.message}`);
      }
    }

    console.log(ran === 0 ? 'schema already up to date' : `${ran} migration(s) applied`);
  } finally {
    await pool.close();
  }
}

main().catch((err) => {
  console.error(err);
  process.exit(1);
});
