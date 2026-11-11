import {readFileSync} from 'node:fs';
import {pathToFileURL} from 'node:url';

const reader = readFileSync(new URL('../ui/video-frame-reader.js', import.meta.url),'utf8');

// Generate a serializable function for Playwright MCP browser_evaluate.
export function videoEvaluation(operation, value, selector = '') {
  if (operation === 'bind') {
    const url = new URL(value);
    if (!['http:','https:'].includes(url.protocol) || url.username || url.password
        || !value || value.length > 768 || typeof selector !== 'string' || selector.length > 256) {
      throw new Error('Expected an HTTP(S) page URL and an optional bounded selector.');
    }
    return `() => (${reader}).bind(${JSON.stringify(url.href)}, ${JSON.stringify(selector)})`;
  }
  if (operation === 'frame' && typeof value === 'string' && /^[a-f0-9]{32}$/.test(value)) {
    return `() => (${reader}).boundFrame(${JSON.stringify(value)})`;
  }
  throw new Error('Usage: PlaywrightVideo.mjs bind <page URL> [video selector] | frame <binding ID>');
}

// The caller supplies its existing Page. It owns the browser connection and its lifetime.
export async function bindVideoPage(page, expectedUrl, selector) {
  return page.evaluate(`(${videoEvaluation('bind',expectedUrl,selector)})()`);
}

export async function captureVideoPage(page, bindingId) {
  return page.evaluate(`(${videoEvaluation('frame',bindingId)})()`);
}

if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
  try {
    process.stdout.write(videoEvaluation(...process.argv.slice(2)) + '\n');
  } catch (error) {
    process.stderr.write(error.message + '\n');
    process.exitCode = 2;
  }
}
