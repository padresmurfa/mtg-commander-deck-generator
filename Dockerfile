# ---- base: shared dependency layer -----------------------------------------------------------
FROM node:20-alpine AS base
WORKDIR /app
COPY package.json package-lock.json ./

# ---- dev: hot-reloading Vite dev server, used by docker-compose + the VS Code dev container ---
FROM base AS dev
# node:20-alpine ships neither of these. VS Code's Source Control panel shells out to `git`, and
# its default Linux terminal profile (plus most devcontainer lifecycle scripts) expects `bash` —
# without them the dev container comes up but is a degraded place to actually work in.
# openssh-client is what git needs for ssh:// remotes.
RUN apk add --no-cache git bash openssh-client
RUN npm ci
COPY . .
EXPOSE 5173
CMD ["npm", "run", "dev", "--", "--host", "0.0.0.0"]

# ---- build: produces the static dist/ bundle -------------------------------------------------
FROM base AS build
ARG BASE_PATH=/
ARG VITE_ANALYTICS_URL
ARG VITE_TAG_REPO_URL
ARG VITE_SPELLCHROMA_DICT_URL
ARG VITE_SPELLCHROMA_INDEX_URL
ENV BASE_PATH=$BASE_PATH \
    VITE_ANALYTICS_URL=$VITE_ANALYTICS_URL \
    VITE_TAG_REPO_URL=$VITE_TAG_REPO_URL \
    VITE_SPELLCHROMA_DICT_URL=$VITE_SPELLCHROMA_DICT_URL \
    VITE_SPELLCHROMA_INDEX_URL=$VITE_SPELLCHROMA_INDEX_URL
RUN npm ci
COPY . .
RUN npm run build

# ---- production: static bundle served by nginx -------------------------------------------------
FROM nginx:1.27-alpine AS production
COPY docker/nginx/site.conf /etc/nginx/conf.d/default.conf
COPY --from=build /app/dist /usr/share/nginx/html
EXPOSE 80
