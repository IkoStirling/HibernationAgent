import assert from 'node:assert/strict';
import http from 'node:http';
import {pathToFileURL} from 'node:url';
import {bindVideoPage,captureVideoPage} from '../tools/PlaywrightVideo.mjs';

const {chromium} = await import(pathToFileURL(process.env.PLAYWRIGHT_PACKAGE_PATH).href);
const server = http.createServer((_,res) => {
  res.setHeader('Content-Type','text/html; charset=utf-8');
  res.end(`<!doctype html><title>Hidden video fixture</title>
    <video id="target" muted autoplay playsinline style="display:none"></video>
    <script>
      window.makeStream = color => {
        const canvas = document.createElement('canvas');
        canvas.width = 96; canvas.height = 54;
        const context = canvas.getContext('2d');
        const paint = () => {
          context.fillStyle = color;
          context.fillRect(0,0,96,54);
        };
        const stream = canvas.captureStream(12);
        setInterval(paint,80);
        paint();
        return stream;
      };
      const target = document.querySelector('video');
      target.srcObject = makeStream('#d86');
      target.play();
    </script>`);
});
await new Promise(resolve => server.listen(0,'127.0.0.1',resolve));
const url = `http://127.0.0.1:${server.address().port}/video`;
let browser;
try {
  browser = await chromium.launch({executablePath:process.env.HIBERNATION_TEST_BROWSER,headless:true,args:['--disable-gpu']});
  const context = await browser.newContext();
  const first = await context.newPage(), second = await context.newPage();
  await Promise.all([first.goto(url),second.goto(url)]);
  await first.waitForFunction(() => document.querySelector('video').readyState >= 2);
  assert.equal(await first.locator('video').evaluate(video => video.getBoundingClientRect().width),0);
  assert.equal((await bindVideoPage(first,url+'?wrong=1','#target')).reason,'page_mismatch');
  const binding = await bindVideoPage(first,url,'#target');
  assert.equal(binding.ok,true);
  const frame = await captureVideoPage(first,binding.binding_id);
  assert.equal(frame.ok,true);
  assert.equal(frame.screenshot_used,false);
  assert.equal(frame.image.mime_type,'image/jpeg');
  const jpeg = Buffer.from(frame.image.base64,'base64');
  assert.ok(jpeg.length > 100 && jpeg[0] === 255 && jpeg[1] === 216);
  assert.equal((await captureVideoPage(second,binding.binding_id)).reason,'page_binding_expired');
  await first.evaluate(() => document.querySelector('video').srcObject = makeStream('#48c'));
  assert.equal((await captureVideoPage(first,binding.binding_id)).reason,'bound_video_changed');
  await first.reload();
  assert.equal((await captureVideoPage(first,binding.binding_id)).reason,'page_binding_expired');
  assert.equal(context.pages().length,2);
  console.log('PASS hidden video JPEG, exact page binding, same-URL tab isolation, source replacement, reload');
} finally {
  await browser?.close();
  server.close();
}
