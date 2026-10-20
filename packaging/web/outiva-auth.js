// Optional Outiva identity for browser clients. Direct visits remain anonymous;
// the Outiva console starts a silent PKCE flow with ?outiva_sso=1.
const AUTHORITY = 'https://accounts.outiva.com/realms/outiva';
const CALLBACK_KEYS = ['code', 'state', 'session_state', 'iss', 'error', 'error_description', 'outiva_sso'];
const encoder = new TextEncoder();

const base64url = bytes => btoa(String.fromCharCode(...bytes)).replaceAll('+', '-').replaceAll('/', '_').replaceAll('=', '');
const decode = value => {
  const padded = value.replaceAll('-', '+').replaceAll('_', '/') + '==='.slice((value.length + 3) % 4);
  return Uint8Array.from(atob(padded), character => character.charCodeAt(0));
};
const jsonPart = value => JSON.parse(new TextDecoder().decode(decode(value)));
const randomValue = (size = 32) => { const bytes = new Uint8Array(size); crypto.getRandomValues(bytes); return base64url(bytes); };
const storageKey = clientId => `outiva.identity.${clientId}`;
const pendingKey = clientId => `outiva.identity.pending.${clientId}`;

function returnToConsole(appId) {
  location.replace(`https://outiva.com/?outiva_sso=complete#/play/${encodeURIComponent(appId)}`);
}

function clearCallback() {
  const url = new URL(location.href);
  CALLBACK_KEYS.forEach(key => url.searchParams.delete(key));
  history.replaceState(null, '', `${url.pathname}${url.search}${url.hash}`);
}

async function verifyIdToken(token, clientId, expectedNonce = '') {
  const [encodedHeader, encodedClaims, encodedSignature] = token.split('.');
  const header = jsonPart(encodedHeader), claims = jsonPart(encodedClaims);
  const audiences = Array.isArray(claims.aud) ? claims.aud : [claims.aud];
  if (header.alg !== 'RS256' || claims.iss !== AUTHORITY || !audiences.includes(clientId) ||
      claims.exp * 1000 <= Date.now() || (expectedNonce && claims.nonce !== expectedNonce)) throw new Error('Invalid identity claims');
  const response = await fetch(`${AUTHORITY}/protocol/openid-connect/certs`, { cache: 'no-store' });
  if (!response.ok) throw new Error('Could not load identity signing keys');
  const key = (await response.json()).keys.find(candidate => candidate.kid === header.kid && candidate.kty === 'RSA');
  if (!key) throw new Error('Unknown identity signing key');
  const cryptoKey = await crypto.subtle.importKey('jwk', key, { name: 'RSASSA-PKCS1-v1_5', hash: 'SHA-256' }, false, ['verify']);
  const valid = await crypto.subtle.verify('RSASSA-PKCS1-v1_5', cryptoKey, decode(encodedSignature), encoder.encode(`${encodedHeader}.${encodedClaims}`));
  if (!valid) throw new Error('Invalid identity signature');
  return { sub: claims.sub, name: claims.name || '', preferred_username: claims.preferred_username || '', email: claims.email || '', exp: claims.exp };
}

async function begin(clientId) {
  const verifier = randomValue(64);
  const pending = { verifier, state: randomValue(), nonce: randomValue(), created: Date.now() };
  sessionStorage.setItem(pendingKey(clientId), JSON.stringify(pending));
  const challenge = base64url(new Uint8Array(await crypto.subtle.digest('SHA-256', encoder.encode(verifier))));
  const query = new URLSearchParams({
    client_id: clientId, redirect_uri: `${location.origin}/`, response_type: 'code', response_mode: 'query',
    scope: 'openid profile email', prompt: 'none', state: pending.state, nonce: pending.nonce,
    code_challenge: challenge, code_challenge_method: 'S256',
  });
  location.replace(`${AUTHORITY}/protocol/openid-connect/auth?${query}`);
}

async function finish(clientId, appId, params) {
  const pending = JSON.parse(sessionStorage.getItem(pendingKey(clientId)) || 'null');
  sessionStorage.removeItem(pendingKey(clientId));
  if (!pending || params.get('state') !== pending.state || Date.now() - pending.created > 10 * 60 * 1000 || params.has('error')) {
    clearCallback(); returnToConsole(appId); return null;
  }
  const response = await fetch(`${AUTHORITY}/protocol/openid-connect/token`, {
    method: 'POST', headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
    body: new URLSearchParams({ grant_type: 'authorization_code', client_id: clientId, redirect_uri: `${location.origin}/`, code: params.get('code') || '', code_verifier: pending.verifier }),
  });
  if (!response.ok) throw new Error('Identity token exchange failed');
  const tokens = await response.json();
  const profile = await verifyIdToken(tokens.id_token, clientId, pending.nonce);
  localStorage.setItem(storageKey(clientId), tokens.id_token);
  clearCallback(); returnToConsole(appId);
  return profile;
}

export async function initialiseOutivaIdentity({ clientId, appId }) {
  const params = new URLSearchParams(location.search);
  try {
    if (params.has('code') || params.has('error')) return await finish(clientId, appId, params);
    if (params.get('outiva_sso') === '1') { await begin(clientId); return null; }
    const token = localStorage.getItem(storageKey(clientId));
    if (!token) return null;
    try { return await verifyIdToken(token, clientId); }
    catch { localStorage.removeItem(storageKey(clientId)); return null; }
  } catch (error) {
    console.warn('Optional Outiva sign-in was not available:', error);
    clearCallback();
    if (params.has('code') || params.has('error') || params.get('outiva_sso') === '1') returnToConsole(appId);
    return null;
  }
}

export function identityCallsign(profile, limit = 16) {
  const source = profile?.preferred_username || profile?.name || profile?.email?.split('@')[0] || '';
  return source.replace(/[^A-Za-z0-9 _-]+/g, ' ').replace(/\s+/g, ' ').trim().slice(0, limit);
}
