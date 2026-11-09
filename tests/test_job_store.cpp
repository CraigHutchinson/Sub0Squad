#include "check.hpp"
#include "sub0squad/job_store.hpp"

using namespace sub0squad;

static void feed(JobStore& s, Journal& j, std::string_view type, const std::string& body)
{
    auto seq = j.append(type, body);
    CHECK(seq.has_value());
    s.apply(j.records().back());
}

int main()
{
    const auto dir = makeTempDir("store");
    auto j = Journal::open(dir);
    CHECK(j.has_value());
    JobStore s;
    JobSpec spec;
    spec.argv = {"true"};
    spec.resumeArgv = {"true", "--resume"};
    const std::string id = s.nextId();
    CHECK_EQ(id, std::string{"j-000001"});
    feed(s, *j, "jobSubmitted", journalbody::submitted(id, spec));
    CHECK(s.find(id) && s.find(id)->state == JobState::queued);
    feed(s, *j, "jobStarted", journalbody::started(id, 1, 4242, 99));
    CHECK(s.find(id)->state == JobState::running && s.find(id)->shimPid == 4242);
    feed(s, *j, "jobCancelRequested", journalbody::cancelRequested(id, 1));
    CHECK(s.find(id)->state == JobState::cancelling);
    feed(s, *j, "jobFinished", journalbody::finished(id, 1, JobState::cancelled, "{\"teardown\":\"verified\"}", "cancelled"));
    CHECK(s.find(id)->state == JobState::cancelled);
    CHECK(s.find(id)->exitJson.find("verified") != std::string::npos);
    feed(s, *j, "jobResumed", journalbody::resumed(id, 2));
    CHECK(s.find(id)->state == JobState::queued && s.find(id)->run == 2);
    CHECK_EQ(s.nextId(), std::string{"j-000002"});

    // Rebuilding from the journal gives the same state.
    JobStore replayed;
    auto r = Journal::replay(dir);
    CHECK(r.has_value());
    for (const auto& rec : r->records) replayed.apply(rec);
    CHECK(replayed.find(id) && replayed.find(id)->state == JobState::queued && replayed.find(id)->run == 2);
    CHECK(replayed.find(id)->spec.resumeArgv.size() == 2);
    // A queued job cancelled before it starts is cancelled at once.
    const std::string id2 = replayed.nextId();
    JournalRecord sub{100, 1.0, "jobSubmitted", journalbody::submitted(id2, spec)};
    replayed.apply(sub);
    replayed.apply(JournalRecord{101, 2.0, "jobCancelRequested", journalbody::cancelRequested(id2, 1)});
    CHECK(replayed.find(id2)->state == JobState::cancelled);
    std::filesystem::remove_all(dir);
    return finish();
}
