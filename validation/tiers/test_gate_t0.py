"""Check token usage reporting and the cost gate boundary."""
import json
import pathlib
import tempfile
import unittest
from unittest.mock import patch

from gate_t0 import attempt_cost, cost_limits, judge_cost, wait_run


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

    def test_warning_hard_cap_and_missing_usage(self):
        limits = {"warning": {"tokensIn": 100, "tokensOut": 20}, "hard": {"tokensIn": 150, "tokensOut": 30}}
        run = dict(tokensIn=100, tokensOut=20, turns=1, missingUsageCalls=0)
        self.assertEqual(judge_cost(run, limits)[0], "PASS")
        self.assertEqual(judge_cost(dict(run, prefillSeconds=600, prefillTimedCalls=1), limits)[0], "PASS")
        self.assertEqual(judge_cost(dict(run, tokensOut=21), limits)[0], "WARN")
        self.assertEqual(judge_cost(dict(run, tokensOut=30), limits)[0], "WARN")
        self.assertEqual(judge_cost(dict(run, tokensOut=31), limits)[0], "FAIL")
        self.assertEqual(judge_cost(dict(run, tokensOut=21, missingUsageCalls=1), limits)[0], "INCOMPLETE")
        self.assertEqual(judge_cost(dict(run, tokensIn=151, missingUsageCalls=1), limits)[0], "FAIL")
        self.assertEqual(judge_cost(dict(run, costStop={"reason": "stopped"}), limits), ("FAIL", "stopped"))

    def test_case_ceiling_and_unknown_revision_fallback(self):
        policy = json.loads(pathlib.Path(__file__).with_name("t0-token-budget.json").read_text(encoding="utf-8"))
        key = policy["cases"]["fix-op"]["key"]
        self.assertEqual(cost_limits(policy, "fix-op", key), (
            {"warning": {"tokensIn": 13048, "tokensOut": 504},
             "hard": {"tokensIn": 15657, "tokensOut": 760}}, "case"))
        self.assertEqual(cost_limits(policy, "fix-op", "changed"), (
            {"warning": {"tokensIn": 49010, "tokensOut": 8044},
             "hard": {"tokensIn": 58812, "tokensOut": 9653}}, "suite fallback"))

    def test_live_watch_waits_at_warning_and_cancels_above_hard_limit(self):
        policy = json.loads(pathlib.Path(__file__).with_name("t0-token-budget.json").read_text(encoding="utf-8"))
        with tempfile.TemporaryDirectory() as directory:
            home = pathlib.Path(directory)
            workflow = home / "state/workflows/w-000001"
            workflow.mkdir(parents=True)
            plan = {"project": "t0-fixture", "items": [{"files": [], "gate": {"tests": [], "minTests": 1},
                                                      "card": {"context": "test"}}]}
            (workflow / "plan.json").write_text(json.dumps(plan), encoding="utf-8")
            commands = []

            def cli(argv, **_):
                commands.append(argv[2])
                return '{"workflows":[{"state":"running"}]}' if argv[2] == "status" else "{}"

            samples = [("", {"tokensIn": 50000, "tokensOut": 8000}), ("", {"tokensIn": 59000, "tokensOut": 9000})]
            with patch("gate_t0.subprocess.check_output", side_effect=cli), patch("gate_t0.time.sleep"), \
                 patch("gate_t0.measure_cost", side_effect=samples):
                self.assertEqual(wait_run(home / "sub0squad", home, "qwen35", policy), "cancelled")
            self.assertEqual(commands, ["status", "status", "cancel"])
            mark = json.loads((home / "cost-stop.json").read_text(encoding="utf-8"))
            self.assertEqual(mark["tokensIn"], 59000)
            self.assertIn("59000 > 58812", mark["reason"])


if __name__ == "__main__":
    unittest.main()
