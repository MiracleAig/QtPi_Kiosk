# Cloudflare Tunnel setup for remote access (QtPi Kiosk on Raspberry Pi / Linux)

Use this guide to access the website served by your Qt kiosk app securely from anywhere.

> ✅ Goal: reach `https://kiosk.yourdomain.com` and have it forward to your Pi app at `http://127.0.0.1:8080`.

> ⚠️ Important: do **not** expose port `8080` directly on your router.

## Prerequisites

- A Cloudflare account.
- A domain managed by Cloudflare DNS (or a subdomain delegated to Cloudflare).
- Your kiosk app running on the Pi/Linux host.
- SSH access to the Pi/Linux host.

---

## 1) Run your Qt app and keep its hosted website local-only on Linux

Set these environment variables before launching the app:

```bash
export KIOSK_WEBUI_BIND=127.0.0.1
export KIOSK_WEBUI_PORT=8080
export KIOSK_WEBUI_TOKEN='replace-with-a-long-random-token'
./appKiosk
```

What this does:

- Your **Qt app remains the primary UI** running on the Pi display.
- The app also hosts a website/API on port `8080` for remote access.
- `KIOSK_WEBUI_BIND=127.0.0.1` keeps that hosted website local-only until Cloudflare Tunnel forwards traffic to it.
- `KIOSK_WEBUI_TOKEN` protects `/`, `/export.csv`, and `/api/inventory.json`.

Token can be sent either by:

- `Authorization: Bearer <token>` header, or
- `?token=<token>` query parameter.

Linux checks (run on the Pi):

```bash
# Confirm app is listening locally on 127.0.0.1:8080
ss -ltnp | rg 8080

# Expect 401 without token
curl -i http://127.0.0.1:8080/api/inventory.json

# Expect 200 with token
curl -i "http://127.0.0.1:8080/api/inventory.json?token=$KIOSK_WEBUI_TOKEN"
```

---

## 2) Install `cloudflared` on Raspberry Pi / Debian Linux

Install from Cloudflare’s apt repository:

```bash
# Add Cloudflare package signing key
sudo mkdir -p --mode=0755 /usr/share/keyrings
curl -fsSL https://pkg.cloudflare.com/cloudflare-main.gpg \
  | sudo tee /usr/share/keyrings/cloudflare-main.gpg >/dev/null

# Add repo (bookworm example)
echo 'deb [signed-by=/usr/share/keyrings/cloudflare-main.gpg] https://pkg.cloudflare.com/cloudflared bookworm main' \
  | sudo tee /etc/apt/sources.list.d/cloudflared.list

# Install
sudo apt update
sudo apt install -y cloudflared
```

Confirm install:

```bash
cloudflared --version
which cloudflared
```

---

## 3) Authenticate cloudflared with Cloudflare

Run login once on Linux host:

```bash
cloudflared tunnel login
```

- A URL appears in the terminal.
- Open it in your browser, sign in to Cloudflare, and authorize your domain.
- A cert file is saved under `~/.cloudflared/`.

---

## 4) Create and wire the tunnel

Create a named tunnel:

```bash
cloudflared tunnel create qtpikiosk
```

Map DNS hostname to the tunnel:

```bash
cloudflared tunnel route dns qtpikiosk kiosk.yourdomain.com
```

This creates a DNS CNAME in Cloudflare automatically.

---

## 5) Create tunnel config on Linux

Create `~/.cloudflared/config.yml`:

```yaml
tunnel: qtpikiosk
credentials-file: /home/pi/.cloudflared/<tunnel-id>.json
ingress:
  - hostname: kiosk.yourdomain.com
    service: http://127.0.0.1:8080
  - service: http_status:404
```

Replace:

- `kiosk.yourdomain.com` with your real hostname.
- `<tunnel-id>` with the JSON file name created by `cloudflared tunnel create`.
- `/home/pi` if your username is not `pi`.

Validate config:

```bash
cloudflared tunnel ingress validate
```

---

## 6) Run tunnel and test remote access

Start tunnel manually first:

```bash
cloudflared tunnel run qtpikiosk
```

Now test from any external device (phone/laptop on cellular or another network):

- `https://kiosk.yourdomain.com/`
- `https://kiosk.yourdomain.com/api/inventory.json`
- `https://kiosk.yourdomain.com/export.csv`

If you get `401`, add your token:

- `https://kiosk.yourdomain.com/?token=YOUR_TOKEN`

From Linux, monitor connections/logs while testing remotely:

```bash
journalctl -u cloudflared -f
```

---

## 7) Make the tunnel auto-start (systemd on Linux)

Install and enable the service:

```bash
sudo cloudflared service install
sudo systemctl enable cloudflared
sudo systemctl restart cloudflared
sudo systemctl status cloudflared --no-pager
```

At boot verification:

```bash
systemctl is-enabled cloudflared
systemctl is-active cloudflared
```

---

## 8) Recommended security hardening (strongly recommended)

1. **Enable Cloudflare Zero Trust Access** for `kiosk.yourdomain.com`.
   - Example policy: allow only your email(s).
2. Keep `KIOSK_WEBUI_TOKEN` secret and rotate it if shared.
3. Keep the app bound to `127.0.0.1`.
4. Do not open router/NAT ports for the app-hosted website.

---

## Troubleshooting (Linux)

### Tunnel says offline

```bash
cloudflared tunnel list
cloudflared tunnel info qtpikiosk
```

Check the service and logs:

```bash
sudo systemctl status cloudflared --no-pager
journalctl -u cloudflared -n 100 --no-pager
```

### Hostname does not resolve

Confirm DNS route command was run:

```bash
cloudflared tunnel route dns qtpikiosk kiosk.yourdomain.com
```

Then verify DNS resolution from any shell:

```bash
dig +short kiosk.yourdomain.com
```

### Qt app works on the Pi, but website is not reachable through tunnel

- Verify kiosk app is running on the Pi.
- Verify app is listening on `127.0.0.1:8080`.
- Verify `config.yml` ingress `service` points to `http://127.0.0.1:8080`.

Helpful Linux checks:

```bash
ss -ltnp | rg 8080
curl -i http://127.0.0.1:8080/
curl -i "http://127.0.0.1:8080/?token=$KIOSK_WEBUI_TOKEN"
```

---

## Optional: use only Cloudflare Access (no URL token)

If you fully trust Cloudflare Access policy, you can remove `KIOSK_WEBUI_TOKEN` from app startup and enforce auth at Cloudflare only. For defense in depth, keeping both is better.
