package clients.java;

import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.net.Socket;
import java.nio.charset.StandardCharsets;

public class SpindleClient {
    private String host;
    private int port;
    private Socket socket;
    private OutputStream out;
    private BufferedReader in;

    public SpindleClient(String host, int port) {
        this.host = host;
        this.port = port;
    }

    public void connect() throws Exception {
        socket = new Socket(host, port);
        socket.setSoTimeout(2000);
        out = socket.getOutputStream();
        in = new BufferedReader(new InputStreamReader(socket.getInputStream(), StandardCharsets.UTF_8));
        System.out.println("Connected to Spindle at " + host + ":" + port);
    }

    public void close() {
        try {
            if (socket != null && !socket.isClosed()) {
                socket.close();
            }
        } catch (Exception e) {
            e.printStackTrace();
        }
    }

    private String sendCommand(String cmd) throws Exception {
        if (socket == null || socket.isClosed()) {
            throw new IllegalStateException("Client is not connected. Call connect() first.");
        }
        out.write((cmd + "\n").getBytes(StandardCharsets.UTF_8));
        out.flush();
        return in.readLine();
    }

    public boolean set(String key, String value) throws Exception {
        String resp = sendCommand("SET " + key + " " + value);
        return "(integer) 1".equals(resp);
    }

    public boolean setEx(String key, String value, int seconds) throws Exception {
        String resp = sendCommand("SET " + key + " " + value + " EX " + seconds);
        return "(integer) 1".equals(resp);
    }

    public String get(String key) throws Exception {
        String resp = sendCommand("GET " + key);
        if ("(nil)".equals(resp)) {
            return null;
        }
        return resp;
    }

    public boolean delete(String key) throws Exception {
        String resp = sendCommand("DEL " + key);
        return resp != null && resp.startsWith("(integer) 1");
    }

    public int ttl(String key) throws Exception {
        String resp = sendCommand("TTL " + key);
        if (resp != null && resp.startsWith("(integer) ")) {
            return Integer.parseInt(resp.substring(10).trim());
        }
        return -2; // or appropriate error/missing code
    }

    public boolean persist(String key) throws Exception {
        String resp = sendCommand("PERSIST " + key);
        return resp != null && resp.startsWith("(integer) 1");
    }

    public static void main(String[] args) {
        SpindleClient client = new SpindleClient("127.0.0.1", 8888);
        try {
            client.connect();
            
            System.out.println("Saving data...");
            client.set("user:100", "Alice");
            client.setEx("session:xyz", "active", 100);
            
            System.out.println("Reading data...");
            System.out.println("user:100 -> " + client.get("user:100"));
            
            System.out.println("TTL for session:xyz -> " + client.ttl("session:xyz"));
            System.out.println("Persist session:xyz -> " + client.persist("session:xyz"));
            System.out.println("TTL for session:xyz after persist -> " + client.ttl("session:xyz"));
            
            System.out.println("Deleting data...");
            client.delete("user:100");
            
        } catch (Exception e) {
            e.printStackTrace();
        } finally {
            client.close();
        }
    }
}
