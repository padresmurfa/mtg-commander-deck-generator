# Standalone image for the local tagger-sync / spellchroma-index jobs. Node's built-in fetch means
# no dependencies are needed — this exists only to give the scripts a container to run in.
FROM node:20-alpine
WORKDIR /job
COPY package.json ./
COPY tagger-sync.mjs spellchroma-index.mjs ./
ENV DATA_DIR=/data
VOLUME ["/data"]
CMD ["node", "tagger-sync.mjs"]
