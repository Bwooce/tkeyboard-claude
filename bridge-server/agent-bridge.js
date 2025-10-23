#!/usr/bin/env node

/**
 * Agent-Optimized Bridge Server for T-Keyboard
 * Simplified server designed to work with Claude agent monitoring
 */

const WebSocket = require('ws');
const http = require('http');
const fs = require('fs');
const crypto = require('crypto');
const { execSync } = require('child_process');
const Bonjour = require('bonjour-service');

// Configuration
const config = {
    wsPort: 8080,
    httpPort: 8081,
    debug: process.env.DEBUG === 'true',
    sessionId: process.env.CLAUDE_SESSION_ID || `tk-${Date.now()}-${process.pid}`,
    claudePid: parseInt(process.env.CLAUDE_PID || process.ppid)
};

// Initialize Bonjour for mDNS
const bonjour = new Bonjour.Bonjour();

// State
let tkeyboardClient = null;
const inputQueue = [];
const MAX_QUEUE_SIZE = 20;
let lastAgentPoll = Date.now();
const AGENT_TIMEOUT = 10000; // 10 seconds without polling = assume rate limited

// WebSocket server for T-Keyboard
const wss = new WebSocket.Server({ port: config.wsPort });

wss.on('connection', (ws) => {
    console.log('T-Keyboard connected');
    tkeyboardClient = ws;

    ws.on('message', (message) => {
        try {
            const data = JSON.parse(message);
            handleKeyboardMessage(data);
        } catch (err) {
            console.error('Invalid message:', err);
        }
    });

    ws.on('close', () => {
        console.log('T-Keyboard disconnected');
        tkeyboardClient = null;
    });

    // Send initial state
    sendToKeyboard({
        type: 'status',
        state: 'idle'
    });
});

// Handle T-Keyboard messages
function handleKeyboardMessage(data) {
    if (config.debug) console.log('From keyboard:', data);

    switch (data.type) {
        case 'register':
            console.log('T-Keyboard registered');
            break;

        case 'key_press':
            // Queue the input for agent to retrieve
            inputQueue.push({
                key: data.key,
                text: data.text,
                timestamp: Date.now()
            });

            // Limit queue size
            while (inputQueue.length > MAX_QUEUE_SIZE) {
                inputQueue.shift();
            }

            console.log(`Queued: "${data.text}" (${inputQueue.length} items)`);
            break;
    }
}

// Send message to T-Keyboard
function sendToKeyboard(data) {
    // Translate simple {"buttons":[...]} format to ESP32's expected format
    if (data.buttons && Array.isArray(data.buttons)) {
        const images = data.images || [];
        const actions = data.actions || [];  // Optional: separate action text
        data = {
            type: 'update_options',
            session_id: Date.now().toString(),
            options: data.buttons.map((text, index) => ({
                text: text || '',
                action: actions[index] || '',  // If empty, ESP32 will use text
                image: images[index] || '',
                color: index === 0 ? '#00FFFF' : index === 1 ? '#FFFF00' : index === 2 ? '#FFFFFF' : '#00FF00'
            }))
        };
    }

    if (tkeyboardClient && tkeyboardClient.readyState === WebSocket.OPEN) {
        const message = JSON.stringify(data);
        console.log(`→ To keyboard: ${message.substring(0, 200)}${message.length > 200 ? '...' : ''}`);
        tkeyboardClient.send(message);
    } else {
        console.log(`✗ Cannot send to keyboard (not connected): ${JSON.stringify(data).substring(0, 100)}`);
    }
}

// HTTP API for Claude agent
const server = http.createServer((req, res) => {
    const url = new URL(req.url, `http://${req.headers.host}`);

    // CORS headers
    res.setHeader('Access-Control-Allow-Origin', '*');
    res.setHeader('Access-Control-Allow-Methods', 'GET, POST');
    res.setHeader('Access-Control-Allow-Headers', 'Content-Type');

    if (req.method === 'OPTIONS') {
        res.writeHead(200);
        res.end();
        return;
    }

    // GET /inputs - Retrieve and clear input queue (polled by daemon)
    if (url.pathname === '/inputs' && req.method === 'GET') {
        const inputs = [...inputQueue];
        inputQueue.length = 0;  // Clear queue

        // Update last poll time - daemon is alive
        lastAgentPoll = Date.now();

        res.writeHead(200, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({
            sessionId: config.sessionId,
            claudePid: config.claudePid,
            inputs
        }));

        if (config.debug && inputs.length > 0) {
            console.log(`Daemon retrieved ${inputs.length} inputs`);
        }
    }

    // POST /update - Update keyboard display
    else if (url.pathname === '/update' && req.method === 'POST') {
        let body = '';
        req.on('data', chunk => body += chunk);
        req.on('end', () => {
            try {
                const data = JSON.parse(body);

                // Check if keyboard is connected
                if (!tkeyboardClient || tkeyboardClient.readyState !== WebSocket.OPEN) {
                    res.writeHead(503);
                    res.end(JSON.stringify({
                        success: false,
                        error: 'T-Keyboard not connected',
                        connected: false
                    }));
                    return;
                }

                sendToKeyboard(data);
                res.writeHead(200);
                res.end(JSON.stringify({ success: true, connected: true }));
            } catch (err) {
                res.writeHead(400);
                res.end(JSON.stringify({ error: err.message }));
            }
        });
    }

    // POST /state/update - Update state (for daemon to signal status)
    else if (url.pathname === '/state/update' && req.method === 'POST') {
        let body = '';
        req.on('data', chunk => body += chunk);
        req.on('end', () => {
            try {
                const data = JSON.parse(body);

                // Check if keyboard is connected
                const isConnected = tkeyboardClient && tkeyboardClient.readyState === WebSocket.OPEN;

                // Update keyboard based on state (if connected)
                if (data.event === 'thinking_start') {
                    sendToKeyboard({
                        type: 'status',
                        state: 'thinking'
                    });
                } else if (data.event === 'thinking_end' || data.event === 'stop') {
                    sendToKeyboard({
                        type: 'status',
                        state: 'idle'
                    });
                } else if (data.event === 'error') {
                    sendToKeyboard({
                        type: 'status',
                        state: 'error',
                        message: data.state?.message
                    });
                } else if (data.event === 'rate_limit') {
                    sendToKeyboard({
                        type: 'status',
                        state: 'limit',
                        countdown: data.state?.countdown || data.state?.retry_after || 60
                    });
                }

                res.writeHead(200);
                res.end(JSON.stringify({ success: true, connected: isConnected }));
            } catch (err) {
                res.writeHead(400);
                res.end(JSON.stringify({ error: err.message }));
            }
        });
    }

    // GET /session/verify - Verify session is still active
    else if (url.pathname === '/session/verify' && req.method === 'GET') {
        try {
            // Check if Claude process is still alive
            process.kill(config.claudePid, 0);
            res.writeHead(200, { 'Content-Type': 'application/json' });
            res.end(JSON.stringify({
                sessionId: config.sessionId,
                claudePid: config.claudePid,
                alive: true
            }));
        } catch (err) {
            res.writeHead(200, { 'Content-Type': 'application/json' });
            res.end(JSON.stringify({
                sessionId: config.sessionId,
                claudePid: config.claudePid,
                alive: false
            }));
        }
    }

    // POST /test/button - Simulate button press (for testing without physical keyboard)
    else if (url.pathname === '/test/button' && req.method === 'POST') {
        let body = '';
        req.on('data', chunk => body += chunk);
        req.on('end', () => {
            try {
                const data = JSON.parse(body);
                // Simulate keyboard message
                handleKeyboardMessage({
                    type: 'key_press',
                    key: data.key || 1,
                    text: data.text || 'Test'
                });
                res.writeHead(200, { 'Content-Type': 'application/json' });
                res.end(JSON.stringify({ success: true, queued: inputQueue.length }));
            } catch (err) {
                res.writeHead(400, { 'Content-Type': 'application/json' });
                res.end(JSON.stringify({ error: err.message }));
            }
        });
    }

    // GET /status - Server status
    else if (url.pathname === '/status' && req.method === 'GET') {
        res.writeHead(200, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({
            sessionId: config.sessionId,
            claudePid: config.claudePid,
            connected: tkeyboardClient !== null,
            queueLength: inputQueue.length,
            uptime: process.uptime()
        }));
    }

    // POST /generate-image - Generate icon dynamically
    else if (url.pathname === '/generate-image' && req.method === 'POST') {
        let body = '';
        req.on('data', chunk => body += chunk);
        req.on('end', () => {
            try {
                const { emoji, text, color } = JSON.parse(body);

                if (!emoji && !text) {
                    res.writeHead(400, { 'Content-Type': 'application/json' });
                    res.end(JSON.stringify({ error: 'Either emoji or text required' }));
                    return;
                }

                // Generate hash-based filename for caching
                const hashInput = `${emoji || ''}-${text || ''}-${color || ''}`;
                const hash = crypto.createHash('md5').update(hashInput).digest('hex').substring(0, 8);
                const filename = `${hash}.rgb`;
                const path = require('path');
                const imagePath = path.join(__dirname, '../images/cache', filename);

                // Check if already exists
                if (fs.existsSync(imagePath)) {
                    console.log(`[Generate Image] Cache hit: ${filename}`);
                    res.writeHead(200, { 'Content-Type': 'application/json' });
                    res.end(JSON.stringify({
                        success: true,
                        filename,
                        cached: true
                    }));
                    return;
                }

                // Generate image
                console.log(`[Generate Image] Creating: ${filename} (emoji: ${emoji}, text: ${text})`);

                // generate-images.js expects: --emoji "EMOJI" name OR --text "TEXT" name
                // Prefer emoji if both provided
                let cmd = 'node generate-images.js';
                if (emoji) {
                    cmd += ` --emoji "${emoji}" ${hash}`;
                } else if (text) {
                    cmd += ` --text "${text}" ${hash}`;
                }
                // Note: color parameter not yet supported by generate-images.js

                try {
                    execSync(cmd, {
                        cwd: __dirname,
                        stdio: 'pipe'
                    });

                    console.log(`[Generate Image] Created: ${filename}`);
                    res.writeHead(200, { 'Content-Type': 'application/json' });
                    res.end(JSON.stringify({
                        success: true,
                        filename,
                        cached: false
                    }));
                } catch (err) {
                    console.error(`[Generate Image] Error: ${err.message}`);
                    res.writeHead(500, { 'Content-Type': 'application/json' });
                    res.end(JSON.stringify({
                        error: 'Image generation failed',
                        details: err.message
                    }));
                }
            } catch (err) {
                console.error(`[Generate Image] Parse error: ${err.message}`);
                res.writeHead(400, { 'Content-Type': 'application/json' });
                res.end(JSON.stringify({ error: 'Invalid JSON' }));
            }
        });
    }

    // GET /images/* - Serve image files
    else if (req.method === 'GET' && url.pathname.startsWith('/images/')) {
        const imageName = url.pathname.substring('/images/'.length);
        const path = require('path');
        const imagePath = path.join(__dirname, '../images/cache', imageName);

        console.log(`[Image Request] ${imageName} -> ${imagePath}`);

        fs.readFile(imagePath, (err, data) => {
            if (err) {
                console.log(`[Image Error] ${imagePath}: ${err.message}`);
                res.writeHead(404, { 'Content-Type': 'text/plain' });
                res.end('Image not found');
            } else {
                console.log(`[Image Served] ${imageName} (${data.length} bytes)`);
                res.writeHead(200, {
                    'Content-Type': 'application/octet-stream',
                    'Content-Length': data.length
                });
                res.end(data);
            }
        });
    }

    else {
        res.writeHead(404);
        res.end('Not found');
    }
});

// Monitor agent health and detect rate limits
let agentWasAlive = true;
setInterval(() => {
    const timeSinceLastPoll = Date.now() - lastAgentPoll;

    if (timeSinceLastPoll > AGENT_TIMEOUT && agentWasAlive) {
        // Agent stopped polling - likely rate limited
        console.log('⚠️  Agent stopped polling - assuming rate limit');
        agentWasAlive = false;

        if (tkeyboardClient && tkeyboardClient.readyState === WebSocket.OPEN) {
            sendToKeyboard({
                type: 'status',
                state: 'limit',
                message: 'Rate limit - waiting for recovery'
            });

            console.log('Sent rate limit state to keyboard');
        }
    } else if (timeSinceLastPoll <= AGENT_TIMEOUT && !agentWasAlive) {
        // Agent resumed - rate limit ended
        console.log('✓ Agent resumed polling - rate limit ended');
        agentWasAlive = true;

        if (tkeyboardClient && tkeyboardClient.readyState === WebSocket.OPEN) {
            sendToKeyboard({
                type: 'status',
                state: 'idle'
            });
        }
    }
}, 5000); // Check every 5 seconds

server.listen(config.httpPort, () => {
    console.log(`
╔════════════════════════════════════════╗
║   T-Keyboard Agent Bridge Server       ║
╠════════════════════════════════════════╣
║   WebSocket: ws://localhost:${config.wsPort}       ║
║   HTTP API:  http://localhost:${config.httpPort}     ║
╠════════════════════════════════════════╣
║   Status: Running                      ║
║   Mode: Agent-Optimized                ║
║   Agent Timeout: ${AGENT_TIMEOUT / 1000}s                 ║
╚════════════════════════════════════════╝

Ready for connections...
`);

    // Publish mDNS service
    bonjour.publish({
        name: 'tkeyboard-bridge',
        type: 'http',
        port: config.httpPort,
        host: 'tkeyboard-bridge.local',
        txt: {
            ws_port: config.wsPort.toString(),
            api_port: config.httpPort.toString()
        }
    });

    console.log('mDNS service published: tkeyboard-bridge.local');
});

// Graceful shutdown
process.on('SIGINT', () => {
    console.log('\nShutting down...');
    bonjour.unpublishAll();
    bonjour.destroy();
    wss.close();
    server.close();
    process.exit(0);
});