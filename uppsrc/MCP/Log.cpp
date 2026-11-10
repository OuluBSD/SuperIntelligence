#include "MCP.h"

NAMESPACE_UPP

McpLogBuffer sMcpLog;

void McpLogBuffer::SetCapacity(int cap) {
    if(cap < 1) cap = 1;
    RWMutex::WriteLock __ (lock);
    capacity = cap;
    if(buf.GetCount() > capacity)
        buf.Remove(0, buf.GetCount() - capacity);
}

void McpLogBuffer::Add(const McpLogEntry& e) {
    RWMutex::WriteLock __ (lock);
    buf.Add(e);
    if(buf.GetCount() > capacity)
        buf.Remove(0, buf.GetCount() - capacity);
}

void McpLogBuffer::Clear() {
    RWMutex::WriteLock __ (lock);
    buf.Clear();
}

Vector<McpLogEntry> McpLogBuffer::Snapshot(int max_items, int min_level) {
    RWMutex::ReadLock __ (lock);
    Vector<McpLogEntry> out;
    int n = buf.GetCount();
    for(int i = max(0, n - max_items); i < n; i++)
        if(buf[i].level >= min_level)
            out.Add(buf[i]);
    return out;
}

int McpLogBuffer::Size() const {
    RWMutex::ReadLock __ (lock);
    return buf.GetCount();
}

END_UPP_NAMESPACE

