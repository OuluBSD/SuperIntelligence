#include "MCP.h"

NAMESPACE_UPP

bool McpServerCore::Start(int port) {
    if(running) return true;
    listen_port = port ? port : 7326;
    stop = 0;
    thr.Run([=]{ const_cast<McpServerCore*>(this)->Loop(); });
    running = true;
    return true;
}

void McpServerCore::Stop() {
    if(!running) return;
    stop = 1;
    if(thr.IsOpen()) thr.Wait();
    if(clients_thr.IsOpen()) clients_thr.Wait();
    running = false;
}

void McpServerCore::Loop() {
    TcpSocket listener;
    if(!listener.Listen(listen_port, 128, false, true)) {
        RLOG("MCP: listen failed on port " << listen_port << ": " << listener.GetErrorDesc());
        fprintf(stderr, "MCP: listen failed on port %d: %s\n", listen_port, listener.GetErrorDesc().ToStd().c_str());
        return;
    }
    fprintf(stderr, "MCP: listening on port %d\n", listen_port);
    listener.NoDelay();
    clients_thr.Run(THISBACK(HandleClients));
    while(!stop) {
        One<TcpSocket> s; s.Create(); s->Timeout(5);
        if(s->Accept(listener)) {
            s->NoDelay();
            McpClient c; c.sock = s.Detach(); c.last_activity = GetSysTime(); c.id = next_client_id++;
            clients_lock.EnterWrite(); clients.Add(pick(c)); clients_lock.LeaveWrite();
        }
    }
}

void McpServerCore::HandleClients() {
    while(!stop) {
        for(int i = 0;; i++) {
            clients_lock.EnterRead();
            if(i >= clients.GetCount()) { clients_lock.LeaveRead(); break; }
            McpClient& c = clients[i];
            clients_lock.LeaveRead();
            if(!c.sock->IsOpen()) { clients_lock.EnterWrite(); clients.Remove(i--); clients_lock.LeaveWrite(); continue; }
            Vector<String> msgs;
            if(ReadFramed(c, msgs)) {
                for(const String& line : msgs) {
                    McpRequest req; String reply;
                    if(ParseRequest(line, req)) {
                        Time start = GetSysTime();
                        reply = Handle(req);
                        int dur = GetSysTime().Get() - start.Get();
                        LogRequest(c.id, req, "ok", dur);
                        if(!IsNotification(req)) { reply.Cat('\n'); c.outbuf.Cat(reply); }
                    } else {
                        reply = MakeError(String(), PARSE_ERROR, "Parse error"); reply.Cat('\n'); c.outbuf.Cat(reply);
                        McpRequest dummy; LogRequest(c.id, dummy, "parse_error", -1);
                    }
                }
            }
            WritePending(c);
            if(c.sock->IsError() || c.sock->IsEof()) { clients_lock.EnterWrite(); clients.Remove(i--); clients_lock.LeaveWrite(); continue; }
        }
        Sleep(5);
    }
    for(auto& c : clients) if(c.sock->IsOpen()) c.sock->Close();
    clients_lock.EnterWrite(); clients.Clear(); clients_lock.LeaveWrite();
}

String McpServerCore::Handle(const McpRequest& req) {
    if(IsNull(req.method)) return MakeError(req.id, INVALID_REQUEST, "Invalid request: missing method");
    if(req.method == "mcp.ping") { ValueMap r; r.Add("text","pong"); return MakeResult(req.id, r); }
    if(req.method == "mcp.capabilities") {
        ValueArray methods;
        methods.Add("mcp.ping"); methods.Add("mcp.capabilities"); methods.Add("mcp.tutorial");
        methods.Add("mcp.log.get"); methods.Add("mcp.log.clear");
        methods.Add("mcp.index.status"); methods.Add("mcp.index.refresh");
        methods.Add("workspace.info");
        methods.Add("build.start"); methods.Add("build.stop"); methods.Add("build.status");
        methods.Add("run.start");
        methods.Add("mainconfig.list"); methods.Add("mainconfig.set");
        methods.Add("buildmethod.list"); methods.Add("buildmethod.set");
        methods.Add("buildmode.get"); methods.Add("buildmode.set");
        methods.Add("package.list"); methods.Add("package.set"); methods.Add("package.files");
        methods.Add("file.get"); methods.Add("file.open");
        methods.Add("file.write");
        methods.Add("package.create"); methods.Add("package.add_file");
        methods.Add("editor.path"); methods.Add("editor.cursor.get"); methods.Add("editor.cursor.set");
        methods.Add("editor.lines"); methods.Add("editor.line.get"); methods.Add("editor.insert");
        methods.Add("console.get"); methods.Add("errors.get");
        methods.Add("find.infiles"); methods.Add("find.next"); methods.Add("find.prev");
        methods.Add("valgrind.run");
        methods.Add("assist.suggestions"); methods.Add("assist.goto"); methods.Add("assist.usage");
        methods.Add("assist.query"); methods.Add("assist.context_goto");
        methods.Add("workspace.open"); methods.Add("workspace.reload"); methods.Add("workspace.close");
        methods.Add("assembly.list"); methods.Add("assembly.get"); methods.Add("assembly.switch");
        methods.Add("assembly.path"); methods.Add("assembly.packages");
        methods.Add("upphub.open");
        methods.Add("debug.state"); methods.Add("debug.session.start"); methods.Add("debug.session.stop");
        methods.Add("debug.continue"); methods.Add("debug.step.over"); methods.Add("debug.step.into"); methods.Add("debug.step.out"); methods.Add("debug.pause");
        methods.Add("debug.breakpoint.set"); methods.Add("debug.breakpoint.clear"); methods.Add("debug.breakpoint.list");
        methods.Add("debug.stack"); methods.Add("debug.locals"); methods.Add("debug.evaluate"); methods.Add("debug.threads");
        methods.Add("debug.registers"); methods.Add("debug.disassembly");
        methods.Add("debug.watch.list"); methods.Add("debug.watch.add"); methods.Add("debug.watch.remove"); methods.Add("debug.watch.clear");
        methods.Add("resource.list"); methods.Add("resource.get");
        methods.Add("layout.files"); methods.Add("layout.open"); methods.Add("layout.current_file");
        methods.Add("layout.list"); methods.Add("layout.add"); methods.Add("layout.insert"); methods.Add("layout.duplicate"); methods.Add("layout.rename"); methods.Add("layout.remove");
        methods.Add("layout.set_current"); methods.Add("layout.set_size");
        methods.Add("layout.items"); methods.Add("layout.item.get"); methods.Add("layout.item.add"); methods.Add("layout.item.remove");
        methods.Add("layout.item.set_rect"); methods.Add("layout.item.set_var");
        methods.Add("layout.item.properties"); methods.Add("layout.item.set_property");
        methods.Add("layout.classes"); methods.Add("layout.save");
        ValueMap caps; caps.Add("protocol","jsonrpc-2.0"); caps.Add("supports_batch", false); caps.Add("resources", true); caps.Add("methods", methods);
        return MakeResult(req.id, caps);
    }
    if(req.method == "mcp.log.get") {
        int limit = 200, min_level = MCP_INFO;
        if(IsValueMap(req.params)) { ValueMap p = req.params; limit = (int)(p.Get("limit", limit)); min_level = (int)(p.Get("min_level", min_level)); }
        ValueArray arr; for(const auto& e : sMcpLog.Snapshot(limit, min_level)) { ValueMap v; v.Add("ts", Format("%", e.ts)); v.Add("level", e.level); v.Add("client_id", e.client_id); v.Add("req_id", e.req_id); v.Add("method", e.method); v.Add("message", e.message); v.Add("duration_ms", e.duration_ms); arr.Add(v);} ValueMap r; r.Add("items", arr); r.Add("size", sMcpLog.Size()); return MakeResult(req.id, r);
    }
    if(req.method == "mcp.log.clear") { int before = sMcpLog.Size(); sMcpLog.Clear(); ValueMap r; r.Add("cleared", before); return MakeResult(req.id, r); }
    if(req.method == "mcp.tutorial") {
        String text =
            "# U++ IDE MCP Server — Tutorial\n"
            "\n"
            "The MCP server runs on port 7326 (default) and speaks JSON-RPC 2.0.\n"
            "Every request is a newline-delimited JSON object; every response is one too.\n"
            "\n"
            "## Connection\n"
            "\n"
            "    {\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"mcp.ping\"}\n"
            "    → {\"jsonrpc\":\"2.0\",\"id\":\"1\",\"result\":{\"text\":\"pong\"}}\n"
            "\n"
            "    {\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"mcp.capabilities\"}\n"
            "    → {\"result\":{\"protocol\":\"jsonrpc-2.0\",\"methods\":[...]}}\n"
            "\n"
            "## Discovery — what is open?\n"
            "\n"
            "    assembly.get          → {\"name\":\"MyApps\"}\n"
            "    assembly.list         → {\"assemblies\":[{\"name\":...,\"upp_dirs\":[...]}]}\n"
            "    workspace.info        → {\"name\":\"MyPkg [...]\",\"packages\":3}\n"
            "    package.list          → {\"packages\":[...],\"active\":\"MyPkg\"}\n"
            "    package.files         {\"package\":\"MyPkg\"}\n"
            "                          → {\"files\":[\"/path/to/Foo.cpp\",...]}\n"
            "    buildmethod.list      → {\"methods\":[\"CLANG\",...],\"current\":\"CLANG\"}\n"
            "    buildmode.get         → {\"mode\":\"debug\"}\n"
            "\n"
            "## Editor\n"
            "\n"
            "    editor.path           → {\"path\":\"/abs/path/to/current/file.cpp\"}\n"
            "    editor.cursor.get     → {\"line\":42,\"col\":7,\"cursor\":1234}\n"
            "    editor.lines          → {\"count\":235}\n"
            "    editor.line.get       {\"line\":1}  → {\"line\":1,\"text\":\"#include...\"}\n"
            "    editor.cursor.set     {\"line\":10,\"col\":0}\n"
            "    editor.insert         {\"text\":\"// inserted\\n\"}\n"
            "    file.open             {\"path\":\"/abs/path/file.cpp\"}\n"
            "    file.get              {\"path\":\"/abs/path/file.cpp\"} → {\"content\":\"...\"}\n"
            "\n"
            "## Writing files\n"
            "\n"
            "    file.write            {\"path\":\"/abs/path/new.cpp\",\"content\":\"...\"}\n"
            "                          → {\"written\":\"/abs/path/new.cpp\",\"bytes\":42}\n"
            "    package.create        {\"name\":\"MyPkg\",\"description\":\"My package\"}\n"
            "                          → {\"path\":\"/abs/path/MyPkg\"}\n"
            "    package.add_file      {\"package\":\"MyPkg\",\"file\":\"Foo.cpp\"}\n"
            "\n"
            "## Build\n"
            "\n"
            "    build.start           → {\"accepted\":true}\n"
            "    build.status          → {\"building\":false}\n"
            "    build.stop\n"
            "    run.start             → {\"accepted\":true}   (builds then runs in terminal)\n"
            "    errors.get            → {\"errors\":[{\"file\":...,\"line\":5,\"text\":\"...\",\"warning\":false}]}\n"
            "    console.get           {\"tail\":10}  → {\"text\":\"last 10 build output lines\"}\n"
            "\n"
            "## Debug\n"
            "\n"
            "    debug.state           → {\"active\":false,\"paused\":false,\"backend\":\"gdb\"}\n"
            "    debug.session.start   → {\"accepted\":true}   (builds + launches debugger)\n"
            "    debug.session.stop\n"
            "    debug.continue / debug.step.over / debug.step.into / debug.step.out / debug.pause\n"
            "    debug.breakpoint.set  {\"file\":\"/abs/path/F.cpp\",\"line\":42}\n"
            "    debug.breakpoint.clear {\"file\":...,\"line\":42}\n"
            "    debug.breakpoint.list → {\"breakpoints\":[{\"file\":...,\"line\":42,\"enabled\":true}]}\n"
            "    debug.stack           → {\"frames\":[{\"level\":0,\"func\":\"main\",\"file\":...,\"line\":10}]}\n"
            "    debug.locals          → {\"locals\":[{\"name\":\"x\",\"value\":\"42\",\"type\":\"int\"}]}\n"
            "    debug.evaluate        {\"expr\":\"x+1\"} → {\"result\":\"43\"}\n"
            "    debug.threads         → {\"threads\":[...]}\n"
            "    debug.registers       → {\"text\":\"rax 0x0 ...\"}\n"
            "    debug.disassembly     → {\"text\":\"0x... <main+0>: push %rbp ...\"}\n"
            "    debug.watch.list      → {\"watches\":[{\"expr\":\"x\",\"value\":\"42\"}]}\n"
            "    debug.watch.add       {\"expr\":\"myVar\"}\n"
            "    debug.watch.remove    {\"index\":0}\n"
            "    debug.watch.clear\n"
            "\n"
            "## Layout editor\n"
            "\n"
            "    layout.files          → {\"files\":[\"/path/Foo.lay\",...]}\n"
            "    layout.open           {\"path\":\"/path/Foo.lay\"}\n"
            "    layout.current_file   → {\"path\":\"/path/Foo.lay\"}\n"
            "    layout.classes        → {\"classes\":[{\"name\":\"Label\",\"group\":\"Static\"},...]}  (*)\n"
            "    layout.list           → {\"layouts\":[{\"index\":0,\"name\":\"FooDlg\",\"size\":\"320x200\"}],\"current\":0}\n"
            "    layout.add            {\"name\":\"MyDlg\"}\n"
            "    layout.insert         {\"before\":0,\"name\":\"MyDlg\"}\n"
            "    layout.duplicate      {\"index\":0,\"name\":\"MyDlgCopy\"}\n"
            "    layout.rename         {\"index\":0,\"name\":\"NewName\"}\n"
            "    layout.remove         {\"index\":1}\n"
            "    layout.set_current    {\"index\":0}\n"
            "    layout.set_size       {\"index\":0,\"width\":400,\"height\":300}\n"
            "    layout.items          {\"layout\":0} → {\"items\":[{\"index\":0,\"type\":\"Label\",\"variable\":\"lbl\",...}]}\n"
            "    layout.item.add       {\"layout\":0,\"type\":\"Label\",\"variable\":\"lbl\",\"left\":10,\"top\":10,\"right\":200,\"bottom\":30}\n"
            "    layout.item.remove    {\"layout\":0,\"item\":0}\n"
            "    layout.item.set_rect  {\"layout\":0,\"item\":0,\"left\":10,\"top\":10,\"right\":200,\"bottom\":30}\n"
            "    layout.item.set_var   {\"layout\":0,\"item\":0,\"variable\":\"new_name\"}\n"
            "    layout.item.properties {\"layout\":0,\"item\":0} → {\"properties\":[{\"name\":\"SetLabel\",\"value\":\"\\\"OK\\\"\"}]}\n"
            "    layout.item.set_property {\"layout\":0,\"item\":0,\"name\":\"SetLabel\",\"value\":\"\\\"OK\\\"\"}\n"
            "    layout.save\n"
            "\n"
            "(*) layout.classes returns non-empty only when the workspace includes packages\n"
            "    with .usc files (e.g. CtrlLib). Results depend on active workspace.\n"
            "\n"
            "## Workspace switching\n"
            "\n"
            "    assembly.switch       {\"name\":\"MyApps\"}   (reloads IDE — no response sent)\n"
            "    workspace.open        {\"package\":\"MyPkg\"}  (sets main package)\n"
            "    workspace.reload      (re-scans current package)\n"
            "    workspace.close\n"
            "\n"
            "## Logging\n"
            "\n"
            "    mcp.log.get           {\"limit\":20,\"min_level\":0} → {\"items\":[{\"ts\":...,\"method\":...,\"message\":\"ok\",\"duration_ms\":0}]}\n"
            "    mcp.log.clear\n"
            "    mcp.tutorial          → this text\n";
        ValueMap r; r.Add("text", text); return MakeResult(req.id, r);
    }
    if(req.method == "mcp.index.status") { ValueMap r; r.Add("ready", false); r.Add("builder", "unknown"); r.Add("last_update", (int)0); r.Add("stale_files", 0); return MakeResult(req.id, r); }
    if(req.method == "mcp.index.refresh") { ValueMap r; r.Add("accepted", true); r.Add("mode","script_build"); return MakeResult(req.id, r); }
    String ext = HandleExtended(req);
    if(!IsNull(ext)) return ext;
    return MakeError(req.id, METHOD_NOT_FOUND, "Method not found");
}

bool McpServerCore::ReadFramed(McpClient& c, Vector<String>& out_msgs) {
    c.sock->Timeout(0);
    if(!c.sock->Peek()) return false;
    // Drain all available bytes in one shot; GetLine with Timeout(0) would
    // time out mid-line when bytes arrive in multiple TCP segments.
    char buf[65536];
    int n = c.sock->Get(buf, sizeof(buf));
    if(c.sock->IsError()) { c.sock->Close(); return false; }
    if(n <= 0) return false;
    c.inbuf.Cat(buf, n);
    c.last_activity = GetSysTime();
    if(c.inbuf.GetLength() > max_message_bytes) { c.sock->Close(); return false; }
    for(;;) { int p = c.inbuf.Find('\n'); if(p < 0) break; String line = c.inbuf.Mid(0,p); c.inbuf.Remove(0,p+1); if(line.GetCount()) out_msgs.Add(line); }
    return out_msgs.GetCount();
}

bool McpServerCore::WritePending(McpClient& c) {
    if(c.outbuf.IsEmpty()) return false;
    int n = c.sock->Put(c.outbuf); if(c.sock->IsError()) { c.sock->Close(); return false; }
    if(n > 0) { c.outbuf.Remove(0, n); c.last_activity = GetSysTime(); return true; }
    return false;
}

void McpServerCore::LogRequest(int client_id, const McpRequest& req, const String& status, int duration_ms) {
    int level = status == "ok" ? MCP_INFO : (status == "parse_error" ? MCP_ERROR : MCP_WARN);
    McpLogEntry e; e.ts = GetSysTime(); e.level = level; e.client_id = client_id; e.req_id = req.id; e.method = req.method; e.message = status; e.duration_ms = duration_ms; sMcpLog.Add(e);
}

END_UPP_NAMESPACE

