#ifndef _MCP_Server_h_
#define _MCP_Server_h_


struct McpClient {
    One<TcpSocket> sock;
    String         inbuf;
    String         outbuf;
    Time           last_activity;
    int            id = 0;
};

class McpServerCore {
public:
    typedef McpServerCore CLASSNAME;
    bool Start(int port = 7326);
    void Stop();
    bool IsRunning() const { return running; }
    int  Port() const { return listen_port; }

protected:
    // Extension point: override to handle methods beyond core ones
    virtual String HandleExtended(const McpRequest& req) { return MakeError(req.id, METHOD_NOT_FOUND, "Method not found"); }

private:
    void Loop();
    String Handle(const McpRequest& req);
    void HandleClients();
    bool ReadFramed(McpClient& c, Vector<String>& out_msgs);
    bool WritePending(McpClient& c);
    void LogRequest(int client_id, const McpRequest& req, const String& status, int duration_ms = -1);

    Thread  thr;
    Thread  clients_thr;
    Atomic  stop {0};
    bool    running = false;
    int     listen_port = 0;
    RWMutex clients_lock;
    Array<McpClient> clients;
    int     next_client_id = 1;
    int     max_message_bytes = 4 * 1024 * 1024; // 4 MB
};


#endif

