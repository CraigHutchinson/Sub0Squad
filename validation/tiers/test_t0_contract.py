import json
import unittest

from t0_contract import CASES, HOLDOUT_CASES, score
from compare_t0_contract import compare


class ContractTest(unittest.TestCase):
    def test_controls(self):
        for _, expected in list(CASES.values()) + list(HOLDOUT_CASES.values()):
            self.assertTrue(score(json.dumps(expected), expected)["exact"])
            self.assertFalse(score("not a call", expected)["parse"])
            self.assertFalse(score(json.dumps({**expected, "extra": 1}), expected)["parse"])
            self.assertFalse(score(json.dumps({**expected, "arguments": {}}), expected)["exact"])
        edit = CASES["edit"][1]
        wrong = {**edit, "arguments": {**edit["arguments"], "expected_count": 2}}
        self.assertFalse(score(json.dumps(wrong), edit)["exact"])
        self.assertFalse(score('{"operation":"edit","operation":"view","arguments":{}}', edit)["parse"])

    def test_comparison_rejects_mismatches_and_counts_pairs(self):
        task, expected = CASES["view"]
        settings = {"model": "m", "seed": 1, "samples": 1, "temperature": 0.7, "max_tokens": 100, "heldout": False}
        row = {"seed": 1, "case": "view", "expected": expected, "messages": [{"role": "user", "content": task}], "score": {"exact": False}}
        left = {"status": "complete", "settings": settings, "results": [row]}
        right = {"status": "complete", "settings": settings, "results": [{**row, "score": {"exact": True}}]}
        self.assertEqual(compare(left, right)["improved_cases"], 1)
        with self.assertRaises(ValueError):
            compare(left, {**right, "settings": {**settings, "model": "different"}})


if __name__ == "__main__":
    unittest.main()
