#pragma once
/// @file event_channel.hpp
/// Worker side of the event channel: one JSON line per event on the inherited descriptor named by `SUB0SQUAD_EVENT_FD`
/// (`sub0squad-event-1`). Silent when the variable is absent, so tools also run outside the squad.

#include <string>
#include <string_view>

namespace sub0squad::events {

void progress(long done, long total, std::string_view phase);
void metric(std::string_view name, double value);
void note(std::string_view text);

}  // namespace sub0squad::events
