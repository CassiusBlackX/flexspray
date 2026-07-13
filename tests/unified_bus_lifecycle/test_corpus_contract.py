import csv
import unittest
from pathlib import Path


class LifecycleCorpusContractTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.cases_root = Path(__file__).resolve().parent / "cases"

    def read_text(self, case_id, filename):
        return (self.cases_root / case_id / filename).read_text(encoding="utf-8")

    def read_csv(self, case_id, filename):
        with (self.cases_root / case_id / filename).open(newline="", encoding="utf-8") as stream:
            return list(csv.DictReader(stream))

    def test_minimal_case_is_one_operation_on_one_explicit_tp(self):
        case_id = "minimal-single-rtp-write"
        self.assertEqual(len(self.read_csv(case_id, "node.csv")), 1)
        self.assertEqual(len(self.read_csv(case_id, "topology.csv")), 1)
        traffic = self.read_csv(case_id, "traffic.csv")
        channels = self.read_csv(case_id, "transport_channel.csv")
        self.assertEqual(len(traffic), 1)
        self.assertEqual(traffic[0]["opType"], "URMA_WRITE")
        self.assertEqual(len(channels), 1)

    def test_shared_tp_case_has_bidirectional_read_write_concurrency(self):
        case_id = "medium-shared-tp-concurrency"
        traffic = self.read_csv(case_id, "traffic.csv")
        channels = self.read_csv(case_id, "transport_channel.csv")
        self.assertEqual(len(traffic), 4)
        self.assertEqual({row["opType"] for row in traffic}, {"URMA_READ", "URMA_WRITE"})
        self.assertEqual({row["delay"] for row in traffic}, {"10ns"})
        self.assertEqual(len(channels), 1)

    def test_multi_tp_case_has_deterministic_retransmission_fault(self):
        case_id = "medium-multi-tp-retransmission"
        attributes = self.read_text(case_id, "network_attribute.txt")
        channels = self.read_csv(case_id, "transport_channel.csv")
        rules = self.read_csv(case_id, "retrans_fault.csv")
        self.assertIn('default ns3::UbApp::EnableMultiPath "true"', attributes)
        self.assertIn('default ns3::UbTransportChannel::EnableRetrans "true"', attributes)
        self.assertGreaterEqual(len([row for row in channels if row["priority"] == "7"]), 3)
        self.assertEqual(len(rules), 1)
        self.assertEqual(rules[0]["packetType"], "DATA")
        self.assertEqual(rules[0]["psn"], "1")
        self.assertEqual(rules[0]["dropCount"], "1")

    def test_ctp_case_spans_entities_vls_and_queue_evidence(self):
        case_id = "medium-ctp-entity-vl-pressure"
        attributes = self.read_text(case_id, "network_attribute.txt")
        traffic = self.read_csv(case_id, "traffic.csv")
        self.assertIn('default ns3::UbApp::TransportMode "CTP"', attributes)
        self.assertIn('global UB_QUEUE_TRACE_ENABLE "true"', attributes)
        self.assertGreater(len({row["srcEntityId"] for row in traffic}), 1)
        self.assertGreater(len({row["dstEntityId"] for row in traffic}), 1)
        self.assertGreater(len({row["priority"] for row in traffic}), 1)

    def test_cross_owner_case_binds_two_ranks_and_positive_link_delay(self):
        case_id = "medium-cross-owner-runtime"
        nodes = self.read_csv(case_id, "node.csv")
        links = self.read_csv(case_id, "topology.csv")
        self.assertEqual({row["systemId"] for row in nodes}, {"0", "1"})
        self.assertEqual(len(links), 1)
        self.assertEqual(links[0]["delay"], "20ns")


if __name__ == "__main__":
    unittest.main()
