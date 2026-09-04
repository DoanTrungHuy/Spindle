const net = require('net');

class SpindleClient {
    /**
     * Create a new SpindleClient
     * @param {string} host - The server host (default: '127.0.0.1')
     * @param {number} port - The server port (default: 8888)
     */
    constructor(host = '127.0.0.1', port = 8888) {
        this.host = host;
        this.port = port;
        this.client = new net.Socket();
        this.isConnected = false;
        
        // Queue for handling asynchronous requests
        this.requestQueue = [];
        
        this.client.on('data', (data) => {
            const response = data.toString().trim();
            // Resolve the oldest pending request
            if (this.requestQueue.length > 0) {
                const { resolve } = this.requestQueue.shift();
                resolve(response);
            }
        });

        this.client.on('error', (err) => {
            if (this.requestQueue.length > 0) {
                const { reject } = this.requestQueue.shift();
                reject(err);
            }
        });
        
        this.client.on('close', () => {
            this.isConnected = false;
        });
    }

    /**
     * Connect to the Spindle Server
     * @returns {Promise<void>}
     */
    connect() {
        return new Promise((resolve, reject) => {
            this.client.connect(this.port, this.host, () => {
                this.isConnected = true;
                resolve();
            });
            this.client.once('error', reject);
        });
    }

    /**
     * Close the connection
     */
    close() {
        if (this.isConnected) {
            this.client.destroy();
            this.isConnected = false;
        }
    }

    /**
     * Internal method to send a command
     */
    _sendCommand(cmd) {
        return new Promise((resolve, reject) => {
            if (!this.isConnected) {
                return reject(new Error('Client is not connected. Call connect() first.'));
            }
            this.requestQueue.push({ resolve, reject });
            this.client.write(cmd + '\n');
        });
    }

    /**
     * Store a key-value pair.
     * @param {string} key 
     * @param {string} value 
     * @param {Object} [options] - Optional settings
     * @param {number} [options.ex] - Expire time in seconds
     * @param {number} [options.px] - Expire time in milliseconds
     * @returns {Promise<boolean>} True on success
     */
    async set(key, value, options = {}) {
        let cmd = `SET ${key} ${value}`;
        if (options.ex !== undefined) {
            cmd += ` EX ${options.ex}`;
        } else if (options.px !== undefined) {
            cmd += ` PX ${options.px}`;
        }
        
        const resp = await this._sendCommand(cmd);
        return resp === 'OK';
    }

    /**
     * Retrieve a value by key.
     * @param {string} key 
     * @returns {Promise<string|null>} The value, or null if not found
     */
    async get(key) {
        const resp = await this._sendCommand(`GET ${key}`);
        if (resp === 'NOT_FOUND') {
            return null;
        }
        return resp;
    }

    /**
     * Delete a key.
     * @param {string} key 
     * @returns {Promise<boolean>} True on success
     */
    async delete(key) {
        const resp = await this._sendCommand(`DEL ${key}`);
        return resp === 'OK';
    }
}

module.exports = SpindleClient;

// ==========================================
// USAGE EXAMPLE
// ==========================================
if (require.main === module) {
    (async () => {
        const client = new SpindleClient('127.0.0.1', 8888);
        
        try {
            await client.connect();
            console.log(`Connected to Spindle at ${client.host}:${client.port}`);
            
            console.log("Saving data...");
            await client.set("user:102", "Charlie");
            await client.set("session:abc", "active", { ex: 2 }); // Expires in 2 seconds
            
            console.log("Reading data...");
            const name = await client.get("user:102");
            console.log(`user:102 -> ${name}`);
            
            console.log(`session:abc -> ${await client.get('session:abc')} (before expire)`);
            
            console.log("Waiting 2.1 seconds for TTL to expire...");
            await new Promise(r => setTimeout(r, 2100));
            console.log(`session:abc -> ${await client.get('session:abc')} (after expire)`);
            
            console.log("Deleting data...");
            await client.delete("user:102");
            console.log(`user:102 after delete -> ${await client.get('user:102')}`);
            
        } catch (err) {
            console.error(err);
        } finally {
            client.close();
        }
    })();
}
