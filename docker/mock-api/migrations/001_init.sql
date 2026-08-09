-- Normalised replacement for the DynamoDB single-table layout the Lambda used.
-- What used to be one `items` table keyed by (pk, sk) is now four real tables.

CREATE TABLE suggestions (
    id              UNIQUEIDENTIFIER NOT NULL CONSTRAINT pk_suggestions PRIMARY KEY,
    title           NVARCHAR(80)     NOT NULL,
    description     NVARCHAR(600)    NOT NULL,
    status          VARCHAR(16)      NOT NULL CONSTRAINT ck_suggestions_status CHECK (status IN ('open', 'shipped')),
    vote_count      INT              NOT NULL CONSTRAINT df_suggestions_votes DEFAULT 0,
    dev_note        NVARCHAR(600)    NULL,
    shipped_version NVARCHAR(64)     NULL,
    shipped_at      DATETIME2(3)     NULL,
    anon_author_id  UNIQUEIDENTIFIER NOT NULL,
    created_at      DATETIME2(3)     NOT NULL
);
GO

-- The board lists newest-first and nothing else, so one covering order is enough.
CREATE INDEX ix_suggestions_created_at ON suggestions (created_at DESC);
GO

CREATE TABLE votes (
    suggestion_id UNIQUEIDENTIFIER NOT NULL,
    anon_id       UNIQUEIDENTIFIER NOT NULL,
    voted_at      DATETIME2(3)     NOT NULL,
    CONSTRAINT pk_votes PRIMARY KEY (suggestion_id, anon_id),
    -- Replaces the manual vote-row cleanup handleDelete used to do by hand.
    CONSTRAINT fk_votes_suggestion FOREIGN KEY (suggestion_id)
        REFERENCES suggestions (id) ON DELETE CASCADE
);
GO

-- "Which of these have I voted for" is looked up per anon id on every board load.
CREATE INDEX ix_votes_anon_id ON votes (anon_id) INCLUDE (suggestion_id);
GO

CREATE TABLE analytics_events (
    id          BIGINT         NOT NULL IDENTITY(1,1) CONSTRAINT pk_analytics_events PRIMARY KEY,
    event       NVARCHAR(128)  NOT NULL,
    occurred_at DATETIME2(3)   NOT NULL,
    -- Free-form per-event payload; the summary aggregation reads it in JS, exactly as the
    -- Lambda read the DynamoDB `metadata` map.
    metadata    NVARCHAR(MAX)  NOT NULL CONSTRAINT ck_analytics_metadata_json CHECK (ISJSON(metadata) = 1),
    -- Stands in for the DynamoDB 90-day TTL attribute; swept by the server on an interval.
    expires_at  DATETIME2(3)   NOT NULL
);
GO

-- The summary query is a range scan over time; the events query filters by type first.
CREATE INDEX ix_analytics_occurred_at ON analytics_events (occurred_at);
GO
CREATE INDEX ix_analytics_event_occurred_at ON analytics_events (event, occurred_at DESC);
GO
CREATE INDEX ix_analytics_expires_at ON analytics_events (expires_at);
GO

-- Per-anon-id, per-UTC-day counters behind the submit/vote limits.
CREATE TABLE rate_limits (
    anon_id    UNIQUEIDENTIFIER NOT NULL,
    action     VARCHAR(16)      NOT NULL CONSTRAINT ck_rate_limits_action CHECK (action IN ('submit', 'vote')),
    day_bucket DATE             NOT NULL,
    count      INT              NOT NULL,
    CONSTRAINT pk_rate_limits PRIMARY KEY (anon_id, action, day_bucket)
);
GO

CREATE INDEX ix_rate_limits_day_bucket ON rate_limits (day_bucket);
GO
