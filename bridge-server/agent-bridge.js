#!/usr/bin/env node

/**
 * Agent-Optimized Bridge Server for T-Keyboard
 * Simplified server designed to work with Claude agent monitoring
 */

const WebSocket = require('ws');
const http = require('http');
const fs = require('fs');
const Bonjour = require('bonjour-service');

// Configuration
const config = {
    wsPort: 8080,
    httpPort: 8081,
    debug: process.env.DEBUG === 'true'
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
    if (tkeyboardClient && tkeyboardClient.readyState === WebSocket.OPEN) {
        tkeyboardClient.send(JSON.stringify(data));
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

    // GET /hook/get-inputs - Retrieve and clear input queue
    if (url.pathname === '/hook/get-inputs' && req.method === 'GET') {
        const inputs = [...inputQueue];
        inputQueue.length = 0;  // Clear queue

        // Update last poll time - agent is alive
        lastAgentPoll = Date.now();

        res.writeHead(200, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ inputs }));

        if (config.debug && inputs.length > 0) {
            console.log(`Agent retrieved ${inputs.length} inputs`);
        }
    }

    // POST /update - Update keyboard display
    else if (url.pathname === '/update' && req.method === 'POST') {
        let body = '';
        req.on('data', chunk => body += chunk);
        req.on('end', () => {
            try {
                const data = JSON.parse(body);
                sendToKeyboard(data);
                res.writeHead(200);
                res.end(JSON.stringify({ success: true }));
            } catch (err) {
                res.writeHead(400);
                res.end(JSON.stringify({ error: err.message }));
            }
        });
    }

    // POST /hook/update - Update state (for agent to signal status)
    else if (url.pathname === '/hook/update' && req.method === 'POST') {
        let body = '';
        req.on('data', chunk => body += chunk);
        req.on('end', () => {
            try {
                const data = JSON.parse(body);

                // Update keyboard based on state
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
                res.end(JSON.stringify({ success: true }));
            } catch (err) {
                res.writeHead(400);
                res.end(JSON.stringify({ error: err.message }));
            }
        });
    }

    // GET /status - Server status
    else if (url.pathname === '/status' && req.method === 'GET') {
        res.writeHead(200, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({
            connected: tkeyboardClient !== null,
            queueLength: inputQueue.length,
            uptime: process.uptime()
        }));
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