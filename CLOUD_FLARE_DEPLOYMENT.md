# Cloudflare remote access setup (QtPi Kiosk)

If you want to access the kiosk web UI from outside your home network, **do not expose port 8080 directly**.
Use a Cloudflare Tunnel and protect it with an access token.

## 1) App configuration

### Linux / Raspberry Pi

```bash
export KIOSK_WEBUI_BIND=127.0.0.1
export KIOSK_WEBUI_PORT=8080
export KIOSK_WEBUI_TOKEN='replace-with-a-long-random-token'
./appKiosk
```

### Windows (PowerShell) - debugging friendly

```powershell
$env:KIOSK_WEBUI_BIND = '127.0.0.1'
$env:KIOSK_WEBUI_PORT = '8080'
$env:KIOSK_WEBUI_TOKEN = 'replace-with-a-long-random-token'
.\build\Desktop_Qt_6_10_2_MinGW_64_bit-Debug\appKiosk.exe
```

### Windows (CMD)

```cmd
set KIOSK_WEBUI_BIND=127.0.0.1
set KIOSK_WEBUI_PORT=8080
set KIOSK_WEBUI_TOKEN=replace-with-a-long-random-token
build\Desktop_Qt_6_10_2_MinGW_64_bit-Debug\appKiosk.exe
```

- `KIOSK_WEBUI_BIND=127.0.0.1` keeps the web UI local-only.
- `KIOSK_WEBUI_TOKEN` enables bearer-token protection for `/`, `/export.csv`, and `/api/inventory.json`.

Requests must include one of:
- `Authorization: Bearer <token>` header, or
- `?token=<token>` query parameter.

## 2) Debug quickly on Windows before Cloudflare

PowerShell checks:

```powershell
# Should return 401 when token is required
Invoke-WebRequest -Uri "http://127.0.0.1:8080/api/inventory.json" -UseBasicParsing

# Should return 200 with token in URL
Invoke-WebRequest -Uri "http://127.0.0.1:8080/api/inventory.json?token=$env:KIOSK_WEBUI_TOKEN" -UseBasicParsing

# Should return 200 with bearer token header
Invoke-WebRequest -Uri "http://127.0.0.1:8080/api/inventory.json" -Headers @{ Authorization = "Bearer $env:KIOSK_WEBUI_TOKEN" } -UseBasicParsing
```

## 3) Cloudflare Tunnel

Install and authenticate `cloudflared`, then create a tunnel to local port 8080:

```bash
cloudflared tunnel login
cloudflared tunnel create qtpikiosk
cloudflared tunnel route dns qtpikiosk kiosk.yourdomain.com
```

Windows config example (`%USERPROFILE%\\.cloudflared\\config.yml`):

```yaml
tunnel: qtpikiosk
credentials-file: C:\\Users\\<you>\\.cloudflared\\<tunnel-id>.json
ingress:
  - hostname: kiosk.yourdomain.com
    service: http://127.0.0.1:8080
  - service: http_status:404
```

Linux config example (`~/.cloudflared/config.yml`):

```yaml
tunnel: qtpikiosk
credentials-file: /home/pi/.cloudflared/<tunnel-id>.json
ingress:
  - hostname: kiosk.yourdomain.com
    service: http://127.0.0.1:8080
  - service: http_status:404
```

Then run:

```bash
cloudflared tunnel run qtpikiosk
```

## 4) Recommended hardening

- In Cloudflare Zero Trust, create an access policy so only your user/email can reach the hostname.
- Keep `KIOSK_WEBUI_TOKEN` secret and rotate it if shared.
- Avoid exposing the tunnel to public unauthenticated traffic.

## 5) Useful URLs

- Landing page: `https://kiosk.yourdomain.com/`
- CSV export: `https://kiosk.yourdomain.com/export.csv`
- JSON view: `https://kiosk.yourdomain.com/api/inventory.json`
