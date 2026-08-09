// Port of infra/lambda/poll.ts + infra/lambda/index.ts. Same dispatch, same validation, same
// response bodies — backed by the normalised suggestions / votes / analytics_events tables
// instead of the DynamoDB single table.
import { randomUUID } from 'node:crypto';
import { query, getPool, uid, date, dt, iso, sql } from './db.mjs';

const ADMIN_SECRET = process.env.POLL_ADMIN_SECRET || '';
const METRICS_SECRET = process.env.METRICS_SECRET || '';

// Production defaults, overridable so a day of local poking doesn't lock you out.
export const LIMITS = {
  submit: Number(process.env.POLL_SUBMIT_LIMIT ?? 3),
  vote: Number(process.env.POLL_VOTE_LIMIT ?? 60),
};

export const MAX_TITLE = 80;
export const MAX_DESCRIPTION = 600;
export const MAX_DEVNOTE = 600;
const ANALYTICS_RETENTION_DAYS = 90;

const UUID_RE = /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/i;
function isValidUuid(v) {
  return !!v && UUID_RE.test(v);
}

function dayBucketUTC(d = new Date()) {
  return d.toISOString().slice(0, 10);
}

function jsonResponse(statusCode, body) {
  return { statusCode, body };
}

function safeEqual(a, b) {
  if (a.length !== b.length) return false;
  let diff = 0;
  for (let i = 0; i < a.length; i++) diff |= a.charCodeAt(i) ^ b.charCodeAt(i);
  return diff === 0;
}

function bearer(headers) {
  return headers?.authorization || headers?.Authorization || '';
}

function isAdmin(headers) {
  if (!ADMIN_SECRET) return false;
  const auth = bearer(headers);
  if (!auth.startsWith('Bearer ')) return false;
  return safeEqual(auth.slice(7), ADMIN_SECRET);
}

function anonIdOf(headers) {
  return headers?.['x-anon-id'] || headers?.['X-Anon-Id'];
}

/** Row → the public shape the frontend's Suggestion type expects. */
function toPublic(r) {
  return {
    id: r.id.toLowerCase(),
    title: r.title,
    description: r.description,
    status: r.status,
    voteCount: r.vote_count,
    devNote: r.dev_note ?? undefined,
    shippedVersion: r.shipped_version ?? undefined,
    shippedAt: iso(r.shipped_at),
    createdAt: iso(r.created_at),
  };
}

const SUGGESTION_COLS =
  'id, title, description, status, vote_count, dev_note, shipped_version, shipped_at, created_at';

function parseBody(body) {
  if (body === undefined || body === null || body === '') return { missing: true };
  try {
    return { value: JSON.parse(body) };
  } catch {
    return { bad: true };
  }
}

async function getSuggestionById(id) {
  if (!isValidUuid(id)) return null;
  const res = await query(`SELECT ${SUGGESTION_COLS} FROM suggestions WHERE id = @id`, { id: uid(id) });
  return res.recordset[0] || null;
}

// ---- rate limiting ---------------------------------------------------------------------------
// The Lambda used a conditional UpdateItem. The equivalent here is a check-and-increment held
// under UPDLOCK/HOLDLOCK for the row (or key range, when the row doesn't exist yet), so two
// concurrent requests can't both read the same count and both be allowed through.
async function checkRateLimit(anonId, action) {
  const res = await query(
    `
    SET XACT_ABORT ON;
    BEGIN TRANSACTION;
      DECLARE @count INT;
      SELECT @count = count
        FROM rate_limits WITH (UPDLOCK, HOLDLOCK)
       WHERE anon_id = @anonId AND action = @action AND day_bucket = @day;

      IF @count IS NULL
      BEGIN
        INSERT INTO rate_limits (anon_id, action, day_bucket, count) VALUES (@anonId, @action, @day, 1);
        SELECT CAST(1 AS BIT) AS allowed;
      END
      ELSE IF @count < @limit
      BEGIN
        UPDATE rate_limits SET count = count + 1
         WHERE anon_id = @anonId AND action = @action AND day_bucket = @day;
        SELECT CAST(1 AS BIT) AS allowed;
      END
      ELSE
        SELECT CAST(0 AS BIT) AS allowed;
    COMMIT TRANSACTION;
    `,
    {
      anonId: uid(anonId),
      action: { type: sql.VarChar(16), value: action },
      day: date(dayBucketUTC()),
      limit: { type: sql.Int, value: LIMITS[action] },
    }
  );
  return res.recordset[0].allowed === true;
}

// ---- poll handlers ---------------------------------------------------------------------------

export async function handleSubmit(body, headers) {
  const anonId = anonIdOf(headers);
  if (!isValidUuid(anonId)) return jsonResponse(400, { error: 'bad_anon_id' });

  const parsed = parseBody(body);
  if (parsed.missing) return jsonResponse(400, { error: 'missing_body' });
  if (parsed.bad) return jsonResponse(400, { error: 'bad_json' });

  const title = typeof parsed.value.title === 'string' ? parsed.value.title.trim() : '';
  const description = typeof parsed.value.description === 'string' ? parsed.value.description.trim() : '';
  if (title.length < 1 || title.length > MAX_TITLE) return jsonResponse(400, { error: 'bad_title', max: MAX_TITLE });
  if (description.length < 1 || description.length > MAX_DESCRIPTION) {
    return jsonResponse(400, { error: 'bad_description', max: MAX_DESCRIPTION });
  }

  if (!(await checkRateLimit(anonId, 'submit'))) {
    return jsonResponse(429, { error: 'rate_limited', action: 'submit', limit: LIMITS.submit });
  }

  const id = randomUUID();
  const createdAt = new Date();
  await query(
    `INSERT INTO suggestions (id, title, description, status, vote_count, anon_author_id, created_at)
     VALUES (@id, @title, @description, 'open', 0, @anonId, @createdAt)`,
    {
      id: uid(id),
      title: { type: sql.NVarChar(MAX_TITLE), value: title },
      description: { type: sql.NVarChar(MAX_DESCRIPTION), value: description },
      anonId: uid(anonId),
      createdAt: dt(createdAt),
    }
  );

  return jsonResponse(200, {
    suggestion: {
      id, title, description, status: 'open', voteCount: 0,
      devNote: undefined, shippedVersion: undefined, shippedAt: undefined,
      createdAt: createdAt.toISOString(),
    },
  });
}

export async function handleList(headers) {
  const anonId = anonIdOf(headers);

  const res = await query(
    `SELECT ${SUGGESTION_COLS} FROM suggestions ORDER BY created_at DESC, id DESC`
  );

  // One indexed lookup for the whole board, rather than the per-suggestion BatchGet the
  // DynamoDB version needed.
  let myVotes = [];
  if (isValidUuid(anonId)) {
    const votes = await query('SELECT suggestion_id FROM votes WHERE anon_id = @anonId', {
      anonId: uid(anonId),
    });
    myVotes = votes.recordset.map((r) => r.suggestion_id.toLowerCase());
  }

  return jsonResponse(200, { suggestions: res.recordset.map(toPublic), myVotes });
}

export async function handleVote(body, headers) {
  const anonId = anonIdOf(headers);
  if (!isValidUuid(anonId)) return jsonResponse(400, { error: 'bad_anon_id' });

  const parsed = parseBody(body);
  if (parsed.missing) return jsonResponse(400, { error: 'missing_body' });
  if (parsed.bad) return jsonResponse(400, { error: 'bad_json' });

  const suggestionId = typeof parsed.value.suggestionId === 'string' ? parsed.value.suggestionId : '';
  if (!suggestionId) return jsonResponse(400, { error: 'bad_suggestion_id' });
  if (parsed.value.vote !== 0 && parsed.value.vote !== 1) return jsonResponse(400, { error: 'bad_vote' });
  const targetVoted = parsed.value.vote === 1;

  const suggestion = await getSuggestionById(suggestionId);
  if (!suggestion) return jsonResponse(404, { error: 'not_found' });

  const existing = await query(
    'SELECT 1 AS found FROM votes WHERE suggestion_id = @sid AND anon_id = @anonId',
    { sid: uid(suggestionId), anonId: uid(anonId) }
  );
  const alreadyVoted = existing.recordset.length > 0;

  // Already in the requested state — no write, and no rate-limit charge.
  if (alreadyVoted === targetVoted) {
    return jsonResponse(200, { suggestionId, voteCount: suggestion.vote_count });
  }

  if (!(await checkRateLimit(anonId, 'vote'))) {
    return jsonResponse(429, { error: 'rate_limited', action: 'vote', limit: LIMITS.vote });
  }

  // The vote row and the denormalised counter have to move together, and the write is
  // conditional on the row's current presence so a double-submit can't double-count.
  const res = await query(
    `
    SET XACT_ABORT ON;
    BEGIN TRANSACTION;
      IF @target = 1 AND NOT EXISTS (SELECT 1 FROM votes WHERE suggestion_id = @sid AND anon_id = @anonId)
      BEGIN
        INSERT INTO votes (suggestion_id, anon_id, voted_at) VALUES (@sid, @anonId, SYSUTCDATETIME());
        UPDATE suggestions SET vote_count = vote_count + 1 WHERE id = @sid;
      END
      ELSE IF @target = 0 AND EXISTS (SELECT 1 FROM votes WHERE suggestion_id = @sid AND anon_id = @anonId)
      BEGIN
        DELETE FROM votes WHERE suggestion_id = @sid AND anon_id = @anonId;
        UPDATE suggestions SET vote_count = vote_count - 1 WHERE id = @sid;
      END

      SELECT vote_count FROM suggestions WHERE id = @sid;
    COMMIT TRANSACTION;
    `,
    { sid: uid(suggestionId), anonId: uid(anonId), target: { type: sql.Bit, value: targetVoted } }
  );

  return jsonResponse(200, { suggestionId, voteCount: res.recordset[0].vote_count });
}

export async function handleDevNote(body, headers) {
  if (!isAdmin(headers)) return jsonResponse(401, { error: 'unauthorized' });
  const parsed = parseBody(body);
  if (parsed.missing) return jsonResponse(400, { error: 'missing_body' });
  if (parsed.bad) return jsonResponse(400, { error: 'bad_json' });

  const suggestionId = typeof parsed.value.suggestionId === 'string' ? parsed.value.suggestionId : '';
  const devNote = typeof parsed.value.devNote === 'string' ? parsed.value.devNote.trim() : '';
  if (!suggestionId) return jsonResponse(400, { error: 'bad_suggestion_id' });
  if (devNote.length > MAX_DEVNOTE) return jsonResponse(400, { error: 'bad_devnote', max: MAX_DEVNOTE });

  const suggestion = await getSuggestionById(suggestionId);
  if (!suggestion) return jsonResponse(404, { error: 'not_found' });

  // Empty note clears it, matching the Lambda's REMOVE devNote.
  const value = devNote.length === 0 ? null : devNote;
  await query('UPDATE suggestions SET dev_note = @devNote WHERE id = @id', {
    id: uid(suggestionId),
    devNote: { type: sql.NVarChar(MAX_DEVNOTE), value },
  });

  return jsonResponse(200, { suggestion: toPublic({ ...suggestion, dev_note: value }) });
}

export async function handleShip(body, headers) {
  if (!isAdmin(headers)) return jsonResponse(401, { error: 'unauthorized' });
  const parsed = parseBody(body);
  if (parsed.missing) return jsonResponse(400, { error: 'missing_body' });
  if (parsed.bad) return jsonResponse(400, { error: 'bad_json' });

  const suggestionId = typeof parsed.value.suggestionId === 'string' ? parsed.value.suggestionId : '';
  const shippedVersion = typeof parsed.value.shippedVersion === 'string' ? parsed.value.shippedVersion.trim() : '';
  if (!suggestionId) return jsonResponse(400, { error: 'bad_suggestion_id' });
  if (!shippedVersion) return jsonResponse(400, { error: 'bad_shipped_version' });

  const suggestion = await getSuggestionById(suggestionId);
  if (!suggestion) return jsonResponse(404, { error: 'not_found' });

  const shippedAt = new Date();
  await query(
    `UPDATE suggestions SET status = 'shipped', shipped_version = @version, shipped_at = @at
      WHERE id = @id`,
    {
      id: uid(suggestionId),
      version: { type: sql.NVarChar(64), value: shippedVersion },
      at: dt(shippedAt),
    }
  );

  return jsonResponse(200, {
    suggestion: toPublic({
      ...suggestion,
      status: 'shipped',
      shipped_version: shippedVersion,
      shipped_at: shippedAt,
    }),
  });
}

export async function handleDelete(body, headers) {
  if (!isAdmin(headers)) return jsonResponse(401, { error: 'unauthorized' });
  const parsed = parseBody(body);
  if (parsed.missing) return jsonResponse(400, { error: 'missing_body' });
  if (parsed.bad) return jsonResponse(400, { error: 'bad_json' });

  const suggestionId = typeof parsed.value.suggestionId === 'string' ? parsed.value.suggestionId : '';
  if (!suggestionId) return jsonResponse(400, { error: 'bad_suggestion_id' });
  if (!isValidUuid(suggestionId)) return jsonResponse(200, { ok: true }); // already gone

  // Vote rows go with it via ON DELETE CASCADE.
  await query('DELETE FROM suggestions WHERE id = @id', { id: uid(suggestionId) });
  return jsonResponse(200, { ok: true });
}

// ---- analytics ingest ------------------------------------------------------------------------

export async function handleIngest(body) {
  const parsed = parseBody(body);
  if (parsed.missing) return jsonResponse(400, { error: 'Missing body' });
  if (parsed.bad) return jsonResponse(400, { error: 'bad_json' });

  const events = Array.isArray(parsed.value.events) ? parsed.value.events : [parsed.value];
  const expiresAt = new Date(Date.now() + ANALYTICS_RETENTION_DAYS * 24 * 60 * 60 * 1000);

  const table = new sql.Table('analytics_events');
  table.columns.add('event', sql.NVarChar(128), { nullable: false });
  table.columns.add('occurred_at', sql.DateTime2(3), { nullable: false });
  table.columns.add('metadata', sql.NVarChar(sql.MAX), { nullable: false });
  table.columns.add('expires_at', sql.DateTime2(3), { nullable: false });

  let skipped = 0;
  for (const e of events) {
    const occurredAt = new Date(e?.timestamp);
    // The column is a real datetime now, so a junk timestamp can't be stored verbatim the way
    // it could in DynamoDB. Drop those rather than fail the whole batch — ingest is fire-and-forget.
    if (!e?.event || Number.isNaN(occurredAt.getTime())) {
      skipped++;
      continue;
    }
    table.rows.add(String(e.event), occurredAt, JSON.stringify(e.metadata || {}), expiresAt);
  }

  if (table.rows.length > 0) {
    const pool = await getPool();
    await pool.request().bulk(table);
  }

  const response = { ingested: table.rows.length };
  if (skipped) response.skipped = skipped;
  return jsonResponse(200, response);
}

// ---- metrics ---------------------------------------------------------------------------------

export function metricsAuthorized(headers) {
  if (!METRICS_SECRET) return true;
  return bearer(headers) === `Bearer ${METRICS_SECRET}`;
}

export async function handleGet(params) {
  const action = params.action || 'summary';
  const from = params.from || new Date(Date.now() - 30 * 24 * 60 * 60 * 1000).toISOString();
  const to = params.to || new Date().toISOString();

  if (action === 'summary') return summarize(from, to);

  if (action === 'events' && params.eventType) {
    const res = await query(
      `SELECT TOP (100) event, occurred_at, metadata
         FROM analytics_events
        WHERE event = @eventType AND occurred_at >= @from AND occurred_at <= @to
        ORDER BY occurred_at DESC`,
      {
        eventType: { type: sql.NVarChar(128), value: params.eventType },
        from: dt(new Date(from)),
        to: dt(new Date(to)),
      }
    );
    return jsonResponse(200, {
      events: res.recordset.map((r) => ({
        event: r.event,
        timestamp: iso(r.occurred_at),
        metadata: JSON.parse(r.metadata),
      })),
    });
  }

  return jsonResponse(400, { error: 'Unknown action' });
}

async function summarize(from, to) {
  const eventCounts = {};
  const commanderCounts = {};
  const themeCounts = {};
  const dailyCounts = {};
  const dailyBreakdown = {};
  const dailyUserSets = {};
  const hourlyCounts = {};
  const hourlyBreakdown = {};
  const hourlyUserSets = {};
  const uniqueUsers = new Set();
  const newUsers = new Set();
  const returningUsers = new Set();
  const userFirstSeen = new Map();
  const fromDay = from.slice(0, 10);
  const regionCounts = {};
  const deviceCounts = {};
  const hostCounts = {};
  const inspectorTabCounts = {};
  const featureAdoption = {
    collectionMode: 0, hyperFocus: 0, tinyLeaders: 0, arenaOnly: 0,
    hasPriceLimit: 0, hasBudgetLimit: 0, hasMusts: 0, hasBans: 0,
    deckCount: 0, regenerations: 0, classicBuild: 0, landCountModified: 0,
  };
  const listActivity = {
    created: 0, deleted: 0, exported: 0, toggledOn: 0, toggledOff: 0,
    totalCardsInCreated: 0, includeToggles: 0, excludeToggles: 0,
  };
  const settingsCounts = {
    budgetOption: {}, bracketLevel: {}, allowedRarities: {}, gameChangerLimit: {},
    deckFormat: {}, comboPreference: {}, deckBudget: {}, maxCardPrice: {}, landCount: {},
  };

  const processItem = (item) => {
    eventCounts[item.event] = (eventCounts[item.event] || 0) + 1;

    const day = item.timestamp?.slice(0, 10);
    if (day) {
      dailyCounts[day] = (dailyCounts[day] || 0) + 1;
      if (!dailyBreakdown[day]) dailyBreakdown[day] = {};
      dailyBreakdown[day][item.event] = (dailyBreakdown[day][item.event] || 0) + 1;
    }

    const hour = item.timestamp?.slice(0, 13);
    if (hour) {
      hourlyCounts[hour] = (hourlyCounts[hour] || 0) + 1;
      if (!hourlyBreakdown[hour]) hourlyBreakdown[hour] = {};
      hourlyBreakdown[hour][item.event] = (hourlyBreakdown[hour][item.event] || 0) + 1;
    }

    const meta = item.metadata;

    if (meta?.userId && typeof meta.userId === 'string') {
      uniqueUsers.add(meta.userId);
      if (day) (dailyUserSets[day] ??= new Set()).add(meta.userId);
      if (hour) (hourlyUserSets[hour] ??= new Set()).add(meta.userId);
      if (!userFirstSeen.has(meta.userId)) userFirstSeen.set(meta.userId, null);
      if (typeof meta.firstSeen === 'string' && meta.firstSeen) {
        userFirstSeen.set(meta.userId, meta.firstSeen);
      }
    }

    if (meta?.region && typeof meta.region === 'string') {
      regionCounts[meta.region] = (regionCounts[meta.region] || 0) + 1;
    }
    if (meta?.deviceType && typeof meta.deviceType === 'string') {
      deviceCounts[meta.deviceType] = (deviceCounts[meta.deviceType] || 0) + 1;
    }
    if (meta?.host && typeof meta.host === 'string') {
      hostCounts[meta.host] = (hostCounts[meta.host] || 0) + 1;
    }

    if (item.event === 'deck_generated') {
      if (meta?.commanderName && typeof meta.commanderName === 'string') {
        commanderCounts[meta.commanderName] = (commanderCounts[meta.commanderName] || 0) + 1;
      }
      if (Array.isArray(meta?.themes)) {
        for (const theme of meta.themes) if (theme) themeCounts[theme] = (themeCounts[theme] || 0) + 1;
      }

      featureAdoption.deckCount++;
      if (meta.collectionMode === true) featureAdoption.collectionMode++;
      if (meta.hyperFocus === true) featureAdoption.hyperFocus++;
      if (meta.tinyLeaders === true) featureAdoption.tinyLeaders++;
      if (meta.arenaOnly === true) featureAdoption.arenaOnly++;
      if (meta.maxCardPrice !== null && meta.maxCardPrice !== undefined) featureAdoption.hasPriceLimit++;
      if (meta.deckBudget !== null && meta.deckBudget !== undefined) featureAdoption.hasBudgetLimit++;
      if (typeof meta.mustIncludeCount === 'number' && meta.mustIncludeCount > 0) featureAdoption.hasMusts++;
      if (typeof meta.bannedCount === 'number' && meta.bannedCount > 0) featureAdoption.hasBans++;
      if (meta?.isRegeneration === true) featureAdoption.regenerations++;
      if (meta?.balancedRoles === false) featureAdoption.classicBuild++;
      if (meta?.landCountModified === true) featureAdoption.landCountModified++;

      const bucket = (key, val) => {
        const s = String(val ?? 'unknown');
        settingsCounts[key][s] = (settingsCounts[key][s] || 0) + 1;
      };
      if (meta.budgetOption !== undefined) bucket('budgetOption', meta.budgetOption);
      if (meta.bracketLevel !== undefined) bucket('bracketLevel', meta.bracketLevel);
      bucket('allowedRarities', Array.isArray(meta.allowedRarities) ? meta.allowedRarities.join(',') : 'none');
      if (meta.gameChangerLimit !== undefined) bucket('gameChangerLimit', meta.gameChangerLimit);

      if (meta.deckFormat !== undefined) {
        const fmt = meta.deckFormat === 99 ? 'Commander' : meta.deckFormat === 60 ? 'Brawl' : 'Custom';
        bucket('deckFormat', fmt);
      }

      if (meta.comboPreference !== undefined) {
        const comboLabels = { 0: 'None', 1: 'A Few', 2: 'Many' };
        bucket('comboPreference', comboLabels[meta.comboPreference] ?? String(meta.comboPreference));
      }

      bucket('deckBudget', meta.deckBudget === null || meta.deckBudget === undefined ? 'None' : `$${meta.deckBudget}`);
      bucket('maxCardPrice', meta.maxCardPrice === null || meta.maxCardPrice === undefined ? 'None' : `$${meta.maxCardPrice}`);

      if (typeof meta.landCount === 'number') {
        const lc = meta.landCount;
        const landBucket = lc <= 33 ? '≤33 (Aggro)' : lc <= 36 ? '34-36' : lc === 37 ? '37 (Standard)' : lc <= 40 ? '38-40' : '41+ (Control)';
        bucket('landCount', landBucket);
      }
    }

    if (item.event === 'inspector_tab_viewed') {
      const rawTab = meta?.tab;
      const tab = typeof rawTab === 'string' && rawTab ? rawTab : 'unknown';
      inspectorTabCounts[tab] = (inspectorTabCounts[tab] || 0) + 1;
    }

    if (item.event === 'list_created') {
      listActivity.created++;
      if (typeof meta?.cardCount === 'number') listActivity.totalCardsInCreated += meta.cardCount;
    }
    if (item.event === 'list_deleted') listActivity.deleted++;
    if (item.event === 'list_exported') listActivity.exported++;
    if (item.event === 'list_toggled') {
      if (meta?.enabled === true) listActivity.toggledOn++;
      else listActivity.toggledOff++;
      if (meta?.mode === 'include') listActivity.includeToggles++;
      if (meta?.mode === 'exclude') listActivity.excludeToggles++;
    }
  };

  // The Lambda fanned this out one DynamoDB query per day; here it's a single indexed range scan.
  const res = await query(
    `SELECT event, occurred_at, metadata
       FROM analytics_events
      WHERE occurred_at >= @from AND occurred_at <= @to
      ORDER BY occurred_at`,
    { from: dt(new Date(from)), to: dt(new Date(to)) }
  );

  for (const row of res.recordset) {
    processItem({
      event: row.event,
      timestamp: iso(row.occurred_at),
      metadata: JSON.parse(row.metadata),
    });
  }

  for (const [userId, firstSeen] of userFirstSeen) {
    if (firstSeen && firstSeen >= fromDay) newUsers.add(userId);
    else returningUsers.add(userId);
  }

  const dailyUniqueUsers = {};
  for (const [day, set] of Object.entries(dailyUserSets)) dailyUniqueUsers[day] = set.size;

  const hourlyUniqueUsers = {};
  for (const [hour, set] of Object.entries(hourlyUserSets)) hourlyUniqueUsers[hour] = set.size;

  return jsonResponse(200, {
    totalEvents: res.recordset.length,
    uniqueUserCount: uniqueUsers.size,
    newUserCount: newUsers.size,
    returningUserCount: returningUsers.size,
    eventCounts, commanderCounts, themeCounts,
    dailyCounts, dailyBreakdown, dailyUniqueUsers,
    hourlyCounts, hourlyBreakdown, hourlyUniqueUsers,
    regionCounts, deviceCounts, hostCounts, inspectorTabCounts,
    featureAdoption, listActivity, settingsCounts,
    dateRange: { from, to },
  });
}
