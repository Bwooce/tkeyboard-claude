#!/usr/bin/env node
/**
 * MCP Proxy Server
 *
 * This is a lightweight stdio MCP server that proxies tool calls to the
 * persistent HTTP/WebSocket server. This allows:
 * - Claude Code to launch one proxy instance per conversation (stdio)
 * - All proxies connect to the same persistent backend
 * - Persistent backend maintains ESP32 connection and input daemon service
 */

import { Server } from '@modelcontextprotocol/sdk/server/index.js';
import { StdioServerTransport } from '@modelcontextprotocol/sdk/server/stdio.js';
import {
  CallToolRequestSchema,
  ListToolsRequestSchema,
  ListResourcesRequestSchema,
  ReadResourceRequestSchema,
} from '@modelcontextprotocol/sdk/types.js';
import * as http from 'http';

const BACKEND_URL = 'http://localhost:8081';

// Helper to make HTTP requests to backend
function httpRequest(path: string, method: string = 'GET', body?: any): Promise<any> {
  return new Promise((resolve, reject) => {
    const options = {
      hostname: 'localhost',
      port: 8081,
      path: path,
      method: method,
      headers: body ? {
        'Content-Type': 'application/json',
        'Content-Length': Buffer.byteLength(JSON.stringify(body))
      } : {}
    };

    const req = http.request(options, (res) => {
      let data = '';
      res.on('data', chunk => data += chunk);
      res.on('end', () => {
        try {
          resolve(JSON.parse(data));
        } catch (e) {
          resolve(data);
        }
      });
    });

    req.on('error', reject);
    if (body) req.write(JSON.stringify(body));
    req.end();
  });
}

// MCP Server
const server = new Server(
  {
    name: 'tkeyboard-proxy',
    version: '1.0.0',
  },
  {
    capabilities: {
      tools: {},
      resources: {},
    },
  }
);

// List available tools
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
              enum: ['git_operations', 'debugging', 'testing', 'question_yesno', 'question_choice', 'file_operations', 'default'],
              description: 'Type of work context'
            },
            detail: {
              type: 'string',
              description: 'Additional context details'
            }
          },
          required: ['context']
        }
      },
      {
        name: 'set_keyboard_buttons',
        description: 'Directly set specific button labels (bypasses AI advisor)',
        inputSchema: {
          type: 'object',
          properties: {
            buttons: {
              type: 'array',
              items: { type: 'string' },
              minItems: 4,
              maxItems: 4,
              description: 'Four button labels'
            },
            actions: {
              type: 'array',
              items: { type: 'string' },
              minItems: 4,
              maxItems: 4,
              description: 'Four actions (text to inject when pressed)'
            },
            images: {
              type: 'array',
              items: { type: 'string' },
              minItems: 4,
              maxItems: 4,
              description: 'Four image filenames (e.g., "yes.rgb")'
            }
          },
          required: ['buttons']
        }
      },
      {
        name: 'get_keyboard_status',
        description: 'Query T-Keyboard connection status and current button configuration',
        inputSchema: {
          type: 'object',
          properties: {}
        }
      }
    ]
  };
});

// Handle tool calls by proxying to backend
server.setRequestHandler(CallToolRequestSchema, async (request) => {
  const { name, arguments: args } = request.params;

  try {
    // Proxy to backend HTTP API
    const response = await httpRequest('/mcp/tool', 'POST', { name, arguments: args });

    return {
      content: [
        {
          type: 'text',
          text: JSON.stringify(response, null, 2)
        }
      ]
    };
  } catch (error: any) {
    return {
      content: [
        {
          type: 'text',
          text: `Error: ${error.message}`
        }
      ],
      isError: true
    };
  }
});

// List resources
server.setRequestHandler(ListResourcesRequestSchema, async () => {
  return {
    resources: [
      {
        uri: 'tkeyboard://status',
        name: 'T-Keyboard Status',
        description: 'Current keyboard connection and button state',
        mimeType: 'application/json'
      }
    ]
  };
});

// Read resources
server.setRequestHandler(ReadResourceRequestSchema, async (request) => {
  const uri = request.params.uri.toString();

  if (uri === 'tkeyboard://status') {
    const status = await httpRequest('/status');
    return {
      contents: [
        {
          uri: request.params.uri,
          mimeType: 'application/json',
          text: JSON.stringify(status, null, 2)
        }
      ]
    };
  }

  throw new Error(`Unknown resource: ${uri}`);
});

// Start server with stdio transport
async function main() {
  const transport = new StdioServerTransport();
  await server.connect(transport);
  console.error('[MCP Proxy] Connected to Claude Code');
}

main().catch(console.error);
