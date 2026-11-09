#include "sub0squad/event_channel.hpp"

#include "json_util.hpp"

#include <unistd.h>
#include <cstdlib>

namespace sub0squad::events {

namespace {
void send(json::Writer& w)
{
    const char* v = std::getenv("SUB0SQUAD_EVENT_FD");
    if (!v) return;
    std::string line{w.view()};
    line.push_back('\n');
    (void)!::write(std::atoi(v), line.data(), line.size());
}
}  // namespace

void progress(long done, long total, std::string_view phase)
{
    json::Writer w;
    w.beginObject().kv("schema", "sub0squad-event-1").kv("type", "progress").kv("done", static_cast<std::int64_t>(done)).kv("total", static_cast<std::int64_t>(total)).kv("phase", phase).endObject();
    send(w);
}
void metric(std::string_view name, double value)
{
    json::Writer w;
    w.beginObject().kv("schema", "sub0squad-event-1").kv("type", "metric").kv("name", name).kv("value", value).endObject();
    send(w);
}
void note(std::string_view text)
{
    json::Writer w;
    w.beginObject().kv("schema", "sub0squad-event-1").kv("type", "note").kv("text", text).endObject();
    send(w);
}

}  // namespace sub0squad::events
