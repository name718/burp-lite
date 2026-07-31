#!/usr/bin/env node
const http = require('http');

const prompt = process.argv[2];
const port = process.env.ASKPASS_PORT;

if (!port) {
  process.exit(1);
}

const req = http.request({
  hostname: '127.0.0.1',
  port: parseInt(port, 10),
  path: '/',
  method: 'POST',
  headers: {
    'Content-Type': 'application/json'
  }
}, (res) => {
  let body = '';
  res.on('data', chunk => body += chunk);
  res.on('end', () => {
    console.log(body);
    process.exit(0);
  });
});

req.on('error', () => {
  process.exit(1);
});

req.write(JSON.stringify({ prompt: prompt || 'Password:' }));
req.end();
