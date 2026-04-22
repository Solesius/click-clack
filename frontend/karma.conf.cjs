// Karma config — Angular 20 + Jasmine + headless Chromium.
// Driven by @angular/build:karma via `ng test`.

const { execSync } = require('child_process')
const fs = require('fs')

// Resolve a Chromium/Chrome binary if CHROME_BIN is not already set.
// Probes common Linux install locations so `npm test` works without
// requiring the caller to export CHROME_BIN manually.
if (!process.env.CHROME_BIN) {
  const candidates = [
    '/usr/bin/chromium-browser',
    '/usr/bin/chromium',
    '/usr/bin/google-chrome',
    '/usr/bin/google-chrome-stable',
    '/snap/bin/chromium',
  ]
  for (const c of candidates) {
    if (fs.existsSync(c)) {
      process.env.CHROME_BIN = c
      break
    }
  }
  // Last resort: try `which chromium-browser`
  if (!process.env.CHROME_BIN) {
    try {
      process.env.CHROME_BIN = execSync('which chromium-browser || which chromium || which google-chrome', {
        stdio: ['pipe', 'pipe', 'ignore'],
      }).toString().trim().split('\n')[0]
    } catch { /* no browser found; karma will emit a useful error */ }
  }
}

module.exports = function (config) {
  config.set({
    basePath: '',
    frameworks: ['jasmine'],
    plugins: [
      require('karma-jasmine'),
      require('karma-chrome-launcher'),
      require('karma-jasmine-html-reporter'),
      require('karma-coverage'),
    ],
    client: {
      jasmine: { random: false, stopOnSpecFailure: false, failSpecWithNoExpectations: true },
      clearContext: false,
    },
    jasmineHtmlReporter: { suppressAll: true },
    coverageReporter: {
      dir: require('path').join(__dirname, './coverage'),
      subdir: '.',
      reporters: [{ type: 'html' }, { type: 'text-summary' }, { type: 'lcovonly' }],
    },
    reporters: ['progress', 'kjhtml'],
    browsers: ['ChromeHeadlessCI'],
    customLaunchers: {
      ChromeHeadlessCI: {
        base: 'ChromeHeadless',
        flags: ['--no-sandbox', '--disable-gpu', '--disable-dev-shm-usage'],
      },
    },
    restartOnFileChange: true,
    singleRun: false,
    autoWatch: true,
  })
}
