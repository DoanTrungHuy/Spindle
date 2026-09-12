import socket
import time
import sys

def test_spindle():
    # Connect to the server
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    try:
        s.connect(('127.0.0.1', 8888))
    except ConnectionRefusedError:
        print("Error: Could not connect to Server on port 8888. Make sure the Server is running.")
        sys.exit(1)

    s.settimeout(2.0)
    
    def send_cmd(cmd):
        s.sendall(cmd.encode('utf-8'))
        response = s.recv(4096).decode('utf-8')
        return response

    print("--- STARTING DATA INTEGRITY TESTS ---")
    
    try:
        # Test 1: Basic SET
        print("[1/11] Testing basic SET operation...")
        resp = send_cmd("SET key1 value1\n")
        if resp != "(integer) 1\n":
            print(f"❌ FAILED! Expected '(integer) 1\\n', got '{resp}'")
            sys.exit(1)
            
        # Test 2: Basic GET
        print("[2/11] Testing basic GET operation...")
        resp = send_cmd("GET key1\n")
        if resp != "value1\n":
            print(f"❌ FAILED! Expected 'value1\\n', got '{resp}'")
            sys.exit(1)
            
        # Test 3: GET non-existent key
        print("[3/11] Testing GET on non-existent key...")
        resp = send_cmd("GET non_existent_key\n")
        if resp != "(nil)\n":
            print(f"❌ FAILED! Expected '(nil)\\n', got '{resp}'")
            sys.exit(1)
            
        # Test 4: DEL existing key
        print("[4/11] Testing DEL operation...")
        resp = send_cmd("DEL key1\n")
        if resp != "(integer) 1\n":
            print(f"❌ FAILED! Expected '(integer) 1\\n', got '{resp}'")
            sys.exit(1)
            
        # Test 5: GET after DEL
        print("[5/11] Testing GET after DEL operation...")
        resp = send_cmd("GET key1\n")
        if resp != "(nil)\n":
            print(f"❌ FAILED! Expected '(nil)\\n', got '{resp}'")
            sys.exit(1)

        # Test 6: Bulk SET and GET
        print("\n[6/11] --- Testing Bulk/Pipelining processing capabilities ---")
        print("Executing SET on 5000 keys continuously...")
        bulk_set = ""
        for i in range(5000):
            bulk_set += f"SET bulk_key_{i} bulk_val_{i}\n"
        
        s.sendall(bulk_set.encode('utf-8'))
        
        # Read exactly 5000 (integer) 1 responses
        bulk_resp = ""
        while bulk_resp.count("\n") < 5000:
            bulk_resp += s.recv(65536).decode('utf-8')
            
        if "ERR" in bulk_resp:
            print("❌ FAILED during Bulk SET!")
            sys.exit(1)

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
            sys.exit(1)
            
        for i in range(5000):
            if lines[i] != f"bulk_val_{i}":
                print(f"❌ FAILED AT KEY {i}! Expected 'bulk_val_{i}', got '{lines[i]}'")
                sys.exit(1)
        
        # Test 7: SET with EX (TTL)
        print("[7/11] Testing SET with EX (TTL)...")
        resp = send_cmd("SET temp_key temp_val EX 2\n")
        if resp != "(integer) 1\n":
            print(f"❌ FAILED! Expected '(integer) 1\\n', got '{resp}'")
            sys.exit(1)
        resp = send_cmd("GET temp_key\n")
        if resp != "temp_val\n":
            print(f"❌ FAILED! Expected 'temp_val\\n', got '{resp}'")
            sys.exit(1)
        time.sleep(2.5)
        resp = send_cmd("GET temp_key\n")
        if resp != "(nil)\n":
            print(f"❌ FAILED! Expected '(nil)\\n', got '{resp}'")
            sys.exit(1)

        # Test 8: TTL command
        print("[8/11] Testing TTL command...")
        send_cmd("SET ttl_key ttl_val EX 10\n")
        resp = send_cmd("TTL ttl_key\n")
        if not resp.startswith("(integer) ") or int(resp.strip()[10:]) <= 0:
            print(f"❌ FAILED! Expected positive integer, got '{resp}'")
            sys.exit(1)

        # Test 9: PERSIST command
        print("[9/11] Testing PERSIST command...")
        send_cmd("SET persist_key persist_val EX 100\n")
        resp = send_cmd("PERSIST persist_key\n")
        if resp != "(integer) 1\n":
            print(f"❌ FAILED! Expected '(integer) 1\\n', got '{resp}'")
            sys.exit(1)
        resp = send_cmd("TTL persist_key\n")
        if resp != "(integer) -1\n":
            print(f"❌ FAILED! Expected '(integer) -1\\n', got '{resp}'")
            sys.exit(1)

        # Test 10: Overwrite existing key
        print("[10/11] Testing Overwrite existing key...")
        send_cmd("SET key1 val1\n")
        send_cmd("SET key1 val2\n")
        resp = send_cmd("GET key1\n")
        if resp != "val2\n":
            print(f"❌ FAILED! Expected 'val2\\n', got '{resp}'")
            sys.exit(1)

        # Test 11: Invalid/unknown command
        print("[11/11] Testing Invalid/unknown command...")
        resp = send_cmd("UNKNOWN_CMD\n")
        if "ERR" not in resp:
            print(f"❌ FAILED! Expected ERR response, got '{resp}'")
            sys.exit(1)

        print("\n✅ ALL TESTS COMPLETED SUCCESSFULLY!")
        print("✅ Data storing, reading, deleting, and pipelining are 100% CORRECT.")

    except Exception as e:
        print(f"❌ An unexpected error occurred: {e}")
        sys.exit(1)
    finally:
        s.close()

if __name__ == "__main__":
    test_spindle()
