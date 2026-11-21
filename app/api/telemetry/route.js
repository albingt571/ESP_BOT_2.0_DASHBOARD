import { NextResponse } from 'next/server';
import { saveTelemetry, completeStatusRequest } from '@/lib/redis';

export const runtime = 'nodejs';
export const dynamic = 'force-dynamic';

export async function POST(request) {
  const expected = process.env.DEVICE_TOKEN;
  const supplied = request.headers.get('x-device-token');
  if (!expected || !supplied || supplied !== expected) {
    return NextResponse.json({ error: 'Unauthorized device' }, { status: 401 });
  }

  let body;
  try {
    body = await request.json();
  } catch {
    return NextResponse.json({ error: 'Invalid JSON' }, { status: 400 });
  }

  if (body?.deviceId !== 'canda-bot' || typeof body?.requestId !== 'string' || body.requestId.length < 10 || body.requestId.length > 100) {
    return NextResponse.json({ error: 'Unknown device' }, { status: 400 });
  }

  const numericFields = [
    'chipRevision', 'cpuMHz', 'cores', 'cpuLoadPercent', 'chipTemperatureC', 'heapTotal', 'heapFree', 'heapMinFree',
    'heapUsed', 'flashSize', 'sketchSize', 'sketchFree', 'psramTotal',
    'psramFree', 'uptimeSeconds', 'wifiRssi', 'internetFailures'
  ];
  for (const field of numericFields) {
    if (!Number.isFinite(Number(body[field]))) {
      return NextResponse.json({ error: `Invalid field: ${field}` }, { status: 400 });
    }
  }

  if (Number(body.cpuLoadPercent) < 0 || Number(body.cpuLoadPercent) > 100 || Number(body.chipTemperatureC) < -40 || Number(body.chipTemperatureC) > 150) {
    return NextResponse.json({ error: 'CPU load or temperature is outside accepted range' }, { status: 400 });
  }

  const safeData = {
    ...body,
    receivedAt: new Date().toISOString(),
    online: true
  };

  try {
    const completed = await completeStatusRequest('canda-bot', body.requestId, safeData);
    if (!completed) return NextResponse.json({ error: 'No matching pending status request' }, { status: 409 });
    await saveTelemetry('canda-bot', safeData);
    return NextResponse.json({ ok: true, receivedAt: safeData.receivedAt });
  } catch (error) {
    console.error('Telemetry storage error:', error);
    return NextResponse.json({ error: 'Telemetry storage unavailable' }, { status: 503 });
  }
}
