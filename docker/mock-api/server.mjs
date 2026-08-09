// Local stand-in for the analytics/poll Lambda Function URL. One route, query-param action
// dispatch, same status codes and JSON bodies as infra/lambda/index.ts — see handlers.mjs.
import express from 'express';
import {
  handleList, handleSubmit, handleVote, handleDevNote, handleShip, handleDelete,
  handleIngest, handleGet, metricsAuthorized,
} from './handlers.mjs';
import { getPool, purgeExpired } from './db.mjs';

const PORT = Number(process.env.PORT || 8082);

const app = express();
app.disable('x-powered-by');

// The client sends Content-Type: application/json, but sendBeacon on unload posts text/plain.
// Take the raw body either way and let the handlers do the parsing, exactly as the Lambda did.
app.use(express.text({ type: '*/*', limit: '5mb' }));

// Mirrors the Function URL's CORS config in infra/lib/analytics-stack.ts. Origins are wide
// open rather than an allow-list because this only ever answers on localhost.
app.use((req, res, next) => {
  res.set({
    'Access-Control-Allow-Origin': '*',
    'Access-Control-Allow-Methods': 'GET, POST, OPTIONS',
    'Access-Control-Allow-Headers': 'Content-Type, Authorization, X-Anon-Id',
    'Access-Control-Max-Age': '3600',
    'Cache-Control': 'no-store',
  });
  if (req.method === 'OPTIONS') return res.sendStatus(204);
  next();
});

app.get('/healthz', async (_req, res) => {
  try {
    const pool = await getPool();
    await pool.request().query('SELECT 1');
    res.json({ ok: true });
  } catch (err) {
    res.status(503).json({ ok: false, error: err.message });
  }
});

app.all('*', async (req, res) => {
  const action = req.query.action;
  const headers = req.headers;
  const body = typeof req.body === 'string' && req.body.length ? req.body : undefined;

  let result;
  try {
    // Poll dispatch — must come before the analytics fall-through.
    if (action === 'poll-list' && req.method === 'GET') result = await handleList(headers);
    else if (action === 'poll-submit' && req.method === 'POST') result = await handleSubmit(body, headers);
    else if (action === 'poll-vote' && req.method === 'POST') result = await handleVote(body, headers);
    else if (action === 'poll-devnote' && req.method === 'POST') result = await handleDevNote(body, headers);
    else if (action === 'poll-ship' && req.method === 'POST') result = await handleShip(body, headers);
    else if (action === 'poll-delete' && req.method === 'POST') result = await handleDelete(body, headers);
    else if (req.method === 'POST') result = await handleIngest(body); // no action → legacy ingest
    else if (req.method === 'GET') {
      if (!metricsAuthorized(headers)) result = { statusCode: 401, body: { error: 'Unauthorized' } };
      else result = await handleGet(req.query);
    } else {
      result = { statusCode: 405, body: { error: 'Method not allowed' } };
    }
  } catch (err) {
    console.error('mock-api handler error:', err);
    result = { statusCode: 500, body: { error: err instanceof Error ? err.message : String(err) } };
  }

  res.status(result.statusCode).json(result.body);
});

// DynamoDB expired TTL rows on its own; sweep hourly to keep the same behaviour.
setInterval(() => {
  purgeExpired()
    .then((n) => { if (n) console.log(`purged ${n} expired rows`); })
    .catch((err) => console.error('purge failed:', err.message));
}, 60 * 60 * 1000).unref();

// Open the pool up front so a cold SQL Server is waited out at boot rather than surfacing as a
// slow first request. migrate.mjs has already run by this point (see docker-compose.yml).
await getPool();

app.listen(PORT, '0.0.0.0', () => {
  console.log(`mock-api listening on :${PORT}`);
});
