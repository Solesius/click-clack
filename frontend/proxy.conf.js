const rawBackendUrl = process.env.CC_BACKEND_URL?.trim() || 'http://localhost:33514';

let backendUrl;
try {
  backendUrl = new URL(rawBackendUrl);
} catch {
  throw new Error(
    `Invalid CC_BACKEND_URL "${rawBackendUrl}". Use an absolute http:// or https:// URL, e.g. http://localhost:33514.`
  );
}

if (!['http:', 'https:'].includes(backendUrl.protocol)) {
  throw new Error(
    `Invalid CC_BACKEND_URL protocol "${backendUrl.protocol}". Only http:// and https:// are supported.`
  );
}

const wsProtocol = backendUrl.protocol === 'https:' ? 'wss:' : 'ws:';
const wsBackendUrl = `${wsProtocol}//${backendUrl.host}`;
const isLocalhost = ['localhost', '127.0.0.1', '::1'].includes(backendUrl.hostname);
// Allow self-signed local certs in dev only; keep strict TLS verification elsewhere.
const allowInsecureTls = isLocalhost;

console.log(
  `[proxy] backend=${backendUrl.origin} ws=${wsBackendUrl} insecureTlsForLocalhost=${allowInsecureTls}`
);

module.exports = {
  '/api': {
    target: backendUrl.origin,
    secure: !allowInsecureTls,
  },
  '/ws': {
    target: wsBackendUrl,
    ws: true,
    secure: !allowInsecureTls,
  },
};
