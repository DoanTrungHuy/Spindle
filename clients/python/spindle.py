import socket

class SpindleClient:
    """
    A simple Python client for Spindle Store.
    """
    
    def __init__(self, host='127.0.0.1', port=8080):
        self.host = host
        self.port = port
        self.conn = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.is_connected = False

    def connect(self):
        """Connect to the Spindle Server"""
        try:
            self.conn.connect((self.host, self.port))
            self.conn.settimeout(2.0)
            self.is_connected = True
            print(f"Connected to Spindle at {self.host}:{self.port}")
        except Exception as e:
            print(f"Connection failed: {e}")
            raise e

    def close(self):
        """Close the connection"""
        if self.is_connected:
            self.conn.close()
            self.is_connected = False

    def _send_command(self, cmd: str) -> str:
        if not self.is_connected:
            raise ConnectionError("Client is not connected. Call connect() first.")
        
        # Send command with a newline character
        full_cmd = f"{cmd}\n"
        self.conn.sendall(full_cmd.encode('utf-8'))
        
        # Receive response
        response = self.conn.recv(4096).decode('utf-8').strip()
        return response

    def set(self, key: str, value: str) -> bool:
        """Store a key-value pair. Returns True on success."""
        resp = self._send_command(f"SET {key} {value}")
        return resp == "OK"

    def get(self, key: str):
        """Retrieve a value by key. Returns None if not found."""
        resp = self._send_command(f"GET {key}")
        if resp == "NOT_FOUND":
            return None
        return resp

    def delete(self, key: str) -> bool:
        """Delete a key. Returns True on success."""
        resp = self._send_command(f"DEL {key}")
        return resp == "OK"

# ==========================================
# USAGE EXAMPLE
# ==========================================
if __name__ == "__main__":
    # Initialize the client
    # Note: Change the port to 8888 or 8080 depending on your server configuration
    client = SpindleClient(host='127.0.0.1', port=8888) 
    
    try:
        client.connect()
        
        # 1. Store data
        print("Saving data...")
        client.set("user:100", "Alice")
        client.set("user:101", "Bob")
        
        # 2. Read data
        print("Reading data...")
        name = client.get("user:100")
        print(f"user:100 -> {name}")
        
        # 3. Read non-existent data
        unknown = client.get("user:999")
        print(f"user:999 -> {unknown}")
        
        # 4. Delete data
        print("Deleting data...")
        client.delete("user:101")
        print(f"user:101 after delete -> {client.get('user:101')}")
        
    finally:
        client.close()
