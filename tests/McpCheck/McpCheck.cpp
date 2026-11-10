#include <Core/Core.h>
#include <MCP/MCP.h>

using namespace Upp;

class ExtendedMcpServer : public McpServerCore {
public:
	virtual String HandleExtended(const McpRequest& req) override {
		if(req.method == "custom.echo") {
			return MakeResult(req.id, req.params);
		}
		if(req.method == "custom.add") {
			if(IsValueMap(req.params)) {
				ValueMap m = req.params;
				int a = (int)m.Get("a", 0);
				int b = (int)m.Get("b", 0);
				ValueMap r;
				r.Add("sum", a + b);
				return MakeResult(req.id, r);
			}
			return MakeError(req.id, INVALID_PARAMS, "Invalid params");
		}
		return McpServerCore::HandleExtended(req);
	}
};

void TestProtocolParsing()
{
	Cout() << "[Test 1] Protocol JSON-RPC Parsing & Formatting...\n";

	// 1. Valid request
	String raw_req = "{\"jsonrpc\":\"2.0\",\"id\":\"req-101\",\"method\":\"math.multiply\",\"params\":{\"a\":6,\"b\":7}}";
	McpRequest req;
	ASSERT(ParseRequest(raw_req, req));
	ASSERT(req.id == "req-101");
	ASSERT(req.method == "math.multiply");
	ASSERT(IsValueMap(req.params));
	ASSERT((int)ValueMap(req.params).Get("a", 0) == 6);
	ASSERT(!IsNotification(req));

	// 2. Notification request (null/missing id)
	String raw_notif = "{\"jsonrpc\":\"2.0\",\"method\":\"event.notify\",\"params\":{\"status\":\"started\"}}";
	McpRequest notif;
	ASSERT(ParseRequest(raw_notif, notif));
	ASSERT(IsNull(notif.id));
	ASSERT(notif.method == "event.notify");
	ASSERT(IsNotification(notif));

	// 3. Invalid JSON-RPC
	McpRequest bad;
	ASSERT(!ParseRequest("not-a-json", bad));
	ASSERT(!ParseRequest("{\"jsonrpc\":\"1.0\",\"id\":1,\"method\":\"test\"}", bad));

	// 4. MakeResult
	ValueMap res_map;
	res_map.Add("answer", 42);
	String res_json = MakeResult("req-101", res_map);
	Value res_val = ParseJSON(res_json);
	ASSERT(IsValueMap(res_val));
	ASSERT(AsString(ValueMap(res_val).Get("id", Value())) == "req-101");
	ASSERT((int)ValueMap(ValueMap(res_val).Get("result", Value())).Get("answer", 0) == 42);

	// 5. MakeError
	String err_json = MakeError("req-101", METHOD_NOT_FOUND, "Method not found");
	Value err_val = ParseJSON(err_json);
	ASSERT(IsValueMap(err_val));
	ASSERT((int)ValueMap(ValueMap(err_val).Get("error", Value())).Get("code", 0) == METHOD_NOT_FOUND);

	Cout() << "  -> PASSED\n";
}

void TestLogBuffer()
{
	Cout() << "[Test 2] McpLogBuffer Circular Logging & Filtering...\n";

	McpLogBuffer log;
	log.SetCapacity(5);
	log.Clear();
	ASSERT(log.Size() == 0);

	for(int i = 0; i < 10; i++) {
		McpLogEntry e;
		e.ts = GetSysTime();
		e.level = (i % 4); // 0=debug, 1=info, 2=warn, 3=error
		e.client_id = 1;
		e.req_id = AsString(i);
		e.method = "test.method";
		e.message = Format("Message %d", i);
		e.duration_ms = i * 10;
		log.Add(e);
	}

	ASSERT(log.Size() == 5);

	// Snapshot all (min_level = 0)
	Vector<McpLogEntry> all = log.Snapshot(10, MCP_DEBUG);
	ASSERT(all.GetCount() == 5);
	// Last item was i=9 (level 9%4 = 1)
	ASSERT(all.Top().req_id == "9");

	// Snapshot min_level = MCP_WARN (levels 2, 3)
	Vector<McpLogEntry> warns = log.Snapshot(10, MCP_WARN);
	for(const auto& item : warns) {
		ASSERT(item.level >= MCP_WARN);
	}

	log.Clear();
	ASSERT(log.Size() == 0);

	Cout() << "  -> PASSED\n";
}

void TestLiveServer()
{
	Cout() << "[Test 3] Live McpServerCore TCP Loopback Interaction...\n";

	ExtendedMcpServer server;
	int port = 17326; // High non-standard port for testing
	ASSERT(server.Start(port));
	ASSERT(server.IsRunning());
	ASSERT(server.Port() == port);

	// Give the listener thread a moment to bind, retry connect if needed
	TcpSocket client;
	client.Timeout(3000);
	bool connected = false;
	for(int attempt = 0; attempt < 30; attempt++) {
		Sleep(50);
		if(client.Connect("127.0.0.1", port) && client.WaitConnect()) {
			connected = true;
			break;
		}
		client.Close();
	}
	if(!connected) {
		Cerr() << "Failed to connect to loopback server on port " << port << " after retries: " << client.GetErrorDesc() << "\n";
		ASSERT(false);
	}
	Cout() << "Connected successfully to port " << port << "\n";

	// Helper lambda to send a line and read single JSON line reply
	auto CallMcp = [&](const String& json) -> Value {
		String payload = json + "\n";
		int put_bytes = client.Put(payload);
		if(put_bytes != payload.GetLength()) {
			Cerr() << "Put failed: put_bytes=" << put_bytes << " expected=" << payload.GetLength()
			       << " error=" << client.GetErrorDesc() << " is_error=" << client.IsError() << "\n";
		}
		ASSERT(put_bytes == payload.GetLength());

		String line = client.GetLine();
		ASSERT(!line.IsEmpty());
		Value resp = ParseJSON(line);
		ASSERT(IsValueMap(resp));
		return resp;
	};

	// 1. Test mcp.ping
	{
		Value v = CallMcp("{\"jsonrpc\":\"2.0\",\"id\":\"p-1\",\"method\":\"mcp.ping\"}");
		ValueMap m = v;
		ASSERT(AsString(m.Get("id", Value())) == "p-1");
		Value res = m.Get("result", Value());
		ASSERT(IsValueMap(res));
		ASSERT(AsString(ValueMap(res).Get("text", Value())) == "pong");
	}

	// 2. Test mcp.capabilities
	{
		Value v = CallMcp("{\"jsonrpc\":\"2.0\",\"id\":\"p-2\",\"method\":\"mcp.capabilities\"}");
		ValueMap m = v;
		ASSERT(AsString(m.Get("id", Value())) == "p-2");
		Value res = m.Get("result", Value());
		ASSERT(IsValueMap(res));
		ASSERT(AsString(ValueMap(res).Get("protocol", Value())) == "jsonrpc-2.0");
	}

	// 3. Test extended method custom.echo
	{
		Value v = CallMcp("{\"jsonrpc\":\"2.0\",\"id\":\"p-3\",\"method\":\"custom.echo\",\"params\":{\"message\":\"hello mcp\"}}");
		ValueMap m = v;
		ASSERT(AsString(m.Get("id", Value())) == "p-3");
		Value res = m.Get("result", Value());
		ASSERT(IsValueMap(res));
		ASSERT(AsString(ValueMap(res).Get("message", Value())) == "hello mcp");
	}

	// 4. Test extended method custom.add
	{
		Value v = CallMcp("{\"jsonrpc\":\"2.0\",\"id\":\"p-4\",\"method\":\"custom.add\",\"params\":{\"a\":17,\"b\":25}}");
		ValueMap m = v;
		ASSERT(AsString(m.Get("id", Value())) == "p-4");
		Value res = m.Get("result", Value());
		ASSERT(IsValueMap(res));
		ASSERT((int)ValueMap(res).Get("sum", 0) == 42);
	}

	// 5. Test unknown method -> METHOD_NOT_FOUND error
	{
		Value v = CallMcp("{\"jsonrpc\":\"2.0\",\"id\":\"p-5\",\"method\":\"nonexistent.method\"}");
		ValueMap m = v;
		ASSERT(AsString(m.Get("id", Value())) == "p-5");
		Value err = m.Get("error", Value());
		ASSERT(IsValueMap(err));
		ASSERT((int)ValueMap(err).Get("code", 0) == METHOD_NOT_FOUND);
	}

	// Clean client disconnect
	client.Close();

	// Clean server stop
	server.Stop();
	ASSERT(!server.IsRunning());

	Cout() << "  -> PASSED\n";
}

CONSOLE_APP_MAIN
{
	StdLogSetup(LOG_COUT | LOG_FILE);
	Cout() << "========================================\n";
	Cout() << "    SuperIntelligence MCP Check Suite   \n";
	Cout() << "========================================\n";

	try {
		TestProtocolParsing();
		TestLogBuffer();
		TestLiveServer();

		Cout() << "\n>>> ALL MCP CHECKS PASSED SUCCESSFULLY <<<\n";
	}
	catch(const Exc& e) {
		Cerr() << "\nFATAL EXCEPTION: " << e << "\n";
		SetExitCode(1);
	}
	catch(...) {
		Cerr() << "\nUNKNOWN FATAL EXCEPTION OCCURRED.\n";
		SetExitCode(1);
	}
}
