#include "check.hpp"
#include "sub0squad/job_spec.hpp"

using namespace sub0squad;

int main()
{
    JobSpec s;
    s.argv = {"echo", "he said \"hi\"\n"};
    s.env["A"] = "b\\c";
    s.wallSeconds = 30;
    s.resumeArgv = {"echo", "resume"};
    s.adapter = {"state-file", "state.json", "optimization-run-1"};
    auto back = JobSpec::fromJson(s.toJson());
    CHECK(back.has_value());
    if (back) {
        CHECK(back->argv == s.argv);
        CHECK(back->env == s.env);
        CHECK_EQ(back->wallSeconds, 30);
        CHECK(back->resumeArgv == s.resumeArgv);
        CHECK_EQ(back->adapter.schema, std::string{"optimization-run-1"});
    }
    CHECK(!JobSpec::fromJson("{}").has_value());                                   // no argv
    CHECK(!JobSpec::fromJson("{\"argv\":[]}").has_value());
    CHECK(!JobSpec::fromJson("not json").has_value());
    CHECK(!JobSpec::fromJson("{\"argv\":[\"x\"],\"tier\":\"frontier\"}").has_value());  // never a frontier tier in M1
    CHECK(JobSpec::fromJson("{\"argv\":[\"x\"],\"tier\":\"frontier-stub\"}").has_value());
    CHECK(!JobSpec::fromJson("{\"argv\":[\"x\"],\"tier\":\"claude\"}").has_value());
    CHECK(!JobSpec::fromJson("{\"schema\":\"job-spec-9\",\"argv\":[\"x\"]}").has_value());
    return finish();
}
