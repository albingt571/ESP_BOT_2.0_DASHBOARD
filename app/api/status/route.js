import { NextResponse } from 'next/server';
import { randomUUID } from 'node:crypto';
import { isAuthenticated } from '@/lib/auth';
import { createStatusRequest, readStatusResponse, readTelemetryHistory } from '@/lib/redis';

export const runtime = 'nodejs';
export const dynamic = 'force-dynamic';

export async function GET(request) {
  if (!await isAuthenticated()) return NextResponse.json({ error: 'Unauthorized' }, { status: 401 });
  const requestId = new URL(request.url).searchParams.get('requestId');
  if (!requestId) return NextResponse.json({ authenticated: true }, { headers: { 'Cache-Control': 'no-store, max-age=0' } });
  try {
    const telemetry = await readStatusResponse(requestId);
    if (!telemetry) return NextResponse.json({ ready: false }, { headers: { 'Cache-Control': 'no-store, max-age=0' } });
    const history = await readTelemetryHistory('canda-bot');
    return NextResponse.json({ ready: true, online: true, telemetry: { ...telemetry, online: true }, history }, { headers: { 'Cache-Control': 'no-store, max-age=0' } });
  } catch (error) {
    console.error('Status response read error:', error);
    return NextResponse.json({ error: 'Unable to read device status. Check Upstash configuration.' }, { status: 503 });
  }
}

export async function POST() {
  if (!await isAuthenticated()) return NextResponse.json({ error: 'Unauthorized' }, { status: 401 });
  try {
    const requestId = randomUUID();
    await createStatusRequest('canda-bot', requestId);
    return NextResponse.json({ ok: true, requestId, message: 'Status request queued. The ESP32 checks every 30 seconds.' }, { headers: { 'Cache-Control': 'no-store, max-age=0' } });
  } catch (error) {
    console.error('Status request creation error:', error);
    return NextResponse.json({ error: 'Unable to create status request. Check Upstash configuration.' }, { status: 503 });
  }
}
