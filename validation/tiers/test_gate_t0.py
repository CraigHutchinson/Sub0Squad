"""Check token usage reporting and the cost gate boundary."""
import json
import pathlib
import tempfile
import unittest

from gate_t0 import attempt_cost, cost_limits, judge_cost


class TokenGateTest(unittest.TestCase):
    def test_interruption_records_lower_bound_and_missing_usage(self):
        with tempfile.TemporaryDirectory() as directory:
            attempt = pathlib.Path(directory)
            (attempt / "conversation.md").write_text(
                "## Turn 0 (12:00), request seed 4\n"
                "### Reply, turn 0 (12:01): 100 prompt tokens, 20 completion tokens, finish stop, 1.2 s prompt\n"
                "Answer:\n\n```\n"
                "## Turn 99 (12:01)\n"
                "### Reply, turn 99 (12:01): 9000 prompt tokens, 9000 completion tokens, finish stop\n"
                "```\n"
                "## Turn 1 (12:01), request seed 5\n"
                "### Reply, turn 1 (12:02): 0 prompt tokens, 0 completion tokens, finish aborted\n",
                encoding="utf-8",
            )
            cost = attempt_cost(attempt, {"turns": 2, "tokensIn": 100, "tokensOut": 20})
        self.assertEqual(cost, {"turns": 2, "tokensIn": 100, "tokensOut": 20, "prefillSeconds": 1.2,
                                "prefillTimedCalls": 1, "missingUsageCalls": 1})

    def test_cap_and_missing_usage(self):
        cap = {"tokensIn": 100, "tokensOut": 20}
        run = dict(cap, turns=1, missingUsageCalls=0)
        self.assertEqual(judge_cost(run, cap)[0], "PASS")
        self.assertEqual(judge_cost(dict(run, missingUsageCalls=1), cap)[0], "INCOMPLETE")
        for metric in cap:
            self.assertEqual(judge_cost(dict(run, missingUsageCalls=1, **{metric: cap[metric] + 1}), cap)[0], "FAIL")

    def test_case_ceiling_and_unknown_revision_fallback(self):
        policy = json.loads(pathlib.Path(__file__).with_name("t0-token-budget.json").read_text(encoding="utf-8"))
        key = policy["cases"]["fix-op"]["key"]
        self.assertEqual(cost_limits(policy, "fix-op", key), ({"tokensIn": 13048, "tokensOut": 504}, "case"))
        self.assertEqual(cost_limits(policy, "fix-op", "changed"), ({"tokensIn": 49010, "tokensOut": 8044}, "suite fallback"))


if __name__ == "__main__":
    unittest.main()
