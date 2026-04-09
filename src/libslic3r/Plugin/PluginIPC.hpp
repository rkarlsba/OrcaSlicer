// OrcaSlicer Plugin IPC (Inter-Process Communication)
// JSON-RPC based communication between OrcaSlicer and plugin processes
// Copyright (C) 2024 OrcaSlicer Contributors
// License: AGPL-3.0

#ifndef slic3r_PluginIPC_hpp_
#define slic3r_PluginIPC_hpp_

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <functional>
#include <optional>
#include <variant>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <thread>
#include <future>

namespace Slic3r {
namespace Plugin {

//==============================================================================
// JSON Value Types (simplified)
//==============================================================================

class JsonValue;
using JsonArray = std::vector<JsonValue>;
using JsonObject = std::map<std::string, JsonValue>;

class JsonValue {
public:
    using Variant = std::variant<
        std::nullptr_t,
        bool,
        int64_t,
        double,
        std::string,
        JsonArray,
        JsonObject
    >;
    
    JsonValue() : m_value(nullptr) {}
    JsonValue(std::nullptr_t) : m_value(nullptr) {}
    JsonValue(bool v) : m_value(v) {}
    JsonValue(int v) : m_value(static_cast<int64_t>(v)) {}
    JsonValue(int64_t v) : m_value(v) {}
    JsonValue(double v) : m_value(v) {}
    JsonValue(const char* v) : m_value(std::string(v)) {}
    JsonValue(const std::string& v) : m_value(v) {}
    JsonValue(std::string&& v) : m_value(std::move(v)) {}
    JsonValue(const JsonArray& v) : m_value(v) {}
    JsonValue(JsonArray&& v) : m_value(std::move(v)) {}
    JsonValue(const JsonObject& v) : m_value(v) {}
    JsonValue(JsonObject&& v) : m_value(std::move(v)) {}
    
    bool is_null() const { return std::holds_alternative<std::nullptr_t>(m_value); }
    bool is_bool() const { return std::holds_alternative<bool>(m_value); }
    bool is_number() const { return std::holds_alternative<int64_t>(m_value) || std::holds_alternative<double>(m_value); }
    bool is_string() const { return std::holds_alternative<std::string>(m_value); }
    bool is_array() const { return std::holds_alternative<JsonArray>(m_value); }
    bool is_object() const { return std::holds_alternative<JsonObject>(m_value); }
    
    bool as_bool(bool default_val = false) const;
    int64_t as_int(int64_t default_val = 0) const;
    double as_double(double default_val = 0.0) const;
    const std::string& as_string() const;
    const JsonArray& as_array() const;
    const JsonObject& as_object() const;
    
    JsonValue& operator[](const std::string& key);
    const JsonValue& operator[](const std::string& key) const;
    JsonValue& operator[](size_t index);
    const JsonValue& operator[](size_t index) const;
    
    // Serialization
    static JsonValue parse(const std::string& json);
    std::string serialize(bool pretty = false) const;
    
private:
    Variant m_value;
    static JsonValue s_null;
};

//==============================================================================
// IPC Message Types
//==============================================================================

enum class IPCMessageType {
    // Host -> Plugin
    Request,        // Method call request
    Response,       // Response to plugin request
    Notification,   // One-way notification
    
    // Plugin -> Host
    PluginRequest,  // Request from plugin to host
    PluginReady,    // Plugin finished loading
    PluginError,    // Plugin error report
    Progress,       // Progress update
};

struct IPCMessage {
    IPCMessageType type;
    int64_t id = 0;          // Request/Response matching ID
    std::string method;       // Method name for requests
    JsonValue params;         // Parameters for request
    JsonValue result;         // Result for response
    std::string error;        // Error message if any
    int error_code = 0;       // Error code (0 = no error)
    
    std::string plugin_id;    // Source/target plugin ID
    
    // Serialize to JSON-RPC format
    std::string serialize() const;
    
    // Parse from JSON-RPC format
    static std::optional<IPCMessage> parse(const std::string& json);
    
    // Builders
    static IPCMessage request(const std::string& method, const JsonValue& params = JsonValue());
    static IPCMessage response(int64_t id, const JsonValue& result);
    static IPCMessage error_response(int64_t id, int code, const std::string& message);
    static IPCMessage notification(const std::string& method, const JsonValue& params = JsonValue());
    static IPCMessage progress(int percent, const std::string& message);
};

//==============================================================================
// IPC Transport Interface
//==============================================================================

class IPCTransport {
public:
    virtual ~IPCTransport() = default;
    
    // Send message (thread-safe)
    virtual bool send(const std::string& data) = 0;
    
    // Receive message (blocking, returns empty on close/error)
    virtual std::string receive() = 0;
    
    // Close connection
    virtual void close() = 0;
    
    // Check if connected
    virtual bool is_connected() const = 0;
};

//==============================================================================
// Stdio Transport - Communication via stdin/stdout
//==============================================================================

class StdioTransport : public IPCTransport {
public:
    StdioTransport(int read_fd, int write_fd);
    ~StdioTransport();
    
    bool send(const std::string& data) override;
    std::string receive() override;
    void close() override;
    bool is_connected() const override;
    
private:
    int m_read_fd;
    int m_write_fd;
    std::atomic<bool> m_connected{true};
    std::mutex m_write_mutex;
    
    // Message framing: length-prefixed
    bool write_message(const std::string& data);
    std::string read_message();
};

//==============================================================================
// IPC Client - Used by plugins to communicate with host
//==============================================================================

class IPCClient {
public:
    using RequestHandler = std::function<JsonValue(const std::string& method, const JsonValue& params)>;
    using NotificationHandler = std::function<void(const std::string& method, const JsonValue& params)>;
    
    IPCClient(std::shared_ptr<IPCTransport> transport);
    ~IPCClient();
    
    // Start processing messages
    void start();
    
    // Stop processing
    void stop();
    
    // Send request and wait for response (synchronous)
    JsonValue call(const std::string& method, const JsonValue& params, int timeout_ms = 30000);
    
    // Send request and return immediately (asynchronous)
    void call_async(const std::string& method, const JsonValue& params,
                    std::function<void(const JsonValue& result, const std::string& error)> callback);
    
    // Send notification (no response expected)
    void notify(const std::string& method, const JsonValue& params);
    
    // Update progress
    void update_progress(int percent, const std::string& message = "");
    
    // Register handler for incoming requests
    void set_request_handler(RequestHandler handler);
    
    // Register handler for incoming notifications
    void set_notification_handler(NotificationHandler handler);
    
    // Check connection
    bool is_connected() const;
    
private:
    void message_loop();
    void handle_message(const IPCMessage& msg);
    
    std::shared_ptr<IPCTransport> m_transport;
    std::thread m_message_thread;
    std::atomic<bool> m_running{false};
    
    RequestHandler m_request_handler;
    NotificationHandler m_notification_handler;
    
    // Pending requests
    std::mutex m_pending_mutex;
    std::condition_variable m_pending_cv;
    std::map<int64_t, std::promise<IPCMessage>> m_pending_requests;
    std::atomic<int64_t> m_next_id{1};
};

//==============================================================================
// IPC Server - Used by host to manage plugin connections
//==============================================================================

class IPCServer {
public:
    using ConnectionHandler = std::function<void(const std::string& plugin_id, std::shared_ptr<IPCClient> client)>;
    using DisconnectionHandler = std::function<void(const std::string& plugin_id)>;
    
    IPCServer();
    ~IPCServer();
    
    // Start server
    bool start();
    
    // Stop server
    void stop();
    
    // Get pending connection for a plugin (after process spawn)
    std::shared_ptr<IPCClient> accept_connection(int read_fd, int write_fd, 
                                                   const std::string& plugin_id);
    
    // Set handlers
    void set_connection_handler(ConnectionHandler handler);
    void set_disconnection_handler(DisconnectionHandler handler);
    
    // Get client for plugin
    std::shared_ptr<IPCClient> get_client(const std::string& plugin_id);
    
    // Remove client
    void remove_client(const std::string& plugin_id);
    
private:
    std::map<std::string, std::shared_ptr<IPCClient>> m_clients;
    std::mutex m_clients_mutex;
    
    ConnectionHandler m_connection_handler;
    DisconnectionHandler m_disconnection_handler;
};

} // namespace Plugin
} // namespace Slic3r

#endif // slic3r_PluginIPC_hpp_
