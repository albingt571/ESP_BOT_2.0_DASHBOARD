import { NextResponse } from 'next/server';
import { readPendingStatusRequest } from '@/lib/redis';

export const runtime = 'nodejs';
export const dynamic = 'force-dynamic';

export async function GET(request) {
  const expected = process.env.DEVICE_TOKEN;
  const supplied = request.headers.get('x-device-token');
  if (!expected || !supplied || supplied !== expected) {
    return NextResponse.json({ error: 'Unauthorized device' }, { status: 401 });
  }
  try {
    const pending = await readPendingStatusRequest('canda-bot');
    return NextResponse.json(
      pending ? { pending: true, requestId: pending.requestId } : { pending: false },
      { headers: { 'Cache-Control': 'no-store, max-age=0' } }
    );
  } catch (error) {
    console.error('Device request read error:', error);
    return NextResponse.json({ error: 'Unable to read device request' }, { status: 503 });
  }
}
