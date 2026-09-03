import socket
import time

def test_kv_store():
    # Connect to the server
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    try:
        s.connect(('127.0.0.1', 8888))
    except ConnectionRefusedError:
        print("Error: Could not connect to Server on port 8888. Make sure the Server is running.")
        return

    s.settimeout(2.0)
    
    def send_cmd(cmd):
        s.sendall(cmd.encode('utf-8'))
        response = s.recv(4096).decode('utf-8')
        return response

    print("--- STARTING DATA INTEGRITY TESTS ---")
    
    try:
        # Test 1: Basic SET
        print("[1/5] Testing basic SET operation...")
        resp = send_cmd("SET key1 value1\n")
        if resp != "OK\n":
            print(f"❌ FAILED! Expected 'OK\\n', got '{resp}'")
            return
            
        # Test 2: Basic GET
        print("[2/5] Testing basic GET operation...")
        resp = send_cmd("GET key1\n")
        if resp != "value1\n":
            print(f"❌ FAILED! Expected 'value1\\n', got '{resp}'")
            return
            
        # Test 3: GET non-existent key
        print("[3/5] Testing GET on non-existent key...")
        resp = send_cmd("GET non_existent_key\n")
        if resp != "NOT_FOUND\n":
            print(f"❌ FAILED! Expected 'NOT_FOUND\\n', got '{resp}'")
            return
            
        # Test 4: DEL existing key
        print("[4/5] Testing DEL operation...")
        resp = send_cmd("DEL key1\n")
        if resp != "OK\n":
            print(f"❌ FAILED! Expected 'OK\\n', got '{resp}'")
            return
            
        # Test 5: GET after DEL
        print("[5/5] Testing GET after DEL operation...")
        resp = send_cmd("GET key1\n")
        if resp != "NOT_FOUND\n":
            print(f"❌ FAILED! Expected 'NOT_FOUND\\n', got '{resp}'")
            return

        # Test 6: Bulk SET and GET
        print("\n--- Testing Bulk/Pipelining processing capabilities ---")
        print("Executing SET on 5000 keys continuously...")
        bulk_set = ""
        for i in range(5000):
            bulk_set += f"SET bulk_key_{i} bulk_val_{i}\n"
        
        s.sendall(bulk_set.encode('utf-8'))
        
        # Read exactly 5000 OK responses
        bulk_resp = ""
        while bulk_resp.count("\n") < 5000:
            bulk_resp += s.recv(65536).decode('utf-8')
            
        if "ERR" in bulk_resp:
            print("❌ FAILED during Bulk SET!")
            return

        print("Executing GET on 5000 keys continuously and verifying data...")
        bulk_get = ""
        for i in range(5000):
            bulk_get += f"GET bulk_key_{i}\n"
            
        s.sendall(bulk_get.encode('utf-8'))
        
        bulk_resp = ""
        while bulk_resp.count("\n") < 5000:
            bulk_resp += s.recv(65536).decode('utf-8')
            
        lines = bulk_resp.strip().split("\n")
        if len(lines) != 5000:
            print(f"❌ FAILED! Expected 5000 responses, got {len(lines)}")
            return
            
        for i in range(5000):
            if lines[i] != f"bulk_val_{i}":
                print(f"❌ FAILED AT KEY {i}! Expected 'bulk_val_{i}', got '{lines[i]}'")
                return
                
        print("✅ ALL TESTS COMPLETED SUCCESSFULLY!")
        print("✅ Data storing, reading, deleting, and pipelining are 100% CORRECT.")

    except Exception as e:
        print(f"❌ An unexpected error occurred: {e}")
    finally:
        s.close()

if __name__ == "__main__":
    test_kv_store()
