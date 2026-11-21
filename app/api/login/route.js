import { NextResponse } from 'next/server';
import { createHash, timingSafeEqual } from 'node:crypto';
import { makeSession, sessionCookie } from '@/lib/auth';

export const runtime = 'nodejs';

export async function POST(request) {
  let body;
  try { body = await request.json(); }
  catch { return NextResponse.json({ error: 'Invalid request' }, { status: 400 }); }

  const expected = process.env.DASHBOARD_PASSWORD || '';
  const supplied = String(body?.password || '');
  if (!expected) return NextResponse.json({ error: 'DASHBOARD_PASSWORD is not configured' }, { status: 503 });

  const a = createHash('sha256').update(supplied).digest();
  const b = createHash('sha256').update(expected).digest();
  if (!timingSafeEqual(a, b)) {
    return NextResponse.json({ error: 'Incorrect password' }, { status: 401 });
  }

  const response = NextResponse.json({ ok: true });
  response.cookies.set(sessionCookie.name, makeSession(), sessionCookie.options);
  return response;
}
