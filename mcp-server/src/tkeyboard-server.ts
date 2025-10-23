#!/usr/bin/env node

import { Server } from '@modelcontextprotocol/sdk/server/index.js';
import { StdioServerTransport } from '@modelcontextprotocol/sdk/server/stdio.js';
import {
  CallToolRequestSchema,
  ListToolsRequestSchema,
  ListResourcesRequestSchema,
  ReadResourceRequestSchema,
} from '@modelcontextprotocol/sdk/types.js';
import WebSocket, { WebSocketServer } from 'ws';
import * as http from 'http';
import { Bonjour } from 'bonjour-service';
import { execSync } from 'child_process';
import { ensureIcons } from './icon-generator.js';

// Configuration
const config = {
  wsPort: 8080,
  httpPort: 8081,
  debug: process.env.DEBUG === 'true',
  sessionId: process.env.CLAUDE_SESSION_ID || `tk-${Date.now()}-${process.pid}`,
  claudePid: parseInt(process.env.CLAUDE_PID || String(process.ppid))
};

// State
let tkeyboardClient: WebSocket | null = null;
const inputQueue: Array<{ key: number; text: string; timestamp: number }> = [];
const MAX_QUEUE_SIZE = 20;
let currentContext = {
  type: 'default',
  detail: '',
  buttons: ['Yes', 'No', 'Proceed', 'Help'],
  images: ['yes.rgb', 'no.rgb', 'proceed.rgb', 'help.rgb'],
  timestamp: Date.now()
};

// Initialize Bonjour for mDNS
const bonjour = new Bonjour();

// WebSocket server for T-Keyboard
const wss = new WebSocketServer({ port: config.wsPort });

wss.on('connection', (ws: WebSocket) => {
  console.log('[WS] T-Keyboard connected');
  tkeyboardClient = ws;

  ws.on('message', (message: WebSocket.RawData) => {
    try {
      const data = JSON.parse(message.toString());
      handleKeyboardMessage(data);
    } catch (err) {
      console.error('[WS] Invalid message:', err);
    }
  });

  ws.on('close', () => {
    console.log('[WS] T-Keyboard disconnected');
    tkeyboardClient = null;
  });

  // Send initial state
  sendToKeyboard({
    type: 'status',
    state: 'idle'
  });

  // Send current buttons
  sendToKeyboard({
    type: 'update_options',
    session_id: Date.now().toString(),
    options: currentContext.buttons.map((text, index) => ({
      text: text || '',
      action: text || '',
      image: currentContext.images[index] || '',
      color: index === 0 ? '#00FFFF' : index === 1 ? '#FFFF00' : index === 2 ? '#FFFFFF' : '#00FF00'
    }))
  });
});

// Handle T-Keyboard messages
function handleKeyboardMessage(data: any) {
  if (config.debug) console.log('[WS] From keyboard:', data);

  switch (data.type) {
    case 'register':
      console.log('[WS] T-Keyboard registered');
      break;

    case 'key_press':
      // Queue the input for daemon to retrieve
      inputQueue.push({
        key: data.key,
        text: data.text,
        timestamp: Date.now()
      });

      // Limit queue size
      while (inputQueue.length > MAX_QUEUE_SIZE) {
        inputQueue.shift();
      }

      console.log(`[Input] Queued: "${data.text}" (${inputQueue.length} items)`);
      break;
  }
}

// Send message to T-Keyboard
function sendToKeyboard(data: any) {
  if (tkeyboardClient && tkeyboardClient.readyState === WebSocket.OPEN) {
    const message = JSON.stringify(data);
    console.log(`[WS] → To keyboard: ${message.substring(0, 200)}${message.length > 200 ? '...' : ''}`);
    tkeyboardClient.send(message);
  } else {
    console.log(`[WS] ✗ Cannot send to keyboard (not connected): ${JSON.stringify(data).substring(0, 100)}`);
  }
}

// HTTP API for input daemon compatibility
const httpServer = http.createServer((req, res) => {
  const url = new URL(req.url!, `http://${req.headers.host}`);

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

    res.writeHead(200, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify({
      sessionId: config.sessionId,
      claudePid: config.claudePid,
      inputs
    }));

    if (config.debug && inputs.length > 0) {
      console.log(`[HTTP] Daemon retrieved ${inputs.length} inputs`);
    }
  }

  // GET /status - Server status
  else if (url.pathname === '/status' && req.method === 'GET') {
    res.writeHead(200, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify({
      sessionId: config.sessionId,
      claudePid: config.claudePid,
      connected: tkeyboardClient !== null,
      queueLength: inputQueue.length,
      currentContext: currentContext.type,
      uptime: process.uptime()
    }));
  }

  // POST /test/button - Simulate button press (for testing)
  else if (url.pathname === '/test/button' && req.method === 'POST') {
    let body = '';
    req.on('data', chunk => body += chunk);
    req.on('end', () => {
      try {
        const data = JSON.parse(body);
        handleKeyboardMessage({
          type: 'key_press',
          key: data.key || 1,
          text: data.text || 'Test'
        });
        res.writeHead(200, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ success: true, queued: inputQueue.length }));
      } catch (err: any) {
        res.writeHead(400, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ error: err.message }));
      }
    });
  }

  else {
    res.writeHead(404);
    res.end('Not found');
  }
});

httpServer.listen(config.httpPort, () => {
  console.log(`
╔════════════════════════════════════════╗
║   T-Keyboard MCP Server                ║
╠════════════════════════════════════════╣
║   WebSocket: ws://localhost:${config.wsPort}       ║
║   HTTP API:  http://localhost:${config.httpPort}     ║
╠════════════════════════════════════════╣
║   Status: Running                      ║
║   Mode: MCP + Dynamic Buttons          ║
╚════════════════════════════════════════╝

Ready for connections...
`);

  // Publish mDNS service
  bonjour.publish({
    name: 'tkeyboard-mcp',
    type: 'http',
    port: config.httpPort,
    host: 'tkeyboard-mcp.local',
    txt: {
      ws_port: config.wsPort.toString(),
      api_port: config.httpPort.toString()
    }
  });

  console.log('[mDNS] Service published: tkeyboard-mcp.local');
});

// MCP Server
const server = new Server(
  {
    name: 'tkeyboard',
    version: '1.0.0',
  },
  {
    capabilities: {
      tools: {},
      resources: {},
    },
  }
);

// Tool: update_keyboard_context
server.setRequestHandler(ListToolsRequestSchema, async () => {
  return {
    tools: [
      {
        name: 'update_keyboard_context',
        description: 'Update T-Keyboard buttons based on current work context. Uses AI to determine optimal buttons.',
        inputSchema: {
          type: 'object',
          properties: {
            context: {
              type: 'string',
              enum: [
                'git_operations',
                'debugging',
                'testing',
                'question_yesno',
                'question_choice',
                'file_operations',
                'default'
              ],
              description: 'High-level category of current work'
            },
            detail: {
              type: 'string',
              description: 'Specific information about the situation (e.g., "3 files modified", "401 auth error")'
            }
          },
          required: ['context']
        }
      },
      {
        name: 'set_keyboard_buttons',
        description: 'Directly set keyboard buttons (bypass AI advisor). Use for explicit control.',
        inputSchema: {
          type: 'object',
          properties: {
            buttons: {
              type: 'array',
              items: { type: 'string' },
              minItems: 4,
              maxItems: 4,
              description: 'Button labels (4 buttons)'
            },
            actions: {
              type: 'array',
              items: { type: 'string' },
              minItems: 4,
              maxItems: 4,
              description: 'Action text (what gets injected). Defaults to buttons if omitted.'
            },
            images: {
              type: 'array',
              items: { type: 'string' },
              minItems: 4,
              maxItems: 4,
              description: 'Image filenames (e.g., "yes.rgb"). Empty string for no image.'
            }
          },
          required: ['buttons']
        }
      },
      {
        name: 'get_keyboard_status',
        description: 'Get current keyboard connection status and button configuration',
        inputSchema: {
          type: 'object',
          properties: {}
        }
      }
    ]
  };
});

server.setRequestHandler(CallToolRequestSchema, async (request) => {
  const { name, arguments: args } = request.params;

  if (name === 'update_keyboard_context') {
    const { context, detail = '' } = args as { context: string; detail?: string };

    console.log(`[MCP] update_keyboard_context: ${context} (${detail})`);

    // Launch button-advisor subagent to determine optimal buttons
    let advisorResult;
    try {
      const advisorInput = JSON.stringify({
        context,
        detail,
        currentState: currentContext
      });

      console.log('[MCP] Launching button-advisor subagent...');

      // Execute button-advisor subagent
      const result = execSync(
        `claude code agent run button-advisor <<< '${advisorInput}'`,
        {
          encoding: 'utf-8',
          maxBuffer: 1024 * 1024,
          shell: '/bin/bash'
        }
      );

      // Parse subagent output (should be JSON)
      advisorResult = JSON.parse(result);
      console.log('[MCP] Button advisor response:', advisorResult);

    } catch (error) {
      console.error('[MCP] Button advisor failed:', error);
      // Fallback to default buttons
      advisorResult = {
        buttons: ['Yes', 'No', 'Proceed', 'Help'],
        emojis: ['✅', '❌', '▶️', '❓'],
        reasoning: 'Fallback due to advisor error'
      };
    }

    // Generate icons for the recommended buttons
    const iconSpecs = advisorResult.buttons.map((btn: string, i: number) => ({
      emoji: advisorResult.emojis[i],
      name: btn.toLowerCase().replace(/\s+/g, '-')
    }));

    let images: string[];
    try {
      images = await ensureIcons(iconSpecs);
    } catch (error) {
      console.error('[MCP] Icon generation failed:', error);
      images = ['', '', '', '']; // Fall back to text-only buttons
    }

    // Update current context
    currentContext = {
      type: context,
      detail: detail,
      buttons: advisorResult.buttons,
      images: images,
      timestamp: Date.now()
    };

    // Send to keyboard
    sendToKeyboard({
      type: 'update_options',
      session_id: Date.now().toString(),
      options: currentContext.buttons.map((text, index) => ({
        text: text || '',
        action: text || '',
        image: currentContext.images[index] || '',
        color: index === 0 ? '#00FFFF' : index === 1 ? '#FFFF00' : index === 2 ? '#FFFFFF' : '#00FF00'
      }))
    });

    return {
      content: [
        {
          type: 'text',
          text: `Keyboard updated to ${context} context.\nButtons: ${advisorResult.buttons.join(', ')}\nReasoning: ${advisorResult.reasoning}`
        }
      ]
    };
  }

  if (name === 'set_keyboard_buttons') {
    const { buttons, actions, images } = args as {
      buttons: string[];
      actions?: string[];
      images?: string[];
    };

    console.log(`[MCP] set_keyboard_buttons: ${buttons.join(', ')}`);

    const finalActions = actions || buttons;
    const finalImages = images || ['', '', '', ''];

    // Update current context
    currentContext = {
      type: 'custom',
      detail: 'manually set',
      buttons: buttons,
      images: finalImages,
      timestamp: Date.now()
    };

    // Send to keyboard
    sendToKeyboard({
      type: 'update_options',
      session_id: Date.now().toString(),
      options: buttons.map((text, index) => ({
        text: text || '',
        action: finalActions[index] || text || '',
        image: finalImages[index] || '',
        color: index === 0 ? '#00FFFF' : index === 1 ? '#FFFF00' : index === 2 ? '#FFFFFF' : '#00FF00'
      }))
    });

    return {
      content: [
        {
          type: 'text',
          text: `Keyboard buttons set to: ${buttons.join(', ')}`
        }
      ]
    };
  }

  if (name === 'get_keyboard_status') {
    return {
      content: [
        {
          type: 'text',
          text: JSON.stringify({
            connected: tkeyboardClient !== null,
            sessionId: config.sessionId,
            claudePid: config.claudePid,
            currentContext: currentContext,
            queueLength: inputQueue.length
          }, null, 2)
        }
      ]
    };
  }

  throw new Error(`Unknown tool: ${name}`);
});

// Resources
server.setRequestHandler(ListResourcesRequestSchema, async () => {
  return {
    resources: [
      {
        uri: 'tkeyboard://status',
        mimeType: 'application/json',
        name: 'Current keyboard status and configuration'
      },
      {
        uri: 'tkeyboard://context',
        mimeType: 'application/json',
        name: 'Current context information'
      }
    ]
  };
});

server.setRequestHandler(ReadResourceRequestSchema, async (request) => {
  const { uri } = request.params;

  if (uri === 'tkeyboard://status') {
    return {
      contents: [
        {
          uri,
          mimeType: 'application/json',
          text: JSON.stringify({
            connected: tkeyboardClient !== null,
            sessionId: config.sessionId,
            currentButtons: currentContext.buttons,
            currentContext: currentContext.type
          }, null, 2)
        }
      ]
    };
  }

  if (uri === 'tkeyboard://context') {
    return {
      contents: [
        {
          uri,
          mimeType: 'application/json',
          text: JSON.stringify(currentContext, null, 2)
        }
      ]
    };
  }

  throw new Error(`Unknown resource: ${uri}`);
});

// Start MCP server
async function main() {
  const transport = new StdioServerTransport();
  await server.connect(transport);
  console.log('[MCP] T-Keyboard MCP server running on stdio');
}

main().catch((error) => {
  console.error('[MCP] Fatal error:', error);
  process.exit(1);
});

// Graceful shutdown
process.on('SIGINT', () => {
  console.log('\nShutting down...');
  bonjour.unpublishAll();
  bonjour.destroy();
  wss.close();
  httpServer.close();
  process.exit(0);
});
