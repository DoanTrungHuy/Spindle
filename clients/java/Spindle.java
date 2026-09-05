package clients.java;

import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.net.Socket;
import java.nio.charset.StandardCharsets;

public class Spindle {
    private String host;
    private int port;
    private Socket socket;
    private OutputStream out;
    private BufferedReader in;

    public Spindle(String host, int port) {
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
        return "OK".equals(resp);
    }

    public boolean setEx(String key, String value, int seconds) throws Exception {
        String resp = sendCommand("SET " + key + " " + value + " EX " + seconds);
        return "OK".equals(resp);
    }

    public String get(String key) throws Exception {
        String resp = sendCommand("GET " + key);
        if ("NOT_FOUND".equals(resp)) {
            return null;
        }
        return resp;
    }

    public boolean delete(String key) throws Exception {
        String resp = sendCommand("DEL " + key);
        return "OK".equals(resp);
    }

    public static void main(String[] args) {
        Spindle client = new Spindle("127.0.0.1", 8888);
        try {
            client.connect();
            
            System.out.println("Saving data...");
            client.set("user:100", "Alice");
            client.setEx("session:xyz", "active", 2);
            
            System.out.println("Reading data...");
            System.out.println("user:100 -> " + client.get("user:100"));
            
            System.out.println("Deleting data...");
            client.delete("user:100");
            
        } catch (Exception e) {
            e.printStackTrace();
        } finally {
            client.close();
        }
    }
}
