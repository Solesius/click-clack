// Dynamic proxy config — reads CC_BACKEND_URL from the environment.
// Defaults to the local hub dev port if not set.
//
//   CC_BACKEND_URL=http://my-server:33514 npm start
//
const backendUrl = process.env.CC_BACKEND_URL ?? 'http://localhost:33514';
const wsBackendUrl = backendUrl.replace(/^http/, 'ws');

console.log(`[proxy] → backend: ${backendUrl}`);

module.exports = {
  '/api': {
    target: backendUrl,
    secure: false,
    changeOrigin: true,
  },
  '/ws': {
    target: wsBackendUrl,
    ws: true,
    secure: false,
    changeOrigin: true,
  },
};
