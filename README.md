# Canda Bot — on-demand ESP32 dashboard

A private Next.js dashboard for the Canda Bot ESP32. The device checks for a pending status request every **30 seconds** and uploads full telemetry only after the dashboard's **Check Status** button is pressed.

## Data flow

1. You sign in to the dashboard and click **Check Status**.
2. Vercel creates a short-lived pending request in Upstash Redis.
3. The ESP32 checks `/api/device-request` every 30 seconds using its device token.
4. If a request is pending, the ESP32 measures CPU load, internal heap, Wi-Fi RSSI, internal temperature, flash/OTA space, PSRAM, uptime and device/network state, then POSTs one response to `/api/telemetry`.
5. The dashboard waits for that response and then renders the values and recorded snapshots.

There is no continuous telemetry upload and no automatic dashboard polling. The ESP32 still makes one lightweight request check every 30 seconds so it can receive commands while it is behind a home router/NAT. If the ESP32 has no Wi-Fi/internet connection, it cannot receive the request.

## Deploy

1. Create a Redis database at <https://console.upstash.com/>.
2. Push this folder to a GitHub repository.
3. Import the repository at <https://vercel.com/new>.
4. Add these Vercel environment variables:
   - `UPSTASH_REDIS_REST_URL`
   - `UPSTASH_REDIS_REST_TOKEN`
   - `DASHBOARD_PASSWORD`
   - `SESSION_SECRET` (use a long random secret)
   - `DEVICE_TOKEN` (use a different long random secret)
5. Deploy the project.
6. Open `firmware/CandaBot_ESP32_Watchdog_Corrected.ino` in Arduino IDE.
7. Replace `YOUR-VERCEL-PROJECT` in **both** `TELEMETRY_URL` and `DEVICE_REQUEST_URL` with your deployed Vercel hostname. For example:
   - `https://your-project.vercel.app/api/telemetry`
   - `https://your-project.vercel.app/api/device-request`
8. Replace `YOUR_DEVICE_TOKEN` with the exact same value used for Vercel's `DEVICE_TOKEN` environment variable.
9. Set your Wi-Fi password and OTA password in the sketch. Keep secrets private.
10. Install Adafruit GFX and Adafruit SSD1306 libraries if not already installed, then upload to the ESP32.
11. Sign in to the deployed dashboard using `DASHBOARD_PASSWORD`, then click **Check Status**.

## What is displayed

- Estimated CPU busy percentage, CPU frequency and core count
- Heap total/free/used/minimum-free
- Wi-Fi RSSI, IP address and MAC address
- ESP32 internal temperature sensor reading (approximate, not ambient)
- Flash size, sketch size and free OTA sketch space
- PSRAM, uptime, OLED availability, sleep state and network state
- A history chart containing on-demand snapshots (not continuous samples)

## Security and limitations

- The device token protects device endpoints; the dashboard uses a signed HttpOnly session cookie.
- The sketch currently uses `WiFiClientSecure.setInsecure()`: traffic is encrypted with HTTPS, but the ESP32 does not validate the server certificate. For a hardened deployment, configure certificate validation.
- CPU load is an estimate based on FreeRTOS idle-task activity. Chip temperature is approximate.
- The website cannot directly reach an ESP32 behind a typical home router. The ESP32 polls the Vercel request endpoint every 30 seconds.
- This project is monitoring-only; it does not add remote GPIO, reboot, or animation controls.
