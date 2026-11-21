import { createHmac, timingSafeEqual } from 'node:crypto';
import { cookies } from 'next/headers';

const COOKIE_NAME = 'canda_dashboard_session';
const MAX_AGE_SECONDS = 7 * 24 * 60 * 60;

function sign(value) {
  const secret = process.env.SESSION_SECRET;
  if (!secret) throw new Error('SESSION_SECRET is not configured');
  return createHmac('sha256', secret).update(value).digest('hex');
}

export function makeSession() {
  const issuedAt = Math.floor(Date.now() / 1000).toString();
  return `${issuedAt}.${sign(issuedAt)}`;
}

export function verifySessionValue(value) {
  if (!value || !process.env.SESSION_SECRET) return false;
  const [issuedAt, signature, extra] = value.split('.');
  if (!issuedAt || !signature || extra) return false;
  const age = Math.floor(Date.now() / 1000) - Number(issuedAt);
  if (!Number.isFinite(age) || age < 0 || age > MAX_AGE_SECONDS) return false;
  const expected = Buffer.from(sign(issuedAt), 'hex');
  let supplied;
  try { supplied = Buffer.from(signature, 'hex'); } catch { return false; }
  return supplied.length === expected.length && timingSafeEqual(supplied, expected);
}

export async function isAuthenticated() {
  const jar = await cookies();
  return verifySessionValue(jar.get(COOKIE_NAME)?.value);
}

export const sessionCookie = {
  name: COOKIE_NAME,
  options: {
    httpOnly: true,
    secure: process.env.NODE_ENV === 'production',
    sameSite: 'strict',
    path: '/',
    maxAge: MAX_AGE_SECONDS
  }
};
