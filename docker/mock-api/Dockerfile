# Local replacement for the analytics/poll Lambda. mssql/tedious is pure JS, so alpine is fine —
# there's nothing here that needs a native toolchain.
FROM node:20-alpine
WORKDIR /srv

COPY package.json ./
RUN npm install --omit=dev --no-audit --no-fund

COPY server.mjs db.mjs handlers.mjs migrate.mjs ./
COPY migrations ./migrations

ENV PORT=8082
EXPOSE 8082
CMD ["node", "server.mjs"]
