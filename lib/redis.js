const baseUrl = process.env.UPSTASH_REDIS_REST_URL?.replace(/\/$/, '');
const token = process.env.UPSTASH_REDIS_REST_TOKEN;

async function command(parts) {
  if (!baseUrl || !token) throw new Error('Upstash Redis environment variables are missing');
  const response = await fetch(baseUrl, {
    method: 'POST',
    headers: {
      Authorization: `Bearer ${token}`,
      'Content-Type': 'application/json'
    },
    body: JSON.stringify(parts),
    cache: 'no-store'
  });
  if (!response.ok) throw new Error(`Redis request failed (${response.status})`);
  const result = await response.json();
  if (result.error) throw new Error(result.error);
  return result.result;
}

export async function saveTelemetry(deviceId, data) {
  await command(['SET', `canda:telemetry:${deviceId}`, JSON.stringify(data), 'EX', '120']);
  const sample = JSON.stringify({
    at: data.receivedAt || new Date().toISOString(),
    cpuLoadPercent: data.cpuLoadPercent,
    heapPercent: data.heapTotal ? (data.heapUsed / data.heapTotal) * 100 : null,
    wifiRssi: data.wifiRssi,
    chipTemperatureC: data.chipTemperatureC
  });
  await command(['LPUSH', `canda:history:${deviceId}`, sample]);
  await command(['LTRIM', `canda:history:${deviceId}`, '0', '287']);
  await command(['EXPIRE', `canda:history:${deviceId}`, '86400']);
}

export async function readTelemetry(deviceId) {
  const value = await command(['GET', `canda:telemetry:${deviceId}`]);
  return value ? JSON.parse(value) : null;
}

export async function readTelemetryHistory(deviceId) {
  const values = await command(['LRANGE', `canda:history:${deviceId}`, '0', '287']);
  return Array.isArray(values) ? values.map(value => JSON.parse(value)).reverse() : [];
}



export async function createStatusRequest(deviceId, requestId) {
  const request = { requestId, createdAt: new Date().toISOString() };
  await command(['SET', `canda:request:${deviceId}`, JSON.stringify(request), 'EX', '180']);
  return request;
}

export async function readPendingStatusRequest(deviceId) {
  const value = await command(['GET', `canda:request:${deviceId}`]);
  return value ? JSON.parse(value) : null;
}

export async function completeStatusRequest(deviceId, requestId, data) {
  const pending = await readPendingStatusRequest(deviceId);
  if (!pending || pending.requestId !== requestId) return false;
  await command(['SET', `canda:response:${requestId}`, JSON.stringify(data), 'EX', '180']);
  await command(['DEL', `canda:request:${deviceId}`]);
  return true;
}

export async function readStatusResponse(requestId) {
  const value = await command(['GET', `canda:response:${requestId}`]);
  return value ? JSON.parse(value) : null;
}
