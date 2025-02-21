#!/bin/bash

# Find the correct container ID using the image name
CONTAINER_ID=$(docker ps --format "{{.ID}}" --filter "ancestor=mmorpg-prototype-chunk-server-chunk-server")

# Check if the container was found
if [ -z "$CONTAINER_ID" ]; then
    echo "❌ required container not running. Start it first with 'docker-compose up -d'."
    exit 1
fi

echo "✅ Found required container: $CONTAINER_ID"

# Create the local include directory if it doesn't exist
mkdir -p ./docker_includes

# Copy only pqxx headers from container to local project
docker cp $CONTAINER_ID:/usr/include/pqxx ./docker_includes

# Verify copy success
if [ -d "./docker_includes/pqxx" ]; then
    echo "✅ Successfully copied pqxx headers to ./docker_includes"
else
    echo "❌ Failed to copy pqxx headers!"
fi
