# Device ↔ server protocol

Plain HTTP on your LAN, JSON responses. Default port `8765`.

## Discovery

The server announces itself via mDNS as `_geistai._tcp`. The firmware uses that when
`GEIST_SERVER_URL` in `secrets.h` is empty. Otherwise it uses the configured URL.

## `GET /health`

```json
{"ok": true, "name": "GeistAI", "version": "0.1.0", "stt": "small",
 "llm": "ollama:gemma3:4b", "voices": ["de", "en", "es"]}
```

## `POST /api/chat?device=<id>&lang=<code|auto>&tts=<0|1>`

Body: a WAV file (`Content-Type: audio/wav`). The device sends 16 kHz, mono, 16-bit PCM. The
server accepts any PCM WAV and resamples it.

| query    | meaning                                                                   |
|----------|---------------------------------------------------------------------------|
| `device` | stable id (the T-Embed's MAC address), used for conversation memory       |
| `lang`   | `auto` = Whisper detects the language, or force one (`de`, `en`, `es` …) |
| `tts`    | `1` = also synthesize a spoken reply                                      |

Response:

```json
{
  "ok": true,
  "transcript": "¿Cuál es la capital de Islandia?",
  "reply": "La capital de Islandia es Reikiavik.",
  "language": "es",
  "audio": "/api/audio/Xk3v9q2b.wav",
  "error": "",
  "timings": {"stt": 410, "llm": 950, "tts": 230}
}
```

- `reply` and `transcript` are cleaned for the device font: no markdown or emoji, only
  Latin-1, Latin Extended-A and common punctuation.
- `audio` is `null` if `tts=0` or if no voice exists for the language. The device then shows the
  text only.
- If nothing was said: `{"ok": false, "error": "no_speech"}` (HTTP 200).
- If the LLM can't be reached: HTTP 502 with `{"ok": false, "error": "LLM not reachable at …"}`.

## `GET /api/audio/<id>.wav`

The synthesized reply: mono, 16-bit PCM at the voice's native rate (usually 22050 Hz). The
firmware reads the sample rate from the WAV header and streams the audio straight to the I2S
amplifier. Files expire after 10 minutes.

## `POST /api/text?device=<id>&lang=<code|auto>&tts=<0|1>`

Body `{"text": "..."}`. Same response as `/api/chat`. Handy for testing from a browser or `curl`:

```bash
curl -s -X POST 'http://localhost:8765/api/text?lang=auto' \
     -H 'Content-Type: application/json' -d '{"text":"Hallo, wer bist du?"}'
```

## `POST /api/reset?device=<id>`

Clears the conversation memory for that device. The firmware calls this from **Settings → Clear chat**.
