FROM python:3.9-slim

WORKDIR /app

# Copy server files
COPY main.py .
COPY generate_playlists.py .

# Default configuration (can be overridden at runtime)
ENV PORT=8090
ENV BIND_ADDRESS=0.0.0.0
ENV PLAYLIST_DIR=/data
ENV CONFIG_DIR=/config
ENV LOG_LEVEL=INFO
ENV WATCH_INTERVAL=300

EXPOSE 8090

CMD ["python", "main.py"]
