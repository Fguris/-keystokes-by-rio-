package com.Rionom.bridge;

import com.Rionom.RioFPSClient;

import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStreamReader;
import java.net.InetAddress;
import java.net.InetSocketAddress;
import java.net.ServerSocket;
import java.net.Socket;
import java.nio.charset.StandardCharsets;

public final class BridgeServer {
    public static final int PORT = 38765;

    private volatile boolean running;
    private Thread thread;
    private ServerSocket serverSocket;

    public void start() {
        if (running) return;
        running = true;

        thread = new Thread(this::runServer, "RioFPS-LocalBridge");
        thread.setDaemon(true);
        thread.start();
    }

    public void stop() {
        running = false;
        BridgeState.disableAll();
        ServerSocket server = serverSocket;
        if (server != null) {
            try { server.close(); } catch (IOException ignored) {}
        }
    }

    private void runServer() {
        try (ServerSocket server = new ServerSocket()) {
            serverSocket = server;
            server.setReuseAddress(true);
            server.bind(new InetSocketAddress(InetAddress.getLoopbackAddress(), PORT), 1);
            RioFPSClient.LOGGER.info("RIO bridge listening on 127.0.0.1:{}", PORT);

            while (running) {
                try (Socket socket = server.accept()) {
                    socket.setTcpNoDelay(true);
                    RioFPSClient.LOGGER.info("RIO controller connected");
                    handleClient(socket);
                } catch (IOException ex) {
                    if (running) {
                        RioFPSClient.LOGGER.warn("RIO bridge client error", ex);
                    }
                } finally {
                    BridgeState.disableAll();
                    RioFPSClient.LOGGER.info("RIO controller disconnected; TG/GL disabled");
                }
            }
        } catch (IOException ex) {
            if (running) {
                RioFPSClient.LOGGER.error("Unable to bind RIO localhost bridge on port {}", PORT, ex);
            }
        } finally {
            serverSocket = null;
            BridgeState.disableAll();
        }
    }

    private void handleClient(Socket socket) throws IOException {
        try (BufferedReader reader = new BufferedReader(
                new InputStreamReader(socket.getInputStream(), StandardCharsets.UTF_8))) {
            String line;
            while (running && (line = reader.readLine()) != null) {
                if (!apply(line.trim())) {
                    RioFPSClient.LOGGER.debug("Ignored RIO bridge command: {}", line);
                }
            }
        }
    }

    private boolean apply(String line) {
        if (line.isEmpty() || line.equals("PING") || line.startsWith("HELLO ")) {
            return true;
        }

        if (line.equals("UNHOOK")) {
            BridgeState.disableAll();
            return true;
        }

        String[] parts = line.split("\\s+", 2);
        if (parts.length != 2) return false;

        try {
            return switch (parts[0]) {
                case "TG_ENABLED" -> {
                    BridgeState.setTGEnabled(parseBool(parts[1]));
                    yield true;
                }
                case "TG_MODE" -> {
                    BridgeState.setTGMode(BridgeState.TGMode.valueOf(parts[1]));
                    yield true;
                }
                case "GL_ENABLED" -> {
                    BridgeState.setGLEnabled(parseBool(parts[1]));
                    yield true;
                }
                case "GL_MODE" -> {
                    BridgeState.setGLMode(BridgeState.GLMode.valueOf(parts[1]));
                    yield true;
                }
                case "GL_WIDTH" -> {
                    BridgeState.setGLWidth(Integer.parseInt(parts[1]));
                    yield true;
                }
                case "GL_COLOR" -> {
                    BridgeState.setGLColor(BridgeState.GLColor.valueOf(parts[1]));
                    yield true;
                }
                default -> false;
            };
        } catch (IllegalArgumentException ex) {
            return false;
        }
    }

    private static boolean parseBool(String value) {
        return value.equals("1") || value.equalsIgnoreCase("true") || value.equalsIgnoreCase("on");
    }
}
