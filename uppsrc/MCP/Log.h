#ifndef _MCP_Log_h_
#define _MCP_Log_h_


enum McpLogLevel { MCP_DEBUG=0, MCP_INFO=1, MCP_WARN=2, MCP_ERROR=3 };

struct McpLogEntry : Moveable<McpLogEntry> {
    Time     ts;
    int      level = MCP_INFO;
    int      client_id = 0;
    String   req_id;
    String   method;
    String   message;
    int      duration_ms = -1;
};

class McpLogBuffer {
public:
    void SetCapacity(int cap);
    void Add(const McpLogEntry& e);
    void Clear();
    Vector<McpLogEntry> Snapshot(int max_items = 200, int min_level = MCP_INFO);
    int Size() const;
private:
    Vector<McpLogEntry> buf;
    int capacity = 1000;
    mutable RWMutex lock;
};

extern McpLogBuffer sMcpLog;


#endif

