FROM python:3.11-slim

RUN apt-get update && apt-get install -y --no-install-recommends espeak-ng && rm -rf /var/lib/apt/lists/*
WORKDIR /app
COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt
COPY geist_server ./geist_server

ENV VOICES_DIR=/data/voices \
    HF_HOME=/data/hf
VOLUME /data
EXPOSE 8765
# Fetch the default voices on first start, then run the server.
CMD ["sh", "-c", "python -m geist_server.download_voices || true; python -m geist_server"]
